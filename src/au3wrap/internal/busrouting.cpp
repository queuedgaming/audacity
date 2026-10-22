/*
* Audacity: A Digital Audio Editor
*/
#include "busrouting.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>

#include "au3-mixer/AudioIOSequences.h"
#include "au3-mixer/BusRouting.h"
#include "au3-mixer/WideSampleSequence.h"
#include "au3-project-rate/ProjectRate.h"
#include "au3-track/Track.h"
#include "au3-wave-track/WaveTrack.h"
#include "au3-xml/XMLWriter.h"

using namespace au::au3;

namespace {
const ChannelGroup::Attachments::RegisteredFactory busRoutingKey {
    [](auto&) { return std::make_unique<BusRoutingData>(); }
};

WaveTrackIORegistry::ObjectReaderEntry busRoutingReader {
    BusRoutingData::XMLTag(),
    [](WaveTrack& track) { return &BusRoutingData::Get(track); }
};

WaveTrackIORegistry::ObjectWriterEntry busRoutingWriter {
    [](const WaveTrack& track, auto& xmlFile) { BusRoutingData::Get(track).WriteXML(xmlFile); }
};

std::string encodeSends(const std::vector<BusSend>& sends)
{
    std::string s;
    char buf[32];
    for (const auto& send : sends) {
        std::snprintf(buf, sizeof(buf), ":%.9g", double(send.level));
        if (!s.empty()) {
            s += ",";
        }
        s += send.busId + buf;
    }
    return s;
}

std::vector<BusSend> decodeSends(const std::string& s)
{
    std::vector<BusSend> sends;
    size_t pos = 0;
    while (pos < s.size()) {
        auto end = s.find(',', pos);
        if (end == std::string::npos) {
            end = s.size();
        }
        const auto item = s.substr(pos, end - pos);
        const auto colon = item.rfind(':');
        if (colon != std::string::npos && colon > 0) {
            BusSend send;
            send.busId = item.substr(0, colon);
            send.level = std::strtof(item.c_str() + colon + 1, nullptr);
            sends.push_back(send);
        }
        pos = end + 1;
    }
    return sends;
}

//! Outgoing edges (output + sends) by bus id, over the project's tracks
using Edges = std::map<std::string, std::set<std::string> >;

Edges busEdges(AudacityProject& project, const WaveTrack* exclude)
{
    Edges edges;
    for (auto* t : TrackList::Get(project).Any<WaveTrack>()) {
        const auto& d = BusRoutingData::Get(*t);
        if (!d.isBus || t == exclude) {
            continue;
        }
        auto& out = edges[d.busId];
        if (!d.outputBusId.empty()) {
            out.insert(d.outputBusId);
        }
        for (const auto& s : d.sends) {
            out.insert(s.busId);
        }
    }
    return edges;
}

bool reaches(const Edges& edges, const std::string& from, const std::string& to)
{
    std::set<std::string> seen;
    std::vector<std::string> stack { from };
    while (!stack.empty()) {
        auto cur = stack.back();
        stack.pop_back();
        if (cur == to) {
            return true;
        }
        if (!seen.insert(cur).second) {
            continue;
        }
        if (auto it = edges.find(cur); it != edges.end()) {
            stack.insert(stack.end(), it->second.begin(), it->second.end());
        }
    }
    return false;
}

const WaveTrack* trackOf(const WideSampleSequence* seq)
{
    if (!seq) {
        return nullptr;
    }
    if (auto* t = dynamic_cast<const WaveTrack*>(seq)) {
        return t;
    }
    if (auto* ps = dynamic_cast<const PlayableSequence*>(seq)) {
        return dynamic_cast<const WaveTrack*>(ps->FindChannelGroup());
    }
    return nullptr;
}
}

// ---- BusRoutingData ----------------------------------------------------------------------
BusRoutingData& BusRoutingData::Get(ChannelGroup& group)
{
    return group.Attachments::Get<BusRoutingData>(busRoutingKey);
}

const BusRoutingData& BusRoutingData::Get(const ChannelGroup& group)
{
    return Get(const_cast<ChannelGroup&>(group));
}

const std::string& BusRoutingData::XMLTag()
{
    static const std::string tag = "busrouting";
    return tag;
}

