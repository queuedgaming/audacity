/*
* Audacity: A Digital Audio Editor
*/
#include <gtest/gtest.h>

#include <cmath>
#include <algorithm>
#include <random>
#include <tuple>
#include <chrono>
#include <cstdio>

#include "../denoisecore.h"

using namespace au::effects::denoise;

namespace {
//! Speech-like voiced phrase: 110-140 Hz glottal pulse train through three formant resonators,
//! with syllable-rate (4 Hz) amplitude modulation and gentle pitch movement
std::vector<float> phrase(double seconds, double rate, unsigned seed, float amp = 0.3f)
{
    const size_t n = size_t(seconds * rate);
    std::vector<float> src(n);
    double phase = 0.0;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    const double f0base = 110.0 + 30.0 * (seed % 3);
    for (size_t i = 0; i < n; ++i) {
        const double t = i / rate;
        const double f0 = f0base * (1.0 + 0.06 * std::sin(2 * M_PI * 0.7 * t));
        phase += f0 / rate;
        if (phase >= 1.0) {
            phase -= 1.0;
        }
        src[i] = float((phase < 0.1 ? 1.0 : 0.0) - 0.1 + 0.02 * u(rng));
    }
    // formants (vowel-ish), biquad bandpass
    std::vector<float> y(n, 0.0f);
    for (auto [fc, bw, g] : { std::tuple { 700.0, 130.0, 1.0 }, std::tuple { 1220.0, 150.0, 0.6 }, std::tuple { 2600.0, 250.0, 0.3 } }) {
        const double r = std::exp(-M_PI * bw / rate);
        const double a1 = -2 * r * std::cos(2 * M_PI * fc / rate), a2 = r * r;
        double y1 = 0, y2 = 0;
        for (size_t i = 0; i < n; ++i) {
            const double v = (1 - r) * src[i] - a1 * y1 - a2 * y2;
            y2 = y1;
            y1 = v;
            y[i] += float(g * v);
        }
    }
    float peak = 1e-9f;
    for (float v : y) {
        peak = std::max(peak, std::abs(v));
    }
    for (size_t i = 0; i < n; ++i) {
        const double t = i / rate;
        const double env = 0.55 + 0.45 * std::sin(2 * M_PI * 4.0 * t - M_PI / 2);
        const double edge = std::min({ 1.0, t / 0.03, (seconds - t) / 0.03 }, [](double a, double b) { return a < b; });
        y[i] = float(amp * y[i] / peak * env * edge);
    }
    return y;
}

//! Breath: band-limited (300 Hz - 6 kHz-ish) noise burst with a rise and fall
std::vector<float> breath(double seconds, double rate, unsigned seed, float amp)
{
    const size_t n = size_t(seconds * rate);
    std::mt19937 rng(seed);
    std::normal_distribution<double> g(0.0, 1.0);
    std::vector<float> y(n);
    double lp = 0, hpPrev = 0, hpOut = 0;
    for (size_t i = 0; i < n; ++i) {
        const double w = g(rng);
        lp += 0.5 * (w - lp);                   // gentle low-pass
        hpOut = 0.96 * (hpOut + lp - hpPrev);   // high-pass
        hpPrev = lp;
        const double t = double(i) / n;
        y[i] = float(amp * hpOut * std::sin(M_PI * t));
    }
    return y;
}

void addNoise(std::vector<float>& x, float amp, unsigned seed)
{
    std::mt19937 rng(seed);
    std::normal_distribution<double> g(0.0, 1.0);
    for (auto& v : x) {
        v += float(amp * g(rng));
    }
}

struct Take {
    std::vector<float> audio;
    std::vector<std::pair<double, double> > breaths; // ground truth
    std::vector<std::pair<double, double> > phrases;
};

//! phrase, breath, phrase, breath, phrase with short silences around the breaths
Take vocalTake(double rate, float noise = 0.0005f)
{
    Take t;
    auto append = [&](const std::vector<float>& v) { t.audio.insert(t.audio.end(), v.begin(), v.end()); };
    auto now = [&]() { return t.audio.size() / rate; };
    append(std::vector<float>(size_t(0.3 * rate), 0.0f));
    for (int p = 0; p < 3; ++p) {
        const double s = now();
        append(phrase(1.6, rate, 1 + p));
        t.phrases.push_back({ s, now() });
        if (p < 2) {
            append(std::vector<float>(size_t(0.12 * rate), 0.0f));
            const double b = now();
            append(breath(0.35, rate, 10 + p, 0.04f));
            t.breaths.push_back({ b, now() });
            append(std::vector<float>(size_t(0.12 * rate), 0.0f));
        }
    }
    append(std::vector<float>(size_t(0.3 * rate), 0.0f));
    addNoise(t.audio, noise, 99);
    return t;
}

double overlap(double a0, double a1, double b0, double b1) { return std::max(0.0, std::min(a1, b1) - std::max(a0, b0)); }

//! SNR of `y` against the clean reference `s` after the best scalar gain
double snrDb(const std::vector<float>& s, const std::vector<float>& y, size_t i0, size_t i1)
{
    double sy = 0, ss = 0;
    for (size_t i = i0; i < i1; ++i) {
        sy += double(s[i]) * y[i];
        ss += double(s[i]) * s[i];
    }
    const double a = sy / ss;
    double e = 0, p = 0;
    for (size_t i = i0; i < i1; ++i) {
        const double d = y[i] - a * s[i];
        e += d * d;
        p += a * s[i] * a * s[i];
    }
    return 10 * std::log10(p / std::max(e, 1e-20));
}
}

