/*
 * Audacity: A Digital Audio Editor
 */
#pragma once

#include <functional>
#include <memory>

#include <QTimer>

#include "effects/builtin/qml/Audacity/BuiltinEffects/builtineffectmodel.h"

namespace au::effects {
struct TruePeakLimiterSettings;
}
struct LimiterMeterState;
class TruePeakLimiter;
namespace au::effects {

class TruePeakLimiterViewModel : public BuiltinEffectModel
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap paramsList READ paramsList NOTIFY paramsListChanged FINAL)
    Q_PROPERTY(QString latencyText READ latencyText NOTIFY latencyChanged FINAL)

    //! Gain reduction (>= 0 dB) and output true peak, polled from the processing at ~30 Hz
    Q_PROPERTY(double currentGrDb READ currentGrDb NOTIFY meterChanged FINAL)
    Q_PROPERTY(double maxGrDb READ maxGrDb NOTIFY meterChanged FINAL)
    Q_PROPERTY(double outputPeakDb READ outputPeakDb NOTIFY meterChanged FINAL)
    Q_PROPERTY(double maxOutputPeakDb READ maxOutputPeakDb NOTIFY meterChanged FINAL)
    Q_PROPERTY(bool meterActive READ meterActive NOTIFY meterChanged FINAL)
    Q_PROPERTY(bool demoFeed READ demoFeed CONSTANT FINAL)

public:
    TruePeakLimiterViewModel(QObject* parent, int instanceId);

    QVariantMap paramsList() const { return m_paramsList; }
    //! Latency at 44.1 and 48 kHz, for display
    QString latencyText() const;

    Q_INVOKABLE void setParam(const QString& key, double val);
    Q_INVOKABLE void resetMeter();

    double currentGrDb() const { return m_currentGrDb; }
    double maxGrDb() const { return m_maxGrDb; }
    double outputPeakDb() const { return m_outPeakDb; }
    double maxOutputPeakDb() const { return m_maxOutPeakDb; }
    bool meterActive() const { return m_meterActive; }
    //! Set by AU_TRUEPEAK_METER_DEMO=1: a synthetic hot signal runs through a private limiter
    //! with the current settings and feeds the meter (headless/screenshot use; no playback needed)
    bool demoFeed() const { return m_demo != nullptr; }

    //! Reads the shared meter once (what the timer does); public for tests
    void pollMeter();

signals:
    void paramsListChanged();
    void latencyChanged();
    void meterChanged();

private:
    void doReload() override;
    void doUpdateSettings() override {}

    using Setter = std::function<void (TruePeakLimiterSettings&, double)>;
    QVariantMap m_paramsList;
    QMap<QString, Setter> m_setters;

    void runDemoBlock();

    std::shared_ptr<LimiterMeterState> m_meter;
    QTimer m_pollTimer;
    unsigned m_lastUpdates = 0;
    int m_idlePolls = 0;
    double m_currentGrDb = 0;
    double m_maxGrDb = 0;
    double m_outPeakDb = -120;
    double m_maxOutPeakDb = -120;
    bool m_meterActive = false;

    struct Demo;
    std::shared_ptr<Demo> m_demo;
};

class TruePeakLimiterViewModelFactory : public EffectViewModelFactory<TruePeakLimiterViewModel>
{
};
}
