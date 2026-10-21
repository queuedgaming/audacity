/*
* Audacity: A Digital Audio Editor
*/
#include "trackviewstatemodel.h"

#include "framework/global/translation.h"

using namespace au::projectscene;
using namespace au::project;

TrackViewStateModel::TrackViewStateModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

IProjectViewStatePtr TrackViewStateModel::viewState() const
{
    IAudacityProjectPtr prj = globalContext()->currentProject();
    return prj ? prj->viewState() : nullptr;
}

void TrackViewStateModel::init()
{
    globalContext()->currentProjectChanged().onNotify(this, [this]() {
        init();
    }, muse::async::Asyncable::Mode::SetReplace);

    IProjectViewStatePtr vs = viewState();
    if (!vs) {
        return;
    }

    if (m_trackId != -1) {
        m_trackHeight = vs->trackHeight(m_trackId);
        m_trackHeight.ch.onReceive(this, [this](int h) {
            if (m_trackHeight.val == h) {
                return;
            }

            auto prj = globalContext()->currentTrackeditProject();
            if (prj && !prj->track(m_trackId)) {
                return;
            }

            m_trackHeight.val = h;
            emit trackHeightChanged();
        }, muse::async::Asyncable::Mode::SetReplace);

        m_isTrackCollapsed = vs->isTrackCollapsed(m_trackId);
        m_isTrackCollapsed.ch.onReceive(this, [this](bool v) {
            if (m_isTrackCollapsed.val == v) {
                return;
            }

            m_isTrackCollapsed.val = v;
            emit isTrackCollapsedChanged();
        }, muse::async::Asyncable::Mode::SetReplace);

        m_channelHeightRatio = vs->channelHeightRatio(m_trackId);
        m_channelHeightRatio.ch.onReceive(this, [this](double ratio) {
            if (m_channelHeightRatio.val == ratio) {
                return;
            }
            m_channelHeightRatio.val = ratio;
            emit channelHeightRatioChanged();
        }, muse::async::Asyncable::Mode::SetReplace);

        m_displayBounds = vs->verticalDisplayBounds(m_trackId);
        m_displayBounds.ch.onReceive(this, [this](std::pair<float, float> bounds) {
            if (m_displayBounds.val == bounds) {
                return;
            }
            m_displayBounds.val = bounds;
            emit displayBoundsChanged();
        }, muse::async::Asyncable::Mode::SetReplace);

        m_rulerType = vs->trackRulerType(m_trackId);
        m_rulerType.ch.onReceive(this, [this](int type) {
            if (m_rulerType.val == type) {
                return;
            }
            m_rulerType.val = type;
            emit isLinearChanged();
        }, muse::async::Asyncable::Mode::SetReplace);

        m_isHiddenLane = vs->isTrackHiddenInLanes(m_trackId);
        m_isHiddenLane.ch.onReceive(this, [this](bool hidden) {
            if (m_isHiddenLane.val == hidden) {
                return;
            }
            m_isHiddenLane.val = hidden;
            emit trackHeightChanged();
            emit takeLanesChanged();
        }, muse::async::Asyncable::Mode::SetReplace);

        m_takeLaneState = vs->takeLaneState(m_trackId);
        vs->takeLanesChanged().onNotify(this, [this]() {
            if (auto v = viewState()) {
                m_takeLaneState = v->takeLaneState(m_trackId);
            }
            emit takeLanesChanged();
        }, muse::async::Asyncable::Mode::SetReplace);

        emit takeLanesChanged();
        emit trackHeightChanged();
        emit isTrackCollapsedChanged();
        emit channelHeightRatioChanged();
        emit displayBoundsChanged();
    }

    playbackController()->isPlayingChanged().onNotify(this, [this]() {
        emit isPlayingChanged();
    }, muse::async::Asyncable::Mode::SetReplace);

    recordController()->isRecordingChanged().onNotify(this, [this]() {
        emit isRecordingChanged();
    }, muse::async::Asyncable::Mode::SetReplace);

    m_meterModel = new playback::PlaybackMeterModel(this);
    emit meterModelChanged();
}

