/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "framework/global/modularity/ioc.h"
#include "framework/global/async/asyncable.h"

#include "context/iglobalcontext.h"
#include "record/irecord.h"
#include "trackedit/iselectioncontroller.h"
#include "trackedit/internal/itracknavigationcontroller.h"

#include "uicomponents/qml/Muse/UiComponents/toolbaritem.h"

namespace au::record {
//! Playback toolbar control showing / editing the project key (next to tempo and time signature)
class PlaybackToolBarProjectKeyItem : public muse::uicomponents::ToolBarItem
{
    Q_OBJECT
    QML_ELEMENT;
    QML_UNCREATABLE("Must be created in C++ only");

    Q_PROPERTY(bool hasKey READ hasKey NOTIFY keyChanged FINAL)
    Q_PROPERTY(QString label READ label NOTIFY keyChanged FINAL)
    Q_PROPERTY(QString accessibleLabel READ accessibleLabel NOTIFY keyChanged FINAL)
    Q_PROPERTY(int tonic READ tonic NOTIFY keyChanged FINAL)
    Q_PROPERTY(bool minor READ minor NOTIFY keyChanged FINAL)
    Q_PROPERTY(double tuningHz READ tuningHz NOTIFY keyChanged FINAL)
    Q_PROPERTY(QStringList tonicNames READ tonicNames CONSTANT FINAL)

    muse::ContextInject<context::IGlobalContext> globalContext{ this };
    muse::ContextInject<record::IRecord> record{ this };
    muse::ContextInject<trackedit::ISelectionController> selectionController{ this };
    muse::ContextInject<trackedit::ITrackNavigationController> trackNavigationController{ this };

public:
    explicit PlaybackToolBarProjectKeyItem(const muse::ui::UiAction& action, muse::uicomponents::ToolBarItemType::Type type,
                                           QObject* parent = nullptr);

    bool hasKey() const;
    QString label() const;
    QString accessibleLabel() const;
    int tonic() const;
    bool minor() const;
    double tuningHz() const;
    QStringList tonicNames() const;

    //! Store the key (one undo step). tuningHz is the A4 reference (415..466 Hz).
    Q_INVOKABLE void apply(int tonic, bool minor, double tuningHz);
    //! Remove the key (one undo step)
    Q_INVOKABLE void clear();
    //! Run key detection on the time selection (or the focused / selected track); does not store anything.
    //! Returns { valid, tonic, minor, tuningHz, summary }.
    Q_INVOKABLE QVariantMap detectFromSelection();

    static double centsToHz(double cents);
    static double hzToCents(double hz);

signals:
    void keyChanged();

private:
    void onProjectChanged();

    std::optional<ProjectKeyInfo> m_key;
};
}
