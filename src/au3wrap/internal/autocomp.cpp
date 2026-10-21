/*
* Audacity: A Digital Audio Editor
*/
#include "autocomp.h"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace au::au3::autocomp;

namespace {
double toDb(double rms)
{
    return 20.0 * std::log10(std::max(rms, 1e-6));
}

size_t windowLen(double rate, const Settings& s)
{
    return std::max<size_t>(1, static_cast<size_t>(s.windowSec * rate));
}

double rmsOf(const std::vector<float>& x, size_t a, size_t b)
{
    double acc = 0.0;
    for (size_t i = a; i < b; ++i) {
        acc += double(x[i]) * x[i];
    }
    return b > a ? std::sqrt(acc / double(b - a)) : 0.0;
}

double percentile(std::vector<double> v, double p)
{
    if (v.empty()) {
        return -120.0;
    }
    const size_t k = std::min(v.size() - 1, static_cast<size_t>(p * double(v.size() - 1) + 0.5));
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return v[k];
}
}

Metrics au::au3::autocomp::analyse(const std::vector<float>& lane, size_t start, size_t end, double rate,
                                   const Settings& s)
{
    Metrics m;
    end = std::min(end, lane.size());
    if (end <= start) {
        m.score = -10.0;
        return m;
    }
    size_t clipped = 0;
    for (size_t i = start; i < end; ++i) {
        clipped += std::fabs(lane[i]) >= s.clipLevel ? 1 : 0;
    }
    m.clipFraction = double(clipped) / double(end - start);

    const size_t w = windowLen(rate, s);
    std::vector<double> dbs;
    for (size_t a = start; a < end; a += w) {
        dbs.push_back(toDb(rmsOf(lane, a, std::min(end, a + w))));
    }
    m.loudDb = percentile(dbs, 0.9);
    m.floorDb = percentile(dbs, 0.1);
    m.snrDb = m.loudDb - m.floorDb;
    // Dropouts: near-digital-silence windows. Count only if the take is otherwise active,
    // so a shared phrase gap doesn't penalise every take alike (it would cancel out anyway).
    size_t drops = 0;
    for (double d : dbs) {
        drops += d < s.dropoutDb ? 1 : 0;
    }
    m.dropoutFraction = dbs.empty() ? 0.0 : double(drops) / double(dbs.size());

    // Broadband noise estimate that also works without gaps: white noise has as much energy in
    // its first difference as in the signal (ratio sqrt2), voiced audio far less
    double diff = 0.0, sig = 0.0;
    for (size_t i = start + 1; i < end; ++i) {
        const double d = double(lane[i]) - lane[i - 1];
        diff += d * d;
        sig += double(lane[i]) * lane[i];
    }
    m.noisiness = sig > 0.0 ? std::min(1.0, std::sqrt(diff / (2.0 * sig))) : 1.0;

    // Score: clean (1 - noisiness), minus penalties
    double score = 1.0 - m.noisiness;
    score -= 20.0 * m.clipFraction;              // 5% clipped samples cost a full point
    score -= 2.0 * m.dropoutFraction;            // shared phrase gaps cost every lane the same
    if (m.snrDb > 20.0) {
        // The segment has quiet parts, so the 10th percentile is a real noise floor:
        // penalise an audible one (-70 dB: none, -30 dB: full half point)
        score -= 0.5 * std::clamp((m.floorDb + 70.0) / 40.0, 0.0, 1.0);
    }
    if (m.loudDb < s.lowLevelDb) {
        score -= std::min(1.0, (s.lowLevelDb - m.loudDb) / 20.0);
    }
    if (m.loudDb < s.dropoutDb) {
        score -= 2.0; // nothing there at all
    }
    m.score = score;
    return m;
}

