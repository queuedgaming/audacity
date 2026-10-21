/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <cstddef>
#include <vector>

#include "pitch.h"

//! Automatic comping: pure, deterministic DSP heuristics (no project or track types),
//! so it can be unit-tested on synthetic buffers.
namespace au::au3::autocomp {
struct Settings {
    double minSegmentSec = 2.0;      //!< shortest segment; boundaries are searched in [min, 2*min]
    double windowSec = 0.02;         //!< analysis window (RMS, energy)
    double hysteresis = 0.08;        //!< keep the previous take unless another scores this much better
    float clipLevel = 0.99f;         //!< |sample| at or above this counts as clipped
    double dropoutDb = -60.0;        //!< windows below this (inside a take's active span) are dropouts
    double lowLevelDb = -35.0;       //!< loud part (90th pct) below this is penalised as too quiet

    // Pitch scoring (YIN, see pitch.h); only applied to segments that are mostly voiced
    bool pitchEnabled = true;
    double pitchWeight = 1.0;        //!< scales the whole pitch penalty
    double jitterToleranceCents = 6.0;     //!< short-term f0 residual tolerated (5-7 Hz vibrato mostly passes the 50 ms smoother)
    double intonationToleranceCents = 10.0; //!< deviation from the reference note tolerated
};

//! Per-take, per-segment metrics and the resulting score (higher is better)
struct Metrics {
    double clipFraction = 0.0;  //!< fraction of samples at or above the clip level
    double dropoutFraction = 0.0; //!< fraction of windows that fall below dropoutDb
    double loudDb = -120.0;     //!< 90th percentile window RMS
    double floorDb = -120.0;    //!< 10th percentile window RMS (noise floor estimate)
    double snrDb = 0.0;         //!< loudDb - floorDb (meaningful when the segment has gaps)
    double noisiness = 0.0;     //!< rms(first difference) / (sqrt2 * rms): ~0 for voiced/low-frequency audio, ~1 for white noise
    // Pitch (zero when pitch scoring is off or the segment is not voiced)
    double voicedFraction = 0.0;   //!< frames this lane is voiced
    double jitterCents = 0.0;      //!< RMS of f0 minus its 50 ms moving average (fast wobble; vibrato mostly passes)
    double intonationCents = 0.0;  //!< mean |200 ms-smoothed f0 - reference note|
    double missedFraction = 0.0;   //!< frames where most other lanes are voiced but this one is not
    double pitchPenalty = 0.0;
    double score = 0.0;
};

struct Segment {
    size_t start = 0;  //!< first sample
    size_t end = 0;    //!< one past the last sample
    int lane = 0;      //!< chosen lane (index into the input)
    std::vector<Metrics> metrics; //!< per lane
};

//! Score one lane over [start, end)
Metrics analyse(const std::vector<float>& lane, size_t start, size_t end, double rate, const Settings& s);

//! Segment boundaries (sample indices, excluding 0 and length): each one is placed at the quietest
//! analysis window (summed over all lanes, i.e. a shared phrase gap) within [min, 2*min] after the
//! previous boundary. A tail shorter than the minimum is merged into the last segment.
std::vector<size_t> findBoundaries(const std::vector<std::vector<float> >& lanes, double rate, const Settings& s);

//! Pitch metrics for every lane over frames [f0, f1) of per-lane YIN tracks (same hop, same start).
//! The reference note per frame is the median of the voiced lanes' smoothed pitch (consensus across
//! takes) snapped to the nearest equal-tempered semitone (A4 = 440 Hz). Adds the penalty to `metrics`.
void scorePitch(const std::vector<std::vector<pitch::Frame> >& tracks, size_t f0, size_t f1,
                std::vector<Metrics>& metrics, const Settings& s);

//! Full run: boundaries, per-segment scores, choice with hysteresis. All lanes must have the same length.
std::vector<Segment> compute(const std::vector<std::vector<float> >& lanes, double rate, const Settings& s);
}
