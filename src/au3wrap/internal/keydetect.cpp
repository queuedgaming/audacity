/*
* Audacity: A Digital Audio Editor
*/
#include "keydetect.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>

using namespace au::au3;
using namespace au::au3::keydetect;

namespace {
constexpr double PI = 3.14159265358979323846;

// Index 0 = tonic
constexpr std::array<double, 12> KK_MAJOR { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
constexpr std::array<double, 12> KK_MINOR { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
constexpr std::array<double, 12> T_MAJOR { 0.748, 0.060, 0.488, 0.082, 0.670, 0.460, 0.096, 0.715, 0.104, 0.366, 0.057, 0.400 };
constexpr std::array<double, 12> T_MINOR { 0.712, 0.084, 0.474, 0.618, 0.049, 0.460, 0.105, 0.747, 0.404, 0.067, 0.133, 0.330 };

double pearson(const Chroma& c, const std::array<double, 12>& p, int tonic)
{
    double mc = 0.0, mp = 0.0;
    for (int i = 0; i < 12; ++i) {
        mc += c[i];
        mp += p[i];
    }
    mc /= 12.0;
    mp /= 12.0;
    double num = 0.0, dc = 0.0, dp = 0.0;
    for (int i = 0; i < 12; ++i) {
        const double a = c[i] - mc;
        const double b = p[(i - tonic + 12) % 12] - mp;
        num += a * b;
        dc += a * a;
        dp += b * b;
    }
    return dc > 0.0 && dp > 0.0 ? num / std::sqrt(dc * dp) : 0.0;
}

double deviation(double cents)
{
    return cents - 100.0 * std::round(cents / 100.0);
}

int pitchClassOf(double centsA440, double tuning)
{
    const long midi = std::lround(69.0 + (centsA440 - tuning) / 100.0);
    return int(((midi % 12) + 12) % 12);
}

void fft(std::vector<std::complex<double> >& a)
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
        const double ang = -2.0 * PI / double(len);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0);
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

Result finish(const Chroma& raw, double tuning, bool fromPitch, double minWeight, const Settings& s)
{
    const double total = std::accumulate(raw.begin(), raw.end(), 0.0);
    Result r;
    if (!(total > minWeight)) {
        r.tuningCents = tuning;
        r.tuningHz = 440.0 * std::pow(2.0, tuning / 1200.0);
        return r;
    }
    Chroma c {};
    for (int i = 0; i < 12; ++i) {
        c[i] = raw[i] / total;
    }
    r = matchKey(c, s);
    r.tuningCents = tuning;
    r.tuningHz = 440.0 * std::pow(2.0, tuning / 1200.0);
    r.usedPitchTrack = fromPitch;
    return r;
}
}

std::string keydetect::pitchClassName(int pc)
{
    static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return names[((pc % 12) + 12) % 12];
}

std::string Result::keyName() const
{
    return pitchClassName(key.tonic) + (key.minor ? " minor" : " major");
}

unsigned keydetect::scaleMask(const KeyCandidate& key)
{
    static const int major[] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int minor[] = { 0, 2, 3, 5, 7, 8, 9, 10, 11 };
    unsigned m = 0;
    if (key.minor) {
        for (int d : minor) {
            m |= 1u << ((key.tonic + d) % 12);
        }
    } else {
        for (int d : major) {
            m |= 1u << ((key.tonic + d) % 12);
        }
    }
    return m;
}

double keydetect::snapCents(double cents, unsigned mask, double tuningCents)
{
    if ((mask & 0xFFFu) == 0) {
        mask = 0xFFFu;
    }
    const double m = 69.0 + (cents - tuningCents) / 100.0;
    const long base = std::lround(m);
    long best = base;
    double bestDist = 1e9;
    for (long k = base - 6; k <= base + 6; ++k) {
        const int pc = int(((k % 12) + 12) % 12);
        if (!(mask & (1u << pc))) {
            continue;
        }
        const double d = std::fabs(double(k) - m);
        if (d < bestDist) {
            bestDist = d;
            best = k;
        }
    }
    return (double(best) - 69.0) * 100.0 + tuningCents;
}

double keydetect::estimateTuning(const std::vector<std::pair<double, double> >& deviations, double priorRatio)
{
    std::array<double, 100> hist {};
    double total = 0.0;
    for (const auto& [dev, w] : deviations) {
        const long b = std::lround(deviation(dev)) + 50;
        hist[size_t(((b % 100) + 100) % 100)] += w;
        total += w;
    }
    if (!(total > 0.0)) {
        return 0.0;
    }
    std::array<double, 100> sm {};
    constexpr int R = 7;
    for (int i = 0; i < 100; ++i) {
        for (int k = -R; k <= R; ++k) {
            sm[size_t(i)] += hist[size_t((i + k + 100) % 100)] * double(R + 1 - std::abs(k));
        }
    }
    const int peak = int(std::max_element(sm.begin(), sm.end()) - sm.begin());
    if (sm[50] >= priorRatio * sm[size_t(peak)]) {
        return 0.0;
    }
    // Refine: circular weighted mean of deviations near the peak
    const double centre = double(peak - 50);
    double acc = 0.0, wsum = 0.0;
    for (const auto& [dev, w] : deviations) {
        double d = deviation(dev) - centre;
        d -= 100.0 * std::round(d / 100.0);
        if (std::fabs(d) <= 10.0) {
            acc += d * w;
            wsum += w;
        }
    }
    double t = centre + (wsum > 0.0 ? acc / wsum : 0.0);
    return t - 100.0 * std::round(t / 100.0);
}

Result keydetect::matchKey(const Chroma& chroma, const Settings& s)
{
    const auto& maj = s.profile == Profile::Temperley ? T_MAJOR : KK_MAJOR;
    const auto& min = s.profile == Profile::Temperley ? T_MINOR : KK_MINOR;
    std::vector<KeyCandidate> all;
    for (int t = 0; t < 12; ++t) {
        all.push_back({ t, false, pearson(chroma, maj, t) });
        all.push_back({ t, true, pearson(chroma, min, t) });
    }
    std::stable_sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.correlation > b.correlation; });

    Result r;
    r.valid = true;
    r.chroma = chroma;
    r.key = all.front();
    for (size_t i = 1; i < all.size() && r.alternatives.size() < s.alternatives; ++i) {
        r.alternatives.push_back(all[i]);
    }
    // Tonality: how much more of the chroma falls inside the key's scale than for a flat (atonal) chroma.
    // Pearson r alone is scale-invariant, so it happily "finds" a key in tiny fluctuations of a chromatic run.
    const unsigned fitMask = scaleMask(r.key) & ~(r.key.minor ? 1u << ((r.key.tonic + 9) % 12) : 0u); // natural minor + leading tone
    double inside = 0.0, total = 0.0;
    int notes = 0;
    for (int i = 0; i < 12; ++i) {
        total += chroma[size_t(i)];
        if (fitMask & (1u << i)) {
            inside += chroma[size_t(i)];
            ++notes;
        }
    }
    const double uniform = double(notes) / 12.0;
    r.tonality = total > 0.0 ? std::clamp((inside / total - uniform) / (1.0 - uniform), 0.0, 1.0) : 0.0;

    const double r1 = r.key.correlation;
    const double r2 = all[1].correlation;
    constexpr double MARGIN = 0.15; // margin over the runner-up that counts as certain
    r.confidence = r.tonality * std::clamp((r1 - r2) / MARGIN, 0.0, 1.0);
    // The scale (note set) is what intonation needs: relative keys share it, so no margin term
    r.scaleConfidence = r.tonality * std::clamp(r1 / 0.5, 0.0, 1.0);
    return r;
}

