/*
 * Audacity: A Digital Audio Editor
 */
#pragma once

#include <memory>
#include <vector>

#include "au3-components/SettingsVisitor.h"
#include "au3-effects/StatefulEffect.h"

#include "denoisecore.h"

class WaveTrack;

namespace au::effects {
struct NoiseSuppressionSettings {
    double mMixPercent = 100.0;
};
inline bool operator==(const NoiseSuppressionSettings& a, const NoiseSuppressionSettings& b)
{
    return a.mMixPercent == b.mMixPercent;
}
inline bool operator!=(const NoiseSuppressionSettings& a, const NoiseSuppressionSettings& b) { return !(a == b); }

struct BreathRemovalSettings {
    double mReductionDb = -12.0;
    bool mSilence = false;
    double mFadeMs = 10.0;
    double mSensitivity = 50.0; //!< 0..100: how quiet relative to the voice a breath may be

    //! The detector settings these map to (shared by the destructive and the realtime versions)
    denoise::BreathSettings toCore() const;
};
inline bool operator==(const BreathRemovalSettings& a, const BreathRemovalSettings& b)
{
    return a.mReductionDb == b.mReductionDb && a.mSilence == b.mSilence && a.mFadeMs == b.mFadeMs
           && a.mSensitivity == b.mSensitivity;
}
inline bool operator!=(const BreathRemovalSettings& a, const BreathRemovalSettings& b) { return !(a == b); }

//! RNNoise-based noise suppression (applied per channel). Destructive and realtime.
class NoiseSuppressionEffect : public EffectWithSettings<NoiseSuppressionSettings, StatefulEffect>
{
public:
    static const ComponentInterfaceSymbol Symbol;

    NoiseSuppressionEffect();

    ComponentInterfaceSymbol GetSymbol() const override;
    TranslatableString GetDescription() const override;
    ManualPageID ManualPage() const override;
    ::EffectType GetType() const override;
    EffectGroup GetGroup() const override { return EffectGroup::NoiseRemovalAndRepair; }
    RealtimeSince RealtimeSupport() const override { return RealtimeSince::Always; }

    std::shared_ptr<::EffectInstance> MakeInstance() const override;
    bool CheckWhetherSkipEffect(const EffectSettings& settings) const override;
    bool Process(::EffectInstance& instance, EffectSettings& settings) override;

    const EffectParameterMethods& Parameters() const override;
    static constexpr EffectParameter Mix { &NoiseSuppressionSettings::mMixPercent, L"Mix", 100.0, 0.0, 100.0, 1 };

    //! Realtime: one streaming RNNoise per channel; latency reported to the host
    class Instance : public StatefulEffect::Instance
    {
    public:
        explicit Instance(NoiseSuppressionEffect& effect);
        size_t GetBlockSize() const override { return m_blockSize; }
        size_t SetBlockSize(size_t maxBlockSize) override;
        unsigned GetAudioInCount() const override { return 1; }
        unsigned GetAudioOutCount() const override { return 1; }
        SampleCount GetLatency(const EffectSettings& settings, double sampleRate) const override;
        bool RealtimeInitialize(EffectSettings& settings, double sampleRate, size_t audioThreadBufferSize) override;
        bool RealtimeAddProcessor(EffectSettings& settings, EffectOutputs* pOutputs, unsigned numChannels,
                                  float sampleRate) override;
        bool RealtimeSuspend() override { return true; }
        bool RealtimeResume() override { return true; }
        bool RealtimeProcessStart(MessagePackage&) override { return true; }
        size_t RealtimeProcess(size_t group, EffectSettings& settings, const float* const* inBuf, float* const* outBuf,
                               size_t numSamples) override;
        bool RealtimeProcessEnd(EffectSettings&) noexcept override { return true; }
        bool RealtimeFinalize(EffectSettings& settings) noexcept override;
        // Block processing: used when the realtime stack is rendered by the mixer (export, mixdown)
        bool ProcessInitialize(EffectSettings& settings, double sampleRate, ChannelNames chanMap) override;
        size_t ProcessBlock(EffectSettings& settings, const float* const* inBlock, float* const* outBlock,
                            size_t blockLen) override;
        bool ProcessFinalize() noexcept override;

