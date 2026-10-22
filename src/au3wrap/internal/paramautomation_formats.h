/*
* Audacity: A Digital Audio Editor
*/
#pragma once

//! Internal: per plugin format parts of parameter automation (see paramautomation.h)

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "paramautomation.h"

class EffectInstanceFactory;

namespace au::au3::paramautomation::formats {
struct Handler {
    ParamFormat format;
    bool (*matches)(const EffectInstanceFactory& factory);
    std::vector<ParamInfo> (*list)(RealtimeEffectState& state);
    std::shared_ptr<const RealtimeEffectAutomation> (*makeApplier)(RealtimeEffectState& state,
                                                                    const std::vector<const ParamLane*>& lanes);
    std::optional<std::string> (*valueText)(RealtimeEffectState& state, const std::string& key, double value);
};

//! Handlers compiled in (VST3 / LV2 depending on build options)
const std::vector<const Handler*>& handlers();

#ifdef AU_PARAMAUTOMATION_VST3
const Handler& vst3Handler();
//! Drop main-thread instances kept for parameter queries
void releaseVst3Instances();
#endif
#ifdef AU_PARAMAUTOMATION_LV2
const Handler& lv2Handler();
#endif

//! Envelope points prepared for the audio thread
struct PreparedLane {
    std::vector<ParamPoint> points; //!< log-domain values when `logarithmic`
    bool step = false;
    bool logarithmic = false;
    double min = 0.0;
    double max = 1.0;
    int stepCount = 0; //!< quantize to this many steps over [min, max] (0: continuous)

    //! Prepare: geometric interpolation needs strictly positive values
    static PreparedLane make(const ParamLane& lane, double min, double max, bool logarithmic, int stepCount);
    double value(double t) const noexcept;
};
}