TEST(DenoiseTests, SuppressionImprovesSnrOfNoisySpeechAt48And44k)
{
    for (double rate : { 48000.0, 44100.0 }) {
        std::vector<float> clean;
        for (int p = 0; p < 3; ++p) {
            auto v = phrase(1.5, rate, 1 + p);
            clean.insert(clean.end(), v.begin(), v.end());
            clean.insert(clean.end(), size_t(0.2 * rate), 0.0f);
        }
        auto noisy = clean;
        addNoise(noisy, 0.03f, 7);
        const size_t i0 = size_t(0.3 * rate), i1 = clean.size() - size_t(0.3 * rate);
        const double before = snrDb(clean, noisy, i0, i1);
        const auto r = suppress(noisy.data(), noisy.size(), rate, 1.0);
        ASSERT_EQ(r.audio.size(), noisy.size());
        const double after = snrDb(clean, r.audio, i0, i1);
        EXPECT_GT(after, before + 6.0) << rate << ": " << before << " -> " << after;
        // noise-only stretches get much quieter
        const size_t g0 = size_t(1.55 * rate), g1 = size_t(1.65 * rate);
        EXPECT_LT(rmsDb(r.audio.data(), g0, g1), rmsDb(noisy.data(), g0, g1) - 10.0) << rate;
        // mix 0 is the original
        const auto dry = suppress(noisy.data(), noisy.size(), rate, 0.0);
        for (size_t i = 0; i < noisy.size(); i += 101) {
            ASSERT_FLOAT_EQ(dry.audio[i], noisy[i]);
        }
    }
}

TEST(DenoiseTests, SuppressionKeepsTheSignalAligned)
{
    // A clean phrase passes almost unchanged and without delay
    const double rate = 48000.0;
    auto x = phrase(1.0, rate, 2);
    const auto r = suppress(x.data(), x.size(), rate, 1.0);
    double best = -1, bestLag = 0;
    for (int lag = -600; lag <= 600; lag += 1) {
        double c = 0;
        for (size_t i = 2000; i + 2000 < x.size(); i += 3) {
            const long long j = (long long)i + lag;
            c += x[i] * r.audio[size_t(j)];
        }
        if (c > best) {
            best = c;
            bestLag = lag;
        }
    }
    EXPECT_NEAR(bestLag, 0, 2);
}

