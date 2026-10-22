/*
* Audacity: A Digital Audio Editor
*/
#include "mixerpanelmodel.h"

#include <algorithm>
#include <QDateTime>

#include "au3wrap/au3types.h"
#include "au3wrap/internal/mixerstrips.h"
#include "playback/iaudiooutput.h"

using namespace au::automation;
namespace mx = au::au3::mixer;

namespace {
constexpr float FLOOR_DB = -60.f;

QString kindName(mx::StripKind k)
{
    switch (k) {
    case mx::StripKind::Track: return "track";
    case mx::StripKind::Bus: return "bus";
    case mx::StripKind::Master: return "master";
    }
    return "track";
}
}

MixerPanelModel::MixerPanelModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
    m_meterTimer.setInterval(50);
    connect(&m_meterTimer, &QTimer::timeout, this, [this]() {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        for (auto& [id, lvl] : m_levels) {
            // fall back to silence when a strip stops sending levels (stopped, muted)
            if (lvl.at && now - lvl.at > 300 && (lvl.l > FLOOR_DB || lvl.r > FLOOR_DB)) {
                lvl.l = lvl.r = FLOOR_DB;
                m_metersDirty = true;
            }
        }
        if (m_metersDirty) {
            m_metersDirty = false;
            emit metersChanged();
        }
    });
}

void MixerPanelModel::init()
{
    if (projectHistory()) {
        projectHistory()->historyChanged().onReceive(this, [this](const auto&) { scheduleReload(); });
    }
    if (routing()) {
        routing()->changed().onNotify(this, [this]() { scheduleReload(); });
    }
    if (trackPlaybackControl()) {
        trackPlaybackControl()->muteOrSoloChanged().onReceive(this, [this](long) { scheduleReload(); });
    }
    if (auto fx = realtimeEffectService()) {
        fx->realtimeEffectStackChanged().onReceive(this, [this](effects::TrackId) { scheduleReload(); });
        fx->isActiveChanged().onReceive(this, [this](const effects::RealtimeEffectStatePtr&) { scheduleReload(); });
        fx->realtimeEffectAdded().onReceive(this, [this](effects::TrackId, effects::RealtimeEffectStatePtr) { scheduleReload(); });
        fx->realtimeEffectRemoved().onReceive(this, [this](effects::TrackId, effects::RealtimeEffectStatePtr) { scheduleReload(); });
        fx->realtimeEffectMoved().onReceive(this, [this](effects::TrackId, effects::EffectChainLinkIndex, effects::EffectChainLinkIndex) {
            scheduleReload();
        });
        fx->realtimeEffectReplaced().onReceive(this, [this](effects::TrackId, effects::EffectChainLinkIndex,
                                                            effects::RealtimeEffectStatePtr) { scheduleReload(); });
    }
    subscribeProject();
    if (globalContext()) {
        // the panel outlives projects: follow the current one
        globalContext()->currentProjectChanged().onNotify(this, [this]() {
            subscribeProject();
            scheduleReload();
        });
    }
    if (playback() && playback()->audioOutput()) {
        playback()->audioOutput()->playbackVolumeChanged().onReceive(this, [this](float) { scheduleReload(); });
    }
    reload();
    m_meterTimer.start();
}

void MixerPanelModel::subscribeProject()
{
    auto prj = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!prj) {
        return;
    }
    using Mode = muse::async::Asyncable::Mode;
    prj->trackAdded().onReceive(this, [this](const trackedit::Track&) { scheduleReload(); }, Mode::SetReplace);
    prj->trackRemoved().onReceive(this, [this](const trackedit::Track&) { scheduleReload(); }, Mode::SetReplace);
    prj->trackChanged().onReceive(this, [this](const trackedit::Track&) { scheduleReload(); }, Mode::SetReplace);
    prj->trackMoved().onReceive(this, [this](const trackedit::Track&, int) { scheduleReload(); }, Mode::SetReplace);
}

void MixerPanelModel::scheduleReload()
{
    if (m_reloadPending) {
        return;
    }
    m_reloadPending = true;
    QMetaObject::invokeMethod(this, [this]() {
        m_reloadPending = false;
        reload();
    }, Qt::QueuedConnection);
}

