/*
* Audacity: A Digital Audio Editor
*/
#include "takelanes.h"

#include <algorithm>
#include <random>
#include <sstream>

#include "au3-project/Project.h"
#include "au3-track/Track.h"
#include "au3-wave-track/WaveTrack.h"
#include "au3-xml/XMLWriter.h"

using namespace au::record;

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
    take->SetName(mainTrack.GetName() + wxString::Format(" - Take %d", nextIndex));
    take->SetMute(true);
    take->SetSelected(false);
    auto& takeInfo = TakeLaneInfo::Get(*take);
    takeInfo.groupId = mainInfo.groupId;
    takeInfo.takeIndex = nextIndex;

    auto& tracks = TrackList::Get(project);
    tracks.Add(take, ::TrackList::DoAssignId::Yes, ::TrackList::EventPublicationSynchrony::Synchronous);
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

bool takelanes::activateTake(AudacityProject& project, WaveTrack& mainTrack, WaveTrack& take)
{
    const auto takes = takesOf(project, mainTrack);
    if (std::find(takes.begin(), takes.end(), &take) == takes.end() || take.Intervals().empty()) {
        return false;
    }
    const double s = take.GetStartTime();
    const double e = take.GetEndTime();

    auto fromMain = std::static_pointer_cast<WaveTrack>(mainTrack.Copy(s, e, false));
    auto fromTake = std::static_pointer_cast<WaveTrack>(take.Copy(s, e, false));

    // ClearAndPaste keeps the audio outside [s, e] exactly where it was
    mainTrack.ClearAndPaste(s, e, *fromTake);
    if (fromMain && !fromMain->Intervals().empty()) {
        take.ClearAndPaste(s, e, *fromMain);
    } else {
        take.Clear(s, e, false);
    }
    return true;
}
