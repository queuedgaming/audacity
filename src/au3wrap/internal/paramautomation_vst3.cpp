/*
* Audacity: A Digital Audio Editor
*/
#include "paramautomation_formats.h"

#include <map>
#include <string>

#include <pluginterfaces/vst/ivsteditcontroller.h>

#include "au3-realtime-effects/RealtimeEffectAutomation.h"
#include "au3-realtime-effects/RealtimeEffectState.h"
#include "au3-vst3/VST3EffectBase.h"
#include "au3-vst3/VST3Instance.h"
#include "au3-vst3/VST3Utils.h"

using namespace au::au3;
using namespace au::au3::paramautomation::formats;
using namespace Steinberg;

namespace {
//! Main-thread instances kept for parameter queries, so that repeated value-to-string calls
//! do not instantiate the plugin each time. Dropped by paramautomation::install().
std::map<const RealtimeEffectState*, std::shared_ptr<EffectInstance> >& instanceCache()
{
    static std::map<const RealtimeEffectState*, std::shared_ptr<EffectInstance> > cache;
    return cache;
}

IPtr<Vst::IEditController> controllerOf(RealtimeEffectState& state)
{
    auto& cached = instanceCache()[&state];
    if (!cached) {
        cached = state.GetInstance();
    }
    auto* vst3 = dynamic_cast<VST3Instance*>(cached.get());
    return vst3 ? vst3->vstEditController() : IPtr<Vst::IEditController> {};
}

std::optional<Vst::ParamID> parseId(const std::string& key)
{
    if (key.empty() || key.find_first_not_of("0123456789") != std::string::npos) {
        return std::nullopt;
    }
    try {
        return Vst::ParamID(std::stoul(key));
    } catch (...) {
        return std::nullopt;
    }
}

//! Is the parameter one the user may want to automate?
bool automatable(const Vst::ParameterInfo& info)
{
    constexpr int32 excluded = Vst::ParameterInfo::kIsReadOnly | Vst::ParameterInfo::kIsHidden
                               | Vst::ParameterInfo::kIsProgramChange | Vst::ParameterInfo::kIsBypass;
    return (info.flags & excluded) == 0 && (info.flags & Vst::ParameterInfo::kCanAutomate) != 0;
}

class Vst3Applier final : public RealtimeEffectAutomation
{
public:
    explicit Vst3Applier(std::vector<std::pair<Vst::ParamID, PreparedLane> > lanes)
        : mLanes(std::move(lanes)) {}

    void Apply(EffectSettings&, double time, EffectInstance* instance) const noexcept override
    {
        // Plugin parameters live in the plugin: queue them for the instance's next process()
        auto* vst3 = dynamic_cast<VST3Instance*>(instance);
        if (!vst3) {
            return;
        }
        for (const auto& [id, lane] : mLanes) {
            vst3->QueueParameterChange(id, lane.value(time));
        }
    }

private:
    const std::vector<std::pair<Vst::ParamID, PreparedLane> > mLanes;
};

bool matches(const EffectInstanceFactory& factory)
{
    return dynamic_cast<const VST3EffectBase*>(&factory) != nullptr;
}

std::vector<ParamInfo> list(RealtimeEffectState& state)
{
    std::vector<ParamInfo> result;
    auto controller = controllerOf(state);
    if (!controller) {
        return result;
    }
    for (int32 i = 0, n = controller->getParameterCount(); i < n; ++i) {
        Vst::ParameterInfo info {};
        if (controller->getParameterInfo(i, info) != kResultOk || !automatable(info)) {
            continue;
        }
        ParamInfo p;
        p.key = std::to_string(info.id);
        p.name = VST3Utils::UTF16ToStdString(info.title);
        p.units = VST3Utils::UTF16ToStdString(info.units);
        p.min = 0.0;
        p.max = 1.0;
        p.def = info.defaultNormalizedValue;
        p.value = controller->getParamNormalized(info.id);
        p.normalized = true;
        p.stepCount = info.stepCount;
        p.isInteger = info.stepCount > 0;
        if (info.stepCount > 0 && info.stepCount <= 64) {
            for (int32 k = 0; k <= info.stepCount; ++k) {
                Vst::String128 text {};
                if (controller->getParamStringByValue(info.id, double(k) / info.stepCount, text) == kResultOk) {
                    p.enumLabels.push_back(VST3Utils::UTF16ToStdString(text));
                }
            }
        }
        result.push_back(std::move(p));
    }
    return result;
}

std::shared_ptr<const RealtimeEffectAutomation> makeApplier(RealtimeEffectState&, const std::vector<const ParamLane*>& lanes)
{
    std::vector<std::pair<Vst::ParamID, PreparedLane> > prepared;
    for (const auto* lane : lanes) {
        if (!lane || lane->points.empty()) {
            continue;
        }
        if (auto id = parseId(lane->paramKey)) {
            // discrete parameters: the plugin maps normalized values to its steps itself
            prepared.emplace_back(*id, PreparedLane::make(*lane, 0.0, 1.0, false, 0));
        }
    }
    if (prepared.empty()) {
        return {};
    }
    return std::make_shared<Vst3Applier>(std::move(prepared));
}

std::optional<std::string> valueText(RealtimeEffectState& state, const std::string& key, double value)
{
    auto id = parseId(key);
    auto controller = id ? controllerOf(state) : IPtr<Vst::IEditController> {};
    if (!controller) {
        return std::nullopt;
    }
    Vst::String128 text {};
    if (controller->getParamStringByValue(*id, value, text) != kResultOk) {
        return std::nullopt;
    }
    std::string result = VST3Utils::UTF16ToStdString(text);
    Vst::ParameterInfo info {};
    for (int32 i = 0, n = controller->getParameterCount(); i < n; ++i) {
        if (controller->getParameterInfo(i, info) == kResultOk && info.id == *id) {
            const auto units = VST3Utils::UTF16ToStdString(info.units);
            if (!units.empty()) {
                result += " " + units;
            }
            break;
        }
    }
    return result;
}

const Handler handler { ParamFormat::VST3, &matches, &list, &makeApplier, &valueText };
}

const Handler& au::au3::paramautomation::formats::vst3Handler()
{
    return handler;
}

void au::au3::paramautomation::formats::releaseVst3Instances()
{
    instanceCache().clear();
}