std::vector<mx::Strip> MixerPanelModel::currentStrips() const
{
    if (m_stripsProvider) {
        return m_stripsProvider();
    }
    auto prj = globalContext() ? globalContext()->currentProject() : nullptr;
    auto* au3prj = prj ? reinterpret_cast<au3::Au3Project*>(prj->au3ProjectPtr()) : nullptr;
    return au3prj ? mx::strips(*au3prj) : std::vector<mx::Strip> {};
}

void MixerPanelModel::reload()
{
    m_strips.clear();
    m_ids.clear();

    const auto all = currentStrips();
    std::map<std::string, std::string> busNames;
    for (const auto& s : all) {
        if (s.kind == mx::StripKind::Bus) {
            busNames[s.busId] = s.name;
        }
    }
    auto nameOfBus = [&](const std::string& busId) {
        const auto it = busNames.find(busId);
        return QString::fromStdString(busId.empty() || it == busNames.end() ? std::string("Master") : it->second);
    };

    for (const auto& s : all) {
        QVariantMap m;
        m["trackId"] = qlonglong(s.trackId);
        m["kind"] = kindName(s.kind);
        m["name"] = QString::fromStdString(s.name);
        m["busId"] = QString::fromStdString(s.busId);
        m["volumeDb"] = s.kind == mx::StripKind::Master && playback() && playback()->audioOutput()
                        ? double(playback()->audioOutput()->playbackVolume()) : s.volumeDb;
        m["pan"] = double(s.pan);
        m["mute"] = s.mute;
        m["solo"] = s.solo;
        m["channels"] = s.channels;
        m["output"] = QString::fromStdString(s.outputBusId);
        m["outputName"] = s.kind == mx::StripKind::Master ? QString("Master") : nameOfBus(s.outputBusId);

        if (s.kind != mx::StripKind::Master) {
            // output choices: master + every bus that can take this strip without a loop
            QVariantList outputs;
            outputs << QVariantMap { { "busId", "" }, { "name", "Master" } };
            QVariantList sendTargets;
            for (const auto& [busId, name] : busNames) {
                if (!routing() || !routing()->canRouteTo(s.trackId, busId)) {
                    continue;
                }
                const QVariantMap entry { { "busId", QString::fromStdString(busId) }, { "name", QString::fromStdString(name) } };
                outputs << entry;
                const bool alreadySent = std::any_of(s.sends.begin(), s.sends.end(), [&](const auto& x) { return x.busId == busId; });
                if (busId != s.outputBusId && !alreadySent) {
                    sendTargets << entry;
                }
            }
            m["outputs"] = outputs;
            m["sendTargets"] = sendTargets;
            QVariantList sends;
            for (const auto& snd : s.sends) {
                sends << QVariantMap { { "busId", QString::fromStdString(snd.busId) },
                    { "name", nameOfBus(snd.busId) },
                    { "level", double(snd.level) } };
            }
            m["sends"] = sends;
        }

        QVariantList inserts;
        const auto stack = realtimeEffectService() ? realtimeEffectService()->effectStack(s.trackId) : std::nullopt;
        for (size_t i = 0; i < s.inserts.size(); ++i) {
            QString name = QString::fromStdString(s.inserts[i].pluginId);
            if (stack && i < stack->size()) {
                if (const auto n = realtimeEffectService()->effectName(stack->at(i))) {
                    name = QString::fromStdString(*n);
                }
            }
            inserts << QVariantMap { { "index", int(i) }, { "name", name }, { "active", s.inserts[i].active } };
        }
        m["inserts"] = inserts;
        m["insertsActive"] = s.insertsActive;

        m_strips << m;
        m_ids.push_back(s.trackId);
    }

    subscribeMeters();
    emit stripsChanged();
    emit metersChanged();
}

void MixerPanelModel::subscribeMeters()
{
    if (!playback() || !playback()->audioOutput()) {
        return;
    }
    auto out = playback()->audioOutput();
    for (const long long id : m_ids) {
        if (m_levels.count(id)) {
            continue;
        }
        m_levels[id] = {};
        auto onSignal = [this, id](audio::audioch_t ch, const audio::MeterSignal& sig) {
            auto& lvl = m_levels[id];
            const float v = std::max(FLOOR_DB, sig.peak.pressure);
            (ch == 0 ? lvl.l : lvl.r) = v;
            lvl.at = QDateTime::currentMSecsSinceEpoch();
            m_metersDirty = true;
        };
        if (id == mx::Strip::MasterId) {
            out->playbackSignalChanges().onReceive(this, onSignal);
        } else {
            out->playbackTrackSignalChanges(id).onReceive(this, onSignal);
        }
    }
}

