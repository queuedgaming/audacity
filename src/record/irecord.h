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
};

using IAudioRecordPtr = std::shared_ptr<IRecord>;
}
