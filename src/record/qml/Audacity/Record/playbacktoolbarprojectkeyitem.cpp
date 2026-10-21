/*
* Audacity: A Digital Audio Editor
*/
#include "playbacktoolbarprojectkeyitem.h"

#include <cmath>

#include "framework/global/translation.h"

using namespace au::record;

PlaybackToolBarProjectKeyItem::PlaybackToolBarProjectKeyItem(const muse::ui::UiAction& action,
                                                             muse::uicomponents::ToolBarItemType::Type type, QObject* parent)
    : muse::uicomponents::ToolBarItem(action, type, parent)
{
    globalContext()->currentProjectChanged().onNotify(this, [this]() {
        onProjectChanged();
    });
    onProjectChanged();
}

void PlaybackToolBarProjectKeyItem::onProjectChanged()
{
    m_key.reset();
    if (globalContext()->currentProject() && record()) {
        record()->projectKeyChanged().onNotify(this, [this]() {
            m_key = record()->projectKey();
            emit keyChanged();
        });
        m_key = record()->projectKey();
    }
    emit keyChanged();
}

double PlaybackToolBarProjectKeyItem::centsToHz(double cents)
{
    return 440.0 * std::pow(2.0, cents / 1200.0);
}

double PlaybackToolBarProjectKeyItem::hzToCents(double hz)
{
    return 1200.0 * std::log2(hz / 440.0);
}

bool PlaybackToolBarProjectKeyItem::hasKey() const
{
    return m_key.has_value();
}

QString PlaybackToolBarProjectKeyItem::label() const
{
    if (!m_key) {
        return muse::qtrc("record", "No key");
    }
    return QString::fromStdString(record()->projectKeyLabel(m_key));
}

QString PlaybackToolBarProjectKeyItem::accessibleLabel() const
{
    if (!m_key) {
        return muse::qtrc("record", "Project key: none set");
    }
    return muse::qtrc("record", "Project key: %1 %2, A equals %3 hertz")
           .arg(tonicNames().at(m_key->tonic))
           .arg(m_key->minor ? muse::qtrc("record", "minor") : muse::qtrc("record", "major"))
           .arg(QString::number(centsToHz(m_key->tuningCents), 'f', 1));
}

int PlaybackToolBarProjectKeyItem::tonic() const
{
    return m_key ? m_key->tonic : 0;
}

bool PlaybackToolBarProjectKeyItem::minor() const
{
    return m_key ? m_key->minor : false;
}

double PlaybackToolBarProjectKeyItem::tuningHz() const
{
    return m_key ? centsToHz(m_key->tuningCents) : 440.0;
}

QStringList PlaybackToolBarProjectKeyItem::tonicNames() const
{
    return { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
}

void PlaybackToolBarProjectKeyItem::apply(int tonic, bool minor, double tuningHz)
{
    if (tonic < 0 || tonic > 11 || !(tuningHz >= 415.0 && tuningHz <= 466.2)) {
        return;
    }
    // Round to 0.1 cent so re-applying the shown value is a no-op
    const double cents = std::round(hzToCents(tuningHz) * 10.0) / 10.0;
    record()->setProjectKey(ProjectKeyInfo { tonic, minor, cents });
}

void PlaybackToolBarProjectKeyItem::clear()
{
    record()->setProjectKey(std::nullopt);
}

QVariantMap PlaybackToolBarProjectKeyItem::detectFromSelection()
{
    QVariantMap out { { "valid", false } };
    trackedit::TrackId track = trackNavigationController()->focusedTrack();
    if (track < 0) {
        const auto selected = selectionController()->selectedTracks();
        if (selected.empty()) {
            out["summary"] = muse::qtrc("record", "Select a track or a time range first.");
            return out;
        }
        track = selected.front();
    }
    double t0 = selectionController()->dataSelectedStartTime();
    double t1 = selectionController()->dataSelectedEndTime();
    if (!(t1 > t0)) {
        t0 = t1 = 0.0;
    }
    const KeyDetection r = record()->detectKey(track, t0, t1, /*store*/ false);
    if (!r.valid) {
        out["summary"] = muse::qtrc("record", "Not enough pitched audio to estimate a key.");
        return out;
    }
    out["valid"] = true;
    out["tonic"] = r.tonic;
    out["minor"] = r.minor;
    out["tuningHz"] = std::round(r.tuningHz * 10.0) / 10.0;
    out["summary"] = muse::qtrc("record", "Detected %1 (confidence %2%), A = %3 Hz. Apply to store it.")
                     .arg(QString::fromStdString(r.keyName))
                     .arg(int(std::lround(r.confidence * 100.0)))
                     .arg(QString::number(r.tuningHz, 'f', 1));
    return out;
}
