/*
 * Audacity: A Digital Audio Editor
 */
#include "truepeaklimiterviewmodel.h"

#include "framework/global/translation.h"
#include "framework/global/log.h"

#include "au3-math/TruePeak.h"
#include "truepeaklimitereffect.h"

using namespace au::effects;

TruePeakLimiterViewModel::TruePeakLimiterViewModel(QObject* parent, int instanceId)
    : BuiltinEffectModel(parent, instanceId)
{
}

void TruePeakLimiterViewModel::doReload()
{
    const auto& s = settings<TruePeakLimiterSettings>();
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
