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

//! Whole-buffer band-limited resampling (Hann-windowed sinc, 24 zero crossings each side).
//! Self-contained so the effect does not depend on resampler preferences.
std::vector<float> resample(const std::vector<float>& in, double factor)
{
    if (factor == 1.0 || in.empty()) {
        return in;
    }
    const double cutoff = std::min(1.0, factor) * 0.95; // fraction of the input Nyquist kept
    const int zc = 24;
    const double halfWidth = zc / cutoff;                // in input samples
    const size_t outLen = static_cast<size_t>(std::lround(in.size() * factor));
    std::vector<float> out(outLen);
    const long long n = static_cast<long long>(in.size());
    for (size_t j = 0; j < outLen; ++j) {
        const double t = j / factor;
        const long long k0 = static_cast<long long>(std::ceil(t - halfWidth));
        const long long k1 = static_cast<long long>(std::floor(t + halfWidth));
        double acc = 0.0;
        for (long long k = std::max(0LL, k0); k <= std::min(n - 1, k1); ++k) {
            const double d = t - double(k);
            const double xs = d * cutoff;
            const double sinc = std::abs(xs) < 1e-9 ? 1.0 : std::sin(M_PI * xs) / (M_PI * xs);
            const double w = 0.5 + 0.5 * std::cos(M_PI * d / halfWidth);
            acc += in[size_t(k)] * sinc * w * cutoff;
        }
        out[j] = float(acc);
    }
    return out;
}

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

struct RnnoiseDeleter {
    void operator()(DenoiseState* s) const { rnnoise_destroy(s); }
};
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
    mix = std::clamp(mix, 0.0, 1.0);
    const double up = RNNOISE_RATE / rate;
    std::vector<float> in48 = resample(std::vector<float>(x, x + n), up);
    const size_t n48 = in48.size();
    // pad: one latency at the end so the tail comes out, and up to a whole frame
    const size_t frames = (n48 + RN_LATENCY + RN_FRAME - 1) / RN_FRAME;
    in48.resize(frames * RN_FRAME, 0.0f);
    std::vector<float> out48(frames * RN_FRAME);
    std::unique_ptr<DenoiseState, RnnoiseDeleter> st(rnnoise_create(nullptr));
    std::vector<float> vad48(frames);
    float bufIn[RN_FRAME], bufOut[RN_FRAME];
    for (size_t f = 0; f < frames; ++f) {
        for (size_t i = 0; i < RN_FRAME; ++i) {
            bufIn[i] = in48[f * RN_FRAME + i] * 32768.0f; // the model works at 16-bit scale
        }
        vad48[f] = rnnoise_process_frame(st.get(), bufOut, bufIn);
        for (size_t i = 0; i < RN_FRAME; ++i) {
            out48[f * RN_FRAME + i] = bufOut[i] / 32768.0f;
        }
    }
    // VAD of frame f describes input [(f - 1) * 480, (f + 1) * 480); report it for 10 ms frames of the
    // input: frame k (input [k*10ms, (k+1)*10ms)) takes the max of the RNNoise frames covering it
    const size_t nFrames = static_cast<size_t>(std::ceil(n / rate / FRAME_SEC));
    r.vad.resize(nFrames);
    for (size_t k = 0; k < nFrames; ++k) {
        const size_t f0 = k, f1 = std::min(frames - 1, k + 1);
        r.vad[k] = std::max(vad48[std::min(f0, frames - 1)], vad48[f1]);
    }
    if (!wantAudio) {
        return r;
    }
    std::vector<float> aligned(out48.begin() + RN_LATENCY, out48.begin() + RN_LATENCY + n48);
    std::vector<float> back = resample(aligned, 1.0 / up);
    back.resize(n, 0.0f);
    r.audio.resize(n);
    for (size_t i = 0; i < n; ++i) {
        r.audio[i] = float(mix * back[i] + (1.0 - mix) * x[i]);
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
    size_t fftN = 1;
    while (fftN < 2 * hop) {
        fftN <<= 1;
    }
    const size_t bLo = std::max<size_t>(1, static_cast<size_t>(300.0 * fftN / rate));
    const size_t bHi = std::min(fftN / 2, static_cast<size_t>(std::min(8000.0, rate / 2) * fftN / rate));
    std::vector<std::complex<float> > buf(fftN);
    ff.rmsDb.resize(nFrames);
    ff.flatness.resize(nFrames);
    for (size_t k = 0; k < nFrames; ++k) {
        const size_t c = k * hop + hop / 2;
        const size_t i0 = c > fftN / 2 ? c - fftN / 2 : 0;
        for (size_t i = 0; i < fftN; ++i) {
            const size_t j = i0 + i;
            const float w = 0.5f - 0.5f * std::cos(2.0f * float(M_PI) * i / (fftN - 1));
            buf[i] = { j < n ? x[j] * w : 0.0f, 0.0f };
        }
        ff.rmsDb[k] = float(rmsDb(x, k * hop, std::min(n, (k + 1) * hop)));
        fft(buf);
        double logSum = 0.0, sum = 0.0;
        for (size_t b = bLo; b < bHi; ++b) {
            const double p = std::norm(buf[b]) + 1e-12;
            logSum += std::log(p);
            sum += p;
        }
        const double m = double(bHi - bLo);
        ff.flatness[k] = m > 0 ? float(std::exp(logSum / m) / (sum / m)) : 0.0f;
    }
    // Voiced: RNNoise says voice, or clearly tonal and loud (sung/sustained notes the model may miss)
    float loudest = -200.0f;
    for (float v : ff.rmsDb) {
        loudest = std::max(loudest, v);
    }
    ff.voiced.resize(nFrames);
    std::vector<float> voicedLevels;
    for (size_t k = 0; k < nFrames; ++k) {
        const bool tonal = ff.flatness[k] < 0.05f && ff.rmsDb[k] > loudest - 30.0f;
        // RNNoise also fires on breaths; voice is harmonic, so it must not be broadband too
        ff.voiced[k] = (vad[k] >= vadThreshold && ff.rmsDb[k] > loudest - 40.0f && ff.flatness[k] < 0.3f) || tonal;
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
        cand[k] = !ff.voiced[k]
                  && ff.rmsDb[k] <= ff.speechDb - s.maxLevelBelowSpeechDb
                  && ff.rmsDb[k] >= ff.speechDb - s.minLevelBelowSpeechDb
                  && ff.rmsDb[k] >= floorDb + s.minAboveFloorDb
                  && ff.flatness[k] >= s.minFlatness;
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
}