TEST(DenoiseTests, BreathsBetweenPhrasesAreDetected)
{
    for (double rate : { 44100.0, 48000.0 }) {
        const Take t = vocalTake(rate);
        const auto found = detectBreaths(t.audio.data(), t.audio.size(), rate, BreathSettings {});
        if (found.size() != t.breaths.size()) {
            for (auto& f : found) std::printf("found %.2f-%.2f lvl %.1f flat %.2f\n", f.t0, f.t1, f.levelDb, f.flatness);
            for (auto& b : t.breaths) std::printf("truth %.2f-%.2f\n", b.first, b.second);
            const auto ff = analyse(t.audio.data(), t.audio.size(), rate, {}, 0.5);
            std::printf("speech %.1f\n", ff.speechDb);
            for (size_t k = 0; k < ff.rmsDb.size(); k += 5) std::printf("%.2f r%.0f f%.2f v%.2f %d\n", k * 0.01, ff.rmsDb[k], ff.flatness[k], ff.vad[k], ff.voiced[k]);
        }
        ASSERT_EQ(found.size(), t.breaths.size()) << rate;
        for (size_t i = 0; i < found.size(); ++i) {
            const auto [b0, b1] = t.breaths[i];
            const double ov = overlap(found[i].t0, found[i].t1, b0, b1);
            EXPECT_GT(ov / (b1 - b0), 0.6) << rate << " breath " << i << " found " << found[i].t0 << "-" << found[i].t1;
            for (const auto& [p0, p1] : t.phrases) {
                EXPECT_LT(overlap(found[i].t0, found[i].t1, p0, p1), 0.03) << rate;
            }
        }
    }
}

TEST(DenoiseTests, NoBreathsWithoutVoiceOrInContinuousSpeech)
{
    const double rate = 48000.0;
    // pure noise: nothing to breathe between
    std::vector<float> noise(size_t(3 * rate), 0.0f);
    addNoise(noise, 0.01f, 3);
    EXPECT_TRUE(detectBreaths(noise.data(), noise.size(), rate, BreathSettings {}).empty());
    // continuous phrases with plain silence between: silence is not a breath
    std::vector<float> x = phrase(1.5, rate, 1);
    x.insert(x.end(), size_t(0.4 * rate), 0.0f);
    auto p2 = phrase(1.5, rate, 2);
    x.insert(x.end(), p2.begin(), p2.end());
    EXPECT_TRUE(detectBreaths(x.data(), x.size(), rate, BreathSettings {}).empty());
}

TEST(DenoiseTests, BreathGainAttenuatesWithFadesAndLeavesPhrasesAlone)
{
    const double rate = 48000.0;
    Take t = vocalTake(rate);
    BreathSettings s;
    const auto found = detectBreaths(t.audio.data(), t.audio.size(), rate, s);
    ASSERT_EQ(found.size(), 2u);
    auto y = t.audio;
    applyGain(y.data(), breathGain(y.size(), rate, found, s));
    for (const auto& b : found) {
        const size_t m0 = size_t((b.t0 + 0.03) * rate), m1 = size_t((b.t1 - 0.03) * rate);
        EXPECT_NEAR(rmsDb(y.data(), m0, m1) - rmsDb(t.audio.data(), m0, m1), -12.0, 0.1);
    }
    for (const auto& [p0, p1] : t.phrases) {
        const size_t a = size_t(p0 * rate), b = size_t(p1 * rate);
        for (size_t i = a; i < b; i += 37) {
            ASSERT_EQ(y[i], t.audio[i]);
        }
    }
    // fades: gain changes gradually (no jump bigger than one fade step)
    const auto g = breathGain(y.size(), rate, found, s);
    float maxStep = 0;
    for (size_t i = 1; i < g.size(); ++i) {
        maxStep = std::max(maxStep, std::abs(g[i] - g[i - 1]));
    }
    EXPECT_LT(maxStep, 0.01f);
    // silence option
    s.silence = true;
    const auto gs = breathGain(y.size(), rate, found, s);
    EXPECT_EQ(gs[size_t((found[0].t0 + found[0].t1) / 2 * rate)], 0.0f);
}

// Not a test: writes the synthetic takes used for the screenshots.
// Run with --gtest_also_run_disabled_tests --gtest_filter='*DumpDemoWavs*'
static void writeWav(const char* path, const std::vector<float>& x, int rate)
{
    FILE* f = std::fopen(path, "wb");
    ASSERT_TRUE(f);
    const uint32_t dataBytes = uint32_t(x.size() * 2);
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + dataBytes); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
    std::fwrite("data", 1, 4, f); u32(dataBytes);
    for (float v : x) {
        const int16_t s = int16_t(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f));
        std::fwrite(&s, 2, 1, f);
    }
    std::fclose(f);
}

TEST(DenoiseTests, DISABLED_DumpDemoWavs)
{
    writeWav("/tmp/denoise_noisy.wav", vocalTake(44100.0, 0.02f).audio, 44100);
    writeWav("/tmp/denoise_breaths.wav", vocalTake(44100.0).audio, 44100);
}

