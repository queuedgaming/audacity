/*
* Audacity: A Digital Audio Editor
*/
#include "recordconfiguration.h"

#include "global/settings.h"

using namespace muse;
using namespace au::record;

namespace {
const muse::Settings::Key MIC_METERING_KEY("record", "record/micMetering");
const muse::Settings::Key INPUT_MONITORING_KEY("record", "record/inputMonitoring");
const muse::Settings::Key LEAD_IN_TIME_DURATION_KEY("record", "record/leadInTimeDuration");
const muse::Settings::Key CROSSFADE_DURATION_KEY("record", "record/crossfadeDuration");
const muse::Settings::Key KEEP_TAKES_KEY("record", "record/keepTakesWhenRecordingOver");
const muse::Settings::Key AUTOCOMP_SEGMENT_KEY("record", "record/autoCompSegmentLength");
const muse::Settings::Key AUTOCOMP_PITCH_KEY("record", "record/autoCompPitchScoring");
const muse::Settings::Key ARTIST_MODE_KEY("record", "record/artistMode");
const muse::Settings::Key COUNT_IN_KEY("record", "record/artistCountIn");
const muse::Settings::Key METRONOME_KEY("record", "record/metronome");
const muse::Settings::Key MONITOR_FX_KEY("record", "record/monitorThroughEffects");
const muse::Settings::Key MONITOR_LOWLAT_KEY("record", "record/lowLatencyMonitoring");
const muse::Settings::Key MONITOR_LOWLAT_MS_KEY("record", "record/lowLatencyThresholdMs");
const muse::Settings::Key MONITOR_GAIN_KEY("record", "record/monitorGainDb");
const muse::Settings::Key MONITOR_MUTE_KEY("record", "record/monitorMuted");
const muse::Settings::Key PUNCH_OUT_AT_SELECTION_END_KEY("record", "record/punchOutAtSelectionEnd");

//! NOTE: these keys deliberately match the au3 preference paths (bridged to the
//! same settings storage), so values recorded by au3 code stay readable;
//! "RecodingTrackName" reproduces the au3 key's typo
const muse::Settings::Key RECORDING_NAME_CUSTOM_KEY("record", "GUI/TrackNames/RecordingNameCustom");
const muse::Settings::Key CUSTOM_TRACK_NAME_KEY("record", "GUI/TrackNames/RecodingTrackName");
const muse::Settings::Key TRACK_NUMBER_KEY("record", "GUI/TrackNames/TrackNumber");
const muse::Settings::Key DATE_STAMP_KEY("record", "GUI/TrackNames/DateStamp");
const muse::Settings::Key TIME_STAMP_KEY("record", "GUI/TrackNames/TimeStamp");
}

