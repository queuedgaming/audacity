/*
* Audacity: A Digital Audio Editor
*/
#include <gtest/gtest.h>

#include <cmath>
#include <numeric>

#include "internal/monitor/monitorprocessor.h"

using namespace au::record::monitor;

namespace {
//! Scales left/right independently and records the block sizes it sees
class ScaleEffect : public MonitorEffect
{
public:
    ScaleEffect(float l, float r, double lat = 0.0, std::vector<size_t>* blocks = nullptr)
        : m_l(l), m_r(r), m_lat(lat), m_blocks(blocks) {}
    std::string name() const override { return "scale"; }
    double latencySeconds() const override { return m_lat; }
    void process(float* const* ch, size_t n) noexcept override
    {
        if (m_blocks) {
            m_blocks->push_back(n);
        }
        for (size_t i = 0; i < n; ++i) {
            ch[0][i] *= m_l;
            ch[1][i] *= m_r;
        }
    }

private:
    float m_l, m_r;
    double m_lat;
    std::vector<size_t>* m_blocks;
};

class AddEffect : public MonitorEffect
{
public:
    explicit AddEffect(float v) : m_v(v) {}
    std::string name() const override { return "add"; }
    double latencySeconds() const override { return 0; }
    void process(float* const* ch, size_t n) noexcept override
    {
        for (size_t i = 0; i < n; ++i) {
            ch[0][i] += m_v;
            ch[1][i] += m_v;
        }
    }

private:
    float m_v;
};

std::vector<float> ramp(size_t frames)
{
    std::vector<float> v(frames * 2);
    for (size_t i = 0; i < frames; ++i) {
        v[2 * i] = 0.001f * float(i % 500);
        v[2 * i + 1] = -0.001f * float(i % 500);
    }
    return v;
}

std::unique_ptr<MonitorChain> chainOf(std::unique_ptr<MonitorEffect> a, std::unique_ptr<MonitorEffect> b = nullptr)
{
    auto c = std::make_unique<MonitorChain>();
    c->effects.push_back(std::move(a));
    if (b) {
        c->effects.push_back(std::move(b));
    }
    return c;
}
}

TEST(MonitorProcessorTests, DryUnityIsTransparent)
{
    MonitorProcessor p;
    auto buf = ramp(300);
    const auto ref = buf;
    p.ProcessPlaythrough(buf.data(), 300);
    EXPECT_EQ(buf, ref);
}

TEST(MonitorProcessorTests, AppliesEffectStackInOrderPerChannel)
{
    MonitorProcessor p;
    // (x * 2 + 0.5) for left; (x * 3 + 0.5) for right: order matters
    p.setChain(chainOf(std::make_unique<ScaleEffect>(2.f, 3.f), std::make_unique<AddEffect>(0.5f)));
    auto buf = ramp(256);
    const auto in = buf;
    p.ProcessPlaythrough(buf.data(), 256);
    for (size_t i = 0; i < 256; ++i) {
        EXPECT_FLOAT_EQ(buf[2 * i], in[2 * i] * 2.f + 0.5f);
        EXPECT_FLOAT_EQ(buf[2 * i + 1], in[2 * i + 1] * 3.f + 0.5f);
    }
}

TEST(MonitorProcessorTests, RecordedCaptureBufferStaysDry)
{
    // AudioIO hands the processor a copy of the input (the playthrough output buffer); the
    // capture buffer that is written to the track is a different buffer and must stay raw.
    MonitorProcessor p;
    p.setChain(chainOf(std::make_unique<ScaleEffect>(0.f, 0.f)));
    const auto capture = ramp(128);
    auto playthrough = capture;
    const auto captureBefore = capture;
    p.ProcessPlaythrough(playthrough.data(), 128);
    EXPECT_EQ(capture, captureBefore);
    EXPECT_FLOAT_EQ(std::accumulate(playthrough.begin(), playthrough.end(), 0.f), 0.f);
}

TEST(MonitorProcessorTests, EffectsDisabledMeansDry)
{
    MonitorProcessor p;
    p.setChain(chainOf(std::make_unique<ScaleEffect>(0.f, 0.f)));
    p.setEffectsEnabled(false);
    auto buf = ramp(64);
    const auto ref = buf;
    p.ProcessPlaythrough(buf.data(), 64);
    EXPECT_EQ(buf, ref);
}

