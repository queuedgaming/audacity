/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <cstddef>
#include <vector>

//! Pure transient (onset) detection and beat-grid helpers (no project types).
namespace au::au3::transients {
struct Settings {
    double sensitivity = 50.0;  //!< 0..100: higher finds softer hits
    double minGapSec = 0.05;    //!< onsets closer than this are merged (keeps the stronger one)
    double preRollSec = 0.002;  //!< slice point sits this much before the attack
};

struct Onset {
    double time = 0.0;      //!< slice point: refined attack minus pre-roll, at a zero crossing (seconds from x[0])
    double attack = 0.0;    //!< refined attack start (seconds)
    double strength = 0.0;  //!< onset-function peak relative to the adaptive threshold (>1)
};

//! Spectral-flux onset function (log magnitude, positive differences, 8 log-spaced bands
//! averaged so a loud low band does not mask the others), adaptive median threshold scaled
//! by sensitivity, peak picking with a minimum gap; each onset is refined in the time domain
//! to the start of the energy rise, then moved back by the pre-roll to a zero crossing.
std::vector<Onset> detect(const float* x, size_t n, double rate, const Settings& settings = {});

//! Onset function per hop (for tests/diagnostics) and its hop in samples
std::vector<float> onsetFunction(const float* x, size_t n, double rate, size_t* hopOut = nullptr);

enum class Division {
    Bar = 0,
    Beat = 1,
    Eighth = 2,      //!< half a beat
    Sixteenth = 3,   //!< quarter of a beat
};

//! Length of one grid step in seconds for a tempo (beats per minute of the lower note value)
//! and time signature
double gridStep(double tempo, int upper, int lower, Division d);

//! Grid lines strictly inside (t0, t1) (grid origin at time 0), at least `margin` from both ends
std::vector<double> gridLines(double t0, double t1, double step, double margin = 1e-3);

//! Move t toward its nearest grid line by strength 0..1
double quantize(double t, double step, double strength);
}