void RecordConfiguration::init()
{
    muse::settings()->setDefaultValue(MIC_METERING_KEY, muse::Val(true));
    muse::settings()->valueChanged(MIC_METERING_KEY).onReceive(nullptr, [this](const muse::Val&) {
        m_isMicMeteringOnChanged.notify();
    });

    muse::settings()->setDefaultValue(INPUT_MONITORING_KEY, muse::Val(false));
    muse::settings()->valueChanged(INPUT_MONITORING_KEY).onReceive(nullptr, [this](const muse::Val&) {
        m_isInputMonitoringOnChanged.notify();
    });

    muse::settings()->setDefaultValue(LEAD_IN_TIME_DURATION_KEY, muse::Val(2.0));
    muse::settings()->valueChanged(LEAD_IN_TIME_DURATION_KEY).onReceive(nullptr, [this](const muse::Val&) {
        m_leadInTimeDurationChanged.notify();
    });

    muse::settings()->setDefaultValue(CROSSFADE_DURATION_KEY, muse::Val(10.0));
    muse::settings()->valueChanged(CROSSFADE_DURATION_KEY).onReceive(nullptr, [this](const muse::Val&) {
        m_crossfadeDurationChanged.notify();
    });

    muse::settings()->setDefaultValue(AUTOCOMP_PITCH_KEY, muse::Val(true));
    muse::settings()->valueChanged(AUTOCOMP_PITCH_KEY).onReceive(nullptr, [this](const muse::Val&) {
        m_autoCompPitchScoringChanged.notify();
    });
    muse::settings()->setDefaultValue(AUTOCOMP_SEGMENT_KEY, muse::Val(2.0));
    muse::settings()->valueChanged(AUTOCOMP_SEGMENT_KEY).onReceive(nullptr, [this](const muse::Val&) {
        m_autoCompSegmentLengthChanged.notify();
    });
    muse::settings()->setDefaultValue(KEEP_TAKES_KEY, muse::Val(true));
    muse::settings()->valueChanged(KEEP_TAKES_KEY).onReceive(nullptr, [this](const muse::Val&) {
        m_keepTakesChanged.notify();
    });

    muse::settings()->setDefaultValue(PUNCH_OUT_AT_SELECTION_END_KEY, muse::Val(true));
    muse::settings()->valueChanged(PUNCH_OUT_AT_SELECTION_END_KEY).onReceive(nullptr, [this](const muse::Val&) {
        m_punchOutAtSelectionEndChanged.notify();
    });
    muse::settings()->setDefaultValue(ARTIST_MODE_KEY, muse::Val(false));
    muse::settings()->valueChanged(ARTIST_MODE_KEY).onReceive(nullptr, [this](const muse::Val&) {
        m_artistModeEnabledChanged.notify();
    });
    muse::settings()->setDefaultValue(COUNT_IN_KEY, muse::Val(false));
    muse::settings()->valueChanged(COUNT_IN_KEY).onReceive(nullptr, [this](const muse::Val&) {
        m_countInEnabledChanged.notify();
    });
    muse::settings()->setDefaultValue(METRONOME_KEY, muse::Val(false));
    muse::settings()->valueChanged(METRONOME_KEY).onReceive(nullptr, [this](const muse::Val&) {
        m_metronomeEnabledChanged.notify();
    });
    muse::settings()->setDefaultValue(MONITOR_FX_KEY, muse::Val(true));
    muse::settings()->valueChanged(MONITOR_FX_KEY).onReceive(nullptr, [this](const muse::Val&) {
        m_monitorThroughEffectsChanged.notify();
    });
    muse::settings()->setDefaultValue(MONITOR_LOWLAT_KEY, muse::Val(false));
    muse::settings()->valueChanged(MONITOR_LOWLAT_KEY).onReceive(nullptr, [this](const muse::Val&) {
        m_lowLatencyMonitoringChanged.notify();
    });
    muse::settings()->setDefaultValue(MONITOR_LOWLAT_MS_KEY, muse::Val(20.0));
    muse::settings()->valueChanged(MONITOR_LOWLAT_MS_KEY).onReceive(nullptr, [this](const muse::Val&) {
        m_lowLatencyThresholdMsChanged.notify();
    });
    muse::settings()->setDefaultValue(MONITOR_GAIN_KEY, muse::Val(0.0));
    muse::settings()->valueChanged(MONITOR_GAIN_KEY).onReceive(nullptr, [this](const muse::Val&) {
        m_monitorGainDbChanged.notify();
    });
    muse::settings()->setDefaultValue(MONITOR_MUTE_KEY, muse::Val(false));
    muse::settings()->valueChanged(MONITOR_MUTE_KEY).onReceive(nullptr, [this](const muse::Val&) {
        m_monitorMutedChanged.notify();
    });
}

bool RecordConfiguration::isMicMeteringOn() const
{
    return muse::settings()->value(MIC_METERING_KEY).toBool();
}

void RecordConfiguration::setIsMicMeteringOn(bool enable)
{
    muse::settings()->setSharedValue(MIC_METERING_KEY, muse::Val(enable));
}

