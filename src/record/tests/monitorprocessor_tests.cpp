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
