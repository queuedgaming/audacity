/*
* Audacity: A Digital Audio Editor
*/
#include "pitch.h"

#include <algorithm>
#include <cmath>

using namespace au::au3::pitch;

double au::au3::pitch::hzToCents(double hz)
{
    return 1200.0 * std::log2(hz / 440.0);
}

std::vector<Frame> au::au3::pitch::yin(const std::vector<float>& input, double rate, const Settings& s)
{
    std::vector<Frame> frames;
    if (input.empty() || rate <= 0.0) {
        return frames;
    }
    // Decimate with a box filter: cheap, and good enough for f0 up to ~1 kHz
    const size_t factor = std::max<size_t>(1, static_cast<size_t>(rate / s.analysisRate));
    const double sr = rate / double(factor);
    std::vector<float> x(input.size() / factor);
    for (size_t i = 0; i < x.size(); ++i) {
        float acc = 0.0f;
        for (size_t k = 0; k < factor; ++k) {
            acc += input[i * factor + k];
        }
        x[i] = acc / float(factor);
    }

    const size_t minLag = std::max<size_t>(2, static_cast<size_t>(sr / s.maxHz));
    const size_t maxLag = static_cast<size_t>(sr / s.minHz) + 1;
    const size_t w = maxLag; // integration window
    const size_t hop = std::max<size_t>(1, static_cast<size_t>(s.hopSec * sr));
    const double silence = std::pow(10.0, s.silenceDb / 20.0);
    std::vector<double> d(maxLag + 2);

    for (size_t start = 0; start + w + maxLag + 1 < x.size(); start += hop) {
        Frame f;
        f.timeSec = (double(start) + double(w) / 2.0) / sr;
        double energy = 0.0;
        for (size_t j = 0; j < w; ++j) {
            energy += double(x[start + j]) * x[start + j];
        }
        if (std::sqrt(energy / double(w)) < silence) {
            frames.push_back(f);
            continue;
        }
        // Difference function and cumulative mean normalisation
        d[0] = 1.0;
        double running = 0.0;
        const float* p = x.data() + start;
        for (size_t tau = 1; tau <= maxLag; ++tau) {
            double acc = 0.0;
            for (size_t j = 0; j < w; ++j) {
                const double diff = double(p[j]) - p[j + tau];
                acc += diff * diff;
            }
            running += acc;
            d[tau] = running > 0.0 ? acc * double(tau) / running : 1.0;
        }
        size_t best = 0;
        for (size_t tau = minLag; tau < maxLag; ++tau) {
            if (d[tau] < s.threshold) {
                while (tau + 1 < maxLag && d[tau + 1] < d[tau]) {
                    ++tau;
                }
                best = tau;
                break;
            }
        }
        if (best == 0) {
            frames.push_back(f); // aperiodic
            continue;
        }
        // Parabolic interpolation around the minimum
        double shift = 0.0;
        if (best > 1 && best + 1 <= maxLag) {
            const double a = d[best - 1], b = d[best], c = d[best + 1];
            const double den = a - 2.0 * b + c;
            if (std::fabs(den) > 1e-12) {
                shift = std::clamp(0.5 * (a - c) / den, -0.5, 0.5);
            }
        }
        f.f0 = sr / (double(best) + shift);
        f.confidence = std::clamp(1.0 - d[best], 0.0, 1.0);
        frames.push_back(f);
    }
    return frames;
}