muse::async::Notification RecordConfiguration::isMicMeteringOnChanged() const
{
    return m_isMicMeteringOnChanged;
}

bool RecordConfiguration::isInputMonitoringOn() const
{
    return muse::settings()->value(INPUT_MONITORING_KEY).toBool();
}

void RecordConfiguration::setIsInputMonitoringOn(bool enable)
{
    muse::settings()->setSharedValue(INPUT_MONITORING_KEY, muse::Val(enable));
}

muse::async::Notification RecordConfiguration::isInputMonitoringOnChanged() const
{
    return m_isInputMonitoringOnChanged;
}

double RecordConfiguration::leadInTimeDuration() const
{
    return muse::settings()->value(LEAD_IN_TIME_DURATION_KEY).toDouble();
}

void RecordConfiguration::setLeadInTimeDuration(double seconds)
{
    muse::settings()->setSharedValue(LEAD_IN_TIME_DURATION_KEY, muse::Val(seconds));
}

muse::async::Notification RecordConfiguration::leadInTimeDurationChanged() const
{
    return m_leadInTimeDurationChanged;
}

double RecordConfiguration::crossfadeDuration() const
{
    return muse::settings()->value(CROSSFADE_DURATION_KEY).toDouble();
}

void RecordConfiguration::setCrossfadeDuration(double milliseconds)
{
    muse::settings()->setSharedValue(CROSSFADE_DURATION_KEY, muse::Val(milliseconds));
}

muse::async::Notification RecordConfiguration::crossfadeDurationChanged() const
{
    return m_crossfadeDurationChanged;
}

bool RecordConfiguration::keepTakesWhenRecordingOver() const
{
    return muse::settings()->value(KEEP_TAKES_KEY).toBool();
}

void RecordConfiguration::setKeepTakesWhenRecordingOver(bool enabled)
{
    muse::settings()->setSharedValue(KEEP_TAKES_KEY, muse::Val(enabled));
}

muse::async::Notification RecordConfiguration::keepTakesWhenRecordingOverChanged() const
{
    return m_keepTakesChanged;
}

bool RecordConfiguration::punchOutAtSelectionEnd() const
{
    return muse::settings()->value(PUNCH_OUT_AT_SELECTION_END_KEY).toBool();
}

void RecordConfiguration::setPunchOutAtSelectionEnd(bool enabled)
{
    muse::settings()->setSharedValue(PUNCH_OUT_AT_SELECTION_END_KEY, muse::Val(enabled));
}

muse::async::Notification RecordConfiguration::punchOutAtSelectionEndChanged() const
{
    return m_punchOutAtSelectionEndChanged;
}

RecordingTrackNameOptions RecordConfiguration::recordingTrackNameOptions() const
{
    RecordingTrackNameOptions options;
    options.useCustomName = muse::settings()->value(RECORDING_NAME_CUSTOM_KEY).toBool();
    options.customName = muse::settings()->value(CUSTOM_TRACK_NAME_KEY).toString();
    options.addTrackNumber = muse::settings()->value(TRACK_NUMBER_KEY).toBool();
    options.addDateStamp = muse::settings()->value(DATE_STAMP_KEY).toBool();
    options.addTimeStamp = muse::settings()->value(TIME_STAMP_KEY).toBool();
    return options;
}

double RecordConfiguration::autoCompSegmentLength() const
{
    return muse::settings()->value(AUTOCOMP_SEGMENT_KEY).toDouble();
}

void RecordConfiguration::setAutoCompSegmentLength(double seconds)
{
    muse::settings()->setSharedValue(AUTOCOMP_SEGMENT_KEY, muse::Val(seconds));
}

muse::async::Notification RecordConfiguration::autoCompSegmentLengthChanged() const
{
    return m_autoCompSegmentLengthChanged;
}

bool RecordConfiguration::autoCompPitchScoring() const
{
    return muse::settings()->value(AUTOCOMP_PITCH_KEY).toBool();
}