std::vector<size_t> au::au3::autocomp::findBoundaries(const std::vector<std::vector<float> >& lanes, double rate,
                                                      const Settings& s)
{
    std::vector<size_t> bounds;
    if (lanes.empty()) {
        return bounds;
    }
    const size_t n = lanes.front().size();
    const size_t w = windowLen(rate, s);
    const size_t minLen = std::max<size_t>(w, static_cast<size_t>(s.minSegmentSec * rate));

    // Energy per window summed over the lanes: a quiet window is a gap in every take
    const size_t nw = (n + w - 1) / w;
    std::vector<double> energy(nw, 0.0);
    for (const auto& lane : lanes) {
        for (size_t k = 0; k < nw; ++k) {
            const double r = rmsOf(lane, k * w, std::min(n, (k + 1) * w));
            energy[k] += r * r;
        }
    }

    size_t prev = 0;
    while (prev + 2 * minLen <= n) {
        const size_t from = (prev + minLen) / w;
        const size_t to = std::min(nw, (prev + 2 * minLen) / w);
        size_t best = from;
        for (size_t k = from; k < to; ++k) {
            if (energy[k] < energy[best]) {
                best = k;
            }
        }
        // Cut in the middle of the quietest window, so the crossfade sits in the gap
        const size_t cut = std::min(n, best * w + w / 2);
        if (cut <= prev || n - cut < minLen) {
            break;
        }
        bounds.push_back(cut);
        prev = cut;
    }
    return bounds;
}

namespace {
constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

//! Moving average of the voiced (non-NaN) values within +-half frames
std::vector<double> smooth(const std::vector<double>& c, size_t half)
{
    std::vector<double> out(c.size(), NaN);
    for (size_t i = 0; i < c.size(); ++i) {
        if (std::isnan(c[i])) {
            continue;
        }
        double acc = 0.0;
        int n = 0;
        for (size_t k = i >= half ? i - half : 0; k <= std::min(c.size() - 1, i + half); ++k) {
            if (!std::isnan(c[k])) {
                acc += c[k];
                ++n;
            }
        }
        out[i] = acc / n;
    }
    return out;
}
}

void au::au3::autocomp::scorePitch(const std::vector<std::vector<pitch::Frame> >& tracks, size_t f0, size_t f1,
                                   std::vector<Metrics>& metrics, const Settings& s)
{
    const size_t nl = tracks.size();
    if (nl == 0 || metrics.size() != nl) {
        return;
    }
    size_t n = f1;
    for (const auto& t : tracks) {
        n = std::min(n, t.size());
    }
    if (n <= f0) {
        return;
    }
    const size_t len = n - f0;
    // Per lane: cents (NaN when unvoiced), fast smoother (50 ms) for jitter, slow (210 ms) for intonation
    std::vector<std::vector<double> > cents(nl, std::vector<double>(len, NaN)), fast(nl), slow(nl);
    for (size_t l = 0; l < nl; ++l) {
        for (size_t i = 0; i < len; ++i) {
            const auto& fr = tracks[l][f0 + i];
            if (fr.voiced()) {
                cents[l][i] = pitch::hzToCents(fr.f0);
            }
        }
        fast[l] = smooth(cents[l], 2);
        slow[l] = smooth(cents[l], 10);
    }
    // Consensus reference note per frame
    std::vector<double> reference(len, NaN);
    std::vector<int> voicedCount(len, 0);
    size_t voicedFrames = 0;
    for (size_t i = 0; i < len; ++i) {
        std::vector<double> v;
        for (size_t l = 0; l < nl; ++l) {
            if (!std::isnan(slow[l][i])) {
                v.push_back(slow[l][i]);
            }
        }
        voicedCount[i] = int(v.size());
        if (v.empty()) {
            continue;
        }
        std::sort(v.begin(), v.end());
        const double med = v.size() % 2 ? v[v.size() / 2] : 0.5 * (v[v.size() / 2 - 1] + v[v.size() / 2]);
        reference[i] = 100.0 * std::round(med / 100.0);
        if (2 * v.size() >= nl) {
            ++voicedFrames;
        }
    }
    // Only judge pitch where the material is mostly voiced
    if (double(voicedFrames) < 0.2 * double(len)) {
        return;
    }
    for (size_t l = 0; l < nl; ++l) {
        Metrics& m = metrics[l];
        double jit = 0.0, inton = 0.0;
        size_t nj = 0, ni = 0, voiced = 0, missed = 0;
        for (size_t i = 0; i < len; ++i) {
            const bool v = !std::isnan(cents[l][i]);
            voiced += v ? 1 : 0;
            const int others = voicedCount[i] - (v ? 1 : 0);
            if (!v && nl > 1 && 2 * others >= int(nl - 1) && others > 0) {
                ++missed;
            }
            if (!v) {
                continue;
            }
            const double r = cents[l][i] - fast[l][i];
            jit += r * r;
            ++nj;
            if (!std::isnan(reference[i])) {
                inton += std::fabs(slow[l][i] - reference[i]);
                ++ni;
            }
        }
        m.voicedFraction = double(voiced) / double(len);
        m.jitterCents = nj ? std::sqrt(jit / double(nj)) : 0.0;
        m.intonationCents = ni ? inton / double(ni) : 0.0;
        m.missedFraction = double(missed) / double(len);
        const double pen = 0.6 * std::clamp((m.jitterCents - s.jitterToleranceCents) / 10.0, 0.0, 1.0)
                           + 0.8 * std::clamp((m.intonationCents - s.intonationToleranceCents) / 30.0, 0.0, 1.0)
                           + 1.0 * m.missedFraction;
        m.pitchPenalty = s.pitchWeight * pen;
        m.score -= m.pitchPenalty;
    }
}

