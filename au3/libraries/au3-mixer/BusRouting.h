/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "au3-audio-graph/AudioGraphBuffers.h"
#include "au3-audio-graph/AudioGraphSource.h"

class WideSampleSequence;

//! Signal routing between the sequences of one playback or export mix.
//! Built on the main thread before mixing starts; read-only while mixing.
struct MIXER_API BusRoutingPlan
{
    struct Send {
        int dest = -1;     //!< index of a bus node
        float level = 1.0f; //!< linear gain, applied on top of the source's post-fader output
    };
    struct Node {
        bool isBus = false;
        int target = -1;   //!< index of the bus node the output goes to; -1 = master
        bool muted = false; //!< output goes to a bus that is not part of this mix and is muted
        std::vector<Send> sends;
    };

    std::vector<Node> nodes;   //!< one per sequence, in the order of the sequences
    std::vector<int> busOrder; //!< bus node indices, each after all buses feeding it

    //! True when anything differs from "every sequence straight to master"
    bool Active() const;

    //! Indices in processing order: all non-bus nodes, then busOrder
    std::vector<int> ProcessingOrder() const;
};

using BusRoutingPlanProvider
    = std::function<BusRoutingPlan(const std::vector<const WideSampleSequence*>&)>;

//! Installed by the application (au3wrap); without one, no routing is applied
MIXER_API void SetBusRoutingPlanProvider(BusRoutingPlanProvider provider);
MIXER_API BusRoutingPlan MakeBusRoutingPlan(const std::vector<const WideSampleSequence*>& sequences);

//! Add `src` (srcCh channels, post-effects) to `dest` (destCh channels) applying the
//! sequence's per-channel volume/pan (if applyVolume) and `level`. Mono sources are spread
//! to every destination channel; a mono destination receives the channel average.
//! Allocation-free.
MIXER_API void BusRouteMix(const WideSampleSequence& seq, bool applyVolume,
                           const float* const* src, unsigned srcCh,
                           float* const* dest, unsigned destCh,
                           float level, size_t len) noexcept;

//! Callback running a bus's effects in place on its summed input (and muting it if needed)
using BusProcessFn = void (*)(void* ctx, int busIndex, float* const* channels, unsigned nChannels, size_t len);

//! One block of routed mixing over per-sequence buffers (used by playback).
//! For each node in `order` (sources, then buses inputs-first): buses are first processed by
//! `processBus`, then every node's buffer is added (with volume/pan) to master or its target
//! bus buffer, and to its send buses. channels[first[i] + c] is channel c of sequence i.
//! Allocation-free.
MIXER_API void BusRouteBlock(const BusRoutingPlan& plan, const std::vector<int>& order,
                             const WideSampleSequence* const* seqs, float* const* channels,
                             const unsigned* first, const unsigned* nChannels,
                             float* const* master, unsigned masterCh, size_t len,
                             BusProcessFn processBus, void* ctx) noexcept;

//! Head of a bus's effect chain in a Mixer: passes the (silent) bus sequence source
//! through, adding the audio routed into the bus for the current block.
class MIXER_API BusInjectStage final : public AudioGraph::Source
{
public:
    BusInjectStage(AudioGraph::Source& upstream, size_t nChannels, size_t bufferSize);

    //! Accumulation buffers for the next block (cleared by ClearInput)
    float* Input(size_t channel) { return mInput[channel].data(); }
    size_t InputChannels() const { return mInput.size(); }
    void ClearInput(size_t len);

    bool AcceptsBuffers(const Buffers& buffers) const override;
    bool AcceptsBlockSize(size_t blockSize) const override;
    std::optional<size_t> Acquire(Buffers& data, size_t bound) override;
    sampleCount Remaining() const override;
    bool Release() override;

private:
    AudioGraph::Source& mUpstream;
    std::vector<std::vector<float> > mInput;
    bool mConsumed = false; //!< the input of this block was already handed downstream
};

class DownmixSource;

//! Replacement for DownmixStage when a BusRoutingPlan is active: pulls every source once per
//! block (sources first, then buses in dependency order) and sums each to master, to its
//! target bus and to its sends.
class MIXER_API RoutedDownmixStage final : public AudioGraph::Source
{
public:
    struct Input {
        std::unique_ptr<DownmixSource> source;
        BusInjectStage* inject = nullptr; //!< non-null for bus nodes
    };
    RoutedDownmixStage(std::vector<Input> inputs, BusRoutingPlan plan, size_t numChannels,
                       size_t bufferSize, bool applyVolume);
    ~RoutedDownmixStage() override;

    bool AcceptsBuffers(const Buffers& buffers) const override;
    bool AcceptsBlockSize(size_t blockSize) const override;
    std::optional<size_t> Acquire(Buffers& data, size_t bound) override;
    sampleCount Remaining() const override;
    bool Release() override;

private:
    std::vector<Input> mInputs;
    BusRoutingPlan mPlan;
    std::vector<int> mOrder;
    AudioGraph::Buffers mFloatBuffers;
    size_t mNumChannels;
    bool mApplyVolume;
};
