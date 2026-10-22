/*
* Audacity: A Digital Audio Editor
*/
#include "au3paramautomationinteraction.h"

#include <algorithm>

#include "au3-components/ComponentInterface.h"
#include "au3-components/EffectInterface.h"
#include "au3-realtime-effects/RealtimeEffectList.h"
#include "au3-realtime-effects/RealtimeEffectState.h"
#include "au3-wave-track/WaveTrack.h"

#include "au3wrap/internal/domaccessor.h"
#include "au3wrap/internal/paramautomation.h"

#include "translation.h"

using namespace au::automation;
using namespace au::au3;

Au3ParamAutomationInteraction::Au3ParamAutomationInteraction(const muse::modularity::ContextPtr& ctx)
    : muse::Contextable(ctx) {}

Au3Project* Au3ParamAutomationInteraction::project() const
{
    auto prj = globalContext() ? globalContext()->currentProject() : nullptr;
    return prj ? reinterpret_cast<Au3Project*>(prj->au3ProjectPtr()) : nullptr;
}

ParamAutomationData* Au3ParamAutomationInteraction::data(trackedit::TrackId trackId) const
{
    auto* prj = project();
    if (!prj) {
        return nullptr;
    }
    Au3WaveTrack* track = DomAccessor::findWaveTrack(*prj, Au3TrackId(trackId));
    return track ? &ParamAutomationData::Get(*track) : nullptr;
}

ParamLane* Au3ParamAutomationInteraction::activeLane(trackedit::TrackId trackId) const
{
    auto* d = data(trackId);
    if (!d || d->activeLane < 0 || d->activeLane >= int(d->lanes.size())) {
        return nullptr;
    }
    return &d->lanes[size_t(d->activeLane)];
}

std::vector<ParamTarget> Au3ParamAutomationInteraction::availableParams(trackedit::TrackId trackId) const
{
    std::vector<ParamTarget> result;
    auto* prj = project();
    if (!prj) {
        return result;
    }
    Au3WaveTrack* track = DomAccessor::findWaveTrack(*prj, Au3TrackId(trackId));
    if (!track) {
        return result;
    }
    auto& list = RealtimeEffectList::Get(*track);
    for (size_t i = 0, n = list.GetStatesCount(); i < n; ++i) {
        auto state = list.GetStateAt(i);
        if (!state) {
            continue;
        }
        const auto* factory = state->GetEffect();
        std::string name = state->GetID().ToStdString();
        if (auto* component = dynamic_cast<const ComponentInterface*>(factory)) {
            name = component->GetSymbol().Translation().ToStdString();
        }
        const int occ = paramautomation::occurrenceAt(list, i);
        if (occ > 0) {
            name += " " + std::to_string(occ + 1);
        }
        for (const auto& p : paramautomation::listParams(*state)) {
            if (p.min == p.max) {
                continue; // unranged values are not useful lanes
            }
            if (p.isBool && p.stepCount == 0) {
                continue; // built-in switches (plugin toggles carry stepCount 1 and are kept)
            }
            if (p.key.rfind("show", 0) == 0) {
                continue; // view-only flags of the dynamics effects (showInput, showOutput, ...)
            }
            ParamTarget t;
            t.effectId = state->GetID().ToStdString();
            t.occurrence = occ;
            t.paramKey = p.key;
            t.effectName = name;
            t.minValue = p.min;
            t.maxValue = p.max;
            t.defaultValue = p.value;
            t.isInteger = p.isInteger;
            t.paramName = p.name.empty() ? p.key : p.name;
            t.units = p.units;
            t.normalized = p.normalized;
            result.push_back(std::move(t));
        }
    }
    return result;
}

bool Au3ParamAutomationInteraction::isShown(trackedit::TrackId trackId) const
{
    auto* d = data(trackId);
    return d && d->shown;
}

void Au3ParamAutomationInteraction::setShown(trackedit::TrackId trackId, bool shown)
{
    auto* d = data(trackId);
    if (!d || d->shown == shown) {
        return;
    }
    d->shown = shown;
    if (shown && d->activeLane < 0) {
        // pick the first lane, or the first available parameter
        if (!d->lanes.empty()) {
            d->activeLane = 0;
        } else {
            auto params = availableParams(trackId);
            if (!params.empty()) {
                setActiveTarget(trackId, params.front());
            }
        }
    }
    projectHistory()->markUnsaved();
    m_changed.send(trackId);
}

std::optional<ParamTarget> Au3ParamAutomationInteraction::activeTarget(trackedit::TrackId trackId) const
{
    const auto* lane = activeLane(trackId);
    if (!lane) {
        return std::nullopt;
    }
    for (auto& t : availableParams(trackId)) {
        if (t.effectId == lane->effectId && t.occurrence == lane->occurrence && t.paramKey == lane->paramKey) {
            return t;
        }
    }
    // the effect was removed: keep editing the data with a neutral range
    ParamTarget t;
    t.effectId = lane->effectId;
    t.occurrence = lane->occurrence;
    t.paramKey = lane->paramKey;
    t.paramName = lane->paramKey;
    t.effectName = muse::trc("automation", "(missing effect)");
    double lo = 0, hi = 1;
    for (auto& p : lane->points) {
        lo = std::min(lo, p.v);
        hi = std::max(hi, p.v);
    }
    t.minValue = lo;
    t.maxValue = hi;
    return t;
}

