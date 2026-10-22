/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "framework/global/modularity/ioc.h"
#include "framework/global/async/asyncable.h"

#include "context/iglobalcontext.h"
#include "record/irecord.h"
#include "trackedit/iselectioncontroller.h"
#include "record/irecordconfiguration.h"

namespace au::record {
//! Settings and status of live input monitoring through realtime effects; shared by the record
//! toolbar popup, the Artist mode dashboard and Preferences.
class LiveMonitoringModel : public QObject, public muse::async::Asyncable, public muse::Contextable
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool monitorInput READ monitorInput WRITE setMonitorInput NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool throughEffects READ throughEffects WRITE setThroughEffects NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool lowLatency READ lowLatency WRITE setLowLatency NOTIFY settingsChanged FINAL)
    Q_PROPERTY(double thresholdMs READ thresholdMs WRITE setThresholdMs NOTIFY settingsChanged FINAL)
    Q_PROPERTY(double gainDb READ gainDb WRITE setGainDb NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY settingsChanged FINAL)

    Q_PROPERTY(double latencyMs READ latencyMs NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString latencyText READ latencyText NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString effectsText READ effectsText NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString bypassedText READ bypassedText NOTIFY statusChanged FINAL)
    Q_PROPERTY(bool hasBypassed READ hasBypassed NOTIFY statusChanged FINAL)

public:
    muse::ContextInject<context::IGlobalContext> globalContext{ this };
    muse::ContextInject<IRecord> record{ this };
    muse::ContextInject<trackedit::ISelectionController> selectionController{ this };
    muse::GlobalInject<IRecordConfiguration> configuration;

    explicit LiveMonitoringModel(QObject* parent = nullptr);

    Q_INVOKABLE void init();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void toggleMute() { setMuted(!muted()); }

    bool monitorInput() const;
    void setMonitorInput(bool on);
    bool throughEffects() const;
    void setThroughEffects(bool on);
    bool lowLatency() const;
    void setLowLatency(bool on);
    double thresholdMs() const;
    void setThresholdMs(double ms);
    double gainDb() const;
    void setGainDb(double db);
    bool muted() const;
    void setMuted(bool on);

    double latencyMs() const;
    QString latencyText() const;
    QString effectsText() const;
    QString bypassedText() const;
    bool hasBypassed() const;

signals:
    void settingsChanged();
    void statusChanged();

private:
    MonitoringStatus status() const;
};
}
