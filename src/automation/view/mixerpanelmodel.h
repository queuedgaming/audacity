/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <map>

#include <QObject>
#include <QTimer>
#include <QVariantList>

#include "framework/global/async/asyncable.h"
#include "framework/global/modularity/ioc.h"
#include "context/iglobalcontext.h"
#include "trackedit/iprojecthistory.h"
#include "playback/iplayback.h"
#include "playback/itrackplaybackcontrol.h"
#include "effects/effects_base/irealtimeeffectservice.h"

#include "automation/ibusroutinginteraction.h"

namespace au::automation {
//! Model of the channel-strip mixer: one strip per wave track and bus, plus master.
//! Edits go through the existing services (track playback control, bus routing, realtime effects),
//! which save them in the project and push undo states.
class MixerPanelModel : public QObject, public muse::async::Asyncable, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList strips READ strips NOTIFY stripsChanged FINAL)
    Q_PROPERTY(QVariantList meters READ meters NOTIFY metersChanged FINAL)

    muse::ContextInject<context::IGlobalContext> globalContext{ this };
    muse::ContextInject<trackedit::IProjectHistory> projectHistory{ this };
    muse::ContextInject<playback::IPlayback> playback{ this };
    muse::ContextInject<playback::ITrackPlaybackControl> trackPlaybackControl{ this };
    muse::ContextInject<effects::IRealtimeEffectService> realtimeEffectService{ this };
    muse::ContextInject<IBusRoutingInteraction> routing{ this };

public:
    explicit MixerPanelModel(QObject* parent = nullptr);

    QVariantList strips() const { return m_strips; }
    //! [leftDb, rightDb] per strip, same order as strips
    QVariantList meters() const;

    Q_INVOKABLE void init();
    Q_INVOKABLE void reload();

    Q_INVOKABLE void setVolume(qlonglong trackId, double db, bool completed);
    Q_INVOKABLE void setPan(qlonglong trackId, double pan, bool completed);
    Q_INVOKABLE void setMute(qlonglong trackId, bool mute);
    Q_INVOKABLE void setSolo(qlonglong trackId, bool solo);
    Q_INVOKABLE bool setOutput(qlonglong trackId, const QString& busId);
    Q_INVOKABLE bool setSend(qlonglong trackId, const QString& busId, double level, bool completed);
    Q_INVOKABLE bool removeSend(qlonglong trackId, const QString& busId);
    Q_INVOKABLE void setInsertActive(qlonglong trackId, int index, bool active);
    Q_INVOKABLE void setInsertsActive(qlonglong trackId, bool active);
    Q_INVOKABLE void addBus();

signals:
    void stripsChanged();
    void metersChanged();

private:
    void subscribeMeters();
    void scheduleReload();
    void notifyTrackChanged(qlonglong trackId);

    QVariantList m_strips;
    std::vector<long long> m_ids;
    struct Level {
        float l = -60.f;
        float r = -60.f;
        qint64 at = 0;
    };
    std::map<long long, Level> m_levels;
    bool m_metersDirty = false;
    QTimer m_meterTimer;
    bool m_reloadPending = false;
};
}
