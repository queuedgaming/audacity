/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <cstddef>
#include <vector>

//! Small, pure pitch tracker (YIN) for analysis features (auto-comp now, pitch tools later).
namespace au::au3::pitch {
struct Settings {
    double hopSec = 0.01;        //!< one frame every 10 ms
    double minHz = 70.0;
    double maxHz = 1000.0;
    double threshold = 0.15;     //!< YIN absolute threshold on the cumulative mean normalised difference
    double silenceDb = -45.0;    //!< frames quieter than this are unvoiced
    double analysisRate = 8000.0; //!< input is decimated (box filter) to at most ~this rate for speed
};

struct Frame {
    double timeSec = 0.0;    //!< frame centre, relative to the start of the input
    double f0 = 0.0;         //!< Hz; 0 when unvoiced
    double confidence = 0.0; //!< 1 - CMND at the chosen lag (0..1); 0 when unvoiced
    bool voiced() const { return f0 > 0.0; }
};

//! YIN f0 track of `x` (sampled at `rate`), one frame per hop
std::vector<Frame> yin(const std::vector<float>& x, double rate, const Settings& s = {});

//! Pitch in cents relative to A4 = 440 Hz (f0 must be > 0)
double hzToCents(double hz);
}
