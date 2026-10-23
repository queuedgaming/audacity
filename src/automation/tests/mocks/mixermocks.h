/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <gmock/gmock.h>

#include "effects/effects_base/irealtimeeffectservice.h"
#include "effects/effects_base/ieffectviewcontroller.h"
#include "playback/itrackplaybackcontrol.h"
#include "automation/ibusroutinginteraction.h"
#include "automation/igainstaginginteraction.h"

namespace au::automation {
class RealtimeEffectServiceMock : public effects::IRealtimeEffectService
{
public:
    using StatePtr = effects::RealtimeEffectStatePtr;
    using TrackId = effects::TrackId;
    MOCK_METHOD(StatePtr, addRealtimeEffect, (TrackId, const effects::EffectId&), (override));
    MOCK_METHOD(void, removeRealtimeEffect, (TrackId, const StatePtr&), (override));
    MOCK_METHOD(StatePtr, replaceRealtimeEffect, (TrackId, int, const effects::EffectId&), (override));
    MOCK_METHOD(StatePtr, replaceRealtimeEffect, (TrackId, const StatePtr&, const effects::EffectId&), (override));
    MOCK_METHOD(void, moveRealtimeEffect, (const StatePtr&, int), (override));
    MOCK_METHOD((muse::async::Channel<TrackId, StatePtr>), realtimeEffectAdded, (), (const, override));
    MOCK_METHOD((muse::async::Channel<TrackId, StatePtr>), realtimeEffectRemoved, (), (const, override));
    MOCK_METHOD((muse::async::Channel<TrackId, effects::EffectChainLinkIndex, StatePtr>), realtimeEffectReplaced, (),
                (const, override));
    MOCK_METHOD((muse::async::Channel<TrackId, effects::EffectChainLinkIndex, effects::EffectChainLinkIndex>),
                realtimeEffectMoved, (), (const, override));
    MOCK_METHOD(muse::async::Channel<TrackId>, realtimeEffectStackChanged, (), (const, override));
    MOCK_METHOD(muse::async::Notification, effectSettingsChanged, (), (const, override));
    MOCK_METHOD(std::optional<TrackId>, trackId, (const StatePtr&), (const, override));
    MOCK_METHOD(std::optional<std::string>, effectName, (const StatePtr&), (const, override));
    MOCK_METHOD(std::optional<std::string>, effectTrackName, (const StatePtr&), (const, override));
    MOCK_METHOD(std::optional<std::vector<StatePtr> >, effectStack, (TrackId), (const, override));
    MOCK_METHOD(bool, isAvailable, (const StatePtr&), (const, override));
    MOCK_METHOD(bool, isActive, (const StatePtr&), (const, override));
    MOCK_METHOD(void, setIsActive, (const StatePtr&, bool), (override));
    MOCK_METHOD(muse::async::Channel<StatePtr>, isActiveChanged, (), (const, override));
    MOCK_METHOD(bool, trackEffectsActive, (TrackId), (const, override));
    MOCK_METHOD(void, setTrackEffectsActive, (TrackId, bool), (override));
};

class EffectViewControllerMock : public effects::IEffectViewController
{
public:
    MOCK_METHOD(muse::Ret, showEffect, (const effects::EffectId&, const effects::EffectInstanceId&), (override));
    MOCK_METHOD(void, showEffect, (const effects::RealtimeEffectStatePtr&), (const, override));
    MOCK_METHOD(void, hideEffect, (const effects::RealtimeEffectStatePtr&), (const, override));
};

class TrackPlaybackControlMock : public playback::ITrackPlaybackControl
{
public:
    MOCK_METHOD(void, setVolume, (long, audio::volume_dbfs_t, bool), (override));
    MOCK_METHOD(audio::volume_dbfs_t, volume, (long), (const, override));
    MOCK_METHOD(void, setPan, (long, audio::pan_t, bool), (override));
    MOCK_METHOD(audio::pan_t, pan, (long), (const, override));
    MOCK_METHOD(void, setSolo, (long, bool, bool), (override));
    MOCK_METHOD(bool, solo, (long), (const, override));
    MOCK_METHOD(void, setMuted, (long, bool, bool), (override));
    MOCK_METHOD(void, setMuted, (const trackedit::TrackIdList&, bool), (override));
    MOCK_METHOD(bool, muted, (long), (const, override));
    MOCK_METHOD(muse::async::Channel<long>, muteOrSoloChanged, (), (const, override));
};

class BusRoutingInteractionMock : public IBusRoutingInteraction
{
public:
    MOCK_METHOD(std::vector<RoutingNode>, nodes, (), (const, override));
    MOCK_METHOD(trackedit::TrackId, addBus, (), (override));
    MOCK_METHOD(bool, canRouteTo, (trackedit::TrackId, const std::string&), (const, override));
    MOCK_METHOD(bool, setOutput, (trackedit::TrackId, const std::string&), (override));
    MOCK_METHOD(bool, setSend, (trackedit::TrackId, const std::string&, float, bool), (override));
    MOCK_METHOD(bool, removeSend, (trackedit::TrackId, const std::string&), (override));
    MOCK_METHOD(muse::async::Notification, changed, (), (const, override));
};

class GainStagingInteractionMock : public IGainStagingInteraction
{
public:
    MOCK_METHOD(double, trimDb, (trackedit::TrackId), (const, override));
    MOCK_METHOD(bool, setTrim, (trackedit::TrackId, double, bool), (override));
    MOCK_METHOD(int, applyTrims, ((const std::map<trackedit::TrackId, double>&)), (override));
    MOCK_METHOD(std::vector<TrackLevels>, analyze, (), (const, override));
    MOCK_METHOD(muse::async::Notification, changed, (), (const, override));
};
}
