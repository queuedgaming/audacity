/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <gmock/gmock.h>

#include "record/irecord.h"

namespace au::record {
class RecordMock : public IRecord
{
public:
    MOCK_METHOD(muse::Ret, start, (), (override));
    MOCK_METHOD(muse::Ret, pause, (), (override));
    MOCK_METHOD(muse::Ret, resume, (), (override));
    MOCK_METHOD(muse::Ret, stop, (), (override));
    MOCK_METHOD(muse::Ret, leadInRecording, (), (override));

    MOCK_METHOD(IAudioInputPtr, audioInput, (), (const, override));

    MOCK_METHOD(muse::async::Channel<muse::secs_t>, recordPositionChanged, (), (const, override));

    MOCK_METHOD(const std::vector<trackedit::ClipKey>&, recordingClipKeys, (), (const, override));

    MOCK_METHOD(muse::async::Notification, recordingFinished, (), (const, override));

    MOCK_METHOD(bool, canSwitchTake, (trackedit::TrackId, TakeSwitch), (const, override));
    MOCK_METHOD(muse::Ret, switchTake, (trackedit::TrackId, TakeSwitch), (override));
    MOCK_METHOD(KeyDetection, detectKey, (trackedit::TrackId, double, double, bool), (override));
    MOCK_METHOD(std::optional<ProjectKeyInfo>, projectKey, (), (const, override));
    MOCK_METHOD(muse::Ret, setProjectKey, (const std::optional<ProjectKeyInfo>&), (override));
    MOCK_METHOD(muse::async::Notification, projectKeyChanged, (), (const, override));
    MOCK_METHOD(std::string, projectKeyLabel, (const std::optional<ProjectKeyInfo>&), (const, override));
    MOCK_METHOD(std::string, lastAutoCompKey, (), (const, override));
    MOCK_METHOD(muse::Ret, setClipFades, (const trackedit::ClipKey&, double, double, int), (override));
};
}
