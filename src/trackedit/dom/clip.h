/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <vector>

#include "global/types/string.h"
#include "../trackedittypes.h"

namespace au::trackedit {
using ClipVersion = int64_t;

struct Clip {
    ClipKey key;
    ClipVersion clipVersion = -1;

    muse::String title;
    ClipColorIndex colorIndex = CLIP_COLOR_INDEX_NONE;
    bool isAutoColor = true;
    int groupId = -1;
    double startTime = 0.0;
    double endTime = 0.0;
    bool stereo = false;

    int pitch = 0;
    double speed = 0.0;
    bool optimizeForVoice = false;

    bool stretchToMatchTempo = false;

    //! Non-destructive fades, seconds (0 = none); curve 0 linear, 1 equal power
    double fadeIn = 0.0;
    double fadeOut = 0.0;
    int fadeCurve = 0;

    inline bool isValid() const { return key.isValid(); }
};

using Clips = std::vector<Clip>;
}
