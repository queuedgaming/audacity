/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include "framework/global/modularity/ioc.h"
#include "framework/ui/iuiconfiguration.h"

#include "../irecordconfiguration.h"

namespace au::record {
class RecordConfiguration : public IRecordConfiguration
{
    muse::GlobalInject<muse::ui::IUiConfiguration> uiConfiguration;

public:
    void init();

    bool isMicMeteringOn() const override;
    void setIsMicMeteringOn(bool enable) override;
    muse::async::Notification isMicMeteringOnChanged() const override;

    bool isInputMonitoringOn() const override;
    void setIsInputMonitoringOn(bool enable) override;
    muse::async::Notification isInputMonitoringOnChanged() const override;

    double leadInTimeDuration() const override;
    void setLeadInTimeDuration(double seconds) override;
    muse::async::Notification leadInTimeDurationChanged() const override;

    double crossfadeDuration() const override;
    void setCrossfadeDuration(double milliseconds) override;
    muse::async::Notification crossfadeDurationChanged() const override;

    bool keepTakesWhenRecordingOver() const override;
    void setKeepTakesWhenRecordingOver(bool enabled) override;
    muse::async::Notification keepTakesWhenRecordingOverChanged() const override;

    double autoCompSegmentLength() const override;
    void setAutoCompSegmentLength(double seconds) override;
    muse::async::Notification autoCompSegmentLengthChanged() const override;

    bool autoCompPitchScoring() const override;
    void setAutoCompPitchScoring(bool enabled) override;
    muse::async::Notification autoCompPitchScoringChanged() const override;

    bool punchOutAtSelectionEnd() const override;
    void setPunchOutAtSelectionEnd(bool enabled) override;
    muse::async::Notification punchOutAtSelectionEndChanged() const override;
    bool artistModeEnabled() const override;
    void setArtistModeEnabled(bool enabled) override;
    muse::async::Notification artistModeEnabledChanged() const override;
    bool countInEnabled() const override;
    void setCountInEnabled(bool enabled) override;
    muse::async::Notification countInEnabledChanged() const override;
    bool metronomeEnabled() const override;
    void setMetronomeEnabled(bool enabled) override;
    muse::async::Notification metronomeEnabledChanged() const override;

    RecordingTrackNameOptions recordingTrackNameOptions() const override;

private:
    muse::async::Notification m_isMicMeteringOnChanged;
    muse::async::Notification m_isInputMonitoringOnChanged;
    muse::async::Notification m_leadInTimeDurationChanged;
    muse::async::Notification m_crossfadeDurationChanged;
    muse::async::Notification m_punchOutAtSelectionEndChanged;
    muse::async::Notification m_artistModeEnabledChanged;
    muse::async::Notification m_countInEnabledChanged;
    muse::async::Notification m_metronomeEnabledChanged;
    muse::async::Notification m_keepTakesChanged;
    muse::async::Notification m_autoCompSegmentLengthChanged;
    muse::async::Notification m_autoCompPitchScoringChanged;
};
}
