/*
 * Audacity: A Digital Audio Editor
 */
#pragma once

#include <vector>

#include "au3-effects/PerTrackEffect.h"
#include "au3-components/SettingsVisitor.h"
#include "au3-math/TruePeak.h"
#include "dynamics/dynamicseffectsmacros.h"

namespace au::effects {
struct TruePeakLimiterSettings {
    static constexpr double ceilingDbDefault = -1.0;
    static constexpr double releaseMsDefault = 100.0;
    static constexpr double lookaheadMsDefault = 5.0;

    double ceilingDb { ceilingDbDefault };
    double releaseMs { releaseMsDefault };
    double lookaheadMs { lookaheadMsDefault };
};

bool operator==(const TruePeakLimiterSettings& a, const TruePeakLimiterSettings& b);

//! Brickwall limiter on the true (inter-sample) peak: 8x oversampled detection, lookahead,
//! ceiling in dBTP. Usable destructively and as a realtime effect; reports its latency.
class TruePeakLimiterEffect : public EffectWithSettings<TruePeakLimiterSettings, PerTrackEffect>
{
public:
    static const ComponentInterfaceSymbol Symbol;

    TruePeakLimiterEffect();
    ~TruePeakLimiterEffect() override = default;

    ComponentInterfaceSymbol GetSymbol() const override;
    TranslatableString GetDescription() const override;
    ManualPageID ManualPage() const override;

    ::EffectType GetType() const override;
    EffectGroup GetGroup() const override { return EffectGroup::VolumeAndCompression; }
    RealtimeSince RealtimeSupport() const override;

    std::shared_ptr<::EffectInstance> MakeInstance() const override;
    bool CheckWhetherSkipEffect(const EffectSettings& settings) const override;
    const EffectParameterMethods& Parameters() const override;

#define TRUEPEAK_PARAM(paramName, paramMin, paramDefault, paramMax, paramStep) \
    DYNAMICS_EFFECT_PARAM(TruePeakLimiterSettings, paramName, paramMin, paramDefault, paramMax, paramStep)

    TRUEPEAK_PARAM(ceilingDb, -12, TruePeakLimiterSettings::ceilingDbDefault, 0, 0.1);
    TRUEPEAK_PARAM(releaseMs, 1, TruePeakLimiterSettings::releaseMsDefault, 1000, 1);
    TRUEPEAK_PARAM(lookaheadMs, 1, TruePeakLimiterSettings::lookaheadMsDefault, 20, 0.1);

    class Instance : public PerTrackEffect::Instance, public EffectInstanceWithBlockSize
    {
    public:
        explicit Instance(const PerTrackEffect& effect);

        bool ProcessInitialize(EffectSettings& settings, double sampleRate, ChannelNames chanMap) override;
        size_t ProcessBlock(EffectSettings& settings, const float* const* inBlock, float* const* outBlock,
                            size_t blockLen) override;

        bool RealtimeInitialize(EffectSettings& settings, double sampleRate, size_t audioThreadBufferSize) override;
        bool RealtimeAddProcessor(EffectSettings& settings, EffectOutputs* pOutputs, unsigned numChannels,
                                  float sampleRate) override;
        bool RealtimeFinalize(EffectSettings& settings) noexcept override;
        size_t RealtimeProcess(size_t group, EffectSettings& settings, const float* const* inbuf,
                               float* const* outbuf, size_t numSamples) override;

        SampleCount GetLatency(const EffectSettings& settings, double sampleRate) const override;
        unsigned GetAudioInCount() const override { return 2; }
        unsigned GetAudioOutCount() const override { return 2; }

    private:
        struct Processor {
            TruePeakLimiter limiter;
            unsigned channels = 2;
            double lookaheadMs = 0;
            double rate = 0;
        };
        static void Prepare(Processor& p, const TruePeakLimiterSettings& s, unsigned channels, double rate);
        static size_t Run(Processor& p, const TruePeakLimiterSettings& s, const float* const* in, float* const* out,
                          size_t len);

        Processor mProcessor;
        std::vector<Processor> mSlaves;
    };
};
}
