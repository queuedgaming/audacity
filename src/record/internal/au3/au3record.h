/*
* Audacity: A Digital Audio Editor
*/

#pragma once

#include <map>

#include <vector>

#include "framework/global/async/asyncable.h"
#include "framework/global/modularity/ioc.h"
#include "framework/actions/iactionsdispatcher.h"

#include "context/iglobalcontext.h"
#include "context/iplaybackstate.h"
#include "trackedit/iprojecthistory.h"

class WaveTrack;
class WaveClip;
#include "trackedit/itrackeditinteraction.h"
#include "trackedit/iselectioncontroller.h"
#include "audio/iaudioengine.h"
#include "audio/driver/iaudiodrivercontroller.h"
#include "au3wrap/au3types.h"

#include "../../irecord.h"
#include "../../irecordconfiguration.h"

struct TransportSequences;
struct AudioIOStartStreamOptions;

namespace au::record {
class Au3Record : public IRecord, public muse::async::Asyncable, public muse::Contextable
{
    muse::GlobalInject<au::audio::IAudioEngine> audioEngine;
    muse::GlobalInject<record::IRecordConfiguration> recordConfiguration;
    muse::GlobalInject<audio::IAudioDriverController> audioDriverController;

    muse::ContextInject<au::context::IGlobalContext> globalContext{ this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher{ this };
    muse::ContextInject<au::trackedit::IProjectHistory> projectHistory{ this };
    muse::ContextInject<trackedit::ITrackeditInteraction> trackeditInteraction{ this };
    muse::ContextInject<au::trackedit::ISelectionController> selectionController{ this };

    friend class Au3RecordTests;

public:
    Au3Record(const muse::modularity::ContextPtr& ctx)
        : muse::Contextable(ctx) {}

    void init();

    muse::Ret start() override;
    muse::Ret pause() override;
    muse::Ret resume() override;
    muse::Ret stop() override;
    muse::Ret leadInRecording() override;

    bool canSwitchTake(trackedit::TrackId trackId, TakeSwitch kind) const override;
    muse::Ret switchTake(trackedit::TrackId trackId, TakeSwitch kind) override;
    KeyDetection detectKey(trackedit::TrackId trackId, double t0, double t1, bool store) override;
    std::optional<ProjectKeyInfo> projectKey() const override;
    muse::Ret setProjectKey(const std::optional<ProjectKeyInfo>& key) override;
    muse::async::Notification projectKeyChanged() const override;
    std::string projectKeyLabel(const std::optional<ProjectKeyInfo>& key) const override;
    std::string lastAutoCompKey() const override { return m_lastAutoCompKey; }
    muse::Ret setClipFades(const trackedit::ClipKey& clip, double fadeIn, double fadeOut, int curve) override;
    muse::Ret detectTransients(const trackedit::ClipKey& clip, double sensitivity, bool toLabelTrack) override;
    muse::Ret clearTransients(const trackedit::ClipKey& clip) override;
    muse::Ret sliceAtTransients(const trackedit::ClipKey& clip, double quantizeStrength, int quantizeDivision) override;
    muse::Ret sliceToGrid(const trackedit::ClipKey& clip, int division) override;

    //! Stop time for lead-in recording: selection end when punch-out applies, else DBL_MAX
    double punchOutTime(double punchInTime) const;

    IAudioInputPtr audioInput() const override;

    muse::async::Channel<muse::secs_t> recordPositionChanged() const override;

    const std::vector<trackedit::ClipKey>& recordingClipKeys() const override;

    muse::async::Notification recordingFinished() const override;

private:
    //! split at project times, crossfade, optionally quantize; returns number of pieces made
    size_t sliceClipAt(::WaveTrack* track, std::shared_ptr<::WaveClip> clip, std::vector<double> cuts, double strength, double step);
    std::vector<double> detectOnsetTimes(::WaveTrack* track, const ::WaveClip& clip, double sensitivity) const;
    struct RecordData {
        trackedit::ClipKey clipKey;          // clip on original track (for commit). In deferred mode, set when recording starts.
        au3::Au3ClipId pendingClipId;        // clip ID on pending track (for callback lookup)
        bool linkedToPendingClip;
        bool deferredClipCreation = false;   // true for lead-in-recording: clip on pending only, not on original yet
    };

    //! Audio that a recording may overwrite, kept so it can be preserved as a take lane
    struct TakeSnapshot {
        std::shared_ptr<au3::Au3WaveTrack> audio;
        double t0 = 0.0;
    };
    std::map<trackedit::TrackId, TakeSnapshot> m_takeSnapshots;
    void preserveOverwrittenTakes();

    au3::Au3Project& projectRef() const;

    bool canStopAudioStream() const;

    muse::Ret doRecord(au3::Au3Project& project, const TransportSequences& sequences, double t0, double t1, bool altAppearance,
                       const double audioStreamSampleRate, double leadInTime = 0.0,
                       std::vector<std::vector<float> >* crossfadeData = nullptr);
    void cancelRecording();
    void commitRecording();

    void notifyAboutRecordClipsChanged();

    void rebuildRecordingClipKeys();

    mutable muse::async::Channel<float> m_playbackVolumeChanged;

    IAudioInputPtr m_audioInput;
    std::vector<RecordData> m_recordData;
    std::vector<trackedit::ClipKey> m_recordingClipKeys;

    muse::ValCh<muse::secs_t> m_recordPosition;
    muse::async::Notification m_recordingFinished;

    context::IPlaybackStatePtr playbackState() const;
    std::string m_lastAutoCompKey;
};
}
