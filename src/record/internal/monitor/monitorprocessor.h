/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "au3-audio-io/PlaythroughProcessor.h"

namespace au::record::monitor {
//! One effect of the monitoring chain. process() runs on the audio thread: realtime safe,
//! in place, two planar channels, at most MonitorProcessor::MAX_BLOCK frames.
class MonitorEffect
{
public:
    virtual ~MonitorEffect() = default;
    virtual std::string name() const = 0;
    virtual double latencySeconds() const = 0;
    virtual void process(float* const* channels, size_t frames) noexcept = 0;
};

//! What an effect of the target track / master stack looks like when planning the chain
struct EffectInfo {
    std::string name;
    double latencySeconds = 0.0;
    bool master = false;
};

//! Which effects are heard while monitoring
struct ChainPlan {
    std::vector<size_t> used;         //!< indices into the planned effects, in processing order
    std::vector<std::string> bypassed; //!< names skipped by low-latency monitoring
    double effectLatencySeconds = 0.0; //!< sum over the used effects
};

//! Low-latency monitoring skips every effect whose own latency is above `thresholdSeconds`
ChainPlan planChain(const std::vector<EffectInfo>& effects, bool lowLatency, double thresholdSeconds);

//! Device part of the monitoring latency: input + output buffering. Uses the measured stream
//! latencies when a stream is open (> 0), else twice the configured buffer length.
double deviceLatencySeconds(double bufferLengthMs, int hwCaptureMs, int hwPlaybackMs);

//! A built chain, owned by the processor while installed
struct MonitorChain {
    std::vector<std::unique_ptr<MonitorEffect> > effects;
};

//! Processes the software playthrough signal: effects of the record target (+ master), then the
//! monitor gain / mute. The chain is swapped atomically from the main thread; gain and mute are
//! atomics. Nothing in process allocates or locks.
class MonitorProcessor final : public PlaythroughProcessor
{
public:
    static constexpr size_t MAX_BLOCK = 1024;

    MonitorProcessor();
    ~MonitorProcessor() override;

    //! main thread: install a new chain (nullptr = dry). Waits for a running callback to finish
    //! before destroying the previous chain.
    void setChain(std::unique_ptr<MonitorChain> chain);
    void setGainDb(double db);
    void setMuted(bool muted);
    void setEffectsEnabled(bool enabled);

    double gainDb() const { return m_gainDb.load(); }
    bool muted() const { return m_muted.load(); }

    void ProcessPlaythrough(float* interleavedStereo, size_t frames) noexcept override;

private:
    std::atomic<MonitorChain*> m_chain{ nullptr };
    std::atomic<bool> m_processing{ false };
    std::atomic<double> m_gainDb{ 0.0 };
    std::atomic<bool> m_muted{ false };
    std::atomic<bool> m_effectsEnabled{ true };
    float m_currentGain = 1.0f; //!< audio thread only: ramps toward the target
    std::vector<float> m_left, m_right;
};
}