bool Au3ParamAutomationInteraction::setActiveTarget(trackedit::TrackId trackId, const ParamTarget& target)
{
    auto* d = data(trackId);
    if (!d) {
        return false;
    }
    if (!d->find(target.effectId, target.occurrence, target.paramKey)) {
        ParamLane lane;
        lane.effectId = target.effectId;
        lane.occurrence = target.occurrence;
        lane.paramKey = target.paramKey;
        d->lanes.push_back(std::move(lane));
    }
    for (size_t i = 0; i < d->lanes.size(); ++i) {
        const auto& l = d->lanes[i];
        if (l.effectId == target.effectId && l.occurrence == target.occurrence && l.paramKey == target.paramKey) {
            d->activeLane = int(i);
        }
    }
    projectHistory()->markUnsaved();
    m_changed.send(trackId);
    return true;
}

AutomationPoints Au3ParamAutomationInteraction::activePoints(trackedit::TrackId trackId) const
{
    AutomationPoints pts;
    if (const auto* lane = activeLane(trackId)) {
        for (const auto& p : lane->points) {
            pts.push_back({ p.t, p.v });
        }
    }
    return pts;
}

void Au3ParamAutomationInteraction::afterEdit(trackedit::TrackId trackId, bool completed, const std::string& what)
{
    if (auto* prj = project()) {
        if (Au3WaveTrack* track = DomAccessor::findWaveTrack(*prj, Au3TrackId(trackId))) {
            paramautomation::installTrack(*track); // audible immediately, also while playing
        }
    }
    if (completed) {
        projectHistory()->pushHistoryState(what, what);
        m_editPending = false;
    } else {
        m_editPending = true;
    }
    m_changed.send(trackId);
}

int Au3ParamAutomationInteraction::addPoint(trackedit::TrackId trackId, double t, double value, bool completed)
{
    auto* lane = activeLane(trackId);
    if (!lane) {
        return -1;
    }
    const int index = int(paramautomation::insertPoint(lane->points, { std::max(0.0, t), value }));
    afterEdit(trackId, completed, muse::trc("automation", "Added automation point"));
    return index;
}

bool Au3ParamAutomationInteraction::setPoint(trackedit::TrackId trackId, int index, double t, double value, bool completed)
{
    auto* lane = activeLane(trackId);
    if (!lane || index < 0 || index >= int(lane->points.size())) {
        return false;
    }
    // keep the order: a point cannot be dragged across its neighbours
    const double eps = 1e-6;
    double lo = index > 0 ? lane->points[size_t(index) - 1].t + eps : 0.0;
    double hi = index + 1 < int(lane->points.size()) ? lane->points[size_t(index) + 1].t - eps : 1e12;
    lane->points[size_t(index)] = { std::clamp(t, lo, std::max(lo, hi)), value };
    afterEdit(trackId, completed, muse::trc("automation", "Moved automation point"));
    return true;
}

bool Au3ParamAutomationInteraction::removePoint(trackedit::TrackId trackId, int index, bool completed)
{
    auto* lane = activeLane(trackId);
    if (!lane || index < 0 || index >= int(lane->points.size())) {
        return false;
    }
    lane->points.erase(lane->points.begin() + index);
    afterEdit(trackId, completed, muse::trc("automation", "Removed automation point"));
    return true;
}

void Au3ParamAutomationInteraction::cancelEdit(trackedit::TrackId trackId)
{
    if (m_editPending) {
        projectHistory()->rollbackState();
        m_editPending = false;
        reinstall();
    }
    m_changed.send(trackId);
}

void Au3ParamAutomationInteraction::reinstall()
{
    if (auto* prj = project()) {
        paramautomation::install(*prj);
    }
}

muse::async::Channel<au::trackedit::TrackId> Au3ParamAutomationInteraction::changed() const
{
    return m_changed;
}

std::string Au3ParamAutomationInteraction::valueText(trackedit::TrackId trackId, double value) const
{
    const auto* lane = activeLane(trackId);
    auto* prj = project();
    Au3WaveTrack* track = prj ? DomAccessor::findWaveTrack(*prj, Au3TrackId(trackId)) : nullptr;
    if (!lane || !track) {
        return {};
    }
    auto& list = RealtimeEffectList::Get(*track);
    for (size_t i = 0, n = list.GetStatesCount(); i < n; ++i) {
        auto state = list.GetStateAt(i);
        if (state && state->GetID().ToStdString() == lane->effectId
            && paramautomation::occurrenceAt(list, i) == lane->occurrence) {
            if (paramautomation::formatOf(*state) == ParamFormat::BuiltIn) {
                return {}; // the model formats built-in values itself
            }
            return paramautomation::valueText(*state, lane->paramKey, value);
        }
    }
    return {};
}
