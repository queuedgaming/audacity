/*
 * Audacity: A Digital Audio Editor
 */
#pragma once

#include <QObject>
#include <QPointF>
#include <QVariantList>
#include <QVector>

#include "async/asyncable.h"
#include "modularity/ioc.h"

#include "automation/iparamautomationinteraction.h"
#include "trackedit/iprojecthistory.h"
#include "trackedit/trackedittypes.h"

namespace au::automation {
//! QML model of the plugin parameter automation lane of one track
class ParamAutomationLaneModel : public QObject, public muse::async::Asyncable, public muse::Contextable
{
    Q_OBJECT

    muse::ContextInject<IParamAutomationInteraction> interaction{ this };
    muse::ContextInject<trackedit::IProjectHistory> projectHistory{ this };

    Q_PROPERTY(QVariant trackId READ trackId WRITE setTrackId NOTIFY trackIdChanged FINAL)
    Q_PROPERTY(bool shown READ shown NOTIFY changed)
    Q_PROPERTY(QVariantList targetOptions READ targetOptions NOTIFY changed)
    Q_PROPERTY(int currentTargetIndex READ currentTargetIndex NOTIFY changed)
    Q_PROPERTY(QString currentTargetName READ currentTargetName NOTIFY changed)
    Q_PROPERTY(QVector<QPointF> points READ points NOTIFY changed)
    Q_PROPERTY(double minValue READ minValue NOTIFY changed)
    Q_PROPERTY(double maxValue READ maxValue NOTIFY changed)
    Q_PROPERTY(double defaultValue READ defaultValue NOTIFY changed)

public:
    explicit ParamAutomationLaneModel(QObject* parent = nullptr);

    Q_INVOKABLE void init();

    QVariant trackId() const;
    void setTrackId(const QVariant& id);

    bool shown() const;
    QVariantList targetOptions() const;
    int currentTargetIndex() const;
    QString currentTargetName() const;
    QVector<QPointF> points() const;
    double minValue() const;
    double maxValue() const;
    double defaultValue() const;

    Q_INVOKABLE void setShown(bool shown);
    Q_INVOKABLE void selectTarget(int index);
    Q_INVOKABLE void addPoint(double t, double value, bool completed);
    Q_INVOKABLE void setPoint(int index, double t, double value, bool completed);
    Q_INVOKABLE void removePoint(int index, bool completed);
    Q_INVOKABLE void cancelDrag();
    Q_INVOKABLE QString valueText(double value) const;

signals:
    void trackIdChanged();
    void changed();

private:
    void reload();

    trackedit::TrackId m_trackId = trackedit::INVALID_TRACK;
    std::vector<ParamTarget> m_targets;
    std::optional<ParamTarget> m_current;
    QVector<QPointF> m_points;
    bool m_shown = false;
};
}