void TrackViewStateModel::changeTrackHeight(int deltaY)
{
    IProjectViewStatePtr vs = viewState();
    if (vs) {
        vs->changeTrackHeight(m_trackId, deltaY);
    }
}

void TrackViewStateModel::changeChannelHeightRatio(double ratio)
{
    IProjectViewStatePtr vs = viewState();
    if (vs) {
        vs->setChannelHeightRatio(m_trackId, ratio);
    }
}

QVariant TrackViewStateModel::trackId() const
{
    return QVariant::fromValue(m_trackId);
}

void TrackViewStateModel::setTrackId(const QVariant& _newTrackId)
{
    trackedit::TrackId newTrackId = _newTrackId.toInt();
    if (m_trackId == newTrackId) {
        return;
    }
    m_trackId = newTrackId;
    emit trackIdChanged();

    init();
}

int TrackViewStateModel::trackHeight() const
{
    return m_isHiddenLane.val ? 0 : m_trackHeight.val;
}

int TrackViewStateModel::takeRole() const
{
    return static_cast<int>(m_takeLaneState.role);
}

int TrackViewStateModel::takeIndex() const
{
    return m_takeLaneState.takeIndex;
}

int TrackViewStateModel::takeCount() const
{
    return m_takeLaneState.takeCount;
}

QString TrackViewStateModel::takeLabel() const
{
    switch (m_takeLaneState.role) {
    case TakeLaneState::Role::Take:
        //: Label of a take lane; %1 is the take number
        return muse::qtrc("projectscene", "Take %1").arg(m_takeLaneState.takeIndex);
    case TakeLaneState::Role::Main:
        //: Shown on a track that has take lanes; %1 is the number of takes
        return m_takeLaneState.takeCount == 1 ? muse::qtrc("projectscene", "1 take")
               : muse::qtrc("projectscene", "%1 takes").arg(m_takeLaneState.takeCount);
    case TakeLaneState::Role::None:
        break;
    }
    return QString();
}

bool TrackViewStateModel::takeLanesExpanded() const
{
    IProjectViewStatePtr vs = viewState();
    return vs && m_takeLaneState.role == TakeLaneState::Role::Main && vs->takeLanesExpanded(m_trackId);
}

bool TrackViewStateModel::isHiddenLane() const
{
    return m_isHiddenLane.val;
}

void TrackViewStateModel::toggleTakeLanes()
{
    IProjectViewStatePtr vs = viewState();
    if (vs && m_takeLaneState.role == TakeLaneState::Role::Main) {
        vs->setTakeLanesExpanded(m_trackId, !vs->takeLanesExpanded(m_trackId));
    }
}

void TrackViewStateModel::activateTake()
{
    if (m_takeLaneState.role == TakeLaneState::Role::Take
        && record()->canSwitchTake(m_trackId, record::TakeSwitch::Activate)) {
        record()->switchTake(m_trackId, record::TakeSwitch::Activate);
    }
}

bool TrackViewStateModel::isTrackCollapsed() const
{
    return m_isTrackCollapsed.val;
}

double TrackViewStateModel::channelHeightRatio() const
{
    return m_channelHeightRatio.val;
}

QVariant TrackViewStateModel::displayBounds() const
{
    const auto& [min, max] = m_displayBounds.val;
    return QVariant::fromValue(QMap<QString, QVariant> {
        { "min", static_cast<double>(min) },
        { "max", static_cast<double>(max) }
    });
}

bool TrackViewStateModel::isLinear() const
{
    const auto prjViewState = viewState();
    if (!prjViewState) {
        return true;
    }

    const auto rulerType = prjViewState->trackRulerType(m_trackId).val;
    return rulerType != static_cast<int>(au::trackedit::TrackRulerType::DbLog);
}

bool TrackViewStateModel::isPlaying() const
{
    return playbackController()->isPlaying();
}

bool TrackViewStateModel::isRecording() const
{
    return recordController()->isRecording();
}

au::playback::PlaybackMeterModel* TrackViewStateModel::meterModel() const
{
    return m_meterModel;
}
