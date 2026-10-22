/*
* Audacity: A Digital Audio Editor
*/
#include "monitorprocessor.h"

#include <algorithm>
#include <cmath>
#include <thread>

namespace au::record::monitor {
ChainPlan planChain(const std::vector<EffectInfo>& effects, bool lowLatency, double thresholdSeconds)
{
    ChainPlan plan;
    for (size_t i = 0; i < effects.size(); ++i) {
        if (lowLatency && effects[i].latencySeconds > thresholdSeconds + 1e-12) {
            plan.bypassed.push_back(effects[i].name);
            continue;
        }
        plan.used.push_back(i);
        plan.effectLatencySeconds += effects[i].latencySeconds;
    }
    return plan;
}

double deviceLatencySeconds(double bufferLengthMs, int hwCaptureMs, int hwPlaybackMs)
{
    if (hwCaptureMs > 0 || hwPlaybackMs > 0) {
        return (std::max(hwCaptureMs, 0) + std::max(hwPlaybackMs, 0)) / 1000.0;
    }
    return 2.0 * std::max(bufferLengthMs, 0.0) / 1000.0;
}

MonitorProcessor::MonitorProcessor()
    : m_left(MAX_BLOCK), m_right(MAX_BLOCK)
{
}

MonitorProcessor::~MonitorProcessor()
{
    setChain(nullptr);
}

void MonitorProcessor::setChain(std::unique_ptr<MonitorChain> chain)
{
    MonitorChain* old = m_chain.exchange(chain.release(), std::memory_order_acq_rel);
    // a callback that loaded the old pointer sets m_processing before loading: once it is clear,
    // nobody uses `old` any more
    while (m_processing.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    delete old;
}

void MonitorProcessor::setGainDb(double db)
{
    m_gainDb.store(std::clamp(db, -60.0, 12.0));
}

void MonitorProcessor::setMuted(bool muted)
{
    m_muted.store(muted);
}

void MonitorProcessor::setEffectsEnabled(bool enabled)
{
    m_effectsEnabled.store(enabled);
}

void MonitorProcessor::ProcessPlaythrough(float* io, size_t frames) noexcept
{
    m_processing.store(true, std::memory_order_seq_cst);
    MonitorChain* chain = m_effectsEnabled.load(std::memory_order_relaxed)
                          ? m_chain.load(std::memory_order_seq_cst) : nullptr;
    const float target = m_muted.load(std::memory_order_relaxed)
                         ? 0.0f : float(std::pow(10.0, m_gainDb.load(std::memory_order_relaxed) / 20.0));

    float* chans[2] { m_left.data(), m_right.data() };
    for (size_t done = 0; done < frames;) {
        const size_t n = std::min(MAX_BLOCK, frames - done);
        float* block = io + 2 * done;
        for (size_t i = 0; i < n; ++i) {
            m_left[i] = block[2 * i];
            m_right[i] = block[2 * i + 1];
        }
        if (chain) {
            for (auto& effect : chain->effects) {
                effect->process(chans, n);
            }
        }
        // linear ramp to the new gain over the block: no zipper noise on level/mute changes
        const float g0 = m_currentGain;
        const float step = (target - g0) / float(n);
        for (size_t i = 0; i < n; ++i) {
            const float g = g0 + step * float(i + 1);
            block[2 * i] = m_left[i] * g;
            block[2 * i + 1] = m_right[i] * g;
        }
        m_currentGain = target;
        done += n;
    }
    m_processing.store(false, std::memory_order_release);
}
}