// ---------------------------------------------------------------------------------------------
// Streaming (realtime) versions

namespace {
//! Run a StreamSuppressor over x (flushed with latency zeros) in blocks from `sizes` (cycled)
//! and return the latency-aligned output, same length as x
std::vector<float> streamSuppress(StreamSuppressor& st, const std::vector<float>& x, const std::vector<size_t>& sizes,
                                  double mix)
{
    const size_t L = st.latency();
    std::vector<float> in(x);
    in.resize(x.size() + L, 0.0f);
    std::vector<float> out(in.size());
    size_t pos = 0, b = 0;
    while (pos < in.size()) {
        const size_t len = std::min(sizes[b++ % sizes.size()], in.size() - pos);
        st.process(in.data() + pos, out.data() + pos, len, mix);
        pos += len;
    }
    return std::vector<float>(out.begin() + L, out.end());
}

std::vector<std::vector<float> > streamGate(StreamBreathGate& g, const std::vector<std::vector<float> >& x,
                                            const std::vector<size_t>& sizes, const BreathSettings& s,
                                            const BreathSettings* sLate = nullptr, size_t switchAt = 0)
{
    const size_t L = g.latency();
    const size_t n = x[0].size();
    std::vector<std::vector<float> > io(x.size());
    for (size_t c = 0; c < x.size(); ++c) {
        io[c] = x[c];
        io[c].resize(n + L, 0.0f);
    }
    size_t pos = 0, b = 0;
    std::vector<float*> p(x.size());
    while (pos < n + L) {
        const size_t len = std::min(sizes[b++ % sizes.size()], n + L - pos);
        for (size_t c = 0; c < x.size(); ++c) {
            p[c] = io[c].data() + pos;
        }
        // in place, like the host does
        g.process(p.data(), p.data(), len, (sLate && pos >= switchAt) ? *sLate : s);
        pos += len;
    }
    for (auto& c : io) {
        c.erase(c.begin(), c.begin() + L);
    }
    return io;
}

double maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b)
{
    double m = 0.0;
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
        m = std::max(m, double(std::abs(a[i] - b[i])));
    }
    return m;
}
}

TEST(DenoiseStreamTests, LatencyIsReportedPerRate)
{
    EXPECT_EQ(StreamSuppressor::latencyFor(48000.0), 962u);          // 480 model + 480 framing + 2
    EXPECT_NEAR(StreamSuppressor::latencyFor(44100.0) / 44100.0, 0.0212, 0.001);
    EXPECT_LT(StreamBreathGate::latencyFor(48000.0) / 48000.0, 0.25);
    EXPECT_GT(StreamBreathGate::latencyFor(48000.0) / 48000.0, 0.15);
    EXPECT_LT(StreamBreathGate::latencyFor(44100.0) / 44100.0, 0.25);
}

TEST(DenoiseStreamTests, StreamingSuppressionMatchesOfflineForAnyBlockSize)
{
    for (double rate : { 48000.0, 44100.0, 22050.0 }) {
        Take t = vocalTake(rate, 0.02f);
        t.audio.resize(size_t(2.5 * rate)); // keep the test quick
        for (double mix : { 1.0, 0.6 }) {
            const auto offline = suppress(t.audio.data(), t.audio.size(), rate, mix).audio;
            const std::vector<std::vector<size_t> > patterns {
                { 512 }, { 1 }, { 7 }, { 333 }, { 1023 }, { 4096 }, { 480 }, { 17, 1500, 3, 256, 999 }
            };
            for (const auto& sizes : patterns) {
                if (sizes[0] == 1 && (rate != 44100.0 || mix != 1.0)) {
                    continue; // sample-by-sample once is enough
                }
                StreamSuppressor st(rate);
                const auto live = streamSuppress(st, t.audio, sizes, mix);
                ASSERT_EQ(live.size(), offline.size());
                EXPECT_LT(maxAbsDiff(live, offline), 1e-6) << rate << " block " << sizes[0] << " mix " << mix;
                EXPECT_EQ(st.underflows(), 0u) << rate << " block " << sizes[0];
            }
        }
    }
}

