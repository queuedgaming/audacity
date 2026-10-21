/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  ClipSegment.cpp

  Matthieu Hodgkinson

**********************************************************************/
#include "ClipSegment.h"
#include "ClipInterface.h"
#include "au3-time-and-pitch/StaffPadTimeAndPitch.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace {
TimeAndPitchInterface::Parameters
GetStretchingParameters(const ClipInterface& clip)
{
    TimeAndPitchInterface::Parameters params;
    params.timeRatio = clip.GetStretchRatio();
    params.pitchRatio = std::pow(2., clip.GetCentShift() / 1200.);
    params.preserveFormants
        =clip.GetPitchAndSpeedPreset() == PitchAndSpeedPreset::OptimizeForVoice;
    return params;
}

sampleCount
GetTotalNumSamplesToProduce(const ClipInterface& clip, double durationToDiscard)
{
    return sampleCount { clip.GetVisibleSampleCount().as_double()
                         * clip.GetStretchRatio()
                         - durationToDiscard * clip.GetRate() + .5 };
}
} // namespace

ClipSegment::ClipSegment(
    const ClipInterface& clip, double durationToDiscard, PlaybackDirection direction,
    const ClipInterface* fadeInPartner, const ClipInterface* fadeOutPartner)
    : mClip{clip}
    , mDirection{direction}
    , mFadeInPartner{fadeInPartner}
    , mFadeOutPartner{fadeOutPartner}
    , mTotalNumSamplesToProduce{GetTotalNumSamplesToProduce(
                                    clip, durationToDiscard)}
    , mSource{clip, durationToDiscard, direction}
    , mPreserveFormants{clip.GetPitchAndSpeedPreset()
                        == PitchAndSpeedPreset::OptimizeForVoice}
    , mCentShift{clip.GetCentShift()}
    , mStretcher{std::make_unique<StaffPadTimeAndPitch>(
                     clip.GetRate(), clip.NChannels(), mSource,
                     GetStretchingParameters(clip))}
    , mOnSemitoneShiftChangeSubscription{clip.SubscribeToCentShiftChange(
                                             [this](int cents) {
        mCentShift = cents;
        mUpdateCentShift = true;
    })},
    mOnFormantPreservationChangeSubscription {
    clip.SubscribeToPitchAndSpeedPresetChange(
        [this](PitchAndSpeedPreset preset) {
        mPreserveFormants
            =preset == PitchAndSpeedPreset::OptimizeForVoice;
        mUpdateFormantPreservation = true;
    })
}
{
    const double rate = clip.GetRate();
    mLength = static_cast<long long>(clip.GetVisibleSampleCount().as_double() * clip.GetStretchRatio() + .5);
    mDiscard = std::max(0LL, mLength - mTotalNumSamplesToProduce.as_long_long());
    mFadeInSamples = std::min(mLength, static_cast<long long>(clip.GetFadeInDuration() * rate + .5));
    mFadeOutSamples = std::min(mLength, static_cast<long long>(clip.GetFadeOutDuration() * rate + .5));
    mFadeCurve = clip.GetFadeCurve();
    // Crossfading with a neighbour's handle needs both sides unstretched, so that sample i of the
    // handle sits at sample i of this clip; otherwise the fade just goes to silence.
    const auto plain = [](const ClipInterface& c) {
        return c.GetStretchRatio() == 1.0 && c.GetCentShift() == 0;
    };
    if (direction != PlaybackDirection::forward || !plain(clip)) {
        mFadeInPartner = mFadeOutPartner = nullptr;
    }
    if (mFadeInPartner && (!plain(*mFadeInPartner) || mFadeInPartner->GetRate() != rate)) {
        mFadeInPartner = nullptr;
    }
    if (mFadeOutPartner && (!plain(*mFadeOutPartner) || mFadeOutPartner->GetRate() != rate)) {
        mFadeOutPartner = nullptr;
    }
}

namespace {
//! Gain of the fading-in side at x in [0, 1]; the other side gets FadeGain(1 - x)
float FadeGain(double x, int curve)
{
    x = std::clamp(x, 0.0, 1.0);
    if (curve == 1) {
        return static_cast<float>(std::sin(x * M_PI / 2));
    }
    return static_cast<float>(x);
}
}

