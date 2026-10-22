/*
* Audacity: A Digital Audio Editor
*/
#include "transients.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace au::au3::transients {
namespace {
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

struct Frames {
    size_t frame = 1024;
    size_t hop = 128;
};

Frames framesFor(double rate)
{
    Frames f;
    // ~23 ms window, ~2.9 ms hop at 44.1 kHz, scaled with the rate
    f.frame = 1024;
    while (f.frame * 44100.0 < 1024.0 * rate * 0.75 && f.frame < 8192) {
        f.frame <<= 1;
    }
    f.hop = f.frame / 8;
    return f;
}

constexpr int BANDS = 8;
}

std::vector<float> onsetFunction(const float* x, size_t n, double rate, size_t* hopOut)
{
    const Frames fr = framesFor(rate);
    if (hopOut) {
        *hopOut = fr.hop;
    }
    std::vector<float> odf;
    if (n == 0 || rate <= 0) {
        return odf;
    }
    const size_t N = fr.frame, H = fr.hop;
    const size_t frames = n / H + 1;
    odf.assign(frames, 0.0f);
    std::vector<float> win(N);
    for (size_t i = 0; i < N; ++i) {
        win[i] = 0.5f - 0.5f * std::cos(2.0f * float(M_PI) * i / N);
    }
    // log-spaced bands 40 Hz .. min(16 kHz, Nyquist)
    std::vector<size_t> edges(BANDS + 1);
    const double fLo = 40.0, fHi = std::min(16000.0, rate / 2.0);
    for (int b = 0; b <= BANDS; ++b) {
        const double f = fLo * std::pow(fHi / fLo, double(b) / BANDS);
        edges[b] = std::clamp<size_t>(size_t(f * N / rate), 1, N / 2);
    }
    std::vector<std::complex<float> > buf(N);
    std::vector<float> prev(N / 2 + 1, 0.0f), cur(N / 2 + 1);
    for (size_t t = 0; t < frames; ++t) {
        // frame t is centred on sample t * H
        const long long start = (long long)(t * H) - (long long)(N / 2);
        for (size_t i = 0; i < N; ++i) {
            const long long j = start + (long long)i;
            buf[i] = { (j >= 0 && j < (long long)n) ? x[j] * win[i] : 0.0f, 0.0f };
        }
        fft(buf);
        for (size_t k = 0; k <= N / 2; ++k) {
            cur[k] = std::log1p(1000.0f * std::abs(buf[k]) / float(N));
        }
        // a window running off the end is not an onset (the truncation itself is broadband)
        if (t > 0 && start + (long long)N <= (long long)n) {
            float sum = 0.0f;
            for (int b = 0; b < BANDS; ++b) {
                float fl = 0.0f;
                for (size_t k = edges[b]; k < std::max(edges[b] + 1, edges[b + 1]); ++k) {
                    // compare with the loudest neighbouring bin of the previous frame (SuperFlux),
                    // so vibrato and glides moving a partial by a bin or two are not onsets
                    float p = prev[k];
                    for (size_t q = (k > 2 ? k - 2 : 0); q <= std::min(N / 2, k + 2); ++q) {
                        p = std::max(p, prev[q]);
                    }
                    fl += std::max(0.0f, cur[k] - p);
                }
                sum += fl / float(std::max<size_t>(1, edges[b + 1] - edges[b]));
            }
            odf[t] = sum / BANDS;
        }
        std::swap(prev, cur);
    }
    return odf;
}

namespace {
//! Start of the energy rise near `centre` (samples): steepest 1 ms rise of a 1 ms RMS envelope
//! in [centre - before, centre + after], backtracked to where the rise begins
size_t refineAttack(const float* x, size_t n, double rate, size_t centre, size_t before, size_t after)
{
    const size_t lo = centre > before ? centre - before : 0;
    const size_t hi = std::min(n, centre + after);
    if (hi <= lo + 4) {
        return centre;
    }
    const size_t step = std::max<size_t>(1, size_t(rate * 0.00025)); // 0.25 ms
    const size_t win = std::max<size_t>(2, size_t(rate * 0.001));    // 1 ms
    std::vector<float> envDb;
    std::vector<size_t> pos;
    for (size_t p = lo; p + win <= hi; p += step) {
        // energy of the first difference: attacks are broadband, while the ripple of a low
        // sustained note (a kick's tail) would otherwise look like rises every half period
        double e = 0.0;
        for (size_t i = std::max<size_t>(p, 1); i < p + win; ++i) {
            const double d = double(x[i]) - x[i - 1];
            e += d * d;
        }
        envDb.push_back(float(10.0 * std::log10(e / win + 1e-12)));
        pos.push_back(p);
    }
    const size_t ahead = std::max<size_t>(1, win / step);
    if (envDb.size() <= ahead + 1) {
        return centre;
    }
    size_t best = 0;
    float bestRise = -1e9f;
    for (size_t i = 0; i + ahead < envDb.size(); ++i) {
        const float r = envDb[i + ahead] - envDb[i];
        if (r > bestRise) {
            bestRise = r;
            best = i;
        }
    }
    // backtrack while the envelope keeps falling toward the quiet before the hit
    size_t i = best;
    while (i > 0 && envDb[i - 1] < envDb[i] - 0.05f && envDb[best + ahead] - envDb[i - 1] < bestRise + 12.0f) {
        --i;
    }
    // the hit starts where the windowed envelope begins to rise: its window end
    return std::min(n - 1, pos[i] + win);
}

size_t zeroCrossingBefore(const float* x, size_t n, size_t at, size_t maxBack)
{
    const size_t lo = at > maxBack ? at - maxBack : 0;
    for (size_t i = std::min(at, n - 1); i > lo; --i) {
        if ((x[i - 1] <= 0.0f && x[i] > 0.0f) || (x[i - 1] >= 0.0f && x[i] < 0.0f) || x[i] == 0.0f) {
            return i;
        }
    }
    return at;
}
}