void RecordConfiguration::setAutoCompPitchScoring(bool enabled)
{
    muse::settings()->setSharedValue(AUTOCOMP_PITCH_KEY, muse::Val(enabled));
}

muse::async::Notification RecordConfiguration::autoCompPitchScoringChanged() const
{
    return m_autoCompPitchScoringChanged;
}

bool RecordConfiguration::artistModeEnabled() const
{
    return muse::settings()->value(ARTIST_MODE_KEY).toBool();
}

void RecordConfiguration::setArtistModeEnabled(bool enabled)
{
    muse::settings()->setSharedValue(ARTIST_MODE_KEY, muse::Val(enabled));
}

muse::async::Notification RecordConfiguration::artistModeEnabledChanged() const
{
    return m_artistModeEnabledChanged;
}

bool RecordConfiguration::countInEnabled() const
{
    return muse::settings()->value(COUNT_IN_KEY).toBool();
}

void RecordConfiguration::setCountInEnabled(bool enabled)
{
    muse::settings()->setSharedValue(COUNT_IN_KEY, muse::Val(enabled));
}

muse::async::Notification RecordConfiguration::countInEnabledChanged() const
{
    return m_countInEnabledChanged;
}

bool RecordConfiguration::metronomeEnabled() const
{
    return muse::settings()->value(METRONOME_KEY).toBool();
}

void RecordConfiguration::setMetronomeEnabled(bool enabled)
{
    muse::settings()->setSharedValue(METRONOME_KEY, muse::Val(enabled));
}

muse::async::Notification RecordConfiguration::metronomeEnabledChanged() const
{
    return m_metronomeEnabledChanged;
}

bool RecordConfiguration::monitorThroughEffects() const
{
    return muse::settings()->value(MONITOR_FX_KEY).toBool();
}

void RecordConfiguration::setMonitorThroughEffects(bool value)
{
    muse::settings()->setSharedValue(MONITOR_FX_KEY, muse::Val(value));
}

muse::async::Notification RecordConfiguration::monitorThroughEffectsChanged() const
{
    return m_monitorThroughEffectsChanged;
}

bool RecordConfiguration::lowLatencyMonitoring() const
{
    return muse::settings()->value(MONITOR_LOWLAT_KEY).toBool();
}

void RecordConfiguration::setLowLatencyMonitoring(bool value)
{
    muse::settings()->setSharedValue(MONITOR_LOWLAT_KEY, muse::Val(value));
}

muse::async::Notification RecordConfiguration::lowLatencyMonitoringChanged() const
{
    return m_lowLatencyMonitoringChanged;
}

double RecordConfiguration::lowLatencyThresholdMs() const
{
    return muse::settings()->value(MONITOR_LOWLAT_MS_KEY).toDouble();
}

void RecordConfiguration::setLowLatencyThresholdMs(double value)
{
    muse::settings()->setSharedValue(MONITOR_LOWLAT_MS_KEY, muse::Val(value));
}

muse::async::Notification RecordConfiguration::lowLatencyThresholdMsChanged() const
{
    return m_lowLatencyThresholdMsChanged;
}

double RecordConfiguration::monitorGainDb() const
{
    return muse::settings()->value(MONITOR_GAIN_KEY).toDouble();
}

void RecordConfiguration::setMonitorGainDb(double value)
{
    muse::settings()->setSharedValue(MONITOR_GAIN_KEY, muse::Val(value));
}

muse::async::Notification RecordConfiguration::monitorGainDbChanged() const
{
    return m_monitorGainDbChanged;
}

bool RecordConfiguration::monitorMuted() const
{
    return muse::settings()->value(MONITOR_MUTE_KEY).toBool();
}

void RecordConfiguration::setMonitorMuted(bool value)
{
    muse::settings()->setSharedValue(MONITOR_MUTE_KEY, muse::Val(value));
}

muse::async::Notification RecordConfiguration::monitorMutedChanged() const
{
    return m_monitorMutedChanged;
}
