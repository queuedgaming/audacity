#pragma once
//! Synthetic drum loops and tones for the transient-detection tests
#include <vector>
#include <random>
#include <cmath>
struct Loop { std::vector<float> x; std::vector<double> hits; };
inline void addHit(std::vector<float>& x, double rate, double t, int kind, float amp, unsigned seed)
{
    std::mt19937 g(seed); std::normal_distribution<float> nd(0.f, 1.f);
    size_t s = size_t(std::llround(t * rate));
    double ph = 0; float lp = 0, hpPrev = 0;
    const size_t len = size_t(rate * (kind == 0 ? 0.25 : kind == 1 ? 0.15 : 0.05));
    for (size_t i = 0; i < len && s + i < x.size(); ++i) {
        double tt = i / rate; float v = 0;
        if (kind == 0) { double f = 50 + 100 * std::exp(-tt * 30); ph += 2 * M_PI * f / rate; v = float(std::sin(ph) * std::exp(-tt * 12)); if (i < rate*0.002) v += 0.5f*nd(g)*float(1 - i/(rate*0.002)); }
        else if (kind == 1) { float n = nd(g); lp += 0.3f * (n - lp); v = float((0.6 * lp + 0.4 * std::sin(2*M_PI*190*tt)) * std::exp(-tt * 25)); }
        else { float n = nd(g); float hp = n - hpPrev; hpPrev = n; v = float(0.5 * hp * std::exp(-tt * 70)); }
        x[s + i] += amp * v;
    }
}
//! drum loop at `tempo`: kick on 1/3, snare on 2/4, hats on eighths (soft); returns hit times
inline Loop drumLoop(double rate, double tempo, int bars, double offset = 0.0, float noise = 0.0003f)
{
    Loop L; const double beat = 60.0 / tempo; const double total = offset + bars * 4 * beat + 0.5;
    L.x.assign(size_t(total * rate), 0.f);
    std::mt19937 g(7); std::normal_distribution<float> nd(0.f, 1.f);
    for (auto& v : L.x) v = noise * nd(g);
    unsigned seed = 1;
    for (int b = 0; b < bars; ++b) for (int e = 0; e < 8; ++e) {
        double t = offset + (b * 4 + e * 0.5) * beat;
        if (e % 4 == 0) addHit(L.x, rate, t, 0, 0.8f, seed++);
        else if (e % 4 == 2) addHit(L.x, rate, t, 1, 0.6f, seed++);
        else addHit(L.x, rate, t, 2, 0.15f, seed++);
        L.hits.push_back(t);
    }
    return L;
}
inline std::vector<float> sustainedTone(double rate, double seconds, double start)
{
    std::vector<float> x(size_t(seconds * rate), 0.f);
    double ph = 0;
    for (size_t i = size_t(start * rate); i < x.size(); ++i) {
        double t = i / rate - start;
        double f = 220 * std::pow(2.0, 0.2 / 12 * std::sin(2 * M_PI * 5.5 * t)); // vibrato +-20 cents
        ph += 2 * M_PI * f / rate;
        double a = std::min(1.0, t / 0.05) * (0.3 + 0.05 * std::sin(2 * M_PI * 0.7 * t)); // slow swell
        x[i] = float(a * (std::sin(ph) + 0.5 * std::sin(2 * ph) + 0.25 * std::sin(3 * ph)));
    }
    return x;
}