std::vector<Onset> detect(const float* x, size_t n, double rate, const Settings& s)
{
    std::vector<Onset> out;
    size_t H = 0;
    const auto odf = onsetFunction(x, n, rate, &H);
    if (odf.size() < 3) {
        return out;
    }
    const size_t frames = odf.size();
    // adaptive threshold: local median (~100 ms before, ~50 ms after) scaled up, plus an offset
    // proportional to the overall activity; sensitivity lowers both
    const double sens = std::clamp(s.sensitivity, 0.0, 100.0) / 100.0;
    double mean = 0.0;
    for (float v : odf) {
        mean += v;
    }
    mean /= double(frames);
    float peakAll = *std::max_element(odf.begin(), odf.end());
    const double mult = 1.3 - 0.5 * sens;              // 1.3 .. 0.8
    const double delta = (1.0 - 0.9 * sens) * mean      // 1.0 .. 0.1 x mean
                         + (0.06 - 0.055 * sens) * peakAll; // and a floor relative to the loudest hit
    const size_t back = std::max<size_t>(3, size_t(0.1 * rate / H));
    const size_t fwd = std::max<size_t>(2, size_t(0.05 * rate / H));
    const size_t localW = std::max<size_t>(2, size_t(0.012 * rate / H)); // local maximum over ±12 ms
    std::vector<float> win;
    std::vector<std::pair<size_t, double> > peaks;
    for (size_t t = 1; t + 1 < frames; ++t) {
        const size_t a = t > back ? t - back : 0, b = std::min(frames, t + fwd + 1);
        win.assign(odf.begin() + a, odf.begin() + b);
        std::nth_element(win.begin(), win.begin() + win.size() / 2, win.end());
        const double thr = mult * win[win.size() / 2] + delta;
        if (odf[t] <= thr || odf[t] <= 0.0f) {
            continue;
        }
        bool isMax = true;
        for (size_t k = (t > localW ? t - localW : 0); k <= std::min(frames - 1, t + localW); ++k) {
            if (odf[k] > odf[t] || (odf[k] == odf[t] && k < t)) {
                isMax = false;
                break;
            }
        }
        if (isMax) {
            peaks.push_back({ t, odf[t] / thr });
        }
    }
    // refine, then enforce the minimum gap (keeping the stronger)
    // the flux peaks while the hit enters the analysis window, i.e. up to half a window before it
    const size_t before = size_t(0.015 * rate), after = size_t(0.025 * rate);
    const size_t preRoll = size_t(std::max(0.0, s.preRollSec) * rate);
    for (const auto& [t, strength] : peaks) {
        const size_t centre = t * H;
        const size_t attack = refineAttack(x, n, rate, centre, before, after);
        size_t cut = attack > preRoll ? attack - preRoll : 0;
        cut = zeroCrossingBefore(x, n, cut, size_t(0.001 * rate));
        Onset o;
        o.attack = attack / rate;
        o.time = cut / rate;
        o.strength = strength;
        if (!out.empty() && o.attack - out.back().attack < s.minGapSec) {
            if (o.strength > out.back().strength) {
                out.back() = o;
            }
            continue;
        }
        out.push_back(o);
    }
    return out;
}

double gridStep(double tempo, int upper, int lower, Division d)
{
    tempo = tempo > 0 ? tempo : 120.0;
    lower = lower > 0 ? lower : 4;
    upper = upper > 0 ? upper : 4;
    const double beat = 60.0 / tempo * 4.0 / lower;
    switch (d) {
    case Division::Bar: return beat * upper;
    case Division::Beat: return beat;
    case Division::Eighth: return beat / 2.0;
    case Division::Sixteenth: return beat / 4.0;
    }
    return beat;
}

std::vector<double> gridLines(double t0, double t1, double step, double margin)
{
    std::vector<double> v;
    if (step <= 0 || t1 <= t0) {
        return v;
    }
    for (long long k = (long long)std::ceil((t0 + margin) / step); k * step < t1 - margin; ++k) {
        v.push_back(k * step);
    }
    return v;
}

double quantize(double t, double step, double strength)
{
    if (step <= 0) {
        return t;
    }
    const double g = std::round(t / step) * step;
    return t + (g - t) * std::clamp(strength, 0.0, 1.0);
}
}
