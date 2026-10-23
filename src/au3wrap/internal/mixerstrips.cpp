/*
* Audacity: A Digital Audio Editor
*/
#include "mixerstrips.h"

#include <cmath>

#include "au3-project/Project.h"
#include "au3-track/Track.h"
#include "au3-wave-track/WaveTrack.h"
#include "au3-realtime-effects/RealtimeEffectList.h"
#include "au3-realtime-effects/RealtimeEffectState.h"

#include "busrouting.h"
#include "gainstaging.h"

namespace au::au3::mixer {
namespace {
void fillInserts(const RealtimeEffectList& list, Strip& s)
{
    s.insertsActive = list.IsActive();
    for (size_t i = 0; i < list.GetStatesCount(); ++i) {
        const auto state = list.GetStateAt(i);
        if (!state) {
            continue;
        }
        s.inserts.push_back({ state->GetID().ToStdString(), state->GetSettings().extra.GetActive() });
    }
}
}

std::vector<Strip> strips(AudacityProject& project)
{
    std::vector<Strip> tracks;
    std::vector<Strip> buses;
    for (auto* wt : TrackList::Get(project).Any<WaveTrack>()) {
        Strip s;
        const auto& r = BusRoutingData::Get(*wt);
        s.trackId = wt->GetId();
        s.kind = r.isBus ? StripKind::Bus : StripKind::Track;
        s.name = wt->GetName().ToStdString();
        s.busId = r.busId;
        s.outputBusId = r.outputBusId;
        for (const auto& snd : r.sends) {
            s.sends.push_back({ snd.busId, snd.level });
        }
        const double vol = wt->GetVolume();
        s.volumeDb = vol > 0 ? 20.0 * std::log10(vol) : -60.0;
        s.pan = wt->GetPan();
        s.trimDb = gainstaging::trimDb(*wt);
        s.mute = wt->GetMute();
        s.solo = wt->GetSolo();
        s.channels = static_cast<int>(wt->NChannels());
        fillInserts(RealtimeEffectList::Get(*wt), s);
        (r.isBus ? buses : tracks).push_back(std::move(s));
    }

    std::vector<Strip> result = std::move(tracks);
    for (auto& b : buses) {
        result.push_back(std::move(b));
    }

    Strip master;
    master.name = "Master";
    fillInserts(RealtimeEffectList::Get(project), master);
    result.push_back(std::move(master));
    return result;
}

std::string outputName(AudacityProject& project, const Strip& strip)
{
    if (strip.kind == StripKind::Master || strip.outputBusId.empty()) {
        return "Master";
    }
    if (auto* bus = busrouting::findBus(project, strip.outputBusId)) {
        return bus->GetName().ToStdString();
    }
    return "Master";
}
}