TEST(DenoiseStreamTests, StreamingSuppressionStrengthIsLiveAndSmooth)
{
    const double rate = 48000.0;
    Take t = vocalTake(rate, 0.02f);
    StreamSuppressor st(rate);
    const size_t L = st.latency();
    std::vector<float> in(t.audio), out(in.size());
    const size_t half = in.size() / 2;
    st.process(in.data(), out.data(), half, 1.0);
    st.process(in.data() + half, out.data() + half, in.size() - half, 0.0); // dry from here on
    // after the change (+ latency + ~10 ms smoothing) the output is the delayed dry input
    double err = 0.0;
    for (size_t i = half + size_t(0.1 * rate); i < in.size(); ++i) {
        err = std::max(err, double(std::abs(out[i] - in[i - L])));
    }
    EXPECT_LT(err, 1e-4);
    // and no jump at the switch
    double jump = 0.0;
    for (size_t i = half; i < half + size_t(0.05 * rate); ++i) {
        jump = std::max(jump, double(std::abs(out[i] - out[i - 1])));
    }
    double typical = 0.0;
    for (size_t i = 1; i < in.size(); ++i) {
        typical = std::max(typical, double(std::abs(in[i] - in[i - 1])));
    }
    EXPECT_LT(jump, typical * 1.5);
}

TEST(DenoiseStreamTests, ResetGivesTheSameOutputAsAFreshInstance)
{
    const double rate = 44100.0;
    Take a = vocalTake(rate, 0.02f);
    Take b = vocalTake(rate, 0.01f);
    b.audio.resize(size_t(2 * rate));
    StreamSuppressor fresh(rate);
    const auto expect = streamSuppress(fresh, b.audio, { 512 }, 1.0);
    StreamSuppressor used(rate);
    std::vector<float> junk(a.audio.size());
    used.process(a.audio.data(), junk.data(), size_t(1.3 * rate), 1.0); // stop mid-phrase
    used.reset();
    const auto got = streamSuppress(used, b.audio, { 512 }, 1.0);
    EXPECT_EQ(maxAbsDiff(got, expect), 0.0);

    StreamBreathGate gFresh(rate, 1), gUsed(rate, 1);
    const auto e2 = streamGate(gFresh, { b.audio }, { 256 }, BreathSettings {});
    std::vector<float> scratch(a.audio.begin(), a.audio.begin() + size_t(3 * rate));
    std::vector<float*> sp { scratch.data() };
    gUsed.process(sp.data(), sp.data(), scratch.size(), BreathSettings {});
    gUsed.reset();
    const auto g2 = streamGate(gUsed, { b.audio }, { 256 }, BreathSettings {});
    EXPECT_EQ(maxAbsDiff(g2[0], e2[0]), 0.0);
}

TEST(DenoiseStreamTests, StreamingBreathRemovalAttenuatesBreathsAndLeavesPhrases)
{
    for (double rate : { 48000.0, 44100.0 }) {
        for (const auto& sizes : std::vector<std::vector<size_t> > { { 512 }, { 437 }, { 64, 1000, 5 } }) {
            const Take t = vocalTake(rate);
            // stereo: slightly different right channel
            std::vector<float> right(t.audio);
            for (auto& v : right) {
                v *= 0.8f;
            }
            StreamBreathGate g(rate, 2);
            BreathSettings s;
            const auto out = streamGate(g, { t.audio, right }, sizes, s);
            EXPECT_EQ(g.undecided(), 0u);
            EXPECT_EQ(g.breathsStarted(), t.breaths.size()) << rate << " block " << sizes[0];
            for (const auto& b : t.breaths) {
                // core of the breath (inside the fades and the 10 ms frame grid)
                const size_t i0 = size_t((b.first + 0.04) * rate), i1 = size_t((b.second - 0.04) * rate);
                const double dL = rmsDb(out[0].data(), i0, i1) - rmsDb(t.audio.data(), i0, i1);
                const double dR = rmsDb(out[1].data(), i0, i1) - rmsDb(right.data(), i0, i1);
                EXPECT_NEAR(dL, -12.0, 0.5) << rate << " breath at " << b.first;
                EXPECT_NEAR(dR, -12.0, 0.5);
            }
            for (const auto& p : t.phrases) {
                const size_t i0 = size_t(p.first * rate), i1 = size_t(p.second * rate);
                std::vector<float> a(t.audio.begin() + i0, t.audio.begin() + i1), o(out[0].begin() + i0,
                                                                                   out[0].begin() + i1);
                EXPECT_EQ(maxAbsDiff(a, o), 0.0) << rate << " phrase at " << p.first;
            }
            // smooth: no gain step larger than the fade allows
            double step = 0.0;
            for (size_t i = 1; i < t.audio.size(); ++i) {
                if (std::abs(t.audio[i]) > 1e-4f && std::abs(t.audio[i - 1]) > 1e-4f) {
                    const double g0 = out[0][i - 1] / t.audio[i - 1], g1 = out[0][i] / t.audio[i];
                    step = std::max(step, std::abs(g1 - g0));
                }
            }
            EXPECT_LT(step, 0.01);
        }
    }
}

