/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <algorithm>

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

//! Curve after a drag: Shift held on release toggles linear <-> equal power
inline int curveAfterDrag(int startCurve, bool shiftHeld)
{
    return shiftHeld ? (startCurve == 1 ? 0 : 1) : startCurve;
}
}
