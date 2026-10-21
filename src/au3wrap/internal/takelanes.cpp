/*
* Audacity: A Digital Audio Editor
*/
#include "takelanes.h"
#include "autocomp.h"
#include "projectkey.h"

#include <algorithm>
#include <map>
#include <random>
#include <sstream>

#include "au3-project/Project.h"
#include "au3-track/Track.h"
#include "au3-wave-track/WaveTrack.h"
#include "au3-wave-track/WaveClip.h"
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
void swapRegion(WaveTrack& a, WaveTrack& b, double s, double e, bool merge = true)
{
    auto fromA = copyRegion(a, s, e);
    auto fromB = copyRegion(b, s, e);
    if (fromB) {
        a.ClearAndPaste(s, e, *fromB, true, merge);
    } else {
        a.Clear(s, e, false);
    }
    if (fromA) {
        b.ClearAndPaste(s, e, *fromA, true, merge);
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

wxString takelanes::compLabel(int index)
{
    return wxString::Format(wxT("Take %d"), index);
}

namespace {
//! Lossless exchange of the clips of `a` and `b` within [s, e]: both tracks are split at s and e
//! (splits keep the hidden audio beyond the cut as handles) and whole clips change tracks, so not
//! a single sample is rewritten.
void swapClips(WaveTrack& a, WaveTrack& b, double s, double e)
{
    for (auto* t : { &a, &b }) {
        t->SplitAt(s);
        t->SplitAt(e);
    }
    const auto inside = [s, e](const WaveTrack::Interval& c) {
        return c.GetPlayStartTime() >= s - 1e-9 && c.GetPlayEndTime() <= e + 1e-9;
    };
    std::vector<WaveTrack::IntervalHolder> fromA, fromB;
    for (const auto& c : a.Intervals()) {
        if (inside(*c)) {
            fromA.push_back(c);
        }
    }
    for (const auto& c : b.Intervals()) {
        if (inside(*c)) {
            fromB.push_back(c);
        }
    }
    std::vector<WaveTrack::IntervalHolder> toB, toA;
    for (const auto& c : fromA) {
        toB.push_back(a.CopyClip(*c, true));
        a.RemoveInterval(c);
    }
    for (const auto& c : fromB) {
        toA.push_back(b.CopyClip(*c, true));
        b.RemoveInterval(c);
    }
    for (const auto& c : toA) {
        a.InsertInterval(c, false);
    }
    for (const auto& c : toB) {
        b.InsertInterval(c, false);
    }
}

//! True if the first `n` samples of `clip` are exactly what `left` holds beyond its end, i.e. the
//! boundary is a plain cut through continuous audio and needs no crossfade
bool continuesSeamlessly(const WaveTrack::Interval& left, const WaveTrack::Interval& clip, size_t n)
{
    if (n == 0 || left.GetRightHandleSampleCount() < sampleCount { n } || left.NChannels() != clip.NChannels()
        || left.GetStretchRatio() != 1.0 || clip.GetStretchRatio() != 1.0) {
        return false;
    }
    std::vector<float> x(n), y(n);
    for (size_t ch = 0; ch < clip.NChannels(); ++ch) {
        left.GetSampleView(ch, left.GetVisibleSampleCount(), n, false).Copy(x.data(), n);
        clip.GetSampleView(ch, 0, n, false).Copy(y.data(), n);
        if (x != y) {
            return false;
        }
    }
    return true;
}

//! Fade-in of `n` samples on the clip of `track` starting at t, crossfading with the abutting clip's
//! handle; none if the boundary is seamless (so comping back renders bit-exactly)
void setBoundaryFade(WaveTrack& track, double t, size_t n)
{
    WaveTrack::IntervalHolder clip, left;
    for (const auto& c : track.Intervals()) {
        if (std::abs(c->GetPlayStartTime() - t) < 1e-9) {
            clip = c;
        } else if (std::abs(c->GetPlayEndTime() - t) < 1e-9) {
            left = c;
        }
    }
    if (!clip) {
        return;
    }
    const size_t len = std::min<size_t>(n, clip->GetVisibleSampleCount().as_size_t());
    const bool seamless = left && continuesSeamlessly(*left, *clip, len);
    clip->SetFadeInDuration(seamless ? 0.0 : len / track.GetRate());
    clip->SetFadeCurve(0);
}
}

bool takelanes::compRange(AudacityProject& project, WaveTrack& mainTrack, WaveTrack& take, double t0, double t1,
                          double crossfade)
{
    const auto takes = takesOf(project, mainTrack);
    if (std::find(takes.begin(), takes.end(), &take) == takes.end()) {
        return false;
    }
    t0 = mainTrack.SnapToSample(t0);
    t1 = mainTrack.SnapToSample(t1);
    if (t1 <= t0) {
        return false;
    }
    swapClips(mainTrack, take, t0, t1);

    // The comped region stays separate clips showing where they came from
    const auto name = compLabel(TakeLaneInfo::Get(take).takeIndex);
    for (const auto& clip : mainTrack.Intervals()) {
        if (clip->GetPlayStartTime() >= t0 - 1e-9 && clip->GetPlayEndTime() <= t1 + 1e-9) {
            clip->SetName(name);
        }
    }
    // Take lanes keep plain, unfaded audio; fades only live on the main track's comp boundaries
    for (const auto& clip : take.Intervals()) {
        if (clip->GetPlayStartTime() >= t0 - 1e-9 && clip->GetPlayEndTime() <= t1 + 1e-9) {
            clip->SetFadeInDuration(0.0);
            clip->SetFadeOutDuration(0.0);
        }
    }
    const auto total = (mainTrack.TimeToLongSamples(t1) - mainTrack.TimeToLongSamples(t0)).as_size_t();
    const size_t fade = std::min<size_t>(total / 2,
                                         static_cast<size_t>(std::max(0.0, crossfade) * mainTrack.GetRate() + 0.5));
    setBoundaryFade(mainTrack, t0, fade);
    setBoundaryFade(mainTrack, t1, fade);
    return true;
}

namespace {
//! Mono mixdown of `track` over [s0, s0 + len) samples, zeros outside clips
std::vector<float> monoSamples(const WaveTrack& track, sampleCount s0, size_t len)
{
    std::vector<float> out(len, 0.0f), tmp(len);
    const size_t nCh = track.NChannels();
    for (size_t c = 0; c < nCh; ++c) {
        float* p = tmp.data();
        track.GetFloats(c, 1, &p, s0, len, false, FillFormat::fillZero, false);
        for (size_t i = 0; i < len; ++i) {
            out[i] += tmp[i] / float(nCh);
        }
    }
    return out;
}
}

std::vector<takelanes::AutoCompChoice> takelanes::autoComp(AudacityProject& project, WaveTrack& mainTrack,
                                                           double minSegmentSec, double crossfade, bool pitchScoring,
                                                           std::string* keySummary)
{
    auto takes = takesOf(project, mainTrack);
    takes.erase(std::remove_if(takes.begin(), takes.end(), [](WaveTrack* t) { return t->Intervals().empty(); }),
                takes.end());
    if (takes.empty()) {
        return {};
    }
    double s = takes.front()->GetStartTime();
    double e = takes.front()->GetEndTime();
    for (auto* t : takes) {
        s = std::min(s, t->GetStartTime());
        e = std::max(e, t->GetEndTime());
    }
    const auto s0 = mainTrack.TimeToLongSamples(s);
    const size_t len = (mainTrack.TimeToLongSamples(e) - s0).as_size_t();
    const double rate = mainTrack.GetRate();

    std::vector<std::vector<float> > lanes { monoSamples(mainTrack, s0, len) };
    for (auto* t : takes) {
        lanes.push_back(monoSamples(*t, s0, len));
    }
    autocomp::Settings settings;
    settings.minSegmentSec = std::max(0.1, minSegmentSec);
    settings.pitchEnabled = pitchScoring;
    if (const auto& k = ProjectKey::Get(project).key()) {
        settings.key = keydetect::KeyCandidate { k->tonic, k->minor, 1.0 };
        settings.tuningCents = k->tuningCents;
    }
    autocomp::Analysis analysis;
    const auto segments = autocomp::compute(lanes, rate, settings, &analysis);
    if (keySummary) {
        if (!pitchScoring) {
            *keySummary = "pitch scoring off";
        } else if (settings.key) {
            *keySummary = "project key " + ProjectKey::shortLabel(ProjectKey::Get(project).key());
        } else if (analysis.grid.scale) {
            *keySummary = "detected " + ProjectKey::shortLabel(ProjectKey::Key { analysis.key.key.tonic, analysis.key.key.minor,
                                                                                 analysis.grid.tuningCents, 0.0 });
        } else {
            *keySummary = "no clear key, chromatic";
        }
    }

    std::vector<AutoCompChoice> choices;
    for (const auto& seg : segments) {
        AutoCompChoice c;
        c.t0 = mainTrack.LongSamplesToTime(s0 + sampleCount(seg.start));
        c.t1 = mainTrack.LongSamplesToTime(s0 + sampleCount(seg.end));
        c.lane = seg.lane;
        if (seg.lane > 0) {
            compRange(project, mainTrack, *takes[seg.lane - 1], c.t0, c.t1, crossfade);
        }
        choices.push_back(c);
    }
    return choices;
}

keydetect::Result takelanes::detectKey(const WaveTrack& track, double t0, double t1)
{
    if (t1 <= t0) {
        t0 = track.GetStartTime();
        t1 = track.GetEndTime();
    }
    t0 = std::max(t0, track.GetStartTime());
    t1 = std::min(t1, track.GetEndTime());
    if (t1 <= t0) {
        return {};
    }
    const auto s0 = track.TimeToLongSamples(t0);
    const size_t len = (track.TimeToLongSamples(t1) - s0).as_size_t();
    return keydetect::detect(monoSamples(track, s0, len), track.GetRate());
}
