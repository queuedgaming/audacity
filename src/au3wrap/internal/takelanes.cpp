/*
* Audacity: A Digital Audio Editor
*/
#include "takelanes.h"

#include <algorithm>
#include <map>
#include <random>
#include <sstream>

#include "au3-project/Project.h"
#include "au3-track/Track.h"
#include "au3-wave-track/WaveTrack.h"
#include "au3-xml/XMLWriter.h"

using namespace au::au3;

namespace {
const ChannelGroup::Attachments::RegisteredFactory takeLaneInfoKey {
    [](auto&) { return std::make_unique<TakeLaneInfo>(); }
};

// Persist take-lane membership inside each <wavetrack> element
WaveTrackIORegistry::ObjectReaderEntry takeLaneReader {
    TakeLaneInfo::XMLTag(),
    [](WaveTrack& track) { return &TakeLaneInfo::Get(track); }
};

WaveTrackIORegistry::ObjectWriterEntry takeLaneWriter {
    [](const WaveTrack& track, auto& xmlFile) { TakeLaneInfo::Get(track).WriteXML(xmlFile); }
};

std::string newGroupId()
{
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::ostringstream ss;
    ss << std::hex << gen() << gen();
    return ss.str();
}

bool sameGroup(const WaveTrack& a, const WaveTrack& b)
{
    const auto& ia = TakeLaneInfo::Get(a);
    const auto& ib = TakeLaneInfo::Get(b);
    return ia.isInGroup() && ia.groupId == ib.groupId;
}
}

TakeLaneInfo& TakeLaneInfo::Get(ChannelGroup& group)
{
    return group.Attachments::Get<TakeLaneInfo>(takeLaneInfoKey);
}

const TakeLaneInfo& TakeLaneInfo::Get(const ChannelGroup& group)
{
    return Get(const_cast<ChannelGroup&>(group));
}

const std::string& TakeLaneInfo::XMLTag()
{
    static const std::string tag = "takelane";
    return tag;
}

std::unique_ptr<ClientData::Cloneable<> > TakeLaneInfo::Clone() const
{
    return std::make_unique<TakeLaneInfo>(*this);
}

bool TakeLaneInfo::HandleXMLTag(const std::string_view& tag, const AttributesList& attrs)
{
    if (tag != XMLTag()) {
        return false;
    }
    for (const auto& [attr, value] : attrs) {
        if (attr == "group") {
            groupId = value.ToWString().ToStdString();
        } else if (attr == "index") {
            long long v = 0;
            if (value.TryGet(v)) {
                takeIndex = static_cast<int>(v);
            }
        } else if (attr == "expanded") {
            bool v = false;
            if (value.TryGet(v)) {
                lanesExpanded = v;
            }
        }
    }
    return true;
}

void TakeLaneInfo::WriteXML(XMLWriter& xmlFile) const
{
    if (!isInGroup()) {
        return; // nothing to save for ordinary tracks
    }
    xmlFile.StartTag(XMLTag());
    xmlFile.WriteAttr(wxT("group"), wxString(groupId));
    xmlFile.WriteAttr(wxT("index"), takeIndex);
    if (takeIndex == 0 && lanesExpanded) {
        xmlFile.WriteAttr(wxT("expanded"), true);
    }
    xmlFile.EndTag(XMLTag());
}

std::shared_ptr<WaveTrack> takelanes::snapshotFrom(const WaveTrack& track, double t0)
{
    const double end = track.GetEndTime();
    if (track.Intervals().empty() || end <= t0) {
        return {};
    }
    return std::static_pointer_cast<WaveTrack>(track.Copy(t0, end, false));
}

