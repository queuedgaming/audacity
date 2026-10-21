/*
 * Audacity: A Digital Audio Editor
 */
#pragma once

#include <QtQml/qqmlregistration.h>

#include "modularity/ioc.h"

#include "async/asyncable.h"

#include "record/irecordconfiguration.h"

namespace au::appshell {
class RecordingPreferencesModel : public QObject, public muse::async::Asyncable
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool isInputMonitoringOn READ isInputMonitoringOn WRITE setIsInputMonitoringOn NOTIFY isInputMonitoringOnChanged)
    Q_PROPERTY(bool isMicMeteringOn READ isMicMeteringOn WRITE setIsMicMeteringOn NOTIFY isMicMeteringOnChanged)
    Q_PROPERTY(double leadInTimeDuration READ leadInTimeDuration NOTIFY leadInTimeDurationChanged)
    Q_PROPERTY(double crossfadeDuration READ crossfadeDuration NOTIFY crossfadeDurationChanged)
    Q_PROPERTY(bool punchOutAtSelectionEnd READ punchOutAtSelectionEnd WRITE setPunchOutAtSelectionEnd NOTIFY punchOutAtSelectionEndChanged)
    Q_PROPERTY(double autoCompSegmentLength READ autoCompSegmentLength NOTIFY autoCompSegmentLengthChanged)
    Q_PROPERTY(bool keepTakesWhenRecordingOver READ keepTakesWhenRecordingOver WRITE setKeepTakesWhenRecordingOver NOTIFY keepTakesWhenRecordingOverChanged)

    muse::GlobalInject<record::IRecordConfiguration> recordConfiguration;

public:
    explicit RecordingPreferencesModel(QObject* parent = nullptr);

    bool isInputMonitoringOn() const;
    void setIsInputMonitoringOn(bool enabled);

    bool isMicMeteringOn() const;
    void setIsMicMeteringOn(bool enabled);

    double leadInTimeDuration() const;
    Q_INVOKABLE void setLeadInTimeDuration(double seconds);

    double crossfadeDuration() const;
    Q_INVOKABLE void setCrossfadeDuration(double milliseconds);

    bool punchOutAtSelectionEnd() const;
    void setPunchOutAtSelectionEnd(bool enabled);

    double autoCompSegmentLength() const;
    Q_INVOKABLE void setAutoCompSegmentLength(double seconds);

    bool keepTakesWhenRecordingOver() const;
    void setKeepTakesWhenRecordingOver(bool enabled);

signals:
    void isInputMonitoringOnChanged();
    void isMicMeteringOnChanged();
    void leadInTimeDurationChanged();
    void crossfadeDurationChanged();
    void punchOutAtSelectionEndChanged();
    void keepTakesWhenRecordingOverChanged();
    void autoCompSegmentLengthChanged();
};
}
