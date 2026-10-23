/*
 * Audacity: A Digital Audio Editor
 */
#include "truepeaklimitereffect.h"

#include <cmath>

#include "au3-command-parameters/ShuttleAutomation.h"
#include "au3-strings/TranslatableString.h"

namespace au::effects {
bool operator==(const TruePeakLimiterSettings& a, const TruePeakLimiterSettings& b)
{
    return a.ceilingDb == b.ceilingDb && a.releaseMs == b.releaseMs && a.lookaheadMs == b.lookaheadMs;
}

const std::vector<TruePeakLimiterPreset>& TruePeakLimiterFactoryPresets()
{
    static const std::vector<TruePeakLimiterPreset> presets {
        { "Streaming (-1 dBTP)", -1.0, 100.0, 5.0 },
        { "Broadcast EBU R128 (-1 dBTP)", -1.0, 150.0, 5.0 },
        { "CD / Transparent (-0.3 dBTP)", -0.3, 200.0, 8.0 },
        { "Podcast (-1.5 dBTP, slow release)", -1.5, 400.0, 5.0 },
        { "Aggressive loud (fast release)", -0.5, 20.0, 2.0 },
    };
    return presets;
}

RegistryPaths TruePeakLimiterEffect::GetFactoryPresets() const
{
    RegistryPaths paths;
    for (const auto& p : TruePeakLimiterFactoryPresets()) {
        paths.push_back(RegistryPath { p.name });
    }
    return paths;
}

OptionalMessage TruePeakLimiterEffect::LoadFactoryPreset(int id, EffectSettings& settings) const
{
    const auto& presets = TruePeakLimiterFactoryPresets();
    if (id < 0 || static_cast<size_t>(id) >= presets.size()) {
        return {};
    }
    // assign fields only, keeping the shared meter
    auto& s = GetSettings(settings);
    s.ceilingDb = presets[id].ceilingDb;
    s.releaseMs = presets[id].releaseMs;
    s.lookaheadMs = presets[id].lookaheadMs;
    return { nullptr };
}

const ComponentInterfaceSymbol TruePeakLimiterEffect::Symbol { TranslatableString("effects-dynamics", "True Peak Limiter") };

const EffectParameterMethods& TruePeakLimiterEffect::Parameters() const
{
    static CapturedParameters<TruePeakLimiterEffect, ceilingDb, releaseMs, lookaheadMs> parameters;
    return parameters;
}

TruePeakLimiterEffect::TruePeakLimiterEffect()
{
    SetLinearEffectFlag(false);
}

ComponentInterfaceSymbol TruePeakLimiterEffect::GetSymbol() const
{
    return Symbol;
}

TranslatableString TruePeakLimiterEffect::GetDescription() const
{
    return TranslatableString("effects-dynamics",
                              "Keeps the true (inter-sample) peak below a ceiling in dBTP, with lookahead.");
}

ManualPageID TruePeakLimiterEffect::ManualPage() const
{
    return L"";
}

::EffectType TruePeakLimiterEffect::GetType() const
{
    return EffectTypeProcess;
}

auto TruePeakLimiterEffect::RealtimeSupport() const -> RealtimeSince
{
    return RealtimeSince::Always;
}

bool TruePeakLimiterEffect::CheckWhetherSkipEffect(const EffectSettings&) const
{
    return false;
}

std::shared_ptr<::EffectInstance> TruePeakLimiterEffect::MakeInstance() const
{
    return std::make_shared<Instance>(*this);
}

TruePeakLimiterEffect::Instance::Instance(const PerTrackEffect& effect)
    : PerTrackEffect::Instance{effect}
{
}

void TruePeakLimiterEffect::Instance::Prepare(Processor& p, const TruePeakLimiterSettings& s, unsigned channels,
                                              double rate)
{
    p.channels = std::max(1u, std::min(2u, channels));
    p.lookaheadMs = s.lookaheadMs;
    p.rate = rate;
    p.limiter.Configure(p.channels, rate, s.ceilingDb, s.releaseMs, s.lookaheadMs);
}

size_t TruePeakLimiterEffect::Instance::Run(Processor& p, const TruePeakLimiterSettings& s, const float* const* in,
                                            float* const* out, size_t len)
{
    if (s.lookaheadMs != p.lookaheadMs) {
        // the delay line length changes: start over (latency changes too)
        Prepare(p, s, p.channels, p.rate);
    } else {
        p.limiter.SetParameters(s.ceilingDb, s.releaseMs);
    }
    p.limiter.Process(in, out, len);
    if (s.meter) {
        s.meter->Publish(p.limiter.BlockMinGain(), p.limiter.BlockOutputPeak());
    }
    return len;
}

bool TruePeakLimiterEffect::Instance::ProcessInitialize(EffectSettings& settings, double sampleRate, ChannelNames)
{
    Prepare(mProcessor, GetSettings(settings), GetAudioInCount(), sampleRate);
    return true;
}

size_t TruePeakLimiterEffect::Instance::ProcessBlock(EffectSettings& settings, const float* const* inBlock,
                                                     float* const* outBlock, size_t blockLen)
{
    return Run(mProcessor, GetSettings(settings), inBlock, outBlock, blockLen);
}

bool TruePeakLimiterEffect::Instance::RealtimeInitialize(EffectSettings&, double, size_t)
{
    SetBlockSize(512);
    mSlaves.clear();
    return true;
}

bool TruePeakLimiterEffect::Instance::RealtimeAddProcessor(EffectSettings& settings, EffectOutputs*,
                                                           unsigned numChannels, float sampleRate)
{
    auto& p = mSlaves.emplace_back();
    Prepare(p, GetSettings(settings), numChannels, sampleRate);
    return true;
}

bool TruePeakLimiterEffect::Instance::RealtimeFinalize(EffectSettings&) noexcept
{
    mSlaves.clear();
    return true;
}

size_t TruePeakLimiterEffect::Instance::RealtimeProcess(size_t group, EffectSettings& settings,
                                                        const float* const* inbuf, float* const* outbuf,
                                                        size_t numSamples)
{
    if (group >= mSlaves.size()) {
        return 0;
    }
    return Run(mSlaves[group], GetSettings(settings), inbuf, outbuf, numSamples);
}

EffectInstance::SampleCount TruePeakLimiterEffect::Instance::GetLatency(const EffectSettings& settings,
                                                                        double sampleRate) const
{
    return TruePeakLimiter::LatencyFor(sampleRate, GetSettings(settings).lookaheadMs);
}
}
