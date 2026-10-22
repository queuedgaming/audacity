/*
 * Audacity: A Digital Audio Editor
 */
#include "paramautomationlanemodel.h"

#include <cmath>

using namespace au::automation;

ParamAutomationLaneModel::ParamAutomationLaneModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void ParamAutomationLaneModel::init()
{
    if (!interaction()) {
        return;
    }
    projectHistory()->historyChanged().onReceive(this, [this](auto) {
        // undo/redo replaced the tracks: re-attach automation to the effect states
        interaction()->reinstall();
        reload();
    });
    interaction()->changed().onReceive(this, [this](trackedit::TrackId id) {
        if (id == m_trackId || id == trackedit::INVALID_TRACK) {
            reload();
        }
    });
    reload();
}

QVariant ParamAutomationLaneModel::trackId() const
{
    return QVariant::fromValue(m_trackId);
}

void ParamAutomationLaneModel::setTrackId(const QVariant& id)
{
    const auto v = id.toLongLong();
    if (v == m_trackId) {
        return;
    }
    m_trackId = v;
    emit trackIdChanged();
    reload();
}

void ParamAutomationLaneModel::reload()
{
    m_targets.clear();
    m_current.reset();
    m_points.clear();
    m_shown = false;
    if (interaction() && m_trackId != trackedit::INVALID_TRACK) {
        m_shown = interaction()->isShown(m_trackId);
        m_targets = interaction()->availableParams(m_trackId);
        m_current = interaction()->activeTarget(m_trackId);
        for (const auto& p : interaction()->activePoints(m_trackId)) {
            m_points.push_back(QPointF(p.xValue, p.yValue));
        }
    }
    emit changed();
}

bool ParamAutomationLaneModel::shown() const { return m_shown; }

QVariantList ParamAutomationLaneModel::targetOptions() const
{
    QVariantList options;
    for (size_t i = 0; i < m_targets.size(); ++i) {
        QVariantMap option;
        option["text"] = QString::fromStdString(m_targets[i].effectName + " \u00b7 " + m_targets[i].paramKey);
        option["value"] = int(i);
        options << option;
    }
    return options;
}

int ParamAutomationLaneModel::currentTargetIndex() const
{
    if (!m_current) {
        return -1;
    }
    for (size_t i = 0; i < m_targets.size(); ++i) {
        if (m_targets[i].sameAs(*m_current)) {
            return int(i);
        }
    }
    return -1;
}

QString ParamAutomationLaneModel::currentTargetName() const
{
    return m_current ? QString::fromStdString(m_current->effectName + " \u00b7 " + m_current->paramKey) : QString();
}

QVector<QPointF> ParamAutomationLaneModel::points() const { return m_points; }
double ParamAutomationLaneModel::minValue() const { return m_current ? m_current->minValue : 0.0; }
double ParamAutomationLaneModel::maxValue() const { return m_current ? m_current->maxValue : 1.0; }
double ParamAutomationLaneModel::defaultValue() const { return m_current ? m_current->defaultValue : 0.0; }

void ParamAutomationLaneModel::setShown(bool shown)
{
    interaction()->setShown(m_trackId, shown);
}

void ParamAutomationLaneModel::selectTarget(int index)
{
    if (index >= 0 && index < int(m_targets.size())) {
        interaction()->setActiveTarget(m_trackId, m_targets[size_t(index)]);
    }
}

void ParamAutomationLaneModel::addPoint(double t, double value, bool completed)
{
    interaction()->addPoint(m_trackId, t, value, completed);
}

void ParamAutomationLaneModel::setPoint(int index, double t, double value, bool completed)
{
    interaction()->setPoint(m_trackId, index, t, value, completed);
}

void ParamAutomationLaneModel::removePoint(int index, bool completed)
{
    interaction()->removePoint(m_trackId, index, completed);
}

void ParamAutomationLaneModel::cancelDrag()
{
    interaction()->cancelEdit(m_trackId);
}

QString ParamAutomationLaneModel::valueText(double value) const
{
    if (m_current && m_current->isInteger) {
        return QString::number(std::lround(value));
    }
    return QString::number(value, 'f', std::abs(value) < 10 ? 2 : 1);
}
