/*
* Audacity: A Digital Audio Editor
*/
#include "gainstaging.h"

#include <algorithm>
#include <cmath>

#include "au3-math/EBUR128.h"
#include "au3-math/TruePeak.h"
#include "au3-track/Track.h"
#include "au3-wave-track/WaveTrack.h"

#include "busrouting.h"

namespace au::au3::gainstaging {
namespace {
double toDb(double lin)
{
    return lin > 0 ? std::max(SilenceDb, 20.0 * std::log10(lin)) : SilenceDb;
}

class Accumulator
{
public:
    Accumulator(size_t channels, double rate)
        : m_channels(channels), m_tp(channels), m_loud(rate, channels) {}

    //! `block[c]` has `len` samples
    void add(const std::vector<const float*>& block, size_t len)
    {
        for (size_t c = 0; c < m_channels; ++c) {
            const float* x = block[c];
            for (size_t i = 0; i < len; ++i) {
                const double v = x[i];
                m_peak = std::max(m_peak, std::fabs(v));
                m_sumSq += v * v;
            }
            m_tp.Process(c, x, len);
        }
        for (size_t i = 0; i < len; ++i) {
            for (size_t c = 0; c < m_channels; ++c) {
                m_loud.ProcessSampleFromChannel(block[c][i], c);
            }
            m_loud.NextSample();
        }
        m_count += len;
    }

    Levels finish()
    {
        m_tp.Finish();
        Levels l;
        l.channels = int(m_channels);
        l.silent = m_peak <= 0.0;
        l.peakDb = toDb(m_peak);
        l.truePeakDb = toDb(std::max<double>(m_peak, m_tp.Peak()));
        l.rmsDb = m_count ? toDb(std::sqrt(m_sumSq / double(m_count * m_channels))) : SilenceDb;
        const double loud = m_count ? m_loud.IntegrativeLoudness() : 0.0;
        l.lufs = loud > 0 ? std::max(SilenceDb, m_loud.IntegrativeLoudnessToLUFS(loud)) : SilenceDb;
        return l;
    }

private:
    size_t m_channels;
    TruePeakMeter m_tp;
    EBUR128 m_loud;
    double m_peak = 0;
    double m_sumSq = 0;
    size_t m_count = 0;
};
}

Levels measure(const std::vector<std::vector<float> >& channels, double rate)
{
    if (channels.empty()) {
        return {};
    }
    Accumulator acc(channels.size(), rate);
    std::vector<const float*> block;
    size_t len = channels[0].size();
    for (const auto& c : channels) {
        block.push_back(c.data());
        len = std::min(len, c.size());
    }
    acc.add(block, len);
    return acc.finish();
}

Levels analyzeTrack(const WaveTrack& track)
{
    const size_t nCh = track.NChannels();
    const double rate = track.GetRate();
    Accumulator acc(nCh, rate);
    const auto start = track.TimeToLongSamples(track.GetStartTime());
    const auto end = track.TimeToLongSamples(track.GetEndTime());
    constexpr size_t blockLen = 65536;
    std::vector<std::vector<float> > buf(nCh, std::vector<float>(blockLen));
    std::vector<float*> ptrs;
    std::vector<const float*> cptrs;
    for (auto& b : buf) {
        ptrs.push_back(b.data());
        cptrs.push_back(b.data());
    }
    for (auto pos = start; pos < end;) {
        const size_t len = limitSampleBufferSize(blockLen, end - pos);
        track.GetFloats(0, nCh, ptrs.data(), pos, len, false, FillFormat::fillZero, false);
        acc.add(cptrs, len);
        pos += len;
    }
    Levels l = acc.finish();
    l.trackId = track.GetId();
    l.name = track.GetName().ToStdString();
    l.trimDb = trimDb(track);
    return l;
}

std::vector<Levels> analyze(AudacityProject& project)
{
    std::vector<Levels> result;
    for (const auto* wt : TrackList::Get(project).Any<const WaveTrack>()) {
        if (BusRoutingData::Get(*wt).isBus) {
            continue;
        }
        result.push_back(analyzeTrack(*wt));
    }
    return result;
}

std::optional<double> suggestTrimDb(const Levels& levels, TargetKind kind, double target)
{
    if (levels.silent) {
        return std::nullopt;
    }
    double measured = levels.rmsDb;
    switch (kind) {
    case TargetKind::Rms: measured = levels.rmsDb;
        break;
    case TargetKind::Lufs: measured = levels.lufs;
        break;
    case TargetKind::Peak: measured = levels.truePeakDb;
        break;
    }
    if (measured <= SilenceDb) {
        return std::nullopt;
    }
    const double trim = std::clamp(target - measured, MinTrimDb, MaxTrimDb);
    return std::round(trim * 10.0) / 10.0;
}

double trimDb(const WaveTrack& track)
{
    const double t = track.GetInputTrim();
    return t > 0 ? 20.0 * std::log10(t) : MinTrimDb;
}

bool setTrimDb(WaveTrack& track, double db)
{
    db = std::clamp(db, MinTrimDb, MaxTrimDb);
    const float lin = std::abs(db) < 1e-9 ? 1.0f : float(std::pow(10.0, db / 20.0));
    if (track.GetInputTrim() == lin) {
        return false;
    }
    track.SetInputTrim(lin);
    return true;
}
}
