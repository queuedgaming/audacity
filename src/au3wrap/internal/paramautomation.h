/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "au3-registries/ClientData.h"
#include "au3-xml/XMLTagHandler.h"

class AudacityProject;
class ChannelGroup;
class RealtimeEffectState;
class RealtimeEffectList;
class RealtimeEffectAutomation;
class WaveTrack;
class XMLWriter;

namespace au::au3 {
//! One breakpoint of a parameter envelope: project time (s) -> real parameter value
struct ParamPoint {
    double t = 0.0;
    double v = 0.0;
    bool operator==(const ParamPoint& o) const { return t == o.t && v == o.v; }
};

//! Automation of one parameter of one realtime effect slot of a track.
//! The slot is identified by (plugin id, occurrence among the same plugin in the track's list).
struct ParamLane {
    std::string effectId;
    int occurrence = 0;
    std::string paramKey;
    bool step = false;     //!< hold each value until the next point instead of interpolating
    bool visible = true;
    std::vector<ParamPoint> points; //!< sorted by t

    bool sameTarget(const ParamLane& o) const
    {
        return effectId == o.effectId && occurrence == o.occurrence && paramKey == o.paramKey;
    }
};

//! Per-track automation data, attached to the wave track; saved in <paramlane> tags of the
//! <wavetrack> element and cloned with the track (so undo/redo restores it).
class ParamAutomationData final : public ClientData::Cloneable<>, public XMLTagHandler
{
public:
    static ParamAutomationData& Get(ChannelGroup& group);
    static const ParamAutomationData& Get(const ChannelGroup& group);
    static const std::string& XMLTag();

    std::unique_ptr<ClientData::Cloneable<> > Clone() const override;
    bool HandleXMLTag(const std::string_view& tag, const AttributesList& attrs) override;
    XMLTagHandler* HandleXMLChild(const std::string_view&) override { return nullptr; }
    void WriteXML(XMLWriter& xmlFile) const;

    ParamLane* find(const std::string& effectId, int occurrence, const std::string& key);

    std::vector<ParamLane> lanes;
    bool shown = false; //!< "Show automation" view state of the track
    int activeLane = -1; //!< index of the lane shown in the overlay
};

//! Description of an automatable (numeric) parameter of an effect
struct ParamInfo {
    std::string key;
    double min = 0.0;
    double max = 1.0;
    double def = 0.0;
    double value = 0.0;   //!< current (unautomated) value in the effect's settings
    bool isInteger = false;
    bool isBool = false;

    // Plugin formats (VST3, LV2)
    std::string name;      //!< display name (empty: use key)
    std::string units;     //!< e.g. "dB", "Hz"
    bool normalized = false; //!< value range is the plugin's normalized 0..1 (VST3)
    bool logarithmic = false; //!< envelope interpolates geometrically (LV2 lv2:logarithmic)
    int stepCount = 0;       //!< >0: discrete (VST3 stepCount; LV2 enumeration/integer)
    std::vector<std::string> enumLabels; //!< labels of discrete values, if any
};

enum class ParamFormat {
    BuiltIn, //!< settings visited with SettingsVisitor; key = settings key
    VST3,    //!< key = decimal VST3 ParamID; lane values normalized 0..1
    LV2,     //!< key = control port symbol; lane values plain port values
};

namespace paramautomation {
//! Envelope value at `t`: linear (or step) between points, holding the end values outside.
//! `fallback` is returned for an empty envelope. Allocation-free.
double evaluate(const ParamPoint* points, size_t count, bool step, double t, double fallback) noexcept;
inline double evaluate(const std::vector<ParamPoint>& pts, bool step, double t, double fallback) noexcept
{
    return evaluate(pts.data(), pts.size(), step, t, fallback);
}

//! Insert keeping time order; replaces a point at exactly the same time. Returns its index.
size_t insertPoint(std::vector<ParamPoint>& pts, ParamPoint p);

//! Numeric parameters of the effect of `state`, with ranges and current values
std::vector<ParamInfo> listParams(const RealtimeEffectState& state);

//! Which plugin format (and so which key/value convention) the effect of `state` uses
ParamFormat formatOf(const RealtimeEffectState& state);

//! Display text of `value` for parameter `key` (main thread; uses the plugin's own
//! value-to-string where it has one, e.g. VST3 getParamStringByValue, LV2 scale point labels)
std::string valueText(const RealtimeEffectState& state, const std::string& key, double value);

//! occurrence of the state at `index` among states of the same plugin in `list`
int occurrenceAt(const RealtimeEffectList& list, size_t index);

//! Build the realtime applier for `lanes` (all of the same effect slot) or null if nothing to do
std::shared_ptr<const RealtimeEffectAutomation> makeApplier(const RealtimeEffectState& state,
                                                             const std::vector<const ParamLane*>& lanes);

//! (Re)install appliers on all realtime effect states of `track` / of all tracks of `project`.
//! Main thread. Safe while playing (see RealtimeEffectState::SetAutomation).
void installTrack(WaveTrack& track);
void install(AudacityProject& project);
}
}
