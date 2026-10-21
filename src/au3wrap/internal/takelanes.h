/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "au3-registries/ClientData.h"
#include "au3-xml/XMLTagHandler.h"

#include "keydetect.h"

#include <wx/string.h>

class AudacityProject;
class ChannelGroup;
class Track;
class WaveTrack;
class XMLWriter;

namespace au::au3 {
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
    //! Main tracks only: whether the take lanes are shown (view state, saved with the project)
    bool lanesExpanded = false;
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

//! Give freshly copied tracks their own take groups so they don't join the originals'.
//! Takes copied without their main track become ordinary tracks.
void regroupCopies(const std::vector<Track*>& copies);

//! Main track of a take track (null if `track` is not a take or its main is gone)
WaveTrack* mainOf(AudacityProject& project, const WaveTrack& track);

//! Display name of take number `index` of a track called `mainName`
wxString takeName(const wxString& mainName, int index);

//! Rename all takes of `mainTrack` after it. Returns the renamed takes.
std::vector<WaveTrack*> renameTakes(AudacityProject& project, const WaveTrack& mainTrack);

//! Move the takes of `mainTrack` so they sit directly below it, in take order.
//! Returns the takes whose position changed.
std::vector<WaveTrack*> restackTakes(AudacityProject& project, const WaveTrack& mainTrack);

//! Restack the takes of every main track in the project. Returns the takes that moved.
std::vector<WaveTrack*> restackAllTakes(AudacityProject& project);

//! True if `track` sits between a main track and that main's takes (i.e. would split a take group)
bool splitsTakeGroup(AudacityProject& project, const WaveTrack& track);

//! Cycle the audio through main and take lanes over the group's take range:
//! forward: main <- take1 <- take2 ... <- takeN <- main; backward is the inverse.
//! Returns false if `mainTrack` has no takes.
bool cycleTakes(AudacityProject& project, WaveTrack& mainTrack, bool forward);

//! Make `take` the active one: swap its audio region with the main track's, so the
//! previously active audio becomes this take lane. Returns false if `take` is not a take of `mainTrack`.
bool activateTake(AudacityProject& project, WaveTrack& mainTrack, WaveTrack& take);

//! Range comping: make [t0, t1] of `take` active on `mainTrack`. The two lanes exchange
//! their audio in that range, so the replaced main audio is kept (untouched) in the take and
//! comping the same range again restores the main track. The comped region becomes its own
//! clip on the main track, named after the take ("Take N"), and gets linear crossfades of
//! `crossfade` seconds (at most half the range) with the replaced main audio at both boundaries.
//! Returns false if `take` is not a take of `mainTrack` or the range is empty.
bool compRange(AudacityProject& project, WaveTrack& mainTrack, WaveTrack& take, double t0, double t1,
               double crossfade);

//! Automatic comping over the take range of `mainTrack`: segments the range at shared phrase gaps,
//! scores the main track and every take per segment (see autocomp.h) and comps the best take into
//! each segment with compRange. Returns the chosen lane per segment (0 = main kept, N = take N's lane
//! position + 1) with segment times; empty if there are no takes.
struct AutoCompChoice {
    double t0 = 0.0;
    double t1 = 0.0;
    int lane = 0;
};
std::vector<AutoCompChoice> autoComp(AudacityProject& project, WaveTrack& mainTrack, double minSegmentSec,
                                     double crossfade, bool pitchScoring = true);

//! Key / scale and tuning of `track` (mono mix) over [t0, t1); the whole track when t1 <= t0
keydetect::Result detectKey(const WaveTrack& track, double t0, double t1);

//! Label for comped regions of take number `index` ("Take N")
wxString compLabel(int index);
}
}
