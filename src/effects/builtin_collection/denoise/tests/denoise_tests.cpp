/*
* Audacity: A Digital Audio Editor
*/
#include <gtest/gtest.h>

#include <cmath>
#include <algorithm>
#include <random>
#include <tuple>

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
