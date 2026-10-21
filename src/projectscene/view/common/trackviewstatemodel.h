/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <QObject>

#include "global/async/asyncable.h"

#include "modularity/ioc.h"
#include "context/iglobalcontext.h"
#include "playback/iplaybackcontroller.h"
#include "playback/iplaybackconfiguration.h"
#include "record/irecordcontroller.h"
#include "record/irecord.h"

#include "playback/view/common/playbackmetermodel.h"
#include "types/val.h"

namespace au::projectscene {
class TrackViewStateModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(QVariant trackId READ trackId WRITE setTrackId NOTIFY trackIdChanged FINAL)
    Q_PROPERTY(int trackHeight READ trackHeight NOTIFY trackHeightChanged FINAL)
    Q_PROPERTY(bool isTrackCollapsed READ isTrackCollapsed NOTIFY isTrackCollapsedChanged FINAL)
    Q_PROPERTY(double channelHeightRatio READ channelHeightRatio NOTIFY channelHeightRatioChanged FINAL)
    Q_PROPERTY(QVariant displayBounds READ displayBounds NOTIFY displayBoundsChanged FINAL)
    Q_PROPERTY(bool isLinear READ isLinear NOTIFY isLinearChanged FINAL)

    //! Take lanes: 0 = ordinary track, 1 = main track with takes, 2 = take lane
    Q_PROPERTY(int takeRole READ takeRole NOTIFY takeLanesChanged FINAL)
    Q_PROPERTY(int takeIndex READ takeIndex NOTIFY takeLanesChanged FINAL)
    Q_PROPERTY(int takeCount READ takeCount NOTIFY takeLanesChanged FINAL)
    Q_PROPERTY(QString takeLabel READ takeLabel NOTIFY takeLanesChanged FINAL)
    Q_PROPERTY(bool takeLanesExpanded READ takeLanesExpanded NOTIFY takeLanesChanged FINAL)
    Q_PROPERTY(bool isHiddenLane READ isHiddenLane NOTIFY takeLanesChanged FINAL)

    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY isPlayingChanged FINAL)
    Q_PROPERTY(bool isRecording READ isRecording NOTIFY isRecordingChanged FINAL)

    Q_PROPERTY(playback::PlaybackMeterModel * meterModel READ meterModel NOTIFY meterModelChanged FINAL)

    muse::GlobalInject<playback::IPlaybackConfiguration> playbackConfiguration;

    muse::ContextInject<context::IGlobalContext> globalContext{ this };
    muse::ContextInject<playback::IPlaybackController> playbackController{ this };
    muse::ContextInject<record::IRecordController> recordController{ this };
    muse::ContextInject<record::IRecord> record{ this };

public:
    TrackViewStateModel(QObject* parent = nullptr);

    Q_INVOKABLE void init();

    QVariant trackId() const;
    void setTrackId(const QVariant& newTrackId);

    int trackHeight() const;
    Q_INVOKABLE void changeTrackHeight(int deltaY);

    bool isTrackCollapsed() const;
    double channelHeightRatio() const;
    Q_INVOKABLE void changeChannelHeightRatio(double ratio);

    QVariant displayBounds() const;

    bool isLinear() const;

    playback::PlaybackMeterModel* meterModel() const;

    int takeRole() const;
    int takeIndex() const;
    int takeCount() const;
    QString takeLabel() const;
    bool takeLanesExpanded() const;
    bool isHiddenLane() const;
    Q_INVOKABLE void toggleTakeLanes();
    //! Make this take lane the active take (same as the "Activate take" action)
    Q_INVOKABLE void activateTake();
    //! Range comping: use the selected time range of this take lane on its main track
    Q_INVOKABLE void compSelection();

    bool isPlaying() const;
    bool isRecording() const;

signals:
    void trackIdChanged();
    void trackHeightChanged();
    void isTrackCollapsedChanged();
    void channelHeightRatioChanged();
    void meterModelChanged();
    void isPlayingChanged();
    void isRecordingChanged();
    void displayBoundsChanged();
    void isLinearChanged();
    void takeLanesChanged();

private:
    IProjectViewStatePtr viewState() const;

    trackedit::TrackId m_trackId = -1;
    muse::ValCh<int> m_trackHeight;
    muse::ValCh<bool> m_isTrackCollapsed;
    muse::ValCh<double> m_channelHeightRatio;
    muse::ValCh<std::pair<float, float> > m_displayBounds;
    muse::ValCh<int> m_rulerType;
    muse::ValCh<bool> m_isHiddenLane;
    TakeLaneState m_takeLaneState;

    playback::PlaybackMeterModel* m_meterModel = nullptr;
};
}
