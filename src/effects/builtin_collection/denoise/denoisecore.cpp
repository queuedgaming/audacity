/*
 * Audacity: A Digital Audio Editor
 */
#include "denoisecore.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <memory>

#include "rnnoise.h"

namespace au::effects::denoise {
namespace {
constexpr size_t RN_FRAME = 480;     // samples per RNNoise frame at 48 kHz
constexpr size_t RN_LATENCY = 480;   // the model's overlap-add delay

void fft(std::vector<std::complex<float> >& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(a[i], a[j]);
        }
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const float ang = -2.0f * float(M_PI) / float(len);
        const std::complex<float> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k) {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

//! Spectral flatness 300 Hz..8 kHz of an fftN window starting at absolute sample i0;
//! `at(j)` returns sample j or 0 outside the signal. Shared by offline and streaming analysis.
template<typename At>
float windowFlatness(At at, long long i0, size_t fftN, double rate, std::vector<std::complex<float> >& buf)
{
    buf.resize(fftN);
    for (size_t i = 0; i < fftN; ++i) {
        const float w = 0.5f - 0.5f * std::cos(2.0f * float(M_PI) * i / (fftN - 1));
        buf[i] = { at(i0 + (long long)i) * w, 0.0f };
    }
    fft(buf);
    const size_t bLo = std::max<size_t>(1, static_cast<size_t>(300.0 * fftN / rate));
    const size_t bHi = std::min(fftN / 2, static_cast<size_t>(std::min(8000.0, rate / 2) * fftN / rate));
    double logSum = 0.0, sum = 0.0;
    for (size_t b = bLo; b < bHi; ++b) {
        const double p = std::norm(buf[b]) + 1e-12;
        logSum += std::log(p);
        sum += p;
    }
    const double m = double(bHi - bLo);
    return m > 0 ? float(std::exp(logSum / m) / (sum / m)) : 0.0f;
}

size_t pow2AtLeast(size_t n)
{
    size_t p = 1;
    while (p < n) {
        p <<= 1;
    }
    return p;
}

size_t fftSizeFor(size_t hop) { return pow2AtLeast(2 * hop); }

//! Voice: RNNoise says voice and the frame is harmonic (RNNoise also fires on breaths), or it is
//! clearly tonal and loud (sung/sustained notes the model may miss). `refDb` is the loudest level.
bool isVoiced(float vad, float rmsDb, float flatness, float refDb, double vadThreshold)
{
    const bool tonal = flatness < 0.05f && rmsDb > refDb - 30.0f;
    return (vad >= vadThreshold && rmsDb > refDb - 40.0f && flatness < 0.3f) || tonal;
}

bool isCandidate(bool voiced, float rmsDb, float flatness, float speechDb, float floorDb, const BreathSettings& s)
{
    return !voiced
           && rmsDb <= speechDb - s.maxLevelBelowSpeechDb
           && rmsDb >= speechDb - s.minLevelBelowSpeechDb
           && rmsDb >= floorDb + s.minAboveFloorDb
           && flatness >= s.minFlatness;
}
}

double rmsDb(const float* x, size_t i0, size_t i1)
{
    if (i1 <= i0) {
        return -200.0;
    }
    double e = 0.0;
    for (size_t i = i0; i < i1; ++i) {
        e += double(x[i]) * x[i];
    }
    e /= double(i1 - i0);
    return 10.0 * std::log10(std::max(e, 1e-20));
}

SuppressResult suppress(const float* x, size_t n, double rate, double mix, bool wantAudio)
{
    SuppressResult r;
    if (n == 0 || rate <= 0) {
        return r;
    }
    // The offline effect is the streaming one run over the whole selection, flushed with zeros
    // and shifted back by its latency, so live and destructive processing sound the same.
    std::vector<float> vad48;
    StreamSuppressor st(rate, wantAudio);
    st.vadLog = &vad48;
    const size_t L = st.latency();
    const size_t total = n + L;
    if (wantAudio) {
        r.audio.resize(n);
    }
    std::vector<float> in(MAX_BLOCK), out(MAX_BLOCK);
    for (size_t pos = 0; pos < total; pos += MAX_BLOCK) {
        const size_t len = std::min(MAX_BLOCK, total - pos);
        for (size_t i = 0; i < len; ++i) {
            in[i] = pos + i < n ? x[pos + i] : 0.0f;
        }
        st.process(in.data(), wantAudio ? out.data() : nullptr, len, mix);
        if (wantAudio) {
            for (size_t i = 0; i < len; ++i) {
                const size_t o = pos + i;
                if (o >= L) {
                    r.audio[o - L] = out[i];
                }
            }
        }
    }
    // VAD of RNNoise frame f is centred on input time f * 10 ms (it covers [f-1, f+1) frames);
    // input frame k ([k, k+1) * 10 ms) takes the max of the RNNoise frames covering it
    const size_t frames = vad48.size();
    const size_t nFrames = static_cast<size_t>(std::ceil(n / rate / FRAME_SEC));
    r.vad.resize(nFrames);
    for (size_t k = 0; k < nFrames && frames > 0; ++k) {
        const size_t f0 = std::min(k, frames - 1), f1 = std::min(frames - 1, k + 1);
        r.vad[k] = std::max(vad48[f0], vad48[f1]);
    }
    return r;
}

FrameFeatures analyse(const float* x, size_t n, double rate, const std::vector<float>& vadIn, double vadThreshold)
{
    FrameFeatures ff;
    const size_t hop = static_cast<size_t>(std::lround(rate * FRAME_SEC));
    if (hop == 0 || n == 0) {
        return ff;
    }
    const size_t nFrames = (n + hop - 1) / hop;
    std::vector<float> vad = vadIn;
    if (vad.size() < nFrames) {
        vad = suppress(x, n, rate, 1.0, false).vad;
    }
    vad.resize(nFrames, 0.0f);
    const size_t fftN = fftSizeFor(hop);
    std::vector<std::complex<float> > buf(fftN);
    ff.rmsDb.resize(nFrames);
    ff.flatness.resize(nFrames);
    auto at = [&](long long j) { return j >= 0 && j < (long long)n ? x[j] : 0.0f; };
    for (size_t k = 0; k < nFrames; ++k) {
        const size_t c = k * hop + hop / 2;
        const size_t i0 = c > fftN / 2 ? c - fftN / 2 : 0;
        ff.rmsDb[k] = float(rmsDb(x, k * hop, std::min(n, (k + 1) * hop)));
        ff.flatness[k] = windowFlatness(at, (long long)i0, fftN, rate, buf);
    }
    // Voiced: RNNoise says voice, or clearly tonal and loud (sung/sustained notes the model may miss)
    float loudest = -200.0f;
    for (float v : ff.rmsDb) {
        loudest = std::max(loudest, v);
    }
    ff.voiced.resize(nFrames);
    std::vector<float> voicedLevels;
    for (size_t k = 0; k < nFrames; ++k) {
        ff.voiced[k] = isVoiced(vad[k], ff.rmsDb[k], ff.flatness[k], loudest, vadThreshold);
        if (ff.voiced[k]) {
            voicedLevels.push_back(ff.rmsDb[k]);
        }
    }
    if (!voicedLevels.empty()) {
        std::sort(voicedLevels.begin(), voicedLevels.end());
        ff.speechDb = voicedLevels[voicedLevels.size() / 2]; // median voiced level
    }
    ff.vad = std::move(vad);
    return ff;
}

std::vector<Breath> detectBreaths(const float* x, size_t n, double rate, const BreathSettings& s,
                                  const std::vector<float>& vad)
{
    std::vector<Breath> out;
    const auto ff = analyse(x, n, rate, vad, s.vadThreshold);
    const size_t nFrames = ff.rmsDb.size();
    if (nFrames == 0 || ff.speechDb <= -100.0f) {
        return out; // no voice: nothing to breathe between
    }
    // Noise floor: 10th percentile frame level; breaths stand clearly above it
    std::vector<float> levels = ff.rmsDb;
    std::sort(levels.begin(), levels.end());
    const float floorDb = levels[levels.size() / 10];
    std::vector<char> cand(nFrames);
    for (size_t k = 0; k < nFrames; ++k) {
        cand[k] = isCandidate(ff.voiced[k], ff.rmsDb[k], ff.flatness[k], ff.speechDb, floorDb, s);
    }
    // close holes of up to 3 unvoiced frames inside a breath
    for (size_t k = 0; k < nFrames; ++k) {
        if (!cand[k]) {
            continue;
        }
        size_t j = k + 1;
        while (j < nFrames && !cand[j] && !ff.voiced[j] && j - k <= 4) {
            ++j;
        }
        if (j < nFrames && cand[j] && j - k <= 4) {
            for (size_t m = k + 1; m < j; ++m) {
                cand[m] = 1;
            }
        }
    }
    const size_t gapFrames = static_cast<size_t>(s.maxGapToVoiceSec / FRAME_SEC);
    auto voicedNear = [&](size_t a, size_t b) {
        const size_t lo = a > gapFrames ? a - gapFrames : 0;
        const size_t hi = std::min(nFrames, b + gapFrames);
        for (size_t k = lo; k < hi; ++k) {
            if (ff.voiced[k]) {
                return true;
            }
        }
        return false;
    };
    for (size_t k = 0; k < nFrames;) {
        if (!cand[k]) {
            ++k;
            continue;
        }
        size_t e = k;
        double flat = 0.0, energy = 0.0;
        while (e < nFrames && cand[e]) {
            flat += ff.flatness[e];
            energy += std::pow(10.0, ff.rmsDb[e] / 10.0);
            ++e;
        }
        const double len = (e - k) * FRAME_SEC;
        if (len >= s.minLenSec && len <= s.maxLenSec && voicedNear(k, e)) {
            Breath b;
            b.t0 = k * FRAME_SEC;
            b.t1 = std::min(e * FRAME_SEC, n / rate);
            b.levelDb = float(10.0 * std::log10(energy / (e - k)));
            b.flatness = float(flat / (e - k));
            out.push_back(b);
        }
        k = e;
    }
    return out;
}

std::vector<float> breathGain(size_t n, double rate, const std::vector<Breath>& breaths, const BreathSettings& s)
{
    std::vector<float> g(n, 1.0f);
    const float target = s.silence ? 0.0f : float(std::pow(10.0, std::min(0.0, s.reductionDb) / 20.0));
    const double fade = std::max(0.0, s.fadeSec);
    for (const auto& b : breaths) {
        // fades sit inside the breath so the neighbouring phrases are untouched
        const double inner = std::min(fade, (b.t1 - b.t0) / 2);
        const size_t i0 = static_cast<size_t>(std::max(0.0, b.t0 * rate));
        const size_t i1 = std::min(n, static_cast<size_t>(b.t1 * rate));
        const size_t f = static_cast<size_t>(inner * rate);
        for (size_t i = i0; i < i1; ++i) {
            const size_t fromStart = i - i0, toEnd = i1 - 1 - i;
            double w = 1.0;
            if (f > 0 && fromStart < f) {
                w = std::min(w, (fromStart + 0.5) / f);
            }
            if (f > 0 && toEnd < f) {
                w = std::min(w, (toEnd + 0.5) / f);
            }
            g[i] = std::min(g[i], float(1.0 + (target - 1.0) * w));
        }
    }
    return g;
}

void applyGain(float* x, const std::vector<float>& gain)
{
    for (size_t i = 0; i < gain.size(); ++i) {
        x[i] *= gain[i];
    }
}
// ---------------------------------------------------------------------------------------------
// StreamResampler

namespace {
constexpr int RS_ZERO_CROSSINGS = 24;

double rsCutoff(double factor) { return std::min(1.0, factor) * 0.95; } // fraction of input Nyquist kept
double rsHalfWidth(double factor) { return factor == 1.0 ? 0.0 : RS_ZERO_CROSSINGS / rsCutoff(factor); }
}

StreamResampler::StreamResampler(double factor)
    : m_factor(factor), m_bypass(factor == 1.0)
{
    if (!m_bypass) {
        m_cutoff = rsCutoff(factor);
        m_hw = rsHalfWidth(factor);
        const size_t n = static_cast<size_t>(std::ceil(m_hw * TABLE_RES)) + 2;
        m_table.resize(n);
        for (size_t i = 0; i < n; ++i) {
            const double d = double(i) / TABLE_RES;
            if (d >= m_hw) {
                m_table[i] = 0.0f;
                continue;
            }
            const double xs = d * m_cutoff;
            const double sinc = xs < 1e-9 ? 1.0 : std::sin(M_PI * xs) / (M_PI * xs);
            const double w = 0.5 + 0.5 * std::cos(M_PI * d / m_hw);
            m_table[i] = float(sinc * w * m_cutoff);
        }
        const size_t cap = pow2AtLeast(static_cast<size_t>(2 * m_hw) + 8);
        m_hist.assign(cap, 0.0f);
        m_mask = cap - 1;
    }
    reset();
}

void StreamResampler::reset()
{
    std::fill(m_hist.begin(), m_hist.end(), 0.0f);
    m_inCount = 0;
    m_outCount = 0;
}

float StreamResampler::kernel(double d) const
{
    const double a = std::abs(d) * TABLE_RES;
    const size_t i = static_cast<size_t>(a);
    if (i + 1 >= m_table.size()) {
        return 0.0f;
    }
    const float f = float(a - double(i));
    return m_table[i] + (m_table[i + 1] - m_table[i]) * f;
}

void StreamResampler::push(const float* in, size_t n, float* out, size_t& outLen, size_t outCap)
{
    if (m_bypass) {
        const size_t c = std::min(n, outCap - outLen);
        std::copy(in, in + c, out + outLen);
        outLen += c;
        return;
    }
    for (size_t s = 0; s < n; ++s) {
        m_hist[size_t(m_inCount) & m_mask] = in[s];
        ++m_inCount;
        // emit every output whose kernel support is now complete
        while (outLen < outCap) {
            const double t = double(m_outCount) / m_factor;
            if (t + m_hw > double(m_inCount - 1)) {
                break;
            }
            const long long k0 = std::max(0LL, static_cast<long long>(std::ceil(t - m_hw)));
            const long long k1 = static_cast<long long>(std::floor(t + m_hw));
            double acc = 0.0;
            for (long long k = k0; k <= k1; ++k) {
                acc += m_hist[size_t(k) & m_mask] * kernel(t - double(k));
            }
            out[outLen++] = float(acc);
            ++m_outCount;
        }
    }
}

// ---------------------------------------------------------------------------------------------
// StreamSuppressor

void StreamSuppressor::Fifo::init(size_t cap)
{
    cap = pow2AtLeast(cap);
    buf.assign(cap, 0.0f);
    mask = cap - 1;
    r = w = 0;
}

size_t StreamSuppressor::latencyFor(double rate)
{
    // An output sample needs: the down-resampler's kernel ahead of it (at 48 kHz), the model's
    // 480-sample delay, up to one frame of buffering (480), and the up-resampler's kernel.
    const double up = RNNOISE_RATE / rate;
    const double hwUp = rsHalfWidth(up);
    const double hwDown = rsHalfWidth(1.0 / up);
    return static_cast<size_t>(std::ceil(hwUp + (hwDown + double(RN_FRAME + RN_LATENCY)) / up)) + 2;
}

StreamSuppressor::StreamSuppressor(double rate, bool wantAudio)
    : m_rate(rate), m_wantAudio(wantAudio), m_latency(latencyFor(rate)),
    m_up(RNNOISE_RATE / rate), m_down(rate / RNNOISE_RATE)
{
    const double ratio = RNNOISE_RATE / rate;
    m_tmp48.resize(static_cast<size_t>(std::ceil(MAX_BLOCK * ratio)) + 64);
    m_frame.resize(RN_FRAME);
    m_tmpOut.resize(static_cast<size_t>(std::ceil(RN_FRAME / ratio)) + 64);
    if (m_wantAudio) {
        m_dry.init(m_latency + 2 * MAX_BLOCK + 64);
        m_wet.init(m_latency + 2 * MAX_BLOCK + 2 * m_tmpOut.size() + 64);
    }
    m_st = rnnoise_create(nullptr);
    reset();
}

StreamSuppressor::~StreamSuppressor()
{
    if (m_st) {
        rnnoise_destroy(m_st);
    }
}

void StreamSuppressor::reset()
{
    rnnoise_init(m_st, nullptr);
    m_up.reset();
    m_down.reset();
    m_frameFill = 0;
    m_dropped = 0;
    m_vadCount = 0;
    std::fill(std::begin(m_vadRing), std::end(m_vadRing), 0.0f);
    m_mixNow = -1.0;
    m_underflows = 0;
    if (m_wantAudio) {
        m_dry.clear();
        m_wet.clear();
        for (size_t i = 0; i < m_latency; ++i) {
            m_dry.push(0.0f);
            m_wet.push(0.0f);
        }
    }
}

void StreamSuppressor::process(const float* in, float* out, size_t n, double mix)
{
    for (size_t pos = 0; pos < n; pos += MAX_BLOCK) {
        const size_t len = std::min(MAX_BLOCK, n - pos);
        processChunk(in + pos, out ? out + pos : nullptr, len, mix);
    }
}

void StreamSuppressor::processChunk(const float* in, float* out, size_t n, double mix)
{
    if (m_wantAudio) {
        for (size_t i = 0; i < n; ++i) {
            m_dry.push(in[i]);
        }
    }
    size_t len48 = 0;
    m_up.push(in, n, m_tmp48.data(), len48, m_tmp48.size());
    float rnOut[RN_FRAME];
    for (size_t s = 0; s < len48; ++s) {
        m_frame[m_frameFill++] = m_tmp48[s] * 32768.0f; // the model works at 16-bit scale
        if (m_frameFill < RN_FRAME) {
            continue;
        }
        m_frameFill = 0;
        const float v = rnnoise_process_frame(m_st, rnOut, m_frame.data());
        m_vadRing[m_vadCount & 255] = v;
        ++m_vadCount;
        if (vadLog) {
            vadLog->push_back(v);
        }
        if (!m_wantAudio) {
            continue;
        }
        size_t first = 0;
        if (m_dropped < RN_LATENCY) { // the model's delay: drop its first 480 outputs
            first = std::min(RN_FRAME, RN_LATENCY - m_dropped);
            m_dropped += first;
        }
        for (size_t i = first; i < RN_FRAME; ++i) {
            rnOut[i] /= 32768.0f;
        }
        size_t outLen = 0;
        m_down.push(rnOut + first, RN_FRAME - first, m_tmpOut.data(), outLen, m_tmpOut.size());
        for (size_t i = 0; i < outLen; ++i) {
            m_wet.push(m_tmpOut[i]);
        }
    }
    if (!m_wantAudio || !out) {
        return;
    }
    mix = std::clamp(mix, 0.0, 1.0);
    if (m_mixNow < 0.0) {
        m_mixNow = mix;
    }
    const double a = 1.0 - std::exp(-1.0 / (0.01 * m_rate)); // ~10 ms smoothing of Strength changes
    for (size_t i = 0; i < n; ++i) {
        float w = 0.0f;
        if (m_wet.size() > 0) {
            w = m_wet.pop();
        } else {
            ++m_underflows;
        }
        const float d = m_dry.pop();
        m_mixNow += (mix - m_mixNow) * a;
        out[i] = float(m_mixNow * w + (1.0 - m_mixNow) * d);
    }
}

// ---------------------------------------------------------------------------------------------
// StreamBreathGate

namespace {
constexpr long long LOOK = 12;      // frames of lookahead used to decide a frame (min length + holes)
constexpr long long FADE_FRAMES = 6; // output looks this many decided frames ahead (fades <= 50 ms)
constexpr long long HOLE = 3;       // unvoiced frames bridged inside a breath

size_t gateHop(double rate) { return std::max<size_t>(1, static_cast<size_t>(std::lround(rate * FRAME_SEC))); }

//! input samples needed beyond frame k's start before frame k can be analysed
size_t analysisReach(double rate)
{
    const size_t hop = gateHop(rate);
    const size_t fftN = fftSizeFor(hop);
    const double hwUp = rsHalfWidth(RNNOISE_RATE / rate);
    const size_t vadReach = static_cast<size_t>(std::ceil(2.0 * rate * FRAME_SEC + hwUp)) + 2;
    return std::max({ vadReach, hop / 2 + fftN / 2, hop });
}
}

size_t StreamBreathGate::latencyFor(double rate)
{
    const size_t hop = gateHop(rate);
    return size_t(FADE_FRAMES + LOOK + 3) * hop + analysisReach(rate);
}

StreamBreathGate::StreamBreathGate(double rate, unsigned channels)
    : m_rate(rate), m_channels(std::max(1u, channels)), m_hop(gateHop(rate)), m_fftN(fftSizeFor(m_hop)),
    m_latency(latencyFor(rate)), m_vad(rate, false)
{
    const size_t monoCap = pow2AtLeast(2 * m_fftN + analysisReach(rate) + MAX_BLOCK + 4 * m_hop);
    m_mono.assign(monoCap, 0.0f);
    m_monoMask = monoCap - 1;
    const size_t dCap = pow2AtLeast(m_latency + MAX_BLOCK + 2);
    m_delay.assign(m_channels, std::vector<float>(dCap, 0.0f));
    m_delayMask = dCap - 1;
    m_vadTmp.resize(MAX_BLOCK);
    m_fftBuf.resize(m_fftN);
    reset();
}

void StreamBreathGate::reset()
{
    m_vad.reset();
    std::fill(m_mono.begin(), m_mono.end(), 0.0f);
    for (auto& d : m_delay) {
        std::fill(d.begin(), d.end(), 0.0f);
    }
    m_count = 0;
    std::fill(std::begin(m_rms), std::end(m_rms), -200.0f);
    std::fill(std::begin(m_flat), std::end(m_flat), 0.0f);
    std::fill(std::begin(m_voiced), std::end(m_voiced), 0);
    std::fill(std::begin(m_cand), std::end(m_cand), 0);
    std::fill(std::begin(m_candLoose), std::end(m_candLoose), 0);
    std::fill(std::begin(m_label), std::end(m_label), 0);
    std::fill(std::begin(m_startOf), std::end(m_startOf), 0);
    m_analysed = m_decided = 0;
    m_lastVoiced = -1000000;
    m_breathStart = -1;
    m_breathLen = 0;
    m_inBreath = m_blockUntilBreak = false;
    m_peakDb = m_speechDb = -200.0f;
    m_floorDb = 0.0f;
    m_haveVoice = m_haveFloor = m_prevVoiced = false;
    m_target = -1.0;
    m_undecided = m_breaths = 0;
}

void StreamBreathGate::analyseReady(const BreathSettings& s)
{
    const size_t reachFlat = m_hop / 2 + m_fftN / 2;
    auto at = [&](long long j) { return j >= 0 && j < m_count ? m_mono[size_t(j) & m_monoMask] : 0.0f; };
    while (true) {
        const long long k = m_analysed;
        const long long start = k * (long long)m_hop;
        if (start + (long long)std::max(reachFlat, m_hop) > m_count) {
            break;
        }
        const size_t v0 = static_cast<size_t>(std::llround(double(start) / (m_rate * FRAME_SEC)));
        if (m_vad.vadFrames() <= v0 + 1) {
            break;
        }
        double e = 0.0;
        for (size_t i = 0; i < m_hop; ++i) {
            const float v = at(start + (long long)i);
            e += double(v) * v;
        }
        const float rms = float(10.0 * std::log10(std::max(e / double(m_hop), 1e-20)));
        const long long c = start + (long long)(m_hop / 2);
        const long long i0 = c > (long long)(m_fftN / 2) ? c - (long long)(m_fftN / 2) : 0;
        const float flat = windowFlatness(at, i0, m_fftN, m_rate, m_fftBuf);
        const float vad = std::max(m_vad.vad(v0), m_vad.vad(v0 + 1));

        m_peakDb = std::max(rms, m_peakDb - 0.02f); // running "loudest", decays 2 dB/s
        // hysteresis: once voiced, stay voiced down to a lower VAD probability
        const double thr = m_prevVoiced ? std::max(0.0, s.vadThreshold - 0.2) : s.vadThreshold;
        const bool voiced = isVoiced(vad, rms, flat, m_peakDb, thr);
        m_prevVoiced = voiced;
        // floor before this frame is folded in
        const float floorDb = m_haveFloor ? m_floorDb : rms;
        const bool cand = m_haveVoice && isCandidate(voiced, rms, flat, m_speechDb, floorDb, s);
        // once a breath has started it may peak a little closer to the voice level
        BreathSettings loose = s;
        loose.maxLevelBelowSpeechDb = s.maxLevelBelowSpeechDb - 3.0;
        const bool candLoose = m_haveVoice && isCandidate(voiced, rms, flat, m_speechDb, floorDb, loose);
        if (voiced) {
            // speech level: rises faster than it falls, so it sits near the typical phrase level
            // (like the median used offline) rather than the syllable dips
            const float a = rms > m_speechDb ? 0.1f : 0.02f;
            m_speechDb = m_haveVoice ? m_speechDb + a * (rms - m_speechDb) : rms;
            m_haveVoice = true;
            m_lastVoiced = k;
        }
        m_floorDb = m_haveFloor ? std::min(rms, m_floorDb + 0.01f) : rms; // falls at once, rises 1 dB/s
        m_haveFloor = true;

        const size_t r = size_t(k) % FR;
        m_rms[r] = rms;
        m_flat[r] = flat;
        m_voiced[r] = voiced;
        m_cand[r] = cand;
        m_candLoose[r] = candLoose;
        ++m_analysed;
    }
}

void StreamBreathGate::decideReady(const BreathSettings& s)
{
    auto cand = [&](long long j) { return j >= 0 && j < m_analysed && m_cand[size_t(j) % FR]; };
    auto voiced = [&](long long j) { return j >= 0 && j < m_analysed && m_voiced[size_t(j) % FR]; };
    // a short unvoiced hole between candidate frames belongs to the breath
    auto bridged = [&](long long j) {
        if (voiced(j)) {
            return false;
        }
        bool before = false, after = false;
        for (long long p = j - 1; p >= j - HOLE && p >= 0; --p) {
            if (voiced(p)) {
                break;
            }
            if (cand(p)) {
                before = true;
                break;
            }
        }
        for (long long q = j + 1; q <= j + HOLE + 1 && q < m_analysed; ++q) {
            if (voiced(q)) {
                break;
            }
            if (cand(q)) {
                after = true;
                break;
            }
        }
        return before && after;
    };
    auto loose = [&](long long j) { return j >= 0 && j < m_analysed && m_candLoose[size_t(j) % FR]; };
    auto closed = [&](long long j) { return cand(j) || bridged(j); };
    // inside a breath (and when measuring a starting one) the looser level limit applies
    auto continues = [&](long long j) { return loose(j) || closed(j); };
    const long long minLen = std::max(1LL, static_cast<long long>(std::lround(s.minLenSec / FRAME_SEC)));
    const long long maxLen = std::max(minLen, static_cast<long long>(std::lround(s.maxLenSec / FRAME_SEC)));
    const long long gap = static_cast<long long>(s.maxGapToVoiceSec / FRAME_SEC);

    while (m_decided + LOOK < m_analysed) {
        const long long d = m_decided;
        char label = 0;
        if (m_inBreath) {
            if (continues(d) && m_breathLen < maxLen) {
                label = 1;
                ++m_breathLen;
            } else {
                m_inBreath = false;
                m_blockUntilBreak = closed(d); // too long: background noise, not a breath
            }
        } else if (m_blockUntilBreak && closed(d)) {
            label = 0;
        } else {
            m_blockUntilBreak = false;
            const bool voiceNear = m_haveVoice && m_lastVoiced >= d - gap;
            if (cand(d) && voiceNear) {
                long long run = 0;
                for (long long j = d; j < m_analysed && run < minLen && continues(j); ++j) {
                    ++run;
                }
                if (run >= minLen) {
                    m_inBreath = true;
                    m_breathStart = d;
                    m_breathLen = 1;
                    ++m_breaths;
                    label = 1;
                }
            }
        }
        m_label[size_t(d) % FR] = label;
        m_startOf[size_t(d) % FR] = m_breathStart;
        ++m_decided;
    }
}

float StreamBreathGate::gainAt(long long i, const BreathSettings& s)
{
    const double target = s.silence ? 0.0 : std::pow(10.0, std::min(0.0, s.reductionDb) / 20.0);
    if (m_target < 0.0) {
        m_target = target;
    }
    m_target += (target - m_target) * (1.0 - std::exp(-1.0 / (0.005 * m_rate)));
    const long long o = i / (long long)m_hop;
    if (o >= m_decided) {
        ++m_undecided;
        return 1.0f;
    }
    if (!m_label[size_t(o) % FR]) {
        return 1.0f;
    }
    const long long startSample = m_startOf[size_t(o) % FR] * (long long)m_hop;
    long long endSample = -1;
    for (long long j = o + 1; j < m_decided && j <= o + FADE_FRAMES; ++j) {
        if (!m_label[size_t(j) % FR]) {
            endSample = j * (long long)m_hop;
            break;
        }
    }
    double f = std::clamp(s.fadeSec, 0.0, 0.05) * m_rate;
    if (endSample > 0) {
        f = std::min(f, double(endSample - startSample) / 2.0);
    }
    f = std::floor(f);
    double w = 1.0;
    const double fromStart = double(i - startSample);
    if (f > 0 && fromStart < f) {
        w = std::min(w, (fromStart + 0.5) / f);
    }
    if (endSample > 0) {
        const double toEnd = double(endSample - 1 - i);
        if (f > 0 && toEnd < f) {
            w = std::min(w, (toEnd + 0.5) / f);
        }
    }
    return float(1.0 + (m_target - 1.0) * w);
}

void StreamBreathGate::process(const float* const* in, float* const* out, size_t n, const BreathSettings& s)
{
    for (size_t pos = 0; pos < n; pos += MAX_BLOCK) {
        const size_t len = std::min(MAX_BLOCK, n - pos);
        // take the input first: out may alias in
        for (size_t j = 0; j < len; ++j) {
            const long long a = m_count + (long long)j;
            float m = 0.0f;
            for (unsigned c = 0; c < m_channels; ++c) {
                const float v = in[c][pos + j];
                m_delay[c][size_t(a) & m_delayMask] = v;
                m += v;
            }
            m /= float(m_channels);
            m_vadTmp[j] = m;
            m_mono[size_t(a) & m_monoMask] = m;
        }
        m_vad.process(m_vadTmp.data(), nullptr, len, 1.0);
        const long long first = m_count;
        m_count += (long long)len;
        analyseReady(s);
        decideReady(s);
        for (size_t j = 0; j < len; ++j) {
            const long long i = first + (long long)j - (long long)m_latency;
            if (i < 0) {
                for (unsigned c = 0; c < m_channels; ++c) {
                    out[c][pos + j] = 0.0f;
                }
                continue;
            }
            const float g = gainAt(i, s);
            for (unsigned c = 0; c < m_channels; ++c) {
                out[c][pos + j] = m_delay[c][size_t(i) & m_delayMask] * g;
            }
        }
    }
}
}