TEST(MonitorProcessorTests, LargeAndOddBlocksAreChunked)
{
    std::vector<size_t> blocks;
    MonitorProcessor p;
    p.setChain(chainOf(std::make_unique<ScaleEffect>(2.f, 2.f, 0.0, &blocks)));
    for (size_t frames : { size_t(1), size_t(7), size_t(1023), size_t(1024), size_t(1025), size_t(4099) }) {
        blocks.clear();
        auto buf = ramp(frames);
        const auto in = buf;
        p.ProcessPlaythrough(buf.data(), frames);
        size_t total = 0;
        for (auto b : blocks) {
            EXPECT_LE(b, MonitorProcessor::MAX_BLOCK);
            total += b;
        }
        EXPECT_EQ(total, frames);
        for (size_t i = 0; i < buf.size(); ++i) {
            ASSERT_FLOAT_EQ(buf[i], in[i] * 2.f) << "frames " << frames << " i " << i;
        }
    }
    blocks.clear();
    float dummy[2] = { 1.f, 1.f };
    p.ProcessPlaythrough(dummy, 0);
    EXPECT_TRUE(blocks.empty());
}

TEST(MonitorProcessorTests, GainRampsToTargetAndClamps)
{
    MonitorProcessor p;
    p.setGainDb(-6.0);
    std::vector<float> buf(2 * 512, 1.f);
    p.ProcessPlaythrough(buf.data(), 512);
    const float target = std::pow(10.f, -6.f / 20.f);
    EXPECT_LT(buf[0], 1.f + 1e-6f);
    EXPECT_GT(buf[0], target);                       // first sample still near unity (no click)
    EXPECT_NEAR(buf[2 * 511], target, 1e-3f);         // ramp completed within the block
    std::fill(buf.begin(), buf.end(), 1.f);
    p.ProcessPlaythrough(buf.data(), 512);
    EXPECT_NEAR(buf[0], target, 1e-4f);               // steady state

    p.setGainDb(100.0);
    EXPECT_DOUBLE_EQ(p.gainDb(), 12.0);
    p.setGainDb(-500.0);
    EXPECT_DOUBLE_EQ(p.gainDb(), -60.0);
}

TEST(MonitorProcessorTests, MuteSilencesAndUnmuteRestores)
{
    MonitorProcessor p;
    p.setMuted(true);
    std::vector<float> buf(2 * 256, 0.5f);
    p.ProcessPlaythrough(buf.data(), 256); // ramp down
    std::fill(buf.begin(), buf.end(), 0.5f);
    p.ProcessPlaythrough(buf.data(), 256);
    for (float s : buf) {
        EXPECT_FLOAT_EQ(s, 0.f);
    }
    p.setMuted(false);
    p.ProcessPlaythrough(buf.data(), 256); // ramp up
    std::fill(buf.begin(), buf.end(), 0.5f);
    p.ProcessPlaythrough(buf.data(), 256);
    for (float s : buf) {
        EXPECT_FLOAT_EQ(s, 0.5f);
    }
}

TEST(MonitorProcessorTests, ChainSwapAndClear)
{
    MonitorProcessor p;
    p.setChain(chainOf(std::make_unique<ScaleEffect>(2.f, 2.f)));
    p.setChain(chainOf(std::make_unique<ScaleEffect>(4.f, 4.f)));
    std::vector<float> buf(4, 1.f);
    p.ProcessPlaythrough(buf.data(), 2);
    EXPECT_FLOAT_EQ(buf[0], 4.f);
    p.setChain(nullptr);
    std::fill(buf.begin(), buf.end(), 1.f);
    p.ProcessPlaythrough(buf.data(), 2);
    EXPECT_FLOAT_EQ(buf[0], 1.f);
}

TEST(MonitorPlanTests, LowLatencyBypassesSlowEffects)
{
    const std::vector<EffectInfo> fx {
        { "EQ", 0.0, false }, { "Breath removal", 0.230, false }, { "Compressor", 0.005, false },
        { "Limiter", 0.050, true } };
    auto plan = planChain(fx, false, 0.020);
    EXPECT_EQ(plan.used, (std::vector<size_t> { 0, 1, 2, 3 }));
    EXPECT_TRUE(plan.bypassed.empty());
    EXPECT_NEAR(plan.effectLatencySeconds, 0.285, 1e-9);

    plan = planChain(fx, true, 0.020);
    EXPECT_EQ(plan.used, (std::vector<size_t> { 0, 2 }));
    EXPECT_EQ(plan.bypassed, (std::vector<std::string> { "Breath removal", "Limiter" }));
    EXPECT_NEAR(plan.effectLatencySeconds, 0.005, 1e-9);

    plan = planChain(fx, true, 0.005); // equal to threshold is kept
    EXPECT_EQ(plan.used, (std::vector<size_t> { 0, 2 }));
}

