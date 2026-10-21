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
class XMLWriter;

namespace au::record {
//! Take-lane membership of a wave track, attached to the track and saved in the project.
//! A main track has a groupId and takeIndex 0; each preserved take is a separate,
//! muted wave track sharing the groupId with takeIndex 1, 2, ...
class TakeLaneInfo final : public ClientData::Cloneable<>, public XMLTagHandler
{
public:
    static TakeLaneInfo& Get(ChannelGroup& group);
    static const TakeLaneInfo& Get(const ChannelGroup& group);
    static const std::string& XMLTag();

    std::unique_ptr<ClientData::Cloneable<> > Clone() const override;

    bool HandleXMLTag(const std::string_view& tag, const AttributesList& attrs) override;
    XMLTagHandler* HandleXMLChild(const std::string_view&) override { return nullptr; }
    void WriteXML(XMLWriter& xmlFile) const;

    bool isInGroup() const { return !groupId.empty(); }
    bool isTake() const { return isInGroup() && takeIndex > 0; }

    std::string groupId;
    int takeIndex = 0;
};

namespace takelanes {
//! Snapshot of a track's audio from `t0` onwards, taken before recording over it.
//! Shares sample blocks with the track, so it is cheap. Null if nothing to preserve.
std::shared_ptr<WaveTrack> snapshotFrom(const WaveTrack& track, double t0);

//! Turn the part of `snapshot` (taken at `t0`) that the new recording [t0, t1] overwrote
//! into a new muted take track placed in the project. Returns null if no audio was overwritten.
WaveTrack* preserveAsTake(AudacityProject& project, WaveTrack& mainTrack,
                          const WaveTrack& snapshot, double t0, double t1);

//! All take tracks of `mainTrack`, ordered by take index.
std::vector<WaveTrack*> takesOf(AudacityProject& project, const WaveTrack& mainTrack);

//! Make `take` the active one: swap its audio region with the main track's, so the
//! previously active audio becomes this take lane. Returns false if `take` is not a take of `mainTrack`.
bool activateTake(AudacityProject& project, WaveTrack& mainTrack, WaveTrack& take);
}
}
