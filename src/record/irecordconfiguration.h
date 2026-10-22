/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <string>

#include "global/modularity/imoduleinterface.h"
#include "global/async/notification.h"
#include "draw/types/color.h"

namespace au::record {
//! Naming scheme for newly recorded tracks
struct RecordingTrackNameOptions {
    bool useCustomName = false;
    std::string customName;
    bool addTrackNumber = false;
    bool addDateStamp = false;
    bool addTimeStamp = false;
};

class IRecordConfiguration : MODULE_GLOBAL_INTERFACE
{
    INTERFACE_ID(IRecordConfiguration)

public:
    virtual ~IRecordConfiguration() = default;

    virtual bool isMicMeteringOn() const = 0;
    virtual void setIsMicMeteringOn(bool enable) = 0;
    virtual muse::async::Notification isMicMeteringOnChanged() const = 0;

    virtual bool isInputMonitoringOn() const = 0;
    virtual void setIsInputMonitoringOn(bool enable) = 0;
    virtual muse::async::Notification isInputMonitoringOnChanged() const = 0;

    virtual double leadInTimeDuration() const = 0;
    virtual void setLeadInTimeDuration(double seconds) = 0;
    virtual muse::async::Notification leadInTimeDurationChanged() const = 0;

    virtual double crossfadeDuration() const = 0;
    virtual void setCrossfadeDuration(double milliseconds) = 0;
    virtual muse::async::Notification crossfadeDurationChanged() const = 0;

    //! Punch-out: when a time selection ends after the punch-in point, lead-in
    //! (punch and roll) recording stops automatically at the selection end
    virtual bool punchOutAtSelectionEnd() const = 0;
    virtual void setPunchOutAtSelectionEnd(bool enabled) = 0;
    virtual muse::async::Notification punchOutAtSelectionEndChanged() const = 0;

    //! Take lanes: when recording over existing audio, keep the overwritten audio as a muted take track
    virtual bool keepTakesWhenRecordingOver() const = 0;
    virtual void setKeepTakesWhenRecordingOver(bool enabled) = 0;
    virtual muse::async::Notification keepTakesWhenRecordingOverChanged() const = 0;

    //! Auto-comp: shortest segment, in seconds
    virtual double autoCompSegmentLength() const = 0;
    virtual void setAutoCompSegmentLength(double seconds) = 0;
    virtual muse::async::Notification autoCompSegmentLengthChanged() const = 0;

    //! Auto-comp: also score pitch stability, intonation and missed notes
    virtual bool autoCompPitchScoring() const = 0;
    virtual void setAutoCompPitchScoring(bool enabled) = 0;
    virtual muse::async::Notification autoCompPitchScoringChanged() const = 0;

    //! Artist Recording Mode: focused performer dashboard over the project window
    virtual bool artistModeEnabled() const = 0;
    virtual void setArtistModeEnabled(bool enabled) = 0;
    virtual muse::async::Notification artistModeEnabledChanged() const = 0;

    //! Artist mode: record with a count-in (lead-in pre-roll of leadInTimeDuration)
    virtual bool countInEnabled() const = 0;
    virtual void setCountInEnabled(bool enabled) = 0;
    virtual muse::async::Notification countInEnabledChanged() const = 0;

    //! Metronome preference (stored; AU4 has no click engine yet)
    virtual bool metronomeEnabled() const = 0;
    virtual void setMetronomeEnabled(bool enabled) = 0;
    virtual muse::async::Notification metronomeEnabledChanged() const = 0;

    //! Live monitoring: hear the input through the record track's realtime effects and the master effects (recording stays dry)
    virtual bool monitorThroughEffects() const = 0;
    virtual void setMonitorThroughEffects(bool value) = 0;
    virtual muse::async::Notification monitorThroughEffectsChanged() const = 0;

    //! Live monitoring: skip effects whose latency is above lowLatencyThresholdMs
    virtual bool lowLatencyMonitoring() const = 0;
    virtual void setLowLatencyMonitoring(bool value) = 0;
    virtual muse::async::Notification lowLatencyMonitoringChanged() const = 0;

    //! Low-latency monitoring threshold, milliseconds
    virtual double lowLatencyThresholdMs() const = 0;
    virtual void setLowLatencyThresholdMs(double value) = 0;
    virtual muse::async::Notification lowLatencyThresholdMsChanged() const = 0;

    //! Live monitoring level, dB (-60..+12)
    virtual double monitorGainDb() const = 0;
    virtual void setMonitorGainDb(double value) = 0;
    virtual muse::async::Notification monitorGainDbChanged() const = 0;

    //! Live monitoring mute
    virtual bool monitorMuted() const = 0;
    virtual void setMonitorMuted(bool value) = 0;
    virtual muse::async::Notification monitorMutedChanged() const = 0;

    virtual RecordingTrackNameOptions recordingTrackNameOptions() const = 0;
};
}
