/*
* Audacity: A Digital Audio Editor
*/
#include "paramautomation.h"
#include "paramautomation_formats.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

#include "au3-components/EffectInterface.h"
#include "au3-components/SettingsVisitor.h"
#include "au3-realtime-effects/RealtimeEffectAutomation.h"
#include "au3-realtime-effects/RealtimeEffectList.h"
#include "au3-realtime-effects/RealtimeEffectState.h"
#include "au3-track/Track.h"
#include "au3-wave-track/WaveTrack.h"
#include "au3-xml/XMLWriter.h"

using namespace au::au3;

namespace {
const au::au3::paramautomation::formats::Handler* handlerFor(const EffectInstanceFactory& factory)
{
    for (const auto* h : au::au3::paramautomation::formats::handlers()) {
        if (h->matches(factory)) {
            return h;
        }
    }
    return nullptr;
}
}

namespace {
const ChannelGroup::Attachments::RegisteredFactory paramAutomationKey {
    [](auto&) { return std::make_unique<ParamAutomationData>(); }
};

WaveTrackIORegistry::ObjectReaderEntry paramAutomationReader {
    ParamAutomationData::XMLTag(),
    [](WaveTrack& track) { return &ParamAutomationData::Get(track); }
};

WaveTrackIORegistry::ObjectWriterEntry paramAutomationWriter {
    [](const WaveTrack& track, auto& xmlFile) { ParamAutomationData::Get(track).WriteXML(xmlFile); }
};

std::string encodePoints(const std::vector<ParamPoint>& pts)
{
    std::string s;
    char buf[64];
    for (const auto& p : pts) {
        std::snprintf(buf, sizeof(buf), "%s%.17g:%.17g", s.empty() ? "" : ",", p.t, p.v);
        s += buf;
    }
    return s;
}

std::vector<ParamPoint> decodePoints(const std::string& s)
{
    std::vector<ParamPoint> pts;
    const char* c = s.c_str();
    while (*c) {
        char* end = nullptr;
        const double t = std::strtod(c, &end);
        if (end == c || *end != ':') {
            break;
        }
        c = end + 1;
        const double v = std::strtod(c, &end);
        if (end == c) {
            break;
        }
        if (std::isfinite(t) && std::isfinite(v)) {
            paramautomation::insertPoint(pts, { t, v });
        }
        c = end;
        if (*c == ',') {
            ++c;
        }
    }
    return pts;
}

// ---- parameter enumeration --------------------------------------------------------------
class Collector final : public ConstSettingsVisitor
{
public:
    std::vector<ParamInfo> params;

    void add(const wxChar* key, double value, double def, double min, double max, bool isInt, bool isBool)
    {
        if (!key) {
            return;
        }
        ParamInfo p;
        p.key = wxString(key).ToStdString();
        p.value = value;
        p.def = def;
        p.min = std::min(min, max);
        p.max = std::max(min, max);
        p.isInteger = isInt;
        p.isBool = isBool;
        params.push_back(std::move(p));
    }

    void Define(bool var, const wxChar* key, bool vdefault, bool, bool, bool) override
    {
        add(key, var, vdefault, 0, 1, true, true);
    }

    void Define(size_t var, const wxChar* key, int vdefault, int vmin, int vmax, int) override
    {
        add(key, double(var), vdefault, vmin, vmax, true, false);
    }

    void Define(int var, const wxChar* key, int vdefault, int vmin, int vmax, int) override
    {
        add(key, var, vdefault, vmin, vmax, true, false);
    }

    void Define(float var, const wxChar* key, float vdefault, float vmin, float vmax, float) override
    {
        add(key, var, vdefault, vmin, vmax, false, false);
    }

    void Define(double var, const wxChar* key, float vdefault, float vmin, float vmax, float) override
    {
        add(key, var, vdefault, vmin, vmax, false, false);
    }

    void Define(double var, const wxChar* key, double vdefault, double vmin, double vmax, double) override
    {
        add(key, var, vdefault, vmin, vmax, false, false);
    }