    private:
        size_t m_blockSize = 512;
        std::vector<std::unique_ptr<denoise::StreamSuppressor> > m_processors;
        std::unique_ptr<denoise::StreamSuppressor> m_block;
    };
};

//! Finds breaths between phrases and turns them down (or silences them) with short fades.
//! Destructive: whole-selection analysis. Realtime: causal with a ~0.23 s lookahead.
class BreathRemovalEffect : public EffectWithSettings<BreathRemovalSettings, StatefulEffect>
{
public:
    static const ComponentInterfaceSymbol Symbol;

    BreathRemovalEffect();

    ComponentInterfaceSymbol GetSymbol() const override;
    TranslatableString GetDescription() const override;
    ManualPageID ManualPage() const override;
    ::EffectType GetType() const override;
    EffectGroup GetGroup() const override { return EffectGroup::NoiseRemovalAndRepair; }
    RealtimeSince RealtimeSupport() const override { return RealtimeSince::Always; }

    std::shared_ptr<::EffectInstance> MakeInstance() const override;
    bool Process(::EffectInstance& instance, EffectSettings& settings) override;

    //! Breaths found by the last Process, for reporting
    size_t mLastBreathCount = 0;

    const EffectParameterMethods& Parameters() const override;
    static constexpr EffectParameter Reduction { &BreathRemovalSettings::mReductionDb, L"Reduction", -12.0, -40.0, 0.0, 1 };
    static constexpr EffectParameter Silence { &BreathRemovalSettings::mSilence, L"Silence", false, false, true, 1 };
    static constexpr EffectParameter Fade { &BreathRemovalSettings::mFadeMs, L"FadeMs", 10.0, 1.0, 50.0, 1 };
    static constexpr EffectParameter Sensitivity { &BreathRemovalSettings::mSensitivity, L"Sensitivity", 50.0, 0.0, 100.0,
                                                   1 };

    //! Realtime: one lookahead gate per track (all its channels share the gain)
    class Instance : public StatefulEffect::Instance
    {
    public:
        explicit Instance(BreathRemovalEffect& effect);
        size_t GetBlockSize() const override { return m_blockSize; }
        size_t SetBlockSize(size_t maxBlockSize) override;
        unsigned GetAudioInCount() const override { return 2; }
        unsigned GetAudioOutCount() const override { return 2; }
        SampleCount GetLatency(const EffectSettings& settings, double sampleRate) const override;
        bool RealtimeInitialize(EffectSettings& settings, double sampleRate, size_t audioThreadBufferSize) override;
        bool RealtimeAddProcessor(EffectSettings& settings, EffectOutputs* pOutputs, unsigned numChannels,
                                  float sampleRate) override;
        bool RealtimeSuspend() override { return true; }
        bool RealtimeResume() override { return true; }
        bool RealtimeProcessStart(MessagePackage&) override { return true; }
        size_t RealtimeProcess(size_t group, EffectSettings& settings, const float* const* inBuf, float* const* outBuf,
                               size_t numSamples) override;
        bool RealtimeProcessEnd(EffectSettings&) noexcept override { return true; }
        bool RealtimeFinalize(EffectSettings& settings) noexcept override;
        // Block processing: used when the realtime stack is rendered by the mixer (export, mixdown)
        bool ProcessInitialize(EffectSettings& settings, double sampleRate, ChannelNames chanMap) override;
        size_t ProcessBlock(EffectSettings& settings, const float* const* inBlock, float* const* outBlock,
                            size_t blockLen) override;
        bool ProcessFinalize() noexcept override;

    private:
        size_t m_blockSize = 512;
        std::vector<std::unique_ptr<denoise::StreamBreathGate> > m_processors;
        std::unique_ptr<denoise::StreamBreathGate> m_block;
        unsigned m_blockChannels = 2;
    };
};
}