TEST(MonitorPlanTests, DeviceLatency)
{
    EXPECT_NEAR(deviceLatencySeconds(100.0, 0, 0), 0.200, 1e-9);
    EXPECT_NEAR(deviceLatencySeconds(100.0, 12, 15), 0.027, 1e-9);
}

// ---- live settings updates -------------------------------------------------------------

namespace {
struct Counted {
    static inline int alive = 0;
    int value = 0;
    explicit Counted(int v = 0) : value(v) { ++alive; }
    Counted(const Counted& o) : value(o.value) { ++alive; }
    Counted& operator=(const Counted&) = default;
    ~Counted() { --alive; }
};

//! Gain effect whose gain is a "setting" mirrored from a main-thread source like Au3MonitorEffect
class MirroredGainEffect : public MonitorEffect
{
public:
    explicit MirroredGainEffect(const float* source) : m_source(source) {}
    std::string name() const override { return "gain"; }
    double latencySeconds() const override { return 0; }
    void syncSettings() override { ++syncs; m_mailbox.post(std::make_unique<float>(*m_source)); }
    void process(float* const* ch, size_t n) noexcept override
    {
        m_mailbox.takeInto(m_gain);
        for (size_t i = 0; i < n; ++i) {
            ch[0][i] *= m_gain;
            ch[1][i] *= m_gain;
        }
    }

    int syncs = 0;

private:
    const float* m_source;
    float m_gain = 1.f;
    SettingsMailbox<float> m_mailbox;
};
}

TEST(SettingsMailboxTests, HandsOverLatestValueAndFreesOldOnes)
{
    Counted::alive = 0;
    {
        SettingsMailbox<Counted> box;
        Counted current(1);
        EXPECT_FALSE(box.takeInto(current));

        box.post(std::make_unique<Counted>(2));
        box.post(std::make_unique<Counted>(3)); // replaces the untaken 2
        EXPECT_EQ(Counted::alive, 2);
        EXPECT_TRUE(box.takeInto(current));
        EXPECT_EQ(current.value, 3);
        EXPECT_FALSE(box.takeInto(current)); // nothing new

        // the parked old value blocks further swaps until the main thread reclaims it
        box.post(std::make_unique<Counted>(4)); // post reclaims the parked one first
        EXPECT_TRUE(box.takeInto(current));
        EXPECT_EQ(current.value, 4);
        box.post(std::make_unique<Counted>(5));
        EXPECT_TRUE(box.takeInto(current));
        box.post(std::make_unique<Counted>(6)); // parked 4 freed here
        EXPECT_TRUE(box.takeInto(current));
        EXPECT_EQ(current.value, 6);
    }
    EXPECT_EQ(Counted::alive, 0); // destructor frees pending + parked
}

TEST(SettingsMailboxTests, AudioSideWaitsForReclaim)
{
    SettingsMailbox<int> box;
    int current = 0;
    box.post(std::make_unique<int>(1));
    EXPECT_TRUE(box.takeInto(current)); // old value parked
    // simulate a post that raced in before reclaim: only possible through post(), which
    // reclaims, so the audio side can always take the newest value on the next block
    box.post(std::make_unique<int>(2));
    EXPECT_TRUE(box.takeInto(current));
    EXPECT_EQ(current, 2);
}

TEST(MonitorProcessorTests, SyncSettingsUpdatesInstalledChainWithoutRebuild)
{
    float sourceGain = 1.f;
    auto chain = std::make_unique<MonitorChain>();
    auto effect = std::make_unique<MirroredGainEffect>(&sourceGain);
    auto* raw = effect.get();
    chain->effects.push_back(std::move(effect));
    MonitorProcessor p;
    p.setChain(std::move(chain));

    std::vector<float> buf(2 * 64, 1.f);
    p.ProcessPlaythrough(buf.data(), 64);
    EXPECT_FLOAT_EQ(buf[0], 1.f);

    sourceGain = 0.25f; // user drags a slider of the track's effect
    p.syncSettings();
    EXPECT_EQ(raw->syncs, 1);
    std::fill(buf.begin(), buf.end(), 1.f);
    p.ProcessPlaythrough(buf.data(), 64);
    EXPECT_FLOAT_EQ(buf[0], 0.25f);
    EXPECT_EQ(p.chainSize(), 1u); // same chain, no rebuild

    p.setChain(nullptr);
    p.syncSettings(); // no chain: no-op
    EXPECT_EQ(p.chainSize(), 0u);
}