    void DefineEnum(int var, const wxChar* key, int vdefault, const EnumValueSymbol[], size_t nStrings) override
    {
        add(key, var, vdefault, 0, nStrings ? double(nStrings - 1) : 0.0, true, false);
    }
};

// ---- realtime applier --------------------------------------------------------------------
struct AppliedLane {
    std::wstring key;
    bool step = false;
    std::vector<ParamPoint> points;
};

//! Writes envelope values into the matching settings members. No allocation, no locks.
class Writer final : public SettingsVisitor
{
public:
    Writer(const std::vector<AppliedLane>& lanes, double t) noexcept
        : mLanes(lanes), mT(t) {}

    void Define(bool& var, const wxChar* key, bool, bool, bool, bool) override
    {
        if (auto v = value(key)) {
            var = *v >= 0.5;
        }
    }

    void Define(size_t& var, const wxChar* key, int, int vmin, int vmax, int) override
    {
        if (auto v = value(key)) {
            var = size_t(std::max(0.0, clamp(std::round(*v), vmin, vmax)));
        }
    }

    void Define(int& var, const wxChar* key, int, int vmin, int vmax, int) override
    {
        if (auto v = value(key)) {
            var = int(clamp(std::round(*v), vmin, vmax));
        }
    }

    void Define(float& var, const wxChar* key, float, float vmin, float vmax, float) override
    {
        if (auto v = value(key)) {
            var = float(clamp(*v, vmin, vmax));
        }
    }

    void Define(double& var, const wxChar* key, float, float vmin, float vmax, float) override
    {
        if (auto v = value(key)) {
            var = clamp(*v, vmin, vmax);
        }
    }

    void Define(double& var, const wxChar* key, double, double vmin, double vmax, double) override
    {
        if (auto v = value(key)) {
            var = clamp(*v, vmin, vmax);
        }
    }

    void DefineEnum(int& var, const wxChar* key, int, const EnumValueSymbol[], size_t nStrings) override
    {
        if (auto v = value(key); v && nStrings > 0) {
            var = int(clamp(std::round(*v), 0, double(nStrings - 1)));
        }
    }

private:
    static double clamp(double v, double a, double b) noexcept
    {
        if (a > b) {
            std::swap(a, b);
        }
        if (a == b) { // no range declared
            return v;
        }
        return std::min(b, std::max(a, v));
    }

    std::optional<double> value(const wxChar* key) const noexcept
    {
        if (!key) {
            return std::nullopt;
        }
        for (const auto& lane : mLanes) {
            if (std::wcscmp(key, lane.key.c_str()) == 0) {
                return paramautomation::evaluate(lane.points.data(), lane.points.size(), lane.step, mT, 0.0);
            }
        }
        return std::nullopt;
    }

    const std::vector<AppliedLane>& mLanes;
    const double mT;
};

class Applier final : public RealtimeEffectAutomation
{
public:
    Applier(EffectSettingsManager& manager, std::vector<AppliedLane> lanes)
        : mManager(manager), mLanes(std::move(lanes)) {}

    void Apply(EffectSettings& settings, double time, EffectInstance*) const noexcept override
    {
        Writer writer(mLanes, time);
        mManager.VisitSettings(writer, settings);
    }

private:
    EffectSettingsManager& mManager;
    const std::vector<AppliedLane> mLanes;
};
}

// ---- ParamAutomationData -----------------------------------------------------------------
ParamAutomationData& ParamAutomationData::Get(ChannelGroup& group)
{
    return group.Attachments::Get<ParamAutomationData>(paramAutomationKey);
}

const ParamAutomationData& ParamAutomationData::Get(const ChannelGroup& group)
{
    return Get(const_cast<ChannelGroup&>(group));
}

const std::string& ParamAutomationData::XMLTag()
{
    static const std::string tag = "paramlane";
    return tag;
}

std::unique_ptr<ClientData::Cloneable<> > ParamAutomationData::Clone() const
{
    return std::make_unique<ParamAutomationData>(*this);
}

ParamLane* ParamAutomationData::find(const std::string& effectId, int occurrence, const std::string& key)
{
    for (auto& l : lanes) {
        if (l.effectId == effectId && l.occurrence == occurrence && l.paramKey == key) {
            return &l;
        }
    }
    return nullptr;
}

