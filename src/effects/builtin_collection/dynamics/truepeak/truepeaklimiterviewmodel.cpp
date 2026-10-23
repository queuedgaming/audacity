/*
 * Audacity: A Digital Audio Editor
 */
#include "truepeaklimiterviewmodel.h"

#include "framework/global/translation.h"
#include "framework/global/log.h"

#include <cmath>
#include <vector>

#include "au3-math/TruePeak.h"
#include "truepeaklimitereffect.h"

using namespace au::effects;

struct TruePeakLimiterViewModel::Demo {
    TruePeakLimiter limiter;
    double rate = 48000;
    double phase = 0;
    size_t t = 0;
    std::vector<float> l, r;
    double configuredLookahead = -1;
};

TruePeakLimiterViewModel::TruePeakLimiterViewModel(QObject* parent, int instanceId)
    : BuiltinEffectModel(parent, instanceId)
{
    if (qEnvironmentVariableIntValue("AU_TRUEPEAK_METER_DEMO") == 1) {
        m_demo = std::make_shared<Demo>();
    }
    m_pollTimer.setInterval(33); // ~30 Hz
    QObject::connect(&m_pollTimer, &QTimer::timeout, this, [this]() {
        if (m_demo) {
            runDemoBlock();
        }
        pollMeter();
    });
    m_pollTimer.start();
}

void TruePeakLimiterViewModel::pollMeter()
{
    if (!m_meter) {
        return;
    }
    const unsigned updates = m_meter->Updates();
    const bool flowing = updates != m_lastUpdates;
    m_lastUpdates = updates;
    m_idlePolls = flowing ? 0 : m_idlePolls + 1;
    const bool active = m_idlePolls < 10; // ~1/3 s without audio: show idle
    const double cur = active ? m_meter->CurrentGrDb() : 0.0;
    const double out = active ? m_meter->OutputPeakDb() : LimiterMeterState::FloorDb;
    const double maxGr = m_meter->MaxGrDb();
    const double maxOut = m_meter->MaxOutputPeakDb();
    if (cur != m_currentGrDb || out != m_outPeakDb || maxGr != m_maxGrDb || maxOut != m_maxOutPeakDb
        || active != m_meterActive) {
        m_currentGrDb = cur;
        m_outPeakDb = out;
        m_maxGrDb = maxGr;
        m_maxOutPeakDb = maxOut;
        m_meterActive = active;
        emit meterChanged();
    }
}

void TruePeakLimiterViewModel::resetMeter()
{
    if (!m_meter) {
        return;
    }
    m_meter->RequestReset();
    // processing may be stopped: also clear directly (the UI is the only other writer then)
    if (!m_meterActive) {
        m_meter->maxGrDb.store(0.0f);
        m_meter->maxOutPeakDb.store(LimiterMeterState::FloorDb);
    }
    m_maxGrDb = 0;
    m_maxOutPeakDb = LimiterMeterState::FloorDb;
    emit meterChanged();
}

void TruePeakLimiterViewModel::runDemoBlock()
{
    if (!m_meter) {
        return;
    }
    const auto& s = settings<TruePeakLimiterSettings>();
    auto& d = *m_demo;
    if (d.configuredLookahead != s.lookaheadMs) {
        d.limiter.Configure(2, d.rate, s.ceilingDb, s.releaseMs, s.lookaheadMs);
        d.configuredLookahead = s.lookaheadMs;
    } else {
        d.limiter.SetParameters(s.ceilingDb, s.releaseMs);
    }
    // 33 ms of a fs/4 tone at 45 degrees (inter-sample peaks +3 dB over the samples),
    // with a slowly pulsing level between -6 and +3 dBFS so the reduction moves
    const size_t n = size_t(d.rate * 0.033);
    d.l.resize(n);
    d.r.resize(n);
    for (size_t i = 0; i < n; ++i, ++d.t) {
        const double env = std::pow(10.0, (-1.5 + 4.5 * std::sin(2 * M_PI * 0.4 * d.t / d.rate)) / 20.0);
        const double x = env * std::sin(M_PI / 2 * double(d.t) + M_PI / 4);
        d.l[i] = float(x);
        d.r[i] = float(0.9 * x);
    }
    const float* in[] = { d.l.data(), d.r.data() };
    float* out[] = { d.l.data(), d.r.data() };
    d.limiter.Process(in, out, n);
    m_meter->Publish(d.limiter.BlockMinGain(), d.limiter.BlockOutputPeak());
}

void TruePeakLimiterViewModel::doReload()
{
    const auto& s = settings<TruePeakLimiterSettings>();
    m_meter = s.meter;
    auto makeItem = [this](const QString& key, const QString& title, const QString& unit, double value, double min,
                           double max, double step, const Setter& setter) {
        QVariantMap item;
        item["key"] = key;
        item["title"] = title;
        item["value"] = value;
        item["unit"] = unit;
        item["min"] = min;
        item["max"] = max;
        item["step"] = step;
        m_setters.insert(key, setter);
        return item;
    };
    using E = TruePeakLimiterEffect;
    m_paramsList.clear();
    m_paramsList["ceilingDb"] = makeItem("ceilingDb", muse::qtrc("effects/truepeak", "Ceiling"), "dBTP", s.ceilingDb,
                                         E::ceilingDb.min, E::ceilingDb.max, E::ceilingDb.step,
                                         [](TruePeakLimiterSettings& x, double v) { x.ceilingDb = v; });
    m_paramsList["releaseMs"] = makeItem("releaseMs", muse::qtrc("effects/truepeak", "Release"), "ms", s.releaseMs,
                                         E::releaseMs.min, E::releaseMs.max, E::releaseMs.step,
                                         [](TruePeakLimiterSettings& x, double v) { x.releaseMs = v; });
    m_paramsList["lookaheadMs"] = makeItem("lookaheadMs", muse::qtrc("effects/truepeak", "Lookahead"), "ms",
                                           s.lookaheadMs, E::lookaheadMs.min, E::lookaheadMs.max, E::lookaheadMs.step,
                                           [](TruePeakLimiterSettings& x, double v) { x.lookaheadMs = v; });
    emit paramsListChanged();
    emit latencyChanged();
}

QString TruePeakLimiterViewModel::latencyText() const
{
    const auto& s = settings<TruePeakLimiterSettings>();
    const auto l48 = TruePeakLimiter::LatencyFor(48000, s.lookaheadMs);
    return muse::qtrc("effects/truepeak", "Latency: %1 samples (%2 ms) at 48 kHz, compensated")
           .arg(qulonglong(l48)).arg(1000.0 * double(l48) / 48000.0, 0, 'f', 2);
}

void TruePeakLimiterViewModel::setParam(const QString& key, double val)
{
    const Setter s = m_setters.value(key, nullptr);
    IF_ASSERT_FAILED(s) {
        return;
    }
    modifySettings<TruePeakLimiterSettings>([s, val](EffectSettings& settings) {
        s(*settings.cast<TruePeakLimiterSettings>(), val);
    });
    emit latencyChanged();
}
