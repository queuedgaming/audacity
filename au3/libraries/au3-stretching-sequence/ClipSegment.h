/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  ClipSegment.h

  Matthieu Hodgkinson

**********************************************************************/
#pragma once

#include "AudioSegment.h"
#include "ClipTimeAndPitchSource.h"
#include "au3-utility/Observer.h"
#include "PlaybackDirection.h"
#include <atomic>
#include <memory>

class ClipInterface;
class TimeAndPitchInterface;

using PitchRatioChangeCbSubscriber
    =std::function<void (std::function<void (double)>)>;

/*!
 * It is important that objects of this class are instantiated and destroyed on
 * the same thread, due to the owned Observer::Subscription.
 */
class STRETCHING_SEQUENCE_API ClipSegment final : public AudioSegment
{
public:
    ClipSegment(const ClipInterface&, double durationToDiscard, PlaybackDirection,
                const ClipInterface* fadeInPartner = nullptr, const ClipInterface* fadeOutPartner = nullptr);
    ~ClipSegment() override;

    // AudioSegment
    size_t GetFloats(float* const* buffers, size_t numSamples) override;
    bool Empty() const override;
    size_t NChannels() const override;

private:
    void ApplyFades(float* const* buffers, size_t numSamples);

    const ClipInterface& mClip;
    const PlaybackDirection mDirection;
    const ClipInterface* mFadeInPartner;
    const ClipInterface* mFadeOutPartner;
    long long mDiscard = 0;
    long long mLength = 0;
    long long mFadeInSamples = 0;
    long long mFadeOutSamples = 0;
    int mFadeCurve = 0;
    const sampleCount mTotalNumSamplesToProduce;
    sampleCount mTotalNumSamplesProduced = 0;
    ClipTimeAndPitchSource mSource;
    bool mPreserveFormants;
    int mCentShift;
    std::atomic<bool> mUpdateFormantPreservation = false;
    std::atomic<bool> mUpdateCentShift = false;
    // Careful that this guy is constructed after `mSource`, which it refers to
    // in its ctor.
    // todo(mhodgkinson) make this safe.
    std::unique_ptr<TimeAndPitchInterface> mStretcher;
    Observer::Subscription mOnSemitoneShiftChangeSubscription;
    Observer::Subscription mOnFormantPreservationChangeSubscription;
};