WaveTrack* takelanes::preserveAsTake(AudacityProject& project, WaveTrack& mainTrack,
                                     const WaveTrack& snapshot, double t0, double t1)
{
    if (t1 <= t0) {
        return nullptr;
    }
    // Only the overwritten part, [0, t1 - t0] in snapshot time
    auto region = std::static_pointer_cast<WaveTrack>(snapshot.Copy(0.0, t1 - t0, false));
    if (!region || region->Intervals().empty()) {
        return nullptr;
    }

    auto& mainInfo = TakeLaneInfo::Get(mainTrack);
    if (!mainInfo.isInGroup()) {
        mainInfo.groupId = newGroupId();
        mainInfo.takeIndex = 0;
    }

    int nextIndex = 1;
    for (auto* t : takesOf(project, mainTrack)) {
        nextIndex = std::max(nextIndex, TakeLaneInfo::Get(*t).takeIndex + 1);
    }

    auto take = WaveTrackFactory::Get(project).Create(mainTrack.NChannels(), mainTrack);
    take->Paste(t0, *region, false);
    take->SetName(takeName(mainTrack.GetName(), nextIndex));
    take->SetMute(true);
    take->SetSelected(false);
    auto& takeInfo = TakeLaneInfo::Get(*take);
    takeInfo.groupId = mainInfo.groupId;
    takeInfo.takeIndex = nextIndex;

    auto& tracks = TrackList::Get(project);
    tracks.Add(take, ::TrackList::DoAssignId::Yes, ::TrackList::EventPublicationSynchrony::Synchronous);
    // Place it directly below the main track and its existing takes
    restackTakes(project, mainTrack);
    return take.get();
}

std::vector<WaveTrack*> takelanes::takesOf(AudacityProject& project, const WaveTrack& mainTrack)
{
    std::vector<WaveTrack*> result;
    if (!TakeLaneInfo::Get(mainTrack).isInGroup()) {
        return result;
    }
    for (auto* t : TrackList::Get(project).Any<WaveTrack>()) {
        if (t != &mainTrack && TakeLaneInfo::Get(*t).isTake() && sameGroup(*t, mainTrack)) {
            result.push_back(t);
        }
    }
    std::sort(result.begin(), result.end(), [](WaveTrack* a, WaveTrack* b) {
        return TakeLaneInfo::Get(*a).takeIndex < TakeLaneInfo::Get(*b).takeIndex;
    });
    return result;
}

namespace {
int positionOf(TrackList& tracks, const Track& t)
{
    return static_cast<int>(std::distance(tracks.begin(), tracks.Find(const_cast<Track*>(&t))));
}

std::shared_ptr<WaveTrack> copyRegion(const WaveTrack& track, double s, double e)
{
    if (track.Intervals().empty()) {
        return {};
    }
    auto copy = std::static_pointer_cast<WaveTrack>(track.Copy(s, e, false));
    if (!copy || copy->Intervals().empty()) {
        return {};
    }
    return copy;
}

//! Exchange the audio of `a` and `b` within [s, e]; audio outside stays in place
void swapRegion(WaveTrack& a, WaveTrack& b, double s, double e)
{
    auto fromA = copyRegion(a, s, e);
    auto fromB = copyRegion(b, s, e);
    if (fromB) {
        a.ClearAndPaste(s, e, *fromB);
    } else {
        a.Clear(s, e, false);
    }
    if (fromA) {
        b.ClearAndPaste(s, e, *fromA);
    } else {
        b.Clear(s, e, false);
    }
}
}

void takelanes::regroupCopies(const std::vector<Track*>& copies)
{
    std::map<std::string, std::string> newIds; // old group -> new group, for groups whose main was copied
    for (auto* t : copies) {
        if (auto* wt = dynamic_cast<WaveTrack*>(t)) {
            const auto& info = TakeLaneInfo::Get(*wt);
            if (info.isInGroup() && !info.isTake()) {
                newIds[info.groupId] = newGroupId();
            }
        }
    }
    for (auto* t : copies) {
        auto* wt = dynamic_cast<WaveTrack*>(t);
        if (!wt) {
            continue;
        }
        auto& info = TakeLaneInfo::Get(*wt);
        if (!info.isInGroup()) {
            continue;
        }
        const auto it = newIds.find(info.groupId);
        if (it != newIds.end()) {
            info.groupId = it->second;
        } else {
            info = TakeLaneInfo {}; // orphaned take copy: plain track
        }
    }
}

WaveTrack* takelanes::mainOf(AudacityProject& project, const WaveTrack& track)
{
    const auto& info = TakeLaneInfo::Get(track);
    if (!info.isTake()) {
        return nullptr;
    }
    for (auto* t : TrackList::Get(project).Any<WaveTrack>()) {
        const auto& ti = TakeLaneInfo::Get(*t);
        if (ti.isInGroup() && !ti.isTake() && ti.groupId == info.groupId) {
            return t;
        }
    }
    return nullptr;
}

