/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "au3-registries/ClientData.h"
#include "au3-xml/XMLTagHandler.h"

class AudacityProject;
class ChannelGroup;
class WaveTrack;
class WideSampleSequence;
class XMLWriter;
struct BusRoutingPlan;

namespace au::au3 {
//! A post-fader send from a track (or bus) to a bus
struct BusSend {
    std::string busId;
    float level = 1.0f; //!< linear
    bool operator==(const BusSend& o) const { return busId == o.busId && level == o.level; }
};

//! Routing data attached to every wave track; saved as a <busrouting> tag inside <wavetrack>
//! and cloned with the track, so undo/redo and copies keep it.
//! A bus is an (empty) wave track with isBus set: it keeps its own realtime effects,
//! volume, pan, mute and solo.
class BusRoutingData final : public ClientData::Cloneable<>, public XMLTagHandler
{
public:
    static BusRoutingData& Get(ChannelGroup& group);
    static const BusRoutingData& Get(const ChannelGroup& group);
    static const std::string& XMLTag();

    std::unique_ptr<ClientData::Cloneable<> > Clone() const override;
    bool HandleXMLTag(const std::string_view& tag, const AttributesList& attrs) override;
    XMLTagHandler* HandleXMLChild(const std::string_view&) override { return nullptr; }
    void WriteXML(XMLWriter& xmlFile) const;

    bool isDefault() const { return !isBus && outputBusId.empty() && sends.empty(); }

    bool isBus = false;
    std::string busId;       //!< stable id of a bus (persisted); empty for normal tracks
    std::string outputBusId; //!< empty: master
    std::vector<BusSend> sends;
};

namespace busrouting {
//! Install the plan provider used by playback (AudioIO) and export (Mixer). Idempotent.
void install();

bool isBus(const WaveTrack& track);
std::vector<WaveTrack*> buses(AudacityProject& project);
WaveTrack* findBus(AudacityProject& project, const std::string& busId);

//! Append a new empty stereo wave track named "Bus N" and make it a bus
WaveTrack* addBusTrack(AudacityProject& project);

//! Turn `track` (expected empty) into a bus with a fresh id; returns the id
std::string makeBus(AudacityProject& project, WaveTrack& track);

//! Would routing `from`'s output or a send of it into bus `toBusId` create a cycle?
bool createsCycle(AudacityProject& project, const WaveTrack& from, const std::string& toBusId);

//! Set the output (empty busId = master). Refuses (returns false) cycles and unknown buses.
bool setOutput(AudacityProject& project, WaveTrack& track, const std::string& busId);
//! Add or update a send. Refuses cycles, unknown buses and sends to the output bus' own target duplicates.
bool setSend(AudacityProject& project, WaveTrack& track, const std::string& busId, float level);
bool removeSend(WaveTrack& track, const std::string& busId);

//! Plan for a set of sequences (pure; used by the provider and tests). References to buses that
//! are not among `sequences`, and anything that would form a cycle, fall back to master / are dropped.
BusRoutingPlan makePlan(const std::vector<const WideSampleSequence*>& sequences);
}
}