TEST(DenoiseStreamTests, StreamingBreathSettingsAreLive)
{
    const double rate = 48000.0;
    const Take t = vocalTake(rate);
    StreamBreathGate g(rate, 1);
    BreathSettings early, late;
    late.silence = true;
    // switch between the two breaths (the gate's output lags its input by its latency)
    const size_t switchAt = size_t(3.2 * rate) + g.latency();
    const auto out = streamGate(g, { t.audio }, { 512 }, early, &late, switchAt);
    const auto& b1 = t.breaths[0];
    const auto& b2 = t.breaths[1];
    auto drop = [&](const std::pair<double, double>& b) {
        const size_t i0 = size_t((b.first + 0.04) * rate), i1 = size_t((b.second - 0.04) * rate);
        return rmsDb(out[0].data(), i0, i1) - rmsDb(t.audio.data(), i0, i1);
    };
    EXPECT_NEAR(drop(b1), -12.0, 0.5);
    EXPECT_LT(drop(b2), -60.0);
}

TEST(DenoiseStreamTests, StreamingBreathRemovalLeavesSpeechAndNoiseAlone)
{
    const double rate = 48000.0;
    std::vector<float> x = phrase(1.5, rate, 1);
    x.insert(x.end(), size_t(0.4 * rate), 0.0f);
    auto p2 = phrase(1.5, rate, 2);
    x.insert(x.end(), p2.begin(), p2.end());
    StreamBreathGate g(rate, 1);
    const auto out = streamGate(g, { x }, { 512 }, BreathSettings {});
    EXPECT_EQ(g.breathsStarted(), 0u);
    EXPECT_EQ(maxAbsDiff(out[0], x), 0.0);

    std::vector<float> noise(size_t(3 * rate), 0.0f);
    addNoise(noise, 0.01f, 3);
    StreamBreathGate g2(rate, 1);
    streamGate(g2, { noise }, { 512 }, BreathSettings {});
    EXPECT_EQ(g2.breathsStarted(), 0u);
}

TEST(DenoiseStreamTests, CpuCostPerBlock)
{
    // Not a pass/fail benchmark: prints the cost of one 512-sample block per channel
    for (double rate : { 48000.0, 44100.0 }) {
        Take t = vocalTake(rate, 0.02f);
        const size_t B = 512;
        const size_t blocks = t.audio.size() / B;
        std::vector<float> out(t.audio.size());
        StreamSuppressor st(rate);
        auto t0 = std::chrono::steady_clock::now();
        for (size_t b = 0; b < blocks; ++b) {
            st.process(t.audio.data() + b * B, out.data() + b * B, B, 1.0);
        }
        auto t1 = std::chrono::steady_clock::now();
        StreamBreathGate g(rate, 1);
        std::vector<float> io(t.audio);
        for (size_t b = 0; b < blocks; ++b) {
            float* p = io.data() + b * B;
            g.process(&p, &p, B, BreathSettings {});
        }
        auto t2 = std::chrono::steady_clock::now();
        const double blockMs = 1000.0 * B / rate;
        const double ns = std::chrono::duration<double, std::micro>(t1 - t0).count() / blocks;
        const double bg = std::chrono::duration<double, std::micro>(t2 - t1).count() / blocks;
        std::printf("[cpu] %.0f Hz, 512-sample block (%.1f ms audio): noise suppression %.0f us (%.1f%% of realtime), "
                    "breath removal %.0f us (%.1f%%) per channel\n",
                    rate, blockMs, ns, 100.0 * ns / (blockMs * 1000.0), bg, 100.0 * bg / (blockMs * 1000.0));
        SUCCEED();
    }
}