Result keydetect::fromPitchTracks(const std::vector<std::vector<pitch::Frame> >& tracks, const Settings& s)
{
    std::vector<std::pair<double, double> > devs;
    for (const auto& t : tracks) {
        for (const auto& f : t) {
            if (f.voiced()) {
                devs.emplace_back(pitch::hzToCents(f.f0), f.confidence);
            }
        }
    }
    const double tuning = estimateTuning(devs, s.tuningPriorRatio);
    Chroma c {};
    for (const auto& [cents, w] : devs) {
        c[size_t(pitchClassOf(cents, tuning))] += 1.0;
    }
    return finish(c, tuning, true, 20.0, s); // at least ~0.2 s of voiced frames
}

Result keydetect::fromSpectrum(const std::vector<float>& x, double rate, const Settings& s)
{
    // Decimate to ~11 kHz (box filter)
    const size_t q = std::max<size_t>(1, size_t(rate / 11025.0));
    const double r = rate / double(q);
    std::vector<double> y(x.size() / q);
    for (size_t i = 0; i < y.size(); ++i) {
        double acc = 0.0;
        for (size_t k = 0; k < q; ++k) {
            acc += x[i * q + k];
        }
        y[i] = acc / double(q);
    }
    constexpr size_t N = 4096;
    constexpr size_t HOP = N / 2;
    std::vector<double> win(N);
    for (size_t i = 0; i < N; ++i) {
        win[i] = 0.5 - 0.5 * std::cos(2.0 * PI * double(i) / double(N));
    }
    const double silence = std::pow(10.0, s.silenceDb / 20.0);
    const size_t kmin = std::max<size_t>(2, size_t(s.minHz * N / r));
    const size_t kmax = std::min(N / 2 - 2, size_t(s.maxHz * N / r));

    struct Peak { double cents; double mag; };
    std::vector<Peak> peaks;
    std::vector<std::complex<double> > buf(N);
    std::vector<double> mag(N / 2);
    for (size_t start = 0; start + N <= y.size(); start += HOP) {
        double e = 0.0;
        for (size_t i = 0; i < N; ++i) {
            e += y[start + i] * y[start + i];
        }
        if (std::sqrt(e / double(N)) < silence) {
            continue;
        }
        for (size_t i = 0; i < N; ++i) {
            buf[i] = y[start + i] * win[i];
        }
        fft(buf);
        double mx = 0.0;
        for (size_t k = 0; k < N / 2; ++k) {
            mag[k] = std::abs(buf[k]);
            if (k >= kmin && k <= kmax) {
                mx = std::max(mx, mag[k]);
            }
        }
        for (size_t k = kmin; k <= kmax; ++k) {
            if (mag[k] <= mag[k - 1] || mag[k] < mag[k + 1] || mag[k] < 0.05 * mx) {
                continue;
            }
            // Parabolic interpolation on log magnitude
            const double a = std::log(mag[k - 1] + 1e-12), b = std::log(mag[k] + 1e-12), c = std::log(mag[k + 1] + 1e-12);
            const double den = a - 2.0 * b + c;
            const double off = den != 0.0 ? 0.5 * (a - c) / den : 0.0;
            const double hz = (double(k) + off) * r / double(N);
            if (hz > 0.0) {
                peaks.push_back({ pitch::hzToCents(hz), mag[k] });
            }
        }
    }
    std::vector<std::pair<double, double> > devs;
    devs.reserve(peaks.size());
    for (const auto& p : peaks) {
        devs.emplace_back(p.cents, p.mag);
    }
    const double tuning = estimateTuning(devs, s.tuningPriorRatio);
    Chroma c {};
    for (const auto& p : peaks) {
        c[size_t(pitchClassOf(p.cents, tuning))] += p.mag;
    }
    return finish(c, tuning, false, 0.0, s);
}

