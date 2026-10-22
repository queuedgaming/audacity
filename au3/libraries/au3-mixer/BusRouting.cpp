/*
* Audacity: A Digital Audio Editor
*/
#include "BusRouting.h"

#include <algorithm>
#include <cassert>
#include <numeric>

#include "au3-math/SampleCount.h"
#include "DownmixSource.h"
#include "WideSampleSequence.h"

namespace {
BusRoutingPlanProvider& provider()
{
    static BusRoutingPlanProvider p;
    return p;
}
}

bool BusRoutingPlan::Active() const
{
    return std::any_of(nodes.begin(), nodes.end(), [](const Node& n) {
        return n.isBus || n.target >= 0 || n.muted || !n.sends.empty();
    });
}

std::vector<int> BusRoutingPlan::ProcessingOrder() const
{
    std::vector<int> order;
    order.reserve(nodes.size());
    for (int i = 0; i < int(nodes.size()); ++i) {
        if (!nodes[size_t(i)].isBus) {
            order.push_back(i);
        }
    }
    order.insert(order.end(), busOrder.begin(), busOrder.end());
    return order;
}

void SetBusRoutingPlanProvider(BusRoutingPlanProvider p)
{
    provider() = std::move(p);
}

BusRoutingPlan MakeBusRoutingPlan(const std::vector<const WideSampleSequence*>& sequences)
{
    BusRoutingPlan plan;
    if (provider()) {
        plan = provider()(sequences);
    }
    if (plan.nodes.size() != sequences.size()) {
        plan = {};
        plan.nodes.resize(sequences.size());
    }
    return plan;
}

void BusRouteMix(const WideSampleSequence& seq, bool applyVolume,
                 const float* const* src, unsigned srcCh,
                 float* const* dest, unsigned destCh,
                 float level, size_t len) noexcept
{
    if (srcCh == 0 || destCh == 0 || len == 0) {
        return;
    }
    auto vol = [&](unsigned c) { return applyVolume ? seq.GetChannelVolume(int(c)) : 1.0f; };
    if (destCh == 1) {
        for (unsigned j = 0; j < srcCh; ++j) {
            const float g = vol(j) * level / float(srcCh);
            for (size_t i = 0; i < len; ++i) {
                dest[0][i] += src[j][i] * g;
            }
        }
        return;
    }
    if (srcCh == 1) {
        for (unsigned n = 0; n < destCh; ++n) {
            const float g = vol(n) * level;
            for (size_t i = 0; i < len; ++i) {
                dest[n][i] += src[0][i] * g;
            }
        }
        return;
    }
    for (unsigned n = 0, cnt = std::min(srcCh, destCh); n < cnt; ++n) {
        const float g = vol(n) * level;
        for (size_t i = 0; i < len; ++i) {
            dest[n][i] += src[n][i] * g;
        }
    }
}

void BusRouteBlock(const BusRoutingPlan& plan, const std::vector<int>& order,
                   const WideSampleSequence* const* seqs, float* const* channels,
                   const unsigned* first, const unsigned* nChannels,
                   float* const* master, unsigned masterCh, size_t len,
                   BusProcessFn processBus, void* ctx) noexcept
{
    for (const int idx : order) {
        const auto i = size_t(idx);
        const auto* seq = seqs[i];
        if (!seq || nChannels[i] == 0) {
            continue;
        }
        const auto& node = plan.nodes[i];
        float* const* src = channels + first[i];
        const unsigned srcCh = std::min(nChannels[i], 2u);
        if (node.isBus && processBus) {
            processBus(ctx, idx, src, nChannels[i], len);
        }
        auto route = [&](int dest, float level) {
            if (dest < 0) {
                BusRouteMix(*seq, true, src, srcCh, master, masterCh, level, len);
            } else {
                BusRouteMix(*seq, true, src, srcCh, channels + first[size_t(dest)],
                            nChannels[size_t(dest)], level, len);
            }
        };
        if (!node.muted) {
            route(node.target, 1.0f);
        }
        for (const auto& send : node.sends) {
            route(send.dest, send.level);
        }
    }
}

// ---- BusInjectStage ------------------------------------------------------------------
BusInjectStage::BusInjectStage(AudioGraph::Source& upstream, size_t nChannels, size_t bufferSize)
    : mUpstream{upstream}
    , mInput(std::max<size_t>(1, nChannels), std::vector<float>(bufferSize, 0.0f))
{
}

void BusInjectStage::ClearInput(size_t len)
{
    mConsumed = false;
    for (auto& ch : mInput) {
        std::fill_n(ch.data(), std::min(len, ch.size()), 0.0f);
    }
}

bool BusInjectStage::AcceptsBuffers(const Buffers& buffers) const
{
    return mUpstream.AcceptsBuffers(buffers);
}

bool BusInjectStage::AcceptsBlockSize(size_t blockSize) const
{
    return blockSize <= mInput[0].size() && mUpstream.AcceptsBlockSize(blockSize);
}

std::optional<size_t> BusInjectStage::Acquire(Buffers& data, size_t bound)
{
    auto result = mUpstream.Acquire(data, bound);
    if (!result || mConsumed) {
        // a second pull within one block (e.g. an effect discarding its latency) gets silence
        return result;
    }
    mConsumed = true;
    const size_t n = std::min(*result, mInput[0].size());
    for (size_t c = 0, cnt = std::min<size_t>(data.Channels(), mInput.size()); c < cnt; ++c) {
        auto* p = &data.GetWritePosition(c);
        const auto* in = mInput[c].data();
        for (size_t i = 0; i < n; ++i) {
            p[i] += in[i];
        }
    }
    return result;
}

