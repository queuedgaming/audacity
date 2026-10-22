/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <QPointF>

#include "context/iglobalcontext.h"
#include "uicomponents/qml/Muse/UiComponents/abstractmenumodel.h"

#include "iprojectsceneconfiguration.h"
#include "record/irecord.h"
#include "types/projectscenetypes.h"

namespace au::projectscene {
class ClipContextMenuModel : public muse::uicomponents::AbstractMenuModel
{
    Q_OBJECT

    muse::GlobalInject<projectscene::IProjectSceneConfiguration> projectSceneConfiguration;
    muse::ContextInject<context::IGlobalContext> globalContext{ this };
    muse::ContextInject<record::IRecord> record{ this };

    Q_PROPERTY(ClipKey clipKey READ clipKey WRITE setClipKey NOTIFY clipKeyChanged FINAL)

public:
    ClipContextMenuModel() = default;

    Q_INVOKABLE void load() override;
    Q_INVOKABLE void handleMenuItem(const QString& itemId) override;

    //! Fade handle dragging: live preview (no undo step) and the final value (one undo step).
    //! Returns QPointF(fadeIn, fadeOut) in seconds, clamped to the clip.
    Q_INVOKABLE QPointF fadeDragPreview(bool fadeInHandle, double startIn, double startOut, double dxSec) const;
    //! Snap the dragged edge to `snappedEdge` (project seconds); returns QPointF(fadeIn, fadeOut)
    Q_INVOKABLE QPointF fadeDragSnap(bool fadeInHandle, double fadeIn, double fadeOut, double snappedEdge) const;
    Q_INVOKABLE QString fadeDragReadout(bool fadeInHandle, double seconds, int curve, bool showCurve) const;
    Q_INVOKABLE void applyFadeDrag(double fadeIn, double fadeOut, int startCurve, bool toggleCurve);

    ClipKey clipKey() const;
    void setClipKey(const ClipKey& newClipKey);

signals:
    void clipKeyChanged();
    void clipTitleEditRequested();

private:
    void onActionsStateChanges(const muse::actions::ActionCodeList& codes) override;
    void updateStretchEnabledState(muse::uicomponents::MenuItem& item);
    void updatePitchSpeedModifiedEnabledState(muse::uicomponents::MenuItem& item);

    muse::uicomponents::MenuItemList makeClipColourItems();
    muse::uicomponents::MenuItem* makeFadesMenu();
    muse::uicomponents::MenuItem* makeTransientsMenu();
    void updateColorCheckedState();
    void updateColorMenu();

    ClipKey m_clipKey;
    muse::actions::ActionCodeList m_colorChangeActionCodeList;
};
}
