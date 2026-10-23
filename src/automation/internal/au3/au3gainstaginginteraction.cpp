/*
* Audacity: A Digital Audio Editor
*/
#include "au3gainstaginginteraction.h"

#include "au3-wave-track/WaveTrack.h"

#include "au3wrap/internal/domaccessor.h"
#include "au3wrap/internal/gainstaging.h"
#include "trackedit/itrackeditproject.h"

#include "translation.h"

using namespace au::automation;
using namespace au::au3;

Au3GainStagingInteraction::Au3GainStagingInteraction(const muse::modularity::ContextPtr& ctx)
    : muse::Contextable(ctx) {}

Au3Project* Au3GainStagingInteraction::project() const
{
    auto prj = globalContext() ? globalContext()->currentProject() : nullptr;
    return prj ? reinterpret_cast<Au3Project*>(prj->au3ProjectPtr()) : nullptr;
}

void Au3GainStagingInteraction::notifyTrack(trackedit::TrackId trackId)
{
    auto prj = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (prj) {
        if (const auto t = prj->track(trackId)) {
            prj->notifyAboutTrackChanged(*t);
        }
    }
}

double Au3GainStagingInteraction::trimDb(trackedit::TrackId trackId) const
{
    auto* prj = project();
    const WaveTrack* track = prj ? DomAccessor::findWaveTrack(*prj, Au3TrackId(trackId)) : nullptr;
    return track ? gainstaging::trimDb(*track) : 0.0;
}

bool Au3GainStagingInteraction::setTrim(trackedit::TrackId trackId, double db, bool completed)
{
    auto* prj = project();
    WaveTrack* track = prj ? DomAccessor::findWaveTrack(*prj, Au3TrackId(trackId)) : nullptr;
    if (!track) {
        return false;
    }
    gainstaging::setTrimDb(*track, db);
    if (completed && projectHistory()) {
        projectHistory()->pushHistoryState(muse::trc("automation", "Changed input trim"),
                                           muse::trc("automation", "Input trim"));
        notifyTrack(trackId);
    }
    m_changed.notify();
    return true;
}

int Au3GainStagingInteraction::applyTrims(const std::map<trackedit::TrackId, double>& trims)
{
    auto* prj = project();
    if (!prj) {
        return 0;
    }
    int changed = 0;
    for (const auto& [id, db] : trims) {
        if (WaveTrack* track = DomAccessor::findWaveTrack(*prj, Au3TrackId(id))) {
            if (gainstaging::setTrimDb(*track, db)) {
                ++changed;
            }
        }
    }
    if (changed > 0) {
        if (projectHistory()) {
            projectHistory()->pushHistoryState(muse::trc("automation", "Applied gain staging trims"),
                                               muse::trc("automation", "Gain staging"));
        }
        for (const auto& [id, db] : trims) {
            notifyTrack(id);
        }
        m_changed.notify();
    }
    return changed;
}

std::vector<TrackLevels> Au3GainStagingInteraction::analyze() const
{
    std::vector<TrackLevels> result;
    auto* prj = project();
    if (!prj) {
        return result;
    }
    for (const auto& l : gainstaging::analyze(*prj)) {
        TrackLevels t;
        t.trackId = l.trackId;
        t.name = l.name;
        t.channels = l.channels;
        t.peakDb = l.peakDb;
        t.truePeakDb = l.truePeakDb;
        t.rmsDb = l.rmsDb;
        t.lufs = l.lufs;
        t.trimDb = l.trimDb;
        t.silent = l.silent;
        result.push_back(t);
    }
    return result;
}

muse::async::Notification Au3GainStagingInteraction::changed() const
{
    return m_changed;
}