bool ParamAutomationData::HandleXMLTag(const std::string_view& tag, const AttributesList& attrs)
{
    if (tag != XMLTag()) {
        return false;
    }
    ParamLane lane;
    for (const auto& [attr, value] : attrs) {
        if (attr == "effect") {
            lane.effectId = value.ToWString().ToStdString();
        } else if (attr == "occurrence") {
            long long v = 0;
            if (value.TryGet(v)) {
                lane.occurrence = int(v);
            }
        } else if (attr == "param") {
            lane.paramKey = value.ToWString().ToStdString();
        } else if (attr == "step") {
            bool v = false;
            if (value.TryGet(v)) {
                lane.step = v;
            }
        } else if (attr == "visible") {
            bool v = true;
            if (value.TryGet(v)) {
                lane.visible = v;
            }
        } else if (attr == "points") {
            lane.points = decodePoints(value.ToWString().ToStdString());
        } else if (attr == "shown") {
            bool v = false;
            if (value.TryGet(v)) {
                shown = v;
            }
        } else if (attr == "active") {
            bool v = false;
            if (value.TryGet(v) && v) {
                activeLane = int(lanes.size());
            }
        }
    }
    if (!lane.effectId.empty() && !lane.paramKey.empty()) {
        lanes.push_back(std::move(lane));
    }
    return true;
}

void ParamAutomationData::WriteXML(XMLWriter& xmlFile) const
{
    for (size_t i = 0; i < lanes.size(); ++i) {
        const auto& lane = lanes[i];
        xmlFile.StartTag(XMLTag());
        xmlFile.WriteAttr(wxT("effect"), wxString(lane.effectId));
        xmlFile.WriteAttr(wxT("occurrence"), lane.occurrence);
        xmlFile.WriteAttr(wxT("param"), wxString(lane.paramKey));
        xmlFile.WriteAttr(wxT("step"), lane.step);
        xmlFile.WriteAttr(wxT("visible"), lane.visible);
        xmlFile.WriteAttr(wxT("points"), wxString(encodePoints(lane.points)));
        xmlFile.WriteAttr(wxT("shown"), shown);
        xmlFile.WriteAttr(wxT("active"), int(i) == activeLane);
        xmlFile.EndTag(XMLTag());
    }
}

// ---- functions ---------------------------------------------------------------------------
double paramautomation::evaluate(const ParamPoint* pts, size_t n, bool step, double t, double fallback) noexcept
{
    if (n == 0) {
        return fallback;
    }
    if (t <= pts[0].t) {
        return pts[0].v;
    }
    if (t >= pts[n - 1].t) {
        return pts[n - 1].v;
    }
    // binary search for the segment [lo, lo+1] containing t
    size_t lo = 0, hi = n - 1;
    while (hi - lo > 1) {
        const size_t mid = (lo + hi) / 2;
        if (pts[mid].t <= t) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    const auto& a = pts[lo];
    const auto& b = pts[hi];
    if (step || b.t <= a.t) {
        return a.v;
    }
    return a.v + (b.v - a.v) * (t - a.t) / (b.t - a.t);
}

size_t paramautomation::insertPoint(std::vector<ParamPoint>& pts, ParamPoint p)
{
    auto it = std::lower_bound(pts.begin(), pts.end(), p.t,
                               [](const ParamPoint& a, double t) { return a.t < t; });
    if (it != pts.end() && it->t == p.t) {
        it->v = p.v;
        return size_t(it - pts.begin());
    }
    it = pts.insert(it, p);
    return size_t(it - pts.begin());
}

std::vector<ParamInfo> paramautomation::listParams(const RealtimeEffectState& cstate)
{
    auto& state = const_cast<RealtimeEffectState&>(cstate);
    const auto* factory = state.GetEffect();
    if (!factory) {
        return {};
    }
    if (const auto* handler = handlerFor(*factory)) {
        return handler->list(state);
    }
    Collector collector;
    factory->VisitSettings(collector, cstate.GetSettings());
    return collector.params;
}

ParamFormat paramautomation::formatOf(const RealtimeEffectState& cstate)
{
    auto& state = const_cast<RealtimeEffectState&>(cstate);
    const auto* factory = state.GetEffect();
    const auto* handler = factory ? handlerFor(*factory) : nullptr;
    return handler ? handler->format : ParamFormat::BuiltIn;
}

std::string paramautomation::valueText(const RealtimeEffectState& cstate, const std::string& key, double value)
{
    auto& state = const_cast<RealtimeEffectState&>(cstate);
    const auto* factory = state.GetEffect();
    if (const auto* handler = factory ? handlerFor(*factory) : nullptr) {
        if (auto text = handler->valueText(state, key, value)) {
            return *text;
        }
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), std::fabs(value) < 10 ? "%.2f" : "%.1f", value);
    return buf;
}

