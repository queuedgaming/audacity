/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace au::projectscene::fadehandles {
struct Fades {
    double in = 0.0;
    double out = 0.0;
};

//! Fades after dragging a fade handle by `dxSec` (positive = rightwards) from `start`.
//! The fade-in handle sits at the end of the fade-in, the fade-out handle at the start of the fade-out,
//! so dragging towards the clip centre lengthens either fade. Each stays within 0..clip length and the
//! two never overlap (in + out <= clip length).
inline Fades drag(const Fades& start, double clipLength, bool fadeInHandle, double dxSec)
{
    Fades f = start;
    const double len = std::max(0.0, clipLength);
    if (fadeInHandle) {
        f.in = std::clamp(start.in + dxSec, 0.0, std::max(0.0, len - start.out));
    } else {
        f.out = std::clamp(start.out - dxSec, 0.0, std::max(0.0, len - start.in));
    }
    return f;
}

//! Fades after snapping the dragged edge to `snappedEdge` (project time) for a clip on
//! [clipStart, clipEnd]: the fade-in edge is its end, the fade-out edge its start. Clamped as in drag().
inline Fades snapEdge(const Fades& dragged, double clipStart, double clipEnd, bool fadeInHandle, double snappedEdge)
{
    const double len = std::max(0.0, clipEnd - clipStart);
    Fades f = dragged;
    if (fadeInHandle) {
        f.in = std::clamp(snappedEdge - clipStart, 0.0, std::max(0.0, len - f.out));
    } else {
        f.out = std::clamp(clipEnd - snappedEdge, 0.0, std::max(0.0, len - f.in));
    }
    return f;
}

//! Readout shown while dragging: "Fade in 250 ms", "Fade out 1.25 s", plus the curve name when asked
inline std::string readout(bool fadeInHandle, double seconds, int curve, bool showCurve)
{
    std::string s = fadeInHandle ? "Fade in " : "Fade out ";
    char buf[32];
    const long ms = std::lround(seconds * 1000.0);
    if (ms < 1000) {
        std::snprintf(buf, sizeof(buf), "%ld ms", ms);
    } else {
        std::snprintf(buf, sizeof(buf), "%.2f s", seconds);
    }
    s += buf;
    if (showCurve) {
        s += curve == 1 ? " \u00b7 equal power" : " \u00b7 linear";
    }
    return s;
}

//! Curve after a drag: Shift held on release toggles linear <-> equal power
inline int curveAfterDrag(int startCurve, bool shiftHeld)
{
    return shiftHeld ? (startCurve == 1 ? 0 : 1) : startCurve;
}
}
