/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <string>
#include <vector>

class AudacityProject;

namespace au::au3::mixer {
enum class StripKind {
    Track,
    Bus,
    Master
};

struct StripInsert {
    std::string pluginId;
    bool active = true;
};

struct StripSend {
    std::string busId;
    float level = 1.0f;
};

//! Snapshot of one channel strip of the mixer (a wave track, a bus, or the master)
struct Strip {
    static constexpr long long MasterId = -2; //!< same as IRealtimeEffectService::masterTrackId

    long long trackId = MasterId;
    StripKind kind = StripKind::Master;
    std::string name;
    std::string busId;      //!< buses only
    std::string outputBusId; //!< empty: master (unused for the master strip)
    std::vector<StripSend> sends;
    double volumeDb = 0.0;  //!< tracks and buses; the master fader is the playback volume
    double trimDb = 0.0;    //!< input trim (pre-inserts), tracks and buses
    float pan = 0.0f;
    bool mute = false;
    bool solo = false;
    int channels = 2;
    bool insertsActive = true; //!< the whole realtime effect stack is enabled
    std::vector<StripInsert> inserts;
};

//! Strips in mixer order: wave tracks (track order), then buses (track order), then master
std::vector<Strip> strips(AudacityProject& project);

//! Name of the output target of a strip (bus name or "Master")
std::string outputName(AudacityProject& project, const Strip& strip);
}
