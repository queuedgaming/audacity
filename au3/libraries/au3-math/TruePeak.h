/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <cstddef>
#include <vector>

//! Polyphase windowed-sinc interpolator used for true-peak (inter-sample) detection,
//! in the spirit of ITU-R BS.1770-4 Annex 2 (oversample, then take the absolute maximum).
class MATH_API TruePeakInterpolator
{
public:
    //! factor: oversampling factor (>= 1); tapsPerPhase: FIR length per phase (even)
    TruePeakInterpolator(size_t channels, size_t factor, size_t tapsPerPhase);

    size_t Factor() const { return mFactor; }
    //! Delay of the interpolated stream, in input samples: after pushing x[n] the
    //! values describe the signal between x[n-Delay()] and x[n-Delay()+1]
    size_t Delay() const { return mDelay; }

    void Reset();
    //! Pushes one input sample of `channel`; returns max |value| at the Factor() points
    //! [n - Delay(), n - Delay() + 1) (the first one is x[n - Delay()] itself)
    float PushAbsMax(size_t channel, float x);

private:
    size_t mFactor;
    size_t mTaps;
    size_t mDelay;
    std::vector<float> mCoefs;                 //!< mFactor * mTaps
    std::vector<std::vector<float> > mHistory; //!< per channel, 2 * mTaps (mirrored ring)
    std::vector<size_t> mPos;
};

//! True-peak meter: maximum absolute value of the oversampled signal.
//! Default parameters: 4x (BS.1770-4's minimum) with 64 taps per phase.
class MATH_API TruePeakMeter
{
public:
    TruePeakMeter(size_t channels, size_t factor = 4, size_t tapsPerPhase = 64);
    void Process(size_t channel, const float* samples, size_t len);
    //! Flushes the interpolator tail (call once at the end)
    void Finish();
    //! Linear true peak over everything processed
    float Peak() const { return mPeak; }
    double PeakDb() const;

private:
    TruePeakInterpolator mInterp;
    size_t mChannels;
    float mPeak = 0.0f;
};

//! Lookahead brickwall limiter on the true peak. The gain needed by every inter-sample peak
//! (8x oversampled detection) is held over the lookahead window, released smoothly, and
//! averaged over the lookahead, so that the gain has fully reached its target when the peak
//! leaves the delay line. Channels are linked. Allocation-free after Configure().
class MATH_API TruePeakLimiter
{
public:
    static constexpr size_t DetectionFactor = 8;
    static constexpr size_t DetectionTaps = 64;
    //! The gain needed by a peak is also held over this many samples on each side, so that
    //! neighbouring samples (which dominate the interpolation) get the same reduction
    static constexpr size_t PeakSpread = 8;
    //! Internal headroom below the ceiling (dB) covering the residual error of 8x detection
    //! and of modulating the signal with a smooth gain
    static constexpr double SafetyMarginDb = 0.1;

    TruePeakLimiter();

    void Configure(size_t channels, double sampleRate, double ceilingDb, double releaseMs, double lookaheadMs);
    //! Changes ceiling / release without resetting the state (lookahead is fixed by Configure)
    void SetParameters(double ceilingDb, double releaseMs);
    void Reset();

    //! Delay of the output relative to the input, in samples
    size_t Latency() const;
    static size_t LatencyFor(double sampleRate, double lookaheadMs);

    //! in and out may alias. Processes `len` frames of all configured channels.
    void Process(const float* const* in, float* const* out, size_t len);

    //! Smallest gain applied so far (linear), for reporting gain reduction
    float MinGain() const { return mMinGain; }
    float CurrentGain() const { return mGain; }

private:
    float NextGain(float detected);

    size_t mChannels = 0;
    double mRate = 44100;
    size_t mLookahead = 1;
    float mCeiling = 1.0f;
    float mReleaseCoef = 0.0f;

    TruePeakInterpolator mInterp;
    std::vector<float> mSpread; //!< last 2 * PeakSpread detected values
    size_t mSpreadPos = 0;
    // sliding minimum of the required gain over mLookahead + 1 steps (monotonic deque, ring)
    std::vector<float> mMinVal;
    std::vector<size_t> mMinIdx;
    size_t mMinHead = 0, mMinSize = 0;
    size_t mStep = 0;
    float mRelease = 1.0f;
    // box filter over mLookahead steps
    std::vector<float> mBox;
    size_t mBoxPos = 0;
    double mBoxSum = 0;
    // audio delay line
    std::vector<std::vector<float> > mDelay;
    size_t mDelayPos = 0;
    float mGain = 1.0f;
    float mMinGain = 1.0f;
};
