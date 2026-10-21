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

    virtual RecordingTrackNameOptions recordingTrackNameOptions() const = 0;
};
}