Result keydetect::detect(const std::vector<float>& x, double rate, const Settings& s)
{
    if (x.empty() || rate <= 0.0) {
        return {};
    }
    if (s.source == Source::Spectral) {
        return fromSpectrum(x, rate, s);
    }
    pitch::Settings ps;
    ps.silenceDb = s.silenceDb;
    const auto frames = pitch::yin(x, rate, ps);
    if (s.source == Source::Pitch) {
        return fromPitchTracks({ frames }, s);
    }
    // Auto: monophonic when most audible frames have a confident f0
    const size_t hop = std::max<size_t>(1, size_t(ps.hopSec * rate));
    const double silence = std::pow(10.0, s.silenceDb / 20.0);
    size_t audible = 0, confident = 0;
    for (const auto& f : frames) {
        const size_t c = size_t(f.timeSec * rate);
        const size_t a = c >= hop ? c - hop : 0;
        const size_t b = std::min(x.size(), c + hop);
        double e = 0.0;
        for (size_t i = a; i < b; ++i) {
            e += double(x[i]) * x[i];
        }
        if (b > a && std::sqrt(e / double(b - a)) >= silence) {
            ++audible;
            if (f.voiced() && f.confidence >= s.monophonicConfidence) {
                ++confident;
            }
        }
    }
    if (audible > 0 && double(confident) >= s.monophonicVoiced * double(audible)) {
        return fromPitchTracks({ frames }, s);
    }
    return fromSpectrum(x, rate, s);
}