QVariantList MixerPanelModel::meters() const
{
    QVariantList list;
    for (const long long id : m_ids) {
        const auto it = m_levels.find(id);
        const Level lvl = it == m_levels.end() ? Level {} : it->second;
        list << QVariant(QVariantList { double(lvl.l), double(lvl.r) });
    }
    return list;
}

void MixerPanelModel::setVolume(qlonglong trackId, double db, bool completed)
{
    if (trackId == mx::Strip::MasterId) {
        if (playback() && playback()->audioOutput()) {
            playback()->audioOutput()->setPlaybackVolume(float(db));
        }
        return;
    }
    if (trackPlaybackControl()) {
        trackPlaybackControl()->setVolume(trackId, float(db), completed);
        if (completed) {
            notifyTrackChanged(trackId);
        }
    }
}

void MixerPanelModel::setPan(qlonglong trackId, double pan, bool completed)
{
    if (trackId != mx::Strip::MasterId && trackPlaybackControl()) {
        trackPlaybackControl()->setPan(trackId, float(std::clamp(pan, -1.0, 1.0)), completed);
        if (completed) {
            notifyTrackChanged(trackId);
        }
    }
}

void MixerPanelModel::notifyTrackChanged(qlonglong trackId)
{
    // the track panel reloads its fader/knob from the track on trackChanged
    auto prj = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (prj) {
        if (const auto t = prj->track(trackId)) {
            prj->notifyAboutTrackChanged(*t);
        }
    }
    scheduleReload();
}

void MixerPanelModel::setMute(qlonglong trackId, bool mute)
{
    if (trackId != mx::Strip::MasterId && trackPlaybackControl()) {
        trackPlaybackControl()->setMuted(trackId, mute, false);
    }
}

void MixerPanelModel::setSolo(qlonglong trackId, bool solo)
{
    if (trackId != mx::Strip::MasterId && trackPlaybackControl()) {
        trackPlaybackControl()->setSolo(trackId, solo, false);
    }
}

bool MixerPanelModel::setOutput(qlonglong trackId, const QString& busId)
{
    return routing() && routing()->setOutput(trackId, busId.toStdString());
}

bool MixerPanelModel::setSend(qlonglong trackId, const QString& busId, double level, bool completed)
{
    return routing() && routing()->setSend(trackId, busId.toStdString(), float(level), completed);
}

bool MixerPanelModel::removeSend(qlonglong trackId, const QString& busId)
{
    return routing() && routing()->removeSend(trackId, busId.toStdString());
}

au::effects::RealtimeEffectStatePtr MixerPanelModel::insertAt(qlonglong trackId, int index) const
{
    auto fx = realtimeEffectService();
    const auto stack = fx ? fx->effectStack(trackId) : std::nullopt;
    if (!stack || index < 0 || size_t(index) >= stack->size()) {
        return nullptr;
    }
    return stack->at(index);
}

void MixerPanelModel::setInsertActive(qlonglong trackId, int index, bool active)
{
    if (const auto state = insertAt(trackId, index)) {
        realtimeEffectService()->setIsActive(state, active);
        scheduleReload();
    }
}

void MixerPanelModel::removeInsert(qlonglong trackId, int index)
{
    if (const auto state = insertAt(trackId, index)) {
        if (effectViewController()) {
            effectViewController()->hideEffect(state);
        }
        realtimeEffectService()->removeRealtimeEffect(trackId, state);
        scheduleReload();
    }
}

void MixerPanelModel::moveInsert(qlonglong trackId, int index, int newIndex)
{
    const auto state = insertAt(trackId, index);
    if (!state || newIndex == index || !insertAt(trackId, newIndex)) {
        return;
    }
    realtimeEffectService()->moveRealtimeEffect(state, newIndex);
    scheduleReload();
}

void MixerPanelModel::showInsert(qlonglong trackId, int index)
{
    if (const auto state = insertAt(trackId, index); state && effectViewController()) {
        effectViewController()->showEffect(state);
    }
}

void MixerPanelModel::setInsertsActive(qlonglong trackId, bool active)
{
    if (auto fx = realtimeEffectService()) {
        fx->setTrackEffectsActive(trackId, active);
        scheduleReload();
    }
}

void MixerPanelModel::addBus()
{
    if (routing()) {
        routing()->addBus();
    }
}
