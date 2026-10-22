/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include "framework/global/modularity/ioc.h"
#include "framework/ui/iuiconfiguration.h"

#include "../irecordconfiguration.h"

namespace au::record {
//! Low-latency monitoring threshold: effects with more latency are bypassed while monitoring.
//! 25 ms keeps Noise suppression (~21 ms) and still bypasses Breath removal (~230 ms).
constexpr double DEFAULT_LOW_LATENCY_THRESHOLD_MS = 25.0;
constexpr double OLD_DEFAULT_LOW_LATENCY_THRESHOLD_MS = 20.0;
//! One-time migration of a stored threshold: the old default becomes the new one
inline double migrateLowLatencyThresholdMs(double stored)
{
    return stored == OLD_DEFAULT_LOW_LATENCY_THRESHOLD_MS ? DEFAULT_LOW_LATENCY_THRESHOLD_MS : stored;
}

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
    bool monitorThroughEffects() const override;
    void setMonitorThroughEffects(bool value) override;
    muse::async::Notification monitorThroughEffectsChanged() const override;
    bool lowLatencyMonitoring() const override;
    void setLowLatencyMonitoring(bool value) override;
    muse::async::Notification lowLatencyMonitoringChanged() const override;
    double lowLatencyThresholdMs() const override;
    void setLowLatencyThresholdMs(double value) override;
    muse::async::Notification lowLatencyThresholdMsChanged() const override;
    double monitorGainDb() const override;
    void setMonitorGainDb(double value) override;
    muse::async::Notification monitorGainDbChanged() const override;
    bool monitorMuted() const override;
    void setMonitorMuted(bool value) override;
    muse::async::Notification monitorMutedChanged() const override;
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
    muse::async::Notification m_monitorThroughEffectsChanged;
    muse::async::Notification m_lowLatencyMonitoringChanged;
    muse::async::Notification m_lowLatencyThresholdMsChanged;
    muse::async::Notification m_monitorGainDbChanged;
    muse::async::Notification m_monitorMutedChanged;
    muse::async::Notification m_artistModeEnabledChanged;
    muse::async::Notification m_countInEnabledChanged;
    muse::async::Notification m_metronomeEnabledChanged;
    muse::async::Notification m_keepTakesChanged;
    muse::async::Notification m_autoCompSegmentLengthChanged;
    muse::async::Notification m_autoCompPitchScoringChanged;
};
}