// ---- format support ------------------------------------------------------------------------
using namespace au::au3::paramautomation::formats;

const std::vector<const Handler*>& paramautomation::formats::handlers()
{
    static const std::vector<const Handler*> all = [] {
        std::vector<const Handler*> h;
#ifdef AU_PARAMAUTOMATION_VST3
        h.push_back(&vst3Handler());
#endif
#ifdef AU_PARAMAUTOMATION_LV2
        h.push_back(&lv2Handler());
#endif
        return h;
    }();
    return all;
}

PreparedLane PreparedLane::make(const ParamLane& lane, double min, double max, bool logarithmic, int stepCount)
{
    PreparedLane p;
    p.step = lane.step;
    p.min = std::min(min, max);
    p.max = std::max(min, max);
    p.stepCount = stepCount;
    p.logarithmic = logarithmic && p.min > 0.0;
    p.points = lane.points;
    if (p.logarithmic) {
        for (auto& pt : p.points) {
            pt.v = std::log(std::clamp(pt.v, p.min, p.max));
        }
    }
    return p;
}

double PreparedLane::value(double t) const noexcept
{
    double v = evaluate(points.data(), points.size(), step, t, logarithmic ? std::log(min) : min);
    if (logarithmic) {
        v = std::exp(v);
    }
    v = std::clamp(v, min, max);
    if (stepCount > 0 && max > min) {
        const double k = std::round((v - min) / (max - min) * stepCount);
        v = min + (max - min) * k / stepCount;
    }
    return v;
}

int paramautomation::occurrenceAt(const RealtimeEffectList& list, size_t index)
{
    const auto target = list.GetStateAt(index);
    if (!target) {
        return 0;
    }
    int occ = 0;
    for (size_t i = 0; i < index; ++i) {
        if (auto s = list.GetStateAt(i); s && s->GetID() == target->GetID()) {
            ++occ;
        }
    }
    return occ;
}

std::shared_ptr<const RealtimeEffectAutomation> paramautomation::makeApplier(
    const RealtimeEffectState& cstate, const std::vector<const ParamLane*>& lanes)
{
    auto& state = const_cast<RealtimeEffectState&>(cstate);
    auto* factory = const_cast<EffectInstanceFactory*>(state.GetEffect());
    if (!factory || lanes.empty()) {
        return {};
    }
    if (const auto* handler = handlerFor(*factory)) {
        return handler->makeApplier(state, lanes);
    }
    std::vector<AppliedLane> applied;
    for (const auto* lane : lanes) {
        if (!lane || lane->points.empty()) {
            continue;
        }
        applied.push_back({ wxString(lane->paramKey).ToStdWstring(), lane->step, lane->points });
    }
    if (applied.empty()) {
        return {};
    }
    return std::make_shared<Applier>(*factory, std::move(applied));
}

void paramautomation::installTrack(WaveTrack& track)
{
    auto& list = RealtimeEffectList::Get(track);
    const auto& data = ParamAutomationData::Get(track);
    for (size_t i = 0, n = list.GetStatesCount(); i < n; ++i) {
        const auto state = list.GetStateAt(i);
        if (!state) {
            continue;
        }
        const auto id = state->GetID().ToStdString();
        const int occ = occurrenceAt(list, i);
        std::vector<const ParamLane*> lanes;
        for (const auto& lane : data.lanes) {
            if (lane.effectId == id && lane.occurrence == occ && !lane.points.empty()) {
                lanes.push_back(&lane);
            }
        }
        auto applier = makeApplier(*state, lanes);
        if (applier || state->GetAutomation()) {
            state->SetAutomation(std::move(applier));
        }
    }
}

void paramautomation::install(AudacityProject& project)
{
#ifdef AU_PARAMAUTOMATION_VST3
    formats::releaseVst3Instances();
#endif
    for (auto* track : TrackList::Get(project).Any<WaveTrack>()) {
        installTrack(*track);
    }
}
