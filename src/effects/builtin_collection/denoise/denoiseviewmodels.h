/*
 * Audacity: A Digital Audio Editor
 */
#pragma once

#include "effects/builtin/qml/Audacity/BuiltinEffects/builtineffectmodel.h"

namespace au::effects {
class NoiseSuppressionViewModel : public BuiltinEffectModel
{
    Q_OBJECT
    Q_PROPERTY(QString effectTitle READ effectTitle CONSTANT FINAL)
    Q_PROPERTY(double mix READ mix WRITE setMix NOTIFY mixChanged FINAL)

public:
    NoiseSuppressionViewModel(QObject* parent, int instanceId);
    QString effectTitle() const;
    double mix() const;
    void setMix(double v);

signals:
    void mixChanged();

private:
    void doReload() override;
};

class NoiseSuppressionViewModelFactory : public EffectViewModelFactory<NoiseSuppressionViewModel>
{
};

class BreathRemovalViewModel : public BuiltinEffectModel
{
    Q_OBJECT
    Q_PROPERTY(QString effectTitle READ effectTitle CONSTANT FINAL)
    Q_PROPERTY(double reduction READ reduction WRITE setReduction NOTIFY changed FINAL)
    Q_PROPERTY(bool silence READ silence WRITE setSilence NOTIFY changed FINAL)
    Q_PROPERTY(double fadeMs READ fadeMs WRITE setFadeMs NOTIFY changed FINAL)
    Q_PROPERTY(double sensitivity READ sensitivity WRITE setSensitivity NOTIFY changed FINAL)

public:
    BreathRemovalViewModel(QObject* parent, int instanceId);
    QString effectTitle() const;
    double reduction() const;
    void setReduction(double v);
    bool silence() const;
    void setSilence(bool v);
    double fadeMs() const;
    void setFadeMs(double v);
    double sensitivity() const;
    void setSensitivity(double v);

signals:
    void changed();

private:
    void doReload() override;
};

class BreathRemovalViewModelFactory : public EffectViewModelFactory<BreathRemovalViewModel>
{
};
}