sampleCount BusInjectStage::Remaining() const
{
    return mUpstream.Remaining();
}

bool BusInjectStage::Release()
{
    return mUpstream.Release();
}

// ---- RoutedDownmixStage ----------------------------------------------------------------
RoutedDownmixStage::RoutedDownmixStage(std::vector<Input> inputs, BusRoutingPlan plan,
                                       size_t numChannels, size_t bufferSize, bool applyVolume)
    : mInputs{std::move(inputs)}
    , mPlan{std::move(plan)}
    , mOrder{mPlan.ProcessingOrder()}
    , mFloatBuffers{3, bufferSize, 1, 1}
    , mNumChannels{numChannels}
    , mApplyVolume{applyVolume}
{
    assert(mPlan.nodes.size() == mInputs.size());
}

RoutedDownmixStage::~RoutedDownmixStage() = default;

bool RoutedDownmixStage::AcceptsBuffers(const Buffers& buffers) const
{
    return buffers.Channels() == mNumChannels && AcceptsBlockSize(buffers.BlockSize());
}

bool RoutedDownmixStage::AcceptsBlockSize(size_t blockSize) const
{
    return blockSize <= mFloatBuffers.BufferSize();
}

std::optional<size_t> RoutedDownmixStage::Acquire(Buffers& data, size_t maxToProcess)
{
    for (unsigned c = 0; c < data.Channels(); ++c) {
        data.ClearBuffer(c, maxToProcess);
    }
    for (auto& in : mInputs) {
        if (in.inject) {
            in.inject->ClearInput(maxToProcess);
        }
    }

    float* masterPtrs[8] {};
    const unsigned masterCh = std::min<unsigned>(unsigned(mNumChannels), 8);
    for (unsigned c = 0; c < masterCh; ++c) {
        masterPtrs[c] = &data.GetWritePosition(c);
    }

    size_t maxOut = 0;
    for (const int idx : mOrder) {
        auto& in = mInputs[size_t(idx)];
        const auto& node = mPlan.nodes[size_t(idx)];
        auto& downstream = in.source->GetDownstream();
        auto oResult = downstream.Acquire(mFloatBuffers, maxToProcess);
        if (!oResult) {
            return 0;
        }
        const auto result = *oResult;
        maxOut = std::max(maxOut, result);

        const unsigned srcCh = unsigned(std::min<size_t>(in.source->NChannels(), 2));
        const float* srcPtrs[2] {};
        for (unsigned j = 0; j < srcCh; ++j) {
            srcPtrs[j] = reinterpret_cast<const float*>(mFloatBuffers.GetReadPosition(j));
        }

        // The DownmixSource of a sequence exposes the sequence's gains through GetChannelGain
        auto route = [&](int destIdx, float level) {
            float* destPtrs[8] {};
            unsigned destCh = 0;
            if (destIdx < 0) {
                destCh = masterCh;
                std::copy_n(masterPtrs, destCh, destPtrs);
            } else {
                auto* inject = mInputs[size_t(destIdx)].inject;
                if (!inject) {
                    return;
                }
                destCh = unsigned(std::min<size_t>(inject->InputChannels(), 8));
                for (unsigned c = 0; c < destCh; ++c) {
                    destPtrs[c] = inject->Input(c);
                }
            }
            if (destCh == 1) {
                for (unsigned j = 0; j < srcCh; ++j) {
                    const float g = (mApplyVolume ? in.source->GetChannelGain(j) : 1.0f) * level / float(srcCh);
                    for (size_t i = 0; i < result; ++i) {
                        destPtrs[0][i] += srcPtrs[j][i] * g;
                    }
                }
            } else if (srcCh == 1) {
                for (unsigned n = 0; n < destCh; ++n) {
                    const float g = (mApplyVolume ? in.source->GetChannelGain(n) : 1.0f) * level;
                    for (size_t i = 0; i < result; ++i) {
                        destPtrs[n][i] += srcPtrs[0][i] * g;
                    }
                }
            } else {
                for (unsigned n = 0, cnt = std::min(srcCh, destCh); n < cnt; ++n) {
                    const float g = (mApplyVolume ? in.source->GetChannelGain(n) : 1.0f) * level;
                    for (size_t i = 0; i < result; ++i) {
                        destPtrs[n][i] += srcPtrs[n][i] * g;
                    }
                }
            }
        };
        if (!node.muted) {
            route(node.target, 1.0f);
        }
        for (const auto& send : node.sends) {
            route(send.dest, send.level);
        }

        downstream.Release();
        mFloatBuffers.Advance(result);
        mFloatBuffers.Rotate();
    }
    assert(maxOut <= maxToProcess);
    return maxOut;
}

sampleCount RoutedDownmixStage::Remaining() const
{
    return std::accumulate(mInputs.begin(), mInputs.end(), sampleCount { 0 },
                           [](sampleCount sum, const Input& in) {
        return std::max(sum, in.source->GetDownstream().Remaining());
    });
}

bool RoutedDownmixStage::Release()
{
    return true;
}
