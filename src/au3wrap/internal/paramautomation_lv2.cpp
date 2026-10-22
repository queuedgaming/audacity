/*
* Audacity: A Digital Audio Editor
*/
#include "paramautomation_formats.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "au3-lv2/LV2EffectBase.h"
#include "au3-lv2/LV2Ports.h"
#include "au3-realtime-effects/RealtimeEffectAutomation.h"
#include "au3-realtime-effects/RealtimeEffectState.h"

using namespace au::au3;
using namespace au::au3::paramautomation::formats;

namespace {
const LV2EffectBase* lv2Of(RealtimeEffectState& state)
{
    return dynamic_cast<const LV2EffectBase*>(state.GetEffect());
}

//! Index in LV2EffectSettings::values of the input control port `symbol`, or -1
int valueIndex(const LV2EffectBase& effect, const std::string& symbol, LV2ControlPortPtr* port = nullptr)
{
    int index = 0;
    for (const auto& p : effect.mPorts.mControlPorts) {
        if (p->mIsInput && p->mSymbol.ToStdString() == symbol) {
            if (port) {
                *port = p;
            }
            return index;
        }
        ++index;
    }
    return -1;
}

bool automatable(const LV2ControlPort& port)
{
    return port.mIsInput && !port.mTrigger;
}

struct Lv2Lane {
    size_t index = 0;
    PreparedLane lane;
    std::vector<double> snap; //!< enumeration: allowed values (sorted)
};

class Lv2Applier final : public RealtimeEffectAutomation
{
public:
    explicit Lv2Applier(std::vector<Lv2Lane> lanes)
        : mLanes(std::move(lanes)) {}

    void Apply(EffectSettings& settings, double time, EffectInstance*) const noexcept override
    {
        // Control input ports are connected to these very values (LV2Wrapper::ConnectControlPorts)
        auto* lv2 = settings.cast<LV2EffectSettings>();
        if (!lv2) {
            return;
        }
        auto& values = lv2->values;
        for (const auto& l : mLanes) {
            if (l.index >= values.size()) {
                continue;
            }
            double v = l.lane.value(time);
            if (!l.snap.empty()) {
                const auto it = std::lower_bound(l.snap.begin(), l.snap.end(), v);
                if (it == l.snap.end()) {
                    v = l.snap.back();
                } else if (it == l.snap.begin()) {
                    v = *it;
                } else {
                    v = (v - *(it - 1) < *it - v) ? *(it - 1) : *it;
                }
            }
            values[l.index] = float(v);
        }
    }

private:
    const std::vector<Lv2Lane> mLanes;
};

bool matches(const EffectInstanceFactory& factory)
{
    return dynamic_cast<const LV2EffectBase*>(&factory) != nullptr;
}

std::vector<ParamInfo> list(RealtimeEffectState& state)
{
    std::vector<ParamInfo> result;
    const auto* effect = lv2Of(state);
    if (!effect) {
        return result;
    }
    const auto* settings = state.GetSettings().cast<LV2EffectSettings>();
    size_t index = 0;
    for (const auto& port : effect->mPorts.mControlPorts) {
        const size_t valueIndex = index++;
        if (!automatable(*port)) {
            continue;
        }
        ParamInfo p;
        p.key = port->mSymbol.ToStdString();
        p.name = port->mName.ToStdString();
        p.units = port->mUnits.ToStdString();
        p.min = port->mMin;
        p.max = port->mMax;
        p.def = port->mDef;
        p.value = settings && valueIndex < settings->values.size() ? settings->values[valueIndex] : port->mDef;
        p.isInteger = port->mInteger || port->mToggle || port->mEnumeration;
        p.logarithmic = port->mLogarithmic;
        if (port->mToggle) {
            p.stepCount = 1;
        } else if (port->mEnumeration) {
            p.stepCount = std::max<int>(1, int(port->mScaleValues.size()) - 1);
        } else if (port->mInteger) {
            p.stepCount = std::max(1, int(std::lround(p.max - p.min)));
        }
        for (const auto& label : port->mScaleLabels) {
            p.enumLabels.push_back(label.ToStdString());
        }
        result.push_back(std::move(p));
    }
    return result;
}

std::shared_ptr<const RealtimeEffectAutomation> makeApplier(RealtimeEffectState& state, const std::vector<const ParamLane*>& lanes)
{
    const auto* effect = lv2Of(state);
    if (!effect) {
        return {};
    }
    std::vector<Lv2Lane> prepared;
    for (const auto* lane : lanes) {
        if (!lane || lane->points.empty()) {
            continue;
        }
        LV2ControlPortPtr port;
        const int index = valueIndex(*effect, lane->paramKey, &port);
        if (index < 0 || !port || !automatable(*port)) {
            continue;
        }
        Lv2Lane l;
        l.index = size_t(index);
        int steps = 0;
        if (port->mToggle) {
            steps = 1;
        } else if (port->mInteger && !port->mEnumeration) {
            steps = std::max(1, int(std::lround(port->mMax - port->mMin)));
        }
        l.lane = PreparedLane::make(*lane, port->mMin, port->mMax, port->mLogarithmic, steps);
        if (port->mEnumeration && !port->mScaleValues.empty()) {
            l.snap = port->mScaleValues;
            std::sort(l.snap.begin(), l.snap.end());
        }
        prepared.push_back(std::move(l));
    }
    if (prepared.empty()) {
        return {};
    }
    return std::make_shared<Lv2Applier>(std::move(prepared));
}

std::optional<std::string> valueText(RealtimeEffectState& state, const std::string& key, double value)
{
    const auto* effect = lv2Of(state);
    LV2ControlPortPtr port;
    if (!effect || valueIndex(*effect, key, &port) < 0 || !port) {
        return std::nullopt;
    }
    if (port->mToggle) {
        return std::string(value >= 0.5 ? "On" : "Off");
    }
    if (!port->mScaleValues.empty() && port->mScaleValues.size() == port->mScaleLabels.size()) {
        size_t best = 0;
        for (size_t i = 1; i < port->mScaleValues.size(); ++i) {
            if (std::fabs(port->mScaleValues[i] - value) < std::fabs(port->mScaleValues[best] - value)) {
                best = i;
            }
        }
        if (port->mEnumeration || std::fabs(port->mScaleValues[best] - value) < 1e-6) {
            return port->mScaleLabels[best].ToStdString();
        }
    }
    char buf[64];
    if (port->mInteger) {
        std::snprintf(buf, sizeof(buf), "%ld", std::lround(value));
    } else {
        std::snprintf(buf, sizeof(buf), std::fabs(value) < 10 ? "%.2f" : "%.1f", value);
    }
    std::string text = buf;
    const auto units = port->mUnits.ToStdString();
    if (!units.empty()) {
        text += " " + units;
    }
    return text;
}

const Handler handler { ParamFormat::LV2, &matches, &list, &makeApplier, &valueText };
}

const Handler& au::au3::paramautomation::formats::lv2Handler()
{
    return handler;
}
