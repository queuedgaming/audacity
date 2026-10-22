/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../monitor/monitorprocessor.h"

class AudacityProject;
class WaveTrack;

namespace au::record::monitor {
//! Result of building the live monitoring chain for a track
struct BuiltChain {
    std::unique_ptr<MonitorChain> chain;
    std::vector<std::string> used;     //!< effect names heard, in order (track first, then master)
    std::vector<std::string> bypassed; //!< skipped by low-latency monitoring
    double effectLatencySeconds = 0.0;
};

//! Build independent effect instances (main thread) from the active realtime effects of
//! `track` (may be null) and of the project's master stack, with their current settings.
//! The track's own playback instances are not shared with the audio thread's monitor path.
//! Identity of what the chain is built from: target track, list on/off, each effect state and
//! its on/off, in order. Changes when effects are added, removed, reordered, replaced or toggled.
std::vector<const void*> monitorChainSignature(AudacityProject& project, WaveTrack* track);

BuiltChain buildMonitorChain(AudacityProject& project, WaveTrack* track, double sampleRate, bool lowLatency,
                             double thresholdSeconds);
}
