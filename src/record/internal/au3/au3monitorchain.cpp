/*
* Audacity: A Digital Audio Editor
*/
#include "au3monitorchain.h"

#include <algorithm>

#include "au3-components/EffectInterface.h"
#include "au3-module-manager/PluginManager.h"
#include "au3-realtime-effects/RealtimeEffectList.h"
#include "au3-realtime-effects/RealtimeEffectState.h"
#include "au3-wave-track/WaveTrack.h"

namespace au::record::monitor {
namespace {
//! A realtime effect instance owned by the monitor path. Mono effects get one processor per
//! channel, multichannel effects one processor for both.
class Au3MonitorEffect final : public MonitorEffect
{
public:
    Au3MonitorEffect(std::string name, std::shared_ptr<EffectInstance> instance, EffectSettings settings, double rate)
        : m_name(std::move(name)), m_instance(std::move(instance)), m_settings(std::move(settings)),
        m_outL(MonitorProcessor::MAX_BLOCK), m_outR(MonitorProcessor::MAX_BLOCK), m_silence(MonitorProcessor::MAX_BLOCK, 0.0f),
        m_dummy(MonitorProcessor::MAX_BLOCK)
    {
        m_instance->SetBlockSize(512);
        m_ok = m_instance->RealtimeInitialize(m_settings, rate, MonitorProcessor::MAX_BLOCK);
        m_in = m_instance->GetAudioInCount();
        m_out = m_instance->GetAudioOutCount();
        if (m_ok) {
            m_processors = m_in >= 2 ? 1 : 2;
            for (unsigned p = 0; p < m_processors && m_ok; ++p) {
                m_ok = m_instance->RealtimeAddProcessor(m_settings, nullptr, std::max(1u, std::min(m_in, 2u)), rate);
            }
        }
        if (m_ok) {
            // one silent block so effects that measure latency on first use report it
            float l[512] = {}, r[512] = {};
            float* chans[2] { l, r };
            process(chans, 512);
            m_latency = double(m_instance->GetLatency(m_settings, rate)) / rate;
        }
    }

    ~Au3MonitorEffect() override
    {
        if (m_ok) {
            m_instance->RealtimeFinalize(m_settings);
        }
    }

    bool ok() const { return m_ok && m_in >= 1 && m_out >= 1; }
    std::string name() const override { return m_name; }
    double latencySeconds() const override { return m_latency; }

    void process(float* const* chans, size_t frames) noexcept override
    {
        if (!m_ok) {
            return;
        }
        EffectInstance::MessagePackage package { m_settings, nullptr };
        m_instance->RealtimeProcessStart(package);
        const size_t block = std::max<size_t>(1, m_instance->GetBlockSize());
        for (unsigned p = 0; p < m_processors; ++p) {
            const float* in[2];
            float* out[2];
            if (m_processors == 1) {
                in[0] = chans[0];
                in[1] = chans[1];
                out[0] = m_outL.data();
                out[1] = m_out >= 2 ? m_outR.data() : m_dummy.data();
            } else {
                in[0] = in[1] = chans[p];
                out[0] = p == 0 ? m_outL.data() : m_outR.data();
                out[1] = m_dummy.data();
            }
            for (size_t done = 0; done < frames; done += block) {
                const size_t n = std::min(block, frames - done);
                const float* inb[2] { in[0] + done, in[1] + done };
                float* outb[2] { out[0] + done, out[1] + done };
                m_instance->RealtimeProcess(p, m_settings, inb, outb, n);
            }
        }
        m_instance->RealtimeProcessEnd(m_settings);
        std::copy(m_outL.begin(), m_outL.begin() + frames, chans[0]);
        if (m_processors == 2 || m_out >= 2) {
            std::copy(m_outR.begin(), m_outR.begin() + frames, chans[1]);
        } else {
            // mono-out effect on a stereo pair: both sides hear the processed left
            std::copy(m_outL.begin(), m_outL.begin() + frames, chans[1]);
        }
    }

private:
    std::string m_name;
    std::shared_ptr<EffectInstance> m_instance;
    EffectSettings m_settings;
    std::vector<float> m_outL, m_outR, m_silence, m_dummy;
    unsigned m_in = 0, m_out = 0, m_processors = 0;
    bool m_ok = false;
    double m_latency = 0.0;
};

void collect(RealtimeEffectList& list, bool master, double rate,
             std::vector<std::unique_ptr<Au3MonitorEffect> >& effects, std::vector<EffectInfo>& infos)
{
    if (!list.IsActive()) {
        return;
    }
    for (size_t i = 0; i < list.GetStatesCount(); ++i) {
        auto state = list.GetStateAt(i);
        if (!state || !state->IsActive()) {
            continue;
        }
        const EffectInstanceFactory* factory = state->GetEffect();
        if (!factory) {
            continue;
        }
        auto instance = factory->MakeInstance();
        if (!instance) {
            continue;
        }
        std::string name = PluginManager::Get().GetName(state->GetID()).Translation().ToStdString();
        if (name.empty()) { // built-in AU4 effects are not always registered under a display name
            name = state->GetEffect()->GetSymbol().Translation().ToStdString();
        }
        if (name.empty()) {
            name = state->GetID().ToStdString();
        }
        auto effect = std::make_unique<Au3MonitorEffect>(name, instance, state->GetSettings(), rate);
        if (!effect->ok()) {
            continue;
        }
        infos.push_back({ name, effect->latencySeconds(), master });
        effects.push_back(std::move(effect));
    }
}
}

BuiltChain buildMonitorChain(AudacityProject& project, WaveTrack* track, double sampleRate, bool lowLatency,
                             double thresholdSeconds)
{
    std::vector<std::unique_ptr<Au3MonitorEffect> > effects;
    std::vector<EffectInfo> infos;
    if (track) {
        collect(RealtimeEffectList::Get(*track), false, sampleRate, effects, infos);
    }
    collect(RealtimeEffectList::Get(project), true, sampleRate, effects, infos);

    const ChainPlan plan = planChain(infos, lowLatency, thresholdSeconds);
    BuiltChain built;
    built.chain = std::make_unique<MonitorChain>();
    for (size_t i : plan.used) {
        built.used.push_back(infos[i].name);
        built.chain->effects.push_back(std::move(effects[i]));
    }
    built.bypassed = plan.bypassed;
    built.effectLatencySeconds = plan.effectLatencySeconds;
    return built;
}
}