wxString takelanes::takeName(const wxString& mainName, int index)
{
    return mainName + wxString::Format(" - Take %d", index);
}

std::vector<WaveTrack*> takelanes::renameTakes(AudacityProject& project, const WaveTrack& mainTrack)
{
    std::vector<WaveTrack*> renamed;
    for (auto* take : takesOf(project, mainTrack)) {
        const wxString name = takeName(mainTrack.GetName(), TakeLaneInfo::Get(*take).takeIndex);
        if (take->GetName() != name) {
            take->SetName(name);
            renamed.push_back(take);
        }
    }
    return renamed;
}

std::vector<WaveTrack*> takelanes::restackTakes(AudacityProject& project, const WaveTrack& mainTrack)
{
    std::vector<WaveTrack*> moved;
    auto& tracks = TrackList::Get(project);
    int target = positionOf(tracks, mainTrack) + 1;
    for (auto* take : takesOf(project, mainTrack)) {
        int pos = positionOf(tracks, *take);
        const int start = pos;
        while (pos > target && tracks.CanMoveUp(*take)) {
            tracks.MoveUp(*take);
            --pos;
        }
        while (pos < target && tracks.CanMoveDown(*take)) {
            tracks.MoveDown(*take);
            ++pos;
        }
        if (pos != start) {
            moved.push_back(take);
        }
        // The main may have shifted if a take moved from above it
        target = positionOf(tracks, *take) + 1;
    }
    return moved;
}

std::vector<WaveTrack*> takelanes::restackAllTakes(AudacityProject& project)
{
    std::vector<WaveTrack*> mains;
    for (auto* t : TrackList::Get(project).Any<WaveTrack>()) {
        const auto& info = TakeLaneInfo::Get(*t);
        if (info.isInGroup() && !info.isTake()) {
            mains.push_back(t);
        }
    }
    std::vector<WaveTrack*> moved;
    for (auto* m : mains) {
        auto m2 = restackTakes(project, *m);
        moved.insert(moved.end(), m2.begin(), m2.end());
    }
    return moved;
}

bool takelanes::splitsTakeGroup(AudacityProject& project, const WaveTrack& track)
{
    auto& tracks = TrackList::Get(project);
    auto it = tracks.Find(const_cast<WaveTrack*>(&track));
    if (it == tracks.end()) {
        return false;
    }
    ++it;
    if (it == tracks.end()) {
        return false;
    }
    const auto* below = dynamic_cast<const WaveTrack*>(*it);
    if (!below || !TakeLaneInfo::Get(*below).isTake()) {
        return false;
    }
    // Directly above a take that is not our own: we are inside someone else's group
    return !sameGroup(*below, track);
}

bool takelanes::cycleTakes(AudacityProject& project, WaveTrack& mainTrack, bool forward)
{
    auto takes = takesOf(project, mainTrack);
    takes.erase(std::remove_if(takes.begin(), takes.end(), [](WaveTrack* t) { return t->Intervals().empty(); }),
                takes.end());
    if (takes.empty()) {
        return false;
    }
    double s = takes.front()->GetStartTime();
    double e = takes.front()->GetEndTime();
    for (auto* t : takes) {
        s = std::min(s, t->GetStartTime());
        e = std::max(e, t->GetEndTime());
    }
    // Chain of pairwise swaps rotates the lanes: [main, t1, ..., tn]
    std::vector<WaveTrack*> lanes { &mainTrack };
    lanes.insert(lanes.end(), takes.begin(), takes.end());
    if (forward) {
        for (size_t i = 0; i + 1 < lanes.size(); ++i) {
            swapRegion(*lanes[i], *lanes[i + 1], s, e);
        }
    } else {
        for (size_t i = lanes.size() - 1; i > 0; --i) {
            swapRegion(*lanes[i - 1], *lanes[i], s, e);
        }
    }
    return true;
}

bool takelanes::activateTake(AudacityProject& project, WaveTrack& mainTrack, WaveTrack& take)
{
    const auto takes = takesOf(project, mainTrack);
    if (std::find(takes.begin(), takes.end(), &take) == takes.end() || take.Intervals().empty()) {
        return false;
    }
    swapRegion(mainTrack, take, take.GetStartTime(), take.GetEndTime());
    return true;
}
