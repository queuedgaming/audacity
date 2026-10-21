/*
 * Audacity: A Digital Audio Editor
 */
#pragma once

#include "au3-components/SettingsVisitor.h"
#include "au3-effects/StatefulEffect.h"

class WaveTrack;

namespace au::effects {
//! RNNoise-based noise suppression (applied per channel)
class NoiseSuppressionEffect : public StatefulEffect
{
public:
    static inline NoiseSuppressionEffect* FetchParameters(NoiseSuppressionEffect& e, EffectSettings&) { return &e; }
    static const ComponentInterfaceSymbol Symbol;

    NoiseSuppressionEffect();

    ComponentInterfaceSymbol GetSymbol() const override;
    TranslatableString GetDescription() const override;
    ManualPageID ManualPage() const override;
    ::EffectType GetType() const override;
    EffectGroup GetGroup() const override { return EffectGroup::NoiseRemovalAndRepair; }

    bool CheckWhetherSkipEffect(const EffectSettings& settings) const override;
    bool Process(::EffectInstance& instance, EffectSettings& settings) override;

    double mMixPercent = 100.0;

    const EffectParameterMethods& Parameters() const override;
    static constexpr EffectParameter Mix { &NoiseSuppressionEffect::mMixPercent, L"Mix", 100.0, 0.0, 100.0, 1 };
};

//! Finds breaths between phrases and turns them down (or silences them) with short fades
class BreathRemovalEffect : public StatefulEffect
{
public:
    static inline BreathRemovalEffect* FetchParameters(BreathRemovalEffect& e, EffectSettings&) { return &e; }
    static const ComponentInterfaceSymbol Symbol;

    BreathRemovalEffect();

    ComponentInterfaceSymbol GetSymbol() const override;
    TranslatableString GetDescription() const override;
    ManualPageID ManualPage() const override;
    ::EffectType GetType() const override;
    EffectGroup GetGroup() const override { return EffectGroup::NoiseRemovalAndRepair; }

    bool Process(::EffectInstance& instance, EffectSettings& settings) override;

    double mReductionDb = -12.0;
    bool mSilence = false;
    double mFadeMs = 10.0;
    double mSensitivity = 50.0; //!< 0..100: how quiet relative to the voice a breath may be

    //! Breaths found by the last Process, for reporting
    size_t mLastBreathCount = 0;

    const EffectParameterMethods& Parameters() const override;
    static constexpr EffectParameter Reduction { &BreathRemovalEffect::mReductionDb, L"Reduction", -12.0, -40.0, 0.0, 1 };
    static constexpr EffectParameter Silence { &BreathRemovalEffect::mSilence, L"Silence", false, false, true, 1 };
    static constexpr EffectParameter Fade { &BreathRemovalEffect::mFadeMs, L"FadeMs", 10.0, 1.0, 50.0, 1 };
    static constexpr EffectParameter Sensitivity { &BreathRemovalEffect::mSensitivity, L"Sensitivity", 50.0, 0.0, 100.0, 1 };
};
}