std::vector<Segment> au::au3::autocomp::compute(const std::vector<std::vector<float> >& lanes, double rate,
                                                const Settings& s)
{
    std::vector<Segment> result;
    if (lanes.empty() || lanes.front().empty()) {
        return result;
    }
    const size_t n = lanes.front().size();
    std::vector<size_t> edges { 0 };
    for (size_t b : findBoundaries(lanes, rate, s)) {
        edges.push_back(b);
    }
    edges.push_back(n);

    std::vector<std::vector<pitch::Frame> > tracks;
    pitch::Settings ps;
    if (s.pitchEnabled) {
        for (const auto& lane : lanes) {
            tracks.push_back(pitch::yin(lane, rate, ps));
        }
    }
    auto frameAt = [&](size_t sample) -> size_t {
        if (tracks.empty()) {
            return 0;
        }
        const double t = double(sample) / rate;
        const auto& fr = tracks.front();
        return static_cast<size_t>(std::lower_bound(fr.begin(), fr.end(), t,
                                                    [](const pitch::Frame& f, double v) { return f.timeSec < v; })
                                   - fr.begin());
    };

    int previous = -1;
    for (size_t i = 0; i + 1 < edges.size(); ++i) {
        Segment seg;
        seg.start = edges[i];
        seg.end = edges[i + 1];
        int best = 0;
        for (size_t l = 0; l < lanes.size(); ++l) {
            seg.metrics.push_back(analyse(lanes[l], seg.start, seg.end, rate, s));
        }
        if (s.pitchEnabled) {
            scorePitch(tracks, frameAt(seg.start), frameAt(seg.end), seg.metrics, s);
        }
        for (size_t l = 0; l < lanes.size(); ++l) {
            if (seg.metrics[l].score > seg.metrics[best].score) {
                best = static_cast<int>(l);
            }
        }
        // Hysteresis: don't switch takes for a marginal gain
        if (previous >= 0 && seg.metrics[previous].score >= seg.metrics[best].score - s.hysteresis) {
            best = previous;
        }
        seg.lane = best;
        previous = best;
        result.push_back(std::move(seg));
    }
    return result;
}
