/*
* Audacity: A Digital Audio Editor
*/
#include "au3busroutinginteraction.h"

#include "au3-wave-track/WaveTrack.h"

#include "au3wrap/internal/busrouting.h"
#include "au3wrap/internal/domaccessor.h"
#include "au3wrap/internal/domconverter.h"
#include "trackedit/itrackeditproject.h"

#include "translation.h"

using namespace au::automation;
using namespace au::au3;

Au3BusRoutingInteraction::Au3BusRoutingInteraction(const muse::modularity::ContextPtr& ctx)
    : muse::Contextable(ctx) {}

Au3Project* Au3BusRoutingInteraction::project() const
{
    auto prj = globalContext() ? globalContext()->currentProject() : nullptr;
    return prj ? reinterpret_cast<Au3Project*>(prj->au3ProjectPtr()) : nullptr;
}

std::vector<RoutingNode> Au3BusRoutingInteraction::nodes() const
{
    std::vector<RoutingNode> result;
    auto* prj = project();
    if (!prj) {
        return result;
    }
    for (auto* t : TrackList::Get(*prj).Any<WaveTrack>()) {
        const auto& d = BusRoutingData::Get(*t);
        RoutingNode n;
        n.trackId = t->GetId();
        n.name = t->GetName().ToStdString();
        n.isBus = d.isBus;
        n.busId = d.busId;
        n.outputBusId = d.outputBusId;
        for (const auto& s : d.sends) {
            n.sends.push_back({ s.busId, s.level });
        }
        result.push_back(std::move(n));
    }
    return result;
}

void Au3BusRoutingInteraction::commit(const std::string& what)
{
    if (projectHistory()) {
        projectHistory()->pushHistoryState(what, what);
    }
    m_changed.notify();
}

au::trackedit::TrackId Au3BusRoutingInteraction::addBus()
{
    auto* prj = project();
    if (!prj) {
        return trackedit::INVALID_TRACK;
    }
    WaveTrack* bus = busrouting::addBusTrack(*prj);
    if (!bus) {
        return trackedit::INVALID_TRACK;
    }
    if (auto tprj = globalContext()->currentTrackeditProject()) {
        tprj->notifyAboutTrackAdded(DomConverter::track(bus));
    }
    commit(muse::trc("automation", "Added bus"));
    return bus->GetId();
}

bool Au3BusRoutingInteraction::canRouteTo(trackedit::TrackId trackId, const std::string& busId) const
{
    auto* prj = project();
    WaveTrack* track = prj ? DomAccessor::findWaveTrack(*prj, Au3TrackId(trackId)) : nullptr;
    if (!track) {
        return false;
    }
    if (busId.empty()) {
        return true;
    }
    return busrouting::findBus(*prj, busId) && !busrouting::createsCycle(*prj, *track, busId);
}

bool Au3BusRoutingInteraction::setOutput(trackedit::TrackId trackId, const std::string& busId)
{
    auto* prj = project();
    WaveTrack* track = prj ? DomAccessor::findWaveTrack(*prj, Au3TrackId(trackId)) : nullptr;
    if (!track || BusRoutingData::Get(*track).outputBusId == busId) {
        return false;
    }
    if (!busrouting::setOutput(*prj, *track, busId)) {
        return false;
    }
    commit(muse::trc("automation", "Changed track output"));
    return true;
}

bool Au3BusRoutingInteraction::setSend(trackedit::TrackId trackId, const std::string& busId, float level, bool completed)
{
    auto* prj = project();
    WaveTrack* track = prj ? DomAccessor::findWaveTrack(*prj, Au3TrackId(trackId)) : nullptr;
    if (!track || !busrouting::setSend(*prj, *track, busId, level)) {
        return false;
    }
    if (completed) {
        commit(muse::trc("automation", "Changed send"));
    } else {
        m_changed.notify();
    }
    return true;
}

bool Au3BusRoutingInteraction::removeSend(trackedit::TrackId trackId, const std::string& busId)
{
    auto* prj = project();
    WaveTrack* track = prj ? DomAccessor::findWaveTrack(*prj, Au3TrackId(trackId)) : nullptr;
    if (!track || !busrouting::removeSend(*track, busId)) {
        return false;
    }
    commit(muse::trc("automation", "Removed send"));
    return true;
}

muse::async::Notification Au3BusRoutingInteraction::changed() const
{
    return m_changed;
}
