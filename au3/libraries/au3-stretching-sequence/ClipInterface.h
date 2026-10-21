/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  ClipInterface.h

  Matthieu Hodgkinson

**********************************************************************/
#pragma once

#include "AudioSegmentSampleView.h"
#include "au3-utility/Observer.h"
#include "au3-math/SampleCount.h"

class STRETCHING_SEQUENCE_API ClipTimes
{
public:
    virtual ~ClipTimes();

    /*!
     * The number of raw audio samples not hidden by trimming.
     */
    virtual sampleCount GetVisibleSampleCount() const = 0;

    virtual int GetRate() const = 0;

    virtual double GetPlayStartTime() const = 0;

    virtual double GetPlayEndTime() const = 0;

    virtual sampleCount TimeToSamples(double time) const = 0;

    virtual double GetStretchRatio() const = 0;
};

enum class PitchAndSpeedPreset
{
    Default,
    OptimizeForVoice,
};

class STRETCHING_SEQUENCE_API ClipInterface : public ClipTimes
{
public:
    ~ClipInterface() override;

    virtual AudioSegmentSampleView GetSampleView(
        size_t iChannel, sampleCount start, size_t length, bool mayThrow = true) const = 0;

    virtual size_t NChannels() const = 0;

    virtual int GetCentShift() const = 0;

    [[nodiscard]] virtual Observer::Subscription
    SubscribeToCentShiftChange(std::function<void(int)> cb) const = 0;

    virtual PitchAndSpeedPreset GetPitchAndSpeedPreset() const = 0;

    //! Non-destructive fades, in seconds of play time, applied when rendering (playback, export, mix).
    //! A fade next to an abutting clip that has hidden audio (a handle) beyond its edge becomes a
    //! crossfade with that audio, see ClipSegment.
    virtual double GetFadeInDuration() const { return 0.0; }
    virtual double GetFadeOutDuration() const { return 0.0; }
    //! 0: linear (constant amplitude), 1: equal power (sin/cos)
    virtual int GetFadeCurve() const { return 0; }
    //! Hidden samples before the play start / after the play end (trimmed but kept)
    virtual sampleCount GetLeftHandleSampleCount() const { return 0; }
    virtual sampleCount GetRightHandleSampleCount() const { return 0; }

    [[nodiscard]] virtual Observer::Subscription
    SubscribeToPitchAndSpeedPresetChange(
        std::function<void(PitchAndSpeedPreset)> cb) const = 0;
};

using ClipConstHolders = std::vector<std::shared_ptr<const ClipInterface> >;
