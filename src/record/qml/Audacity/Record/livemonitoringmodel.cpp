/*
* Audacity: A Digital Audio Editor
*/
#include "livemonitoringmodel.h"

#include <algorithm>

using namespace au::record;

LiveMonitoringModel::LiveMonitoringModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void LiveMonitoringModel::init()
{
    for (auto n : { configuration()->isInputMonitoringOnChanged(), configuration()->monitorThroughEffectsChanged(),
                    configuration()->lowLatencyMonitoringChanged(), configuration()->lowLatencyThresholdMsChanged(),
                    configuration()->monitorGainDbChanged(), configuration()->monitorMutedChanged() }) {
        n.onNotify(this, [this]() { emit settingsChanged(); });
    }
    if (record()) {
        record()->monitoringStatusChanged().onNotify(this, [this]() { emit statusChanged(); });
    }
    if (selectionController()) {
        // the monitored effects follow the record target (first selected track)
        selectionController()->tracksSelected().onReceive(this, [this](const trackedit::TrackIdList&) { refresh(); });
    }
    globalContext()->currentProjectChanged().onNotify(this, [this]() { refresh(); });
    refresh();
}

void LiveMonitoringModel::refresh()
{
    if (record() && globalContext()->currentProject()) {
        record()->refreshMonitoring();
    }
    emit statusChanged();
}

bool LiveMonitoringModel::monitorInput() const { return configuration()->isInputMonitoringOn(); }
void LiveMonitoringModel::setMonitorInput(bool on) { configuration()->setIsInputMonitoringOn(on); }
bool LiveMonitoringModel::throughEffects() const { return configuration()->monitorThroughEffects(); }
void LiveMonitoringModel::setThroughEffects(bool on) { configuration()->setMonitorThroughEffects(on); }
bool LiveMonitoringModel::lowLatency() const { return configuration()->lowLatencyMonitoring(); }
void LiveMonitoringModel::setLowLatency(bool on) { configuration()->setLowLatencyMonitoring(on); }
double LiveMonitoringModel::thresholdMs() const { return configuration()->lowLatencyThresholdMs(); }
void LiveMonitoringModel::setThresholdMs(double ms) { configuration()->setLowLatencyThresholdMs(std::clamp(ms, 1.0, 500.0)); }
double LiveMonitoringModel::gainDb() const { return configuration()->monitorGainDb(); }
void LiveMonitoringModel::setGainDb(double db) { configuration()->setMonitorGainDb(std::clamp(db, -60.0, 12.0)); }
bool LiveMonitoringModel::muted() const { return configuration()->monitorMuted(); }
void LiveMonitoringModel::setMuted(bool on) { configuration()->setMonitorMuted(on); }

MonitoringStatus LiveMonitoringModel::status() const
{
    return record() ? record()->monitoringStatus() : MonitoringStatus {};
}

double LiveMonitoringModel::latencyMs() const
{
    return status().totalLatencyMs();
}

QString LiveMonitoringModel::latencyText() const
{
    const auto st = status();
    return tr("Monitoring latency ≈ %1 ms (device %2 ms + effects %3 ms)")
           .arg(QString::number(st.totalLatencyMs(), 'f', 0), QString::number(st.deviceLatencyMs, 'f', 0),
                QString::number(st.effectLatencyMs, 'f', 0));
}

static QString joinNames(const std::vector<std::string>& names)
{
    QStringList list;
    for (const auto& n : names) {
        list << QString::fromStdString(n);
    }
    return list.join(", ");
}

QString LiveMonitoringModel::effectsText() const
{
    const auto st = status();
    if (!throughEffects()) {
        return tr("Hearing the dry input");
    }
    const QString target = st.targetTrack.empty() ? tr("no track selected") : QString::fromStdString(st.targetTrack);
    if (st.effects.empty()) {
        return tr("Effects heard (%1 + master): none").arg(target);
    }
    return tr("Effects heard (%1 + master): %2").arg(target, joinNames(st.effects));
}

QString LiveMonitoringModel::bypassedText() const
{
    const auto st = status();
    return st.bypassed.empty() ? QString() : tr("Bypassed for low latency: %1").arg(joinNames(st.bypassed));
}

bool LiveMonitoringModel::hasBypassed() const
{
    return !status().bypassed.empty();
}
