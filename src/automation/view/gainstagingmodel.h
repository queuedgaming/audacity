/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <algorithm>
#include <map>

#include <QObject>
#include <QVariantList>

#include "framework/global/modularity/ioc.h"
#include "automation/igainstaginginteraction.h"

namespace au::automation {
//! Gain staging dialog: measures every track and suggests input trims to reach a target level
class GainStagingModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged FINAL)
    Q_PROPERTY(int targetKind READ targetKind WRITE setTargetKind NOTIFY targetChanged FINAL)
    Q_PROPERTY(double target READ target WRITE setTarget NOTIFY targetChanged FINAL)
    Q_PROPERTY(int applicableCount READ applicableCount NOTIFY rowsChanged FINAL)

    muse::ContextInject<IGainStagingInteraction> gainStaging{ this };

    friend class GainStagingModelTest;

public:
    enum Kind { Rms = 0, Lufs = 1, Peak = 2 };
    Q_ENUM(Kind)

    explicit GainStagingModel(QObject* parent = nullptr);

    QVariantList rows() const { return m_rows; }
    int targetKind() const { return m_kind; }
    void setTargetKind(int kind);
    double target() const { return m_target; }
    void setTarget(double target);
    int applicableCount() const;

    //! Default targets per kind: -18 dBFS RMS, -23 LUFS, -1 dBTP
    Q_INVOKABLE static double defaultTarget(int kind);

    Q_INVOKABLE void analyze();
    //! Applies every suggested trim (rows with "use" checked) as one undo step; returns how many changed
    Q_INVOKABLE int apply();
    Q_INVOKABLE void setUse(int row, bool use);

signals:
    void rowsChanged();
    void targetChanged();

private:
    void rebuild();

    std::vector<TrackLevels> m_levels;
    std::map<trackedit::TrackId, bool> m_use;
    QVariantList m_rows;
    int m_kind = Rms;
    double m_target = -18.0;
};
}
