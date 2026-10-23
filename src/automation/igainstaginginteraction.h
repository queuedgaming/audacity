/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <map>
#include <string>
#include <vector>

#include "async/notification.h"
#include "modularity/imoduleinterface.h"

#include "trackedit/trackedittypes.h"

namespace au::automation {
struct TrackLevels {
    trackedit::TrackId trackId = trackedit::INVALID_TRACK;
    std::string name;
    int channels = 0;
    double peakDb = -150.0;
    double truePeakDb = -150.0;
    double rmsDb = -150.0;
    double lufs = -150.0;
    double trimDb = 0.0;
    bool silent = true;
};

//! Per-track input trim and level analysis for gain staging
class IGainStagingInteraction : MODULE_EXPORT_INTERFACE
{
    INTERFACE_ID(IGainStagingInteraction)

public:
    virtual ~IGainStagingInteraction() = default;

    virtual double trimDb(trackedit::TrackId trackId) const = 0;
    //! `completed` = false while dragging (one undo step at the end). False if no such track.
    virtual bool setTrim(trackedit::TrackId trackId, double db, bool completed) = 0;
    //! Sets several trims as a single undo step; returns how many changed
    virtual int applyTrims(const std::map<trackedit::TrackId, double>& trims) = 0;

    //! Levels of every (non-bus) track's audio, before trim and effects
    virtual std::vector<TrackLevels> analyze() const = 0;

    virtual muse::async::Notification changed() const = 0;
};
}