void ClipSegment::ApplyFades(float* const* buffers, size_t numSamples)
{
    if ((mFadeInSamples == 0 && mFadeOutSamples == 0) || numSamples == 0) {
        return;
    }
    const auto nChannels = mClip.NChannels();
    const bool forward = mDirection == PlaybackDirection::forward;
    // position (relative to the play start, in output samples) of buffer offset k
    const long long produced = mTotalNumSamplesProduced.as_long_long();
    const auto posOf = [&](long long k) {
        return forward ? mDiscard + produced + k : mLength - 1 - (mDiscard + produced + k);
    };
    const long long n = static_cast<long long>(numSamples);
    std::vector<float> partner;
    const auto fetch = [&](const ClipInterface& clip, size_t ch, long long start, long long count) {
        partner.assign(static_cast<size_t>(std::max(0LL, count)), 0.0f);
        if (count > 0) {
            const auto pch = std::min(ch, clip.NChannels() - 1);
            clip.GetSampleView(pch, sampleCount { start }, static_cast<size_t>(count), false)
            .Copy(partner.data(), partner.size());
        }
    };
    const long long p0 = posOf(0);
    if (mFadeInSamples > 0) {
        for (size_t ch = 0; ch < nChannels; ++ch) {
            long long avail = 0;
            if (mFadeInPartner && p0 < mFadeInSamples) {
                // left neighbour's right handle: its sample (visible + p) continues under our sample p
                const auto handle = mFadeInPartner->GetRightHandleSampleCount().as_long_long();
                avail = std::clamp(std::min(n, mFadeInSamples - p0), 0LL, std::max(0LL, handle - p0));
                fetch(*mFadeInPartner, ch, mFadeInPartner->GetVisibleSampleCount().as_long_long() + p0, avail);
            }
            for (long long k = 0; k < n; ++k) {
                const auto p = posOf(k);
                if (p < 0 || p >= mFadeInSamples) {
                    continue;
                }
                const double x = (p + 0.5) / mFadeInSamples;
                float v = buffers[ch][k] * FadeGain(x, mFadeCurve);
                if (k < avail) {
                    v += partner[k] * FadeGain(1.0 - x, mFadeCurve);
                }
                buffers[ch][k] = v;
            }
        }
    }
    if (mFadeOutSamples > 0) {
        for (size_t ch = 0; ch < nChannels; ++ch) {
            long long kFirst = 0, avail = 0;
            if (mFadeOutPartner && p0 + n > mLength - mFadeOutSamples) {
                // right neighbour's left handle: its sample (p - length) is under our sample p
                const auto handle = mFadeOutPartner->GetLeftHandleSampleCount().as_long_long();
                const long long firstP = std::max({ p0, mLength - mFadeOutSamples, mLength - handle });
                kFirst = firstP - p0;
                avail = std::max(0LL, std::min(n, mLength - p0) - kFirst);
                fetch(*mFadeOutPartner, ch, firstP - mLength, avail);
            }
            for (long long k = 0; k < n; ++k) {
                const auto q = mLength - 1 - posOf(k);
                if (q < 0 || q >= mFadeOutSamples) {
                    continue;
                }
                const double x = (q + 0.5) / mFadeOutSamples;
                float v = buffers[ch][k] * FadeGain(x, mFadeCurve);
                const long long idx = k - kFirst;
                if (idx >= 0 && idx < avail) {
                    v += partner[idx] * FadeGain(1.0 - x, mFadeCurve);
                }
                buffers[ch][k] = v;
            }
        }
    }
}

ClipSegment::~ClipSegment()
{
    mOnSemitoneShiftChangeSubscription.Reset();
    mOnFormantPreservationChangeSubscription.Reset();
}

size_t ClipSegment::GetFloats(float* const* buffers, size_t numSamples)
{
    // Check if formant preservation of pitch shift needs to be updated.
    // This approach is not immune to a race condition, but it is unlikely and
    // not critical, as it would only affect one playback pass, during which the
    // user could easily correct the mistake if needed. On the other hand, we
    // cannot trust that the observer subscriptions do not get called after
    // destruction of this object, so better not do anything too sophisticated
    // there.
    if (mUpdateFormantPreservation.exchange(false)) {
        mStretcher->OnFormantPreservationChange(mPreserveFormants);
    }
    if (mUpdateCentShift.exchange(false)) {
        mStretcher->OnCentShiftChange(mCentShift);
    }
    const auto numSamplesToProduce = limitSampleBufferSize(
        numSamples, mTotalNumSamplesToProduce - mTotalNumSamplesProduced);
    mStretcher->GetSamples(buffers, numSamplesToProduce);
    ApplyFades(buffers, numSamplesToProduce);
    mTotalNumSamplesProduced += numSamplesToProduce;
    return numSamplesToProduce;
}

bool ClipSegment::Empty() const
{
    return mTotalNumSamplesProduced == mTotalNumSamplesToProduce;
}

size_t ClipSegment::NChannels() const
{
    return mSource.NChannels();
}
