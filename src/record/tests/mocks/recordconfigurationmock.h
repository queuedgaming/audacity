/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <gmock/gmock.h>

#include "record/irecordconfiguration.h"

namespace au::record {
class RecordConfigurationMock : public IRecordConfiguration
{
public:
    MOCK_METHOD(bool, isMicMeteringOn, (), (const, override));
    MOCK_METHOD(void, setIsMicMeteringOn, (bool), (override));
    MOCK_METHOD(muse::async::Notification, isMicMeteringOnChanged, (), (const, override));

    MOCK_METHOD(bool, isInputMonitoringOn, (), (const, override));
    MOCK_METHOD(void, setIsInputMonitoringOn, (bool), (override));
    MOCK_METHOD(muse::async::Notification, isInputMonitoringOnChanged, (), (const, override));

    MOCK_METHOD(double, leadInTimeDuration, (), (const, override));
    MOCK_METHOD(void, setLeadInTimeDuration, (double), (override));
    MOCK_METHOD(muse::async::Notification, leadInTimeDurationChanged, (), (const, override));

    MOCK_METHOD(double, crossfadeDuration, (), (const, override));
    MOCK_METHOD(void, setCrossfadeDuration, (double), (override));
    MOCK_METHOD(muse::async::Notification, crossfadeDurationChanged, (), (const, override));

    MOCK_METHOD(bool, keepTakesWhenRecordingOver, (), (const, override));
    MOCK_METHOD(void, setKeepTakesWhenRecordingOver, (bool), (override));
    MOCK_METHOD(muse::async::Notification, keepTakesWhenRecordingOverChanged, (), (const, override));
    MOCK_METHOD(double, autoCompSegmentLength, (), (const, override));
    MOCK_METHOD(void, setAutoCompSegmentLength, (double), (override));
    MOCK_METHOD(muse::async::Notification, autoCompSegmentLengthChanged, (), (const, override));
    MOCK_METHOD(bool, autoCompPitchScoring, (), (const, override));
    MOCK_METHOD(void, setAutoCompPitchScoring, (bool), (override));
    MOCK_METHOD(muse::async::Notification, autoCompPitchScoringChanged, (), (const, override));
    MOCK_METHOD(bool, punchOutAtSelectionEnd, (), (const, override));
    MOCK_METHOD(void, setPunchOutAtSelectionEnd, (bool), (override));
    MOCK_METHOD(muse::async::Notification, punchOutAtSelectionEndChanged, (), (const, override));
    MOCK_METHOD(bool, artistModeEnabled, (), (const, override));
    MOCK_METHOD(void, setArtistModeEnabled, (bool), (override));
    MOCK_METHOD(muse::async::Notification, artistModeEnabledChanged, (), (const, override));
    MOCK_METHOD(bool, countInEnabled, (), (const, override));
    MOCK_METHOD(void, setCountInEnabled, (bool), (override));
    MOCK_METHOD(muse::async::Notification, countInEnabledChanged, (), (const, override));
    MOCK_METHOD(bool, metronomeEnabled, (), (const, override));
    MOCK_METHOD(void, setMetronomeEnabled, (bool), (override));
    MOCK_METHOD(muse::async::Notification, metronomeEnabledChanged, (), (const, override));

    MOCK_METHOD(RecordingTrackNameOptions, recordingTrackNameOptions, (), (const, override));
};
}
