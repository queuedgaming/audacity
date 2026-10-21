/*
 * Audacity: A Digital Audio Editor
 */
#include "denoiseeffects.h"

#include "denoisecore.h"

#include <algorithm>
#include <optional>

#include "au3-effects/EffectOutputTracks.h"
#include "au3-command-parameters/ShuttleAutomation.h"
#include "au3-strings/TranslatableString.h"
#include "au3-wave-track/WaveTrack.h"

namespace au::effects {
namespace {
struct Range {
    sampleCount start;
    size_t len = 0;
};

std::optional<Range> selectedRange(const WaveTrack& track, double t0, double t1)
{
    t0 = std::max(t0, track.GetStartTime());
    t1 = std::min(t1, track.GetEndTime());
    if (t1 <= t0) {
        return std::nullopt;
    }
    const auto s = track.TimeToLongSamples(t0);
    const auto e = track.TimeToLongSamples(t1);
    return Range { s, (e - s).as_size_t() };
}

std::vector<float> read(const WaveChannel& ch, const Range& r)
{
    std::vector<float> v(r.len);
    ch.GetFloats(v.data(), r.start, r.len, FillFormat::fillZero);
    return v;
}
}

// ---------------- Noise suppression ----------------

const ComponentInterfaceSymbol NoiseSuppressionEffect::Symbol { TranslatableString("effects-noisesuppression", "Noise suppression") };

const EffectParameterMethods& NoiseSuppressionEffect::Parameters() const
{
    static CapturedParameters<NoiseSuppressionEffect, Mix> parameters;
    return parameters;
}

NoiseSuppressionEffect::NoiseSuppressionEffect()
{
    SetLinearEffectFlag(false);
}

std::shared_ptr<::EffectInstance> NoiseSuppressionEffect::MakeInstance() const
{
    // like StatefulEffect: the instance calls through to the effect for destructive processing,
    // but keeps its own streaming state so every realtime stack slot is independent
    return std::make_shared<Instance>(const_cast<NoiseSuppressionEffect&>(*this));
}

ComponentInterfaceSymbol NoiseSuppressionEffect::GetSymbol() const { return Symbol; }

TranslatableString NoiseSuppressionEffect::GetDescription() const
{
    return TranslatableString("effects-noisesuppression",
                              "Removes steady and changing background noise from speech and vocals (RNNoise)");
}

ManualPageID NoiseSuppressionEffect::ManualPage() const { return L"Noise_Suppression"; }

::EffectType NoiseSuppressionEffect::GetType() const { return EffectTypeProcess; }

bool NoiseSuppressionEffect::CheckWhetherSkipEffect(const EffectSettings& settings) const
{
    return GetSettings(settings).mMixPercent <= 0.0;
}

bool NoiseSuppressionEffect::Process(::EffectInstance&, EffectSettings& settings)
{
    const double mix = GetSettings(settings).mMixPercent / 100.0;
    EffectOutputTracks outputs { *mTracks, GetType(), { { mT0, mT1 } } };
    int count = 0;
    for (auto track : outputs.Get().Selected<WaveTrack>()) {
        const auto range = selectedRange(*track, mT0, mT1);
        if (!range) {
            continue;
        }
        for (const auto ch : track->Channels()) {
            if (TrackProgress(count, 0.0)) {
                return false;
            }
            auto x = read(*ch, *range);
            const auto r = denoise::suppress(x.data(), x.size(), track->GetRate(), mix);
            if (!ch->SetFloats(r.audio.data(), range->start, range->len)) {
                return false;
            }
            ++count;
            if (TrackProgress(count - 1, 1.0)) {
                return false;
            }
        }
    }
    outputs.Commit();
    return true;
}

// ---------------- Breath removal ----------------

const ComponentInterfaceSymbol BreathRemovalEffect::Symbol { TranslatableString("effects-breathremoval", "Breath removal") };

const EffectParameterMethods& BreathRemovalEffect::Parameters() const
{
    static CapturedParameters<BreathRemovalEffect, Reduction, Silence, Fade, Sensitivity> parameters;
    return parameters;
}

denoise::BreathSettings BreathRemovalSettings::toCore() const
{
    denoise::BreathSettings s;
    s.reductionDb = mReductionDb;
    s.silence = mSilence;
    s.fadeSec = mFadeMs / 1000.0;
    // Sensitivity 0..100 maps the quietest accepted breath from 35 dB to 75 dB below the voice
    s.minLevelBelowSpeechDb = 35.0 + 0.4 * std::clamp(mSensitivity, 0.0, 100.0);
    return s;
}

BreathRemovalEffect::BreathRemovalEffect()
{
    SetLinearEffectFlag(false);
}

std::shared_ptr<::EffectInstance> BreathRemovalEffect::MakeInstance() const
{
    return std::make_shared<Instance>(const_cast<BreathRemovalEffect&>(*this));
}

ComponentInterfaceSymbol BreathRemovalEffect::GetSymbol() const { return Symbol; }

TranslatableString BreathRemovalEffect::GetDescription() const
{
    return TranslatableString("effects-breathremoval",
                              "Finds breaths between sung or spoken phrases and turns them down");
}

ManualPageID BreathRemovalEffect::ManualPage() const { return L"Breath_Removal"; }

::EffectType BreathRemovalEffect::GetType() const { return EffectTypeProcess; }

bool BreathRemovalEffect::Process(::EffectInstance&, EffectSettings& settings)
{
    EffectOutputTracks outputs { *mTracks, GetType(), { { mT0, mT1 } } };
    const denoise::BreathSettings s = GetSettings(settings).toCore();
    mLastBreathCount = 0;
    int count = 0;
    for (auto track : outputs.Get().Selected<WaveTrack>()) {
        const auto range = selectedRange(*track, mT0, mT1);
        if (!range) {
            continue;
        }
        if (TrackProgress(count, 0.0)) {
            return false;
        }
        // Detect on the channel mix, apply the same gain to every channel (keeps the stereo image)
        std::vector<std::vector<float> > chans;
        for (const auto ch : track->Channels()) {
            chans.push_back(read(*ch, *range));
        }
        std::vector<float> mono(range->len, 0.0f);
        for (const auto& c : chans) {
            for (size_t i = 0; i < mono.size(); ++i) {
                mono[i] += c[i] / float(chans.size());
            }
        }
        const double rate = track->GetRate();
        const auto breaths = denoise::detectBreaths(mono.data(), mono.size(), rate, s);
        mLastBreathCount += breaths.size();
        const auto gain = denoise::breathGain(mono.size(), rate, breaths, s);
        size_t ci = 0;
        for (const auto ch : track->Channels()) {
            denoise::applyGain(chans[ci].data(), gain);
            if (!ch->SetFloats(chans[ci].data(), range->start, range->len)) {
                return false;
            }
            ++ci;
        }
        if (TrackProgress(count++, 1.0)) {
            return false;
        }
    }
    if (mLastBreathCount == 0) {
        mLastError = TranslatableString("effects-breathremoval",
                                        "No breaths found in the selection. Nothing changed.").translated().toStdString();
        return false;
    }
    outputs.Commit();
    return true;
}

// ---------------- Realtime instances ----------------

NoiseSuppressionEffect::Instance::Instance(NoiseSuppressionEffect& effect)
    : StatefulEffect::Instance(effect) {}

size_t NoiseSuppressionEffect::Instance::SetBlockSize(size_t maxBlockSize)
{
    m_blockSize = std::clamp<size_t>(maxBlockSize, 1, denoise::MAX_BLOCK);
    return m_blockSize;
}

::EffectInstance::SampleCount NoiseSuppressionEffect::Instance::GetLatency(const EffectSettings&, double sampleRate) const
{
    // Only the realtime path has latency; destructive processing compensates internally
    return m_processors.empty() ? 0 : denoise::StreamSuppressor::latencyFor(sampleRate);
}

bool NoiseSuppressionEffect::Instance::RealtimeInitialize(EffectSettings&, double, size_t)
{
    SetBlockSize(512);
    m_processors.clear(); // a new processing scope (play, seek, loop) starts from a clean state
    return true;
}

bool NoiseSuppressionEffect::Instance::RealtimeAddProcessor(EffectSettings&, EffectOutputs*, unsigned, float sampleRate)
{
    m_processors.push_back(std::make_unique<denoise::StreamSuppressor>(sampleRate));
    return true;
}

size_t NoiseSuppressionEffect::Instance::RealtimeProcess(size_t group, EffectSettings& settings,
                                                         const float* const* inBuf, float* const* outBuf,
                                                         size_t numSamples)
{
    if (group >= m_processors.size()) {
        return 0;
    }
    const double mix = NoiseSuppressionEffect::GetSettings(settings).mMixPercent / 100.0;
    m_processors[group]->process(inBuf[0], outBuf[0], numSamples, mix);
    return numSamples;
}

bool NoiseSuppressionEffect::Instance::RealtimeFinalize(EffectSettings&) noexcept
{
    m_processors.clear();
    return true;
}

BreathRemovalEffect::Instance::Instance(BreathRemovalEffect& effect)
    : StatefulEffect::Instance(effect) {}

size_t BreathRemovalEffect::Instance::SetBlockSize(size_t maxBlockSize)
{
    m_blockSize = std::clamp<size_t>(maxBlockSize, 1, denoise::MAX_BLOCK);
    return m_blockSize;
}

::EffectInstance::SampleCount BreathRemovalEffect::Instance::GetLatency(const EffectSettings&, double sampleRate) const
{
    return m_processors.empty() ? 0 : denoise::StreamBreathGate::latencyFor(sampleRate);
}

bool BreathRemovalEffect::Instance::RealtimeInitialize(EffectSettings&, double, size_t)
{
    SetBlockSize(512);
    m_processors.clear();
    return true;
}

bool BreathRemovalEffect::Instance::RealtimeAddProcessor(EffectSettings&, EffectOutputs*, unsigned, float sampleRate)
{
    // the host always hands us two channel buffers (a mono track's are the same), so detect on
    // the mix of both and apply one gain to both
    m_processors.push_back(std::make_unique<denoise::StreamBreathGate>(sampleRate, 2));
    return true;
}

size_t BreathRemovalEffect::Instance::RealtimeProcess(size_t group, EffectSettings& settings, const float* const* inBuf,
                                                      float* const* outBuf, size_t numSamples)
{
    if (group >= m_processors.size()) {
        return 0;
    }
    const denoise::BreathSettings s = BreathRemovalEffect::GetSettings(settings).toCore();
    m_processors[group]->process(inBuf, outBuf, numSamples, s);
    return numSamples;
}

bool BreathRemovalEffect::Instance::RealtimeFinalize(EffectSettings&) noexcept
{
    m_processors.clear();
    return true;
}
}
