/*
 * Audacity: A Digital Audio Editor
 */
#pragma once

#include <functional>

#include "effects/builtin/qml/Audacity/BuiltinEffects/builtineffectmodel.h"

namespace au::effects {
struct TruePeakLimiterSettings;

class TruePeakLimiterViewModel : public BuiltinEffectModel
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap paramsList READ paramsList NOTIFY paramsListChanged FINAL)
    Q_PROPERTY(QString latencyText READ latencyText NOTIFY latencyChanged FINAL)

public:
    TruePeakLimiterViewModel(QObject* parent, int instanceId);

    QVariantMap paramsList() const { return m_paramsList; }
    //! Latency at 44.1 and 48 kHz, for display
    QString latencyText() const;

    Q_INVOKABLE void setParam(const QString& key, double val);

signals:
    void paramsListChanged();
    void latencyChanged();

private:
    void doReload() override;
    void doUpdateSettings() override {}

    using Setter = std::function<void (TruePeakLimiterSettings&, double)>;
    QVariantMap m_paramsList;
    QMap<QString, Setter> m_setters;
};

class TruePeakLimiterViewModelFactory : public EffectViewModelFactory<TruePeakLimiterViewModel>
{
};
}
