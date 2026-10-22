/*
* Audacity: A Digital Audio Editor
*/

#pragma once

#include <memory>
#include <vector>

#include <optional>

#include "async/notification.h"
#include "global/types/ret.h"

#include "modularity/imoduleinterface.h"

#include "trackedit/trackedittypes.h"
#include "iaudioinput.h"
#include "taketypes.h"

namespace au::record {
class IRecord : MODULE_EXPORT_INTERFACE
{
    INTERFACE_ID(IRecord)
public:
    virtual ~IRecord() = default;

    virtual muse::Ret start() = 0;
    virtual muse::Ret pause() = 0;
    virtual muse::Ret resume() = 0;
    virtual muse::Ret stop() = 0;
    virtual muse::Ret leadInRecording() = 0;

    virtual IAudioInputPtr audioInput() const = 0;

    virtual muse::async::Channel<muse::secs_t> recordPositionChanged() const = 0;

    virtual const std::vector<trackedit::ClipKey>& recordingClipKeys() const = 0;

    virtual muse::async::Notification recordingFinished() const = 0;

    //! Take lanes of the given track (a main track or one of its takes)
    virtual bool canSwitchTake(trackedit::TrackId trackId, TakeSwitch kind) const = 0;
    virtual muse::Ret switchTake(trackedit::TrackId trackId, TakeSwitch kind) = 0;

    //! Key / scale and tuning of the track over [t0, t1) (whole track when t1 <= t0);
    //! when `store` and the result is valid, it becomes the project key
    virtual KeyDetection detectKey(trackedit::TrackId trackId, double t0, double t1, bool store) = 0;

    //! Project key: read, set or clear (nullopt). Each change is one undo step; no-op if unchanged.
    virtual std::optional<ProjectKeyInfo> projectKey() const = 0;
    virtual muse::Ret setProjectKey(const std::optional<ProjectKeyInfo>& key) = 0;
    //! Sent on every change of the current project's key (edits, undo/redo, load)
    virtual muse::async::Notification projectKeyChanged() const = 0;
    //! "D min, A442" or "No key"
    virtual std::string projectKeyLabel(const std::optional<ProjectKeyInfo>& key) const = 0;
    //! What the last auto-comp scored intonation against ("project key D min, A442", "detected ...", "no clear key, chromatic")
    virtual std::string lastAutoCompKey() const = 0;

    //! Non-destructive clip fades (seconds; curve 0 linear, 1 equal power), one undo step
    virtual muse::Ret setClipFades(const trackedit::ClipKey& clip, double fadeIn, double fadeOut, int curve) = 0;

    //! Transients (beat slicing). Each call is one undo step.
    //! Detect onsets in the clip and store them as markers on it; optionally also as numbered labels
    //! on a new "Transients" label track. sensitivity 0..100.
    virtual muse::Ret detectTransients(const trackedit::ClipKey& clip, double sensitivity, bool toLabelTrack) = 0;
    virtual muse::Ret clearTransients(const trackedit::ClipKey& clip) = 0;
    //! Split the clip at its markers (detecting first if it has none), with short crossfades at the cuts.
    //! quantizeStrength 0..1 moves each slice toward the nearest grid line of the given division
    //! (0 Bar, 1 Beat, 2 1/8, 3 1/16) of the project tempo.
    virtual muse::Ret sliceAtTransients(const trackedit::ClipKey& clip, double quantizeStrength = 0.0, int quantizeDivision = 2) = 0;
    //! Split the clip at every grid line of the project tempo and time signature
    virtual muse::Ret sliceToGrid(const trackedit::ClipKey& clip, int division) = 0;
};

using IAudioRecordPtr = std::shared_ptr<IRecord>;
}
