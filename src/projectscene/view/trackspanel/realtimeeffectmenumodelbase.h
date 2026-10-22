/*
 * Audacity: A Digital Audio Editor
 */
#pragma once

#include "internal/irealtimeeffectpaneltrackselection.h"
#include "uicomponents/qml/Muse/UiComponents/abstractmenumodel.h"
#include "trackedit/trackedittypes.h"
#include "effects/effects_base/ieffectviewcontroller.h"
#include "effects/effects_base/ieffectsmenuprovider.h"
#include "effects/effects_base/irealtimeeffectservice.h"
#include "effects/effects_base/effectstypes.h"
#include <QObject>

namespace au::projectscene {
class RealtimeEffectMenuModelBase : public muse::uicomponents::AbstractMenuModel, public effects::IEffectMenuItemFactory
{
    Q_OBJECT
    Q_PROPERTY(bool isMasterTrack READ isMasterTrack WRITE prop_setIsMasterTrack NOTIFY isMasterTrackChanged)
    //! Optional explicit target track (e.g. a mixer strip); NO_TARGET_TRACK = follow the effects panel selection
    Q_PROPERTY(qlonglong targetTrackId READ targetTrackId WRITE setTargetTrackId NOTIFY targetTrackIdChanged)

    muse::ContextInject<effects::IEffectsMenuProvider> effectsMenuProvider{ this };
    muse::ContextInject<IRealtimeEffectPanelTrackSelection> trackSelection{ this };

public:
    explicit RealtimeEffectMenuModelBase(QObject* parent = nullptr);

    static constexpr qlonglong NO_TARGET_TRACK = -1000000;

    Q_INVOKABLE void init();
    qlonglong targetTrackId() const { return m_targetTrackId; }
    void setTargetTrackId(qlonglong id);
    Q_INVOKABLE void load() final override;

protected:
    std::optional<au::trackedit::TrackId> trackId() const;
    bool isMasterTrack() const { return m_isMasterTrack; }
    muse::uicomponents::MenuItemList effectMenus();

    muse::ContextInject<effects::IEffectViewController> effectViewController{ this };
    muse::ContextInject<effects::IRealtimeEffectService> realtimeEffectService{ this };

signals:
    void isMasterTrackChanged();
    void targetTrackIdChanged();

private:
    void prop_setIsMasterTrack(bool isMasterTrack);

    virtual void doPopulateMenu() { setItems(effectMenus()); }
    virtual void onSelectedTrackIdChanged() {}

    muse::uicomponents::MenuItem* makeMenuSeparator() final override { return makeSeparator(); }

    bool m_isMasterTrack = false;
    qlonglong m_targetTrackId = NO_TARGET_TRACK;
    bool m_menuIsUpToDate = false;
};
}
