/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "framework/global/modularity/ioc.h"
#include "framework/global/async/asyncable.h"
#include "framework/actions/iactionsdispatcher.h"

#include "context/iglobalcontext.h"
#include "record/irecord.h"
#include "record/irecordconfiguration.h"
#include "record/irecordcontroller.h"
#include "trackedit/iselectioncontroller.h"

namespace au::record {
//! View model of the Artist Recording Mode dashboard: a performer-facing summary of the
//! recording state (transport, input level, take, tempo/key) with large controls.
class ArtistModeModel : public QObject, public muse::async::Asyncable, public muse::Contextable
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool active READ active NOTIFY activeChanged FINAL)
    Q_PROPERTY(bool isRecording READ isRecording NOTIFY transportChanged FINAL)
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY transportChanged FINAL)
    Q_PROPERTY(QString positionText READ positionText NOTIFY positionChanged FINAL)

    Q_PROPERTY(QString trackName READ trackName NOTIFY trackChanged FINAL)
    Q_PROPERTY(int takeLaneCount READ takeLaneCount NOTIFY trackChanged FINAL)
    Q_PROPERTY(int takeNumber READ takeNumber NOTIFY trackChanged FINAL)

    Q_PROPERTY(QString tempoText READ tempoText NOTIFY projectInfoChanged FINAL)
    Q_PROPERTY(QString timeSignatureText READ timeSignatureText NOTIFY projectInfoChanged FINAL)
    Q_PROPERTY(QString keyText READ keyText NOTIFY projectInfoChanged FINAL)

    Q_PROPERTY(bool metronome READ metronome WRITE setMetronome NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool countIn READ countIn WRITE setCountIn NOTIFY settingsChanged FINAL)
    Q_PROPERTY(double countInSeconds READ countInSeconds NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool punchOut READ punchOut WRITE setPunchOut NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool keepTakes READ keepTakes WRITE setKeepTakes NOTIFY settingsChanged FINAL)

    //! Input meter: current level, held peak (both dBFS), latched clip indicator
    Q_PROPERTY(double inputLevelDb READ inputLevelDb NOTIFY levelChanged FINAL)
    Q_PROPERTY(double peakHoldDb READ peakHoldDb NOTIFY levelChanged FINAL)
    Q_PROPERTY(bool inputClipped READ inputClipped NOTIFY levelChanged FINAL)

    Q_PROPERTY(bool hasLastTake READ hasLastTake NOTIFY lastTakeChanged FINAL)
    Q_PROPERTY(QString lastTakeText READ lastTakeText NOTIFY lastTakeChanged FINAL)
    Q_PROPERTY(bool lastTakeClipped READ lastTakeClipped NOTIFY lastTakeChanged FINAL)

public:
    muse::ContextInject<context::IGlobalContext> globalContext{ this };
    muse::ContextInject<IRecord> record{ this };
    muse::ContextInject<IRecordController> recordController{ this };
    muse::GlobalInject<IRecordConfiguration> configuration;
    muse::ContextInject<trackedit::ISelectionController> selectionController{ this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher{ this };

    explicit ArtistModeModel(QObject* parent = nullptr);

    //! Anything at or above this is shown as clipping on the input meter
    static constexpr double CLIP_DB = -0.1;
    //! Peak hold falls back to the live level after this many seconds
    static constexpr double PEAK_HOLD_SECONDS = 2.0;

    Q_INVOKABLE void init();

    Q_INVOKABLE void toggleRecord();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void nextTake();
    Q_INVOKABLE void activateTake();
    Q_INVOKABLE void autoComp();
    Q_INVOKABLE void resetClipIndicator();
    Q_INVOKABLE void exitMode();

    bool active() const;
    bool isRecording() const;
    bool isPlaying() const;
    QString positionText() const;
    QString trackName() const { return m_trackName; }
    int takeLaneCount() const { return m_takeLaneCount; }
    //! the number the next (or current) recording on the armed track will get
    int takeNumber() const { return m_takeLaneCount + 1; }
    QString tempoText() const;
    QString timeSignatureText() const;
    QString keyText() const;

    bool metronome() const;
    void setMetronome(bool on);
    bool countIn() const;
    void setCountIn(bool on);
    double countInSeconds() const;
    bool punchOut() const;
    void setPunchOut(bool on);
    bool keepTakes() const;
    void setKeepTakes(bool on);

    double inputLevelDb() const { return m_levelDb; }
    double peakHoldDb() const { return m_peakDb; }
    bool inputClipped() const { return m_clipped; }

    bool hasLastTake() const { return m_lastTake.has_value(); }
    QString lastTakeText() const;
    bool lastTakeClipped() const { return m_lastTake && m_lastTake->clipped; }

    //! Exposed for tests (and wired to the input meter / recorder in init())
    void onMeterSignal(double peakDb, double nowSec);
    void onRecordingFinished();
    void updateTrack();
    void setPosition(double secs);

    static QString formatPosition(double secs);

signals:
    void activeChanged();
    void transportChanged();
    void positionChanged();
    void trackChanged();
    void projectInfoChanged();
    void settingsChanged();
    void levelChanged();
    void lastTakeChanged();

private:
    void dispatch(const char* action);
    trackedit::TrackId armedTrack() const;

    double m_position = 0.0;
    QString m_trackName;
    int m_takeLaneCount = 0;
    double m_levelDb = -144.0;
    double m_peakDb = -144.0;
    double m_peakAt = 0.0;
    bool m_clipped = false;
    std::optional<TakeStats> m_lastTake;
};
}