std::unique_ptr<ClientData::Cloneable<> > BusRoutingData::Clone() const
{
    return std::make_unique<BusRoutingData>(*this);
}

bool BusRoutingData::HandleXMLTag(const std::string_view& tag, const AttributesList& attrs)
{
    if (tag != XMLTag()) {
        return false;
    }
    for (const auto& [attr, value] : attrs) {
        if (attr == "isbus") {
            bool v = false;
            if (value.TryGet(v)) {
                isBus = v;
            }
        } else if (attr == "busid") {
            busId = value.ToWString().ToStdString();
        } else if (attr == "output") {
            outputBusId = value.ToWString().ToStdString();
        } else if (attr == "sends") {
            sends = decodeSends(value.ToWString().ToStdString());
        }
    }
    return true;
}

void BusRoutingData::WriteXML(XMLWriter& xmlFile) const
{
    if (isDefault()) {
        return;
    }
    xmlFile.StartTag(XMLTag());
    xmlFile.WriteAttr(wxT("isbus"), isBus);
    if (!busId.empty()) {
        xmlFile.WriteAttr(wxT("busid"), wxString(busId));
    }
    if (!outputBusId.empty()) {
        xmlFile.WriteAttr(wxT("output"), wxString(outputBusId));
    }
    if (!sends.empty()) {
        xmlFile.WriteAttr(wxT("sends"), wxString(encodeSends(sends)));
    }
    xmlFile.EndTag(XMLTag());
}

// ---- functions ---------------------------------------------------------------------------
void busrouting::install()
{
    SetBusRoutingPlanProvider(&busrouting::makePlan);
}

bool busrouting::isBus(const WaveTrack& track)
{
    return BusRoutingData::Get(track).isBus;
}

std::vector<WaveTrack*> busrouting::buses(AudacityProject& project)
{
    std::vector<WaveTrack*> result;
    for (auto* t : TrackList::Get(project).Any<WaveTrack>()) {
        if (isBus(*t)) {
            result.push_back(t);
        }
    }
    return result;
}

WaveTrack* busrouting::findBus(AudacityProject& project, const std::string& busId)
{
    if (busId.empty()) {
        return nullptr;
    }
    for (auto* t : buses(project)) {
        if (BusRoutingData::Get(*t).busId == busId) {
            return t;
        }
    }
    return nullptr;
}

WaveTrack* busrouting::addBusTrack(AudacityProject& project)
{
    auto& tracks = TrackList::Get(project);
    const auto track = WaveTrackFactory::Get(project).Create(2, floatSample, ProjectRate::Get(project).GetRate());
    track->SetName(tracks.MakeUniqueTrackName(wxT("Bus")));
    makeBus(project, *track);
    tracks.Add(track);
    return track.get();
}

std::string busrouting::makeBus(AudacityProject& project, WaveTrack& track)
{
    int n = 1;
    std::string id;
    do {
        id = "bus" + std::to_string(n++);
    } while (findBus(project, id));
    auto& d = BusRoutingData::Get(track);
    d.isBus = true;
    d.busId = id;
    return id;
}

bool busrouting::createsCycle(AudacityProject& project, const WaveTrack& from, const std::string& toBusId)
{
    const auto& d = BusRoutingData::Get(from);
    if (!d.isBus) {
        return false; // only buses have inputs, so a plain track can't be part of a cycle
    }
    if (d.busId == toBusId) {
        return true;
    }
    // A cycle appears iff `toBusId` already reaches `from`
    return reaches(busEdges(project, nullptr), toBusId, d.busId);
}

bool busrouting::setOutput(AudacityProject& project, WaveTrack& track, const std::string& busId)
{
    if (!busId.empty() && (!findBus(project, busId) || createsCycle(project, track, busId))) {
        return false;
    }
    auto& d = BusRoutingData::Get(track);
    d.outputBusId = busId;
    // a send to the output bus would double the signal
    d.sends.erase(std::remove_if(d.sends.begin(), d.sends.end(),
                                 [&](const BusSend& s) { return s.busId == busId; }), d.sends.end());
    return true;
}

bool busrouting::setSend(AudacityProject& project, WaveTrack& track, const std::string& busId, float level)
{
    auto& d = BusRoutingData::Get(track);
    if (busId.empty() || busId == d.outputBusId || !findBus(project, busId)
        || createsCycle(project, track, busId)) {
        return false;
    }
    level = std::clamp(level, 0.0f, 4.0f);
    for (auto& s : d.sends) {
        if (s.busId == busId) {
            s.level = level;
            return true;
        }
    }
    d.sends.push_back({ busId, level });
    return true;
}

bool busrouting::removeSend(WaveTrack& track, const std::string& busId)
{
    auto& d = BusRoutingData::Get(track);
    const auto before = d.sends.size();
    d.sends.erase(std::remove_if(d.sends.begin(), d.sends.end(),
                                 [&](const BusSend& s) { return s.busId == busId; }), d.sends.end());
    return d.sends.size() != before;
}

BusRoutingPlan busrouting::makePlan(const std::vector<const WideSampleSequence*>& sequences)
{
    BusRoutingPlan plan;
    plan.nodes.resize(sequences.size());

    std::map<std::string, int> busIndex;
    std::vector<const BusRoutingData*> data(sequences.size(), nullptr);
    for (size_t i = 0; i < sequences.size(); ++i) {
        if (const auto* t = trackOf(sequences[i])) {
            data[i] = &BusRoutingData::Get(*t);
            if (data[i]->isBus && !data[i]->busId.empty()) {
                plan.nodes[i].isBus = true;
                busIndex.emplace(data[i]->busId, int(i));
            }
        }
    }
    auto indexOf = [&](const std::string& id) {
        auto it = busIndex.find(id);
        return it == busIndex.end() ? -1 : it->second;
    };

    // Edges, refusing any that would close a cycle (stale or hand-edited projects)
    std::vector<std::vector<int> > out(sequences.size());
    auto reachable = [&](int from, int to) {
        std::vector<int> stack { from };
        std::vector<bool> seen(sequences.size(), false);
        while (!stack.empty()) {
            const int cur = stack.back();
            stack.pop_back();
            if (cur == to) {
                return true;
            }
            if (seen[size_t(cur)]) {
                continue;
            }
            seen[size_t(cur)] = true;
            stack.insert(stack.end(), out[size_t(cur)].begin(), out[size_t(cur)].end());
        }
        return false;
    };
    auto addEdge = [&](int from, int to) {
        if (to < 0 || to == from || reachable(to, from)) {
            return false;
        }
        out[size_t(from)].push_back(to);
        return true;
    };
    for (size_t i = 0; i < sequences.size(); ++i) {
        if (!data[i]) {
            continue;
        }
        auto& node = plan.nodes[i];
        const int target = indexOf(data[i]->outputBusId);
        node.target = addEdge(int(i), target) ? target : -1;
        if (target < 0 && !data[i]->outputBusId.empty()) {
            // the bus is not part of this mix (e.g. excluded from export because it is muted)
            if (const auto* t = trackOf(sequences[i]); t && t->GetOwner()) {
                for (const auto* b : t->GetOwner()->Any<const WaveTrack>()) {
                    const auto& bd = BusRoutingData::Get(*b);
                    if (bd.isBus && bd.busId == data[i]->outputBusId && b->GetMute()) {
                        node.muted = true;
                    }
                }
            }
        }
        for (const auto& s : data[i]->sends) {
            const int dest = indexOf(s.busId);
            if (dest >= 0 && dest != node.target && addEdge(int(i), dest)) {
                node.sends.push_back({ dest, s.level });
            }
        }
    }

    // Topological order of buses (Kahn), counting only bus->bus edges
    std::vector<int> indeg(sequences.size(), 0);
    for (size_t i = 0; i < sequences.size(); ++i) {
        if (plan.nodes[i].isBus) {
            for (int d : out[i]) {
                ++indeg[size_t(d)];
            }
        }
    }
    std::vector<int> ready;
    for (size_t i = 0; i < sequences.size(); ++i) {
        if (plan.nodes[i].isBus && indeg[i] == 0) {
            ready.push_back(int(i));
        }
    }
    while (!ready.empty()) {
        const int b = ready.front();
        ready.erase(ready.begin());
        plan.busOrder.push_back(b);
        for (int d : out[size_t(b)]) {
            if (--indeg[size_t(d)] == 0) {
                ready.push_back(d);
            }
        }
    }
    return plan;
}
