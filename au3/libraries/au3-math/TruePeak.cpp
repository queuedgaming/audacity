/*
* Audacity: A Digital Audio Editor
*/
#include "TruePeak.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

namespace {
double besselI0(double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 50; ++k) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-12 * sum) {
            break;
        }
    }
    return sum;
}
}

TruePeakInterpolator::TruePeakInterpolator(size_t channels, size_t factor, size_t tapsPerPhase)
    : mFactor(std::max<size_t>(1, factor))
    , mTaps(std::max<size_t>(2, tapsPerPhase & ~size_t(1)))
    , mDelay(mTaps / 2 - 1)
    , mCoefs(mFactor * mTaps)
    , mHistory(channels, std::vector<float>(2 * mTaps, 0.0f))
    , mPos(channels, 0)
{
    // y(m + p/F) = sum_j x[n - j] * h_p[j],  m = n - D,  h_p[j] = sinc(j - D + p/F) * kaiser
    const double beta = 8.0;
    const double halfWidth = mTaps / 2.0 + 1.0;
    const double i0b = besselI0(beta);
    for (size_t p = 0; p < mFactor; ++p) {
        double sum = 0;
        for (size_t j = 0; j < mTaps; ++j) {
            const double t = double(j) - double(mDelay) + double(p) / double(mFactor);
            // window centred on the interpolated point
            const double u = t / halfWidth;
            const double w = std::fabs(u) >= 1.0 ? 0.0 : besselI0(beta * std::sqrt(1.0 - u * u)) / i0b;
            const double s = std::fabs(t) < 1e-12 ? 1.0 : std::sin(M_PI * t) / (M_PI * t);
            const double c = s * w;
            mCoefs[p * mTaps + j] = float(c);
            sum += c;
        }
        if (p > 0 && sum != 0.0) {
            for (size_t j = 0; j < mTaps; ++j) {
                mCoefs[p * mTaps + j] = float(mCoefs[p * mTaps + j] / sum);
            }
        }
    }
    // phase 0 passes the input sample through exactly
    for (size_t j = 0; j < mTaps; ++j) {
        mCoefs[j] = j == mDelay ? 1.0f : 0.0f;
    }
}

void TruePeakInterpolator::Reset()
{
    for (auto& h : mHistory) {
        std::fill(h.begin(), h.end(), 0.0f);
    }
    std::fill(mPos.begin(), mPos.end(), 0);
}

float TruePeakInterpolator::PushAbsMax(size_t channel, float x)
{
    assert(channel < mHistory.size());
    auto& h = mHistory[channel];
    size_t& pos = mPos[channel];
    pos = (pos == 0 ? mTaps : pos) - 1;
    h[pos] = x;
    h[pos + mTaps] = x;
    // h[pos + j] = x[n - j]
    const float* hist = h.data() + pos;
    float result = std::fabs(hist[mDelay]);
    for (size_t p = 1; p < mFactor; ++p) {
        const float* c = mCoefs.data() + p * mTaps;
        float acc = 0.0f;
        for (size_t j = 0; j < mTaps; ++j) {
            acc += c[j] * hist[j];
        }
        result = std::max(result, std::fabs(acc));
    }
    return result;
}

TruePeakMeter::TruePeakMeter(size_t channels, size_t factor, size_t tapsPerPhase)
    : mInterp(channels, factor, tapsPerPhase), mChannels(channels)
{
}

void TruePeakMeter::Process(size_t channel, const float* samples, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        mPeak = std::max(mPeak, mInterp.PushAbsMax(channel, samples[i]));
    }
}

void TruePeakMeter::Finish()
{
    for (size_t c = 0; c < mChannels; ++c) {
        for (size_t i = 0; i < 2 * mInterp.Delay() + 2; ++i) {
            mPeak = std::max(mPeak, mInterp.PushAbsMax(c, 0.0f));
        }
    }
}

double TruePeakMeter::PeakDb() const
{
    return mPeak > 0 ? 20.0 * std::log10(mPeak) : -std::numeric_limits<double>::infinity();
}

TruePeakLimiter::TruePeakLimiter()
    : mInterp(1, DetectionFactor, DetectionTaps)
{
}

size_t TruePeakLimiter::LatencyFor(double sampleRate, double lookaheadMs)
{
    const size_t look = std::max<size_t>(1, size_t(std::lround(lookaheadMs * sampleRate / 1000.0)));
    return look + (DetectionTaps / 2 - 1) + PeakSpread;
}

size_t TruePeakLimiter::Latency() const
{
    return mLookahead + mInterp.Delay() + PeakSpread;
}

void TruePeakLimiter::Configure(size_t channels, double sampleRate, double ceilingDb, double releaseMs,
                                double lookaheadMs)
{
    mChannels = std::max<size_t>(1, channels);
    mRate = sampleRate > 0 ? sampleRate : 44100;
    mLookahead = std::max<size_t>(1, size_t(std::lround(lookaheadMs * mRate / 1000.0)));
    mInterp = TruePeakInterpolator(mChannels, DetectionFactor, DetectionTaps);
    mMinVal.assign(mLookahead + 2, 1.0f);
    mMinIdx.assign(mLookahead + 2, 0);
    mBox.assign(mLookahead, 1.0f);
    mSpread.assign(2 * PeakSpread, 0.0f);
    mDelay.assign(mChannels, std::vector<float>(Latency(), 0.0f));
    mOutMeter = TruePeakInterpolator(mChannels, 4, 16);
    SetParameters(ceilingDb, releaseMs);
    Reset();
}

void TruePeakLimiter::SetParameters(double ceilingDb, double releaseMs)
{
    mCeiling = float(std::pow(10.0, (ceilingDb - SafetyMarginDb) / 20.0));
    const double rel = std::max(0.1, releaseMs) * mRate / 1000.0;
    mReleaseCoef = float(std::exp(-1.0 / rel));
}

void TruePeakLimiter::Reset()
{
    mInterp.Reset();
    std::fill(mSpread.begin(), mSpread.end(), 0.0f);
    mSpreadPos = 0;
    mMinHead = mMinSize = 0;
    mStep = 0;
    mRelease = 1.0f;
    std::fill(mBox.begin(), mBox.end(), 1.0f);
    mBoxPos = 0;
    mBoxSum = double(mBox.size());
    for (auto& d : mDelay) {
        std::fill(d.begin(), d.end(), 0.0f);
    }
    mDelayPos = 0;
    mGain = 1.0f;
    mMinGain = 1.0f;
    mBlockMinGain = 1.0f;
    mBlockOutPeak = 0.0f;
    mOutMeter.Reset();
}

float TruePeakLimiter::NextGain(float e)
{
    // e covers [m, m + 1); the detected value of sample m - PeakSpread is the maximum over
    // [m - 2 * PeakSpread, m) i.e. PeakSpread samples on each side of it
    mSpread[mSpreadPos] = e;
    mSpreadPos = (mSpreadPos + 1) % mSpread.size();
    const float d = *std::max_element(mSpread.begin(), mSpread.end());
    const float req = d > mCeiling ? mCeiling / d : 1.0f;

    // sliding minimum over the last mLookahead + 1 steps
    const size_t cap = mMinVal.size();
    while (mMinSize > 0) {
        const size_t back = (mMinHead + mMinSize - 1) % cap;
        if (mMinVal[back] >= req) {
            --mMinSize;
        } else {
            break;
        }
    }
    {
        const size_t slot = (mMinHead + mMinSize) % cap;
        mMinVal[slot] = req;
        mMinIdx[slot] = mStep;
        ++mMinSize;
    }
    while (mStep - mMinIdx[mMinHead] > mLookahead) {
        mMinHead = (mMinHead + 1) % cap;
        --mMinSize;
    }
    const float held = mMinVal[mMinHead];
    ++mStep;

    // smooth release, never above the held requirement
    mRelease = std::min(held, mRelease + (1.0f - mRelease) * (1.0f - mReleaseCoef));

    // average over the lookahead: reaches the requirement exactly when the peak is output
    mBoxSum += double(mRelease) - double(mBox[mBoxPos]);
    mBox[mBoxPos] = mRelease;
    mBoxPos = (mBoxPos + 1) % mBox.size();
    if (mBoxPos == 0) {
        // limit drift of the running sum
        double s = 0;
        for (const float v : mBox) {
            s += v;
        }
        mBoxSum = s;
    }
    // the mean can never exceed the largest element; clamp rounding
    return float(std::min(1.0, mBoxSum / double(mBox.size())));
}

void TruePeakLimiter::Process(const float* const* in, float* const* out, size_t len)
{
    const size_t delayLen = mDelay.empty() ? 0 : mDelay[0].size();
    float blockMin = 1.0f;
    float outPeak = 0.0f;
    for (size_t i = 0; i < len; ++i) {
        float e = 0.0f;
        for (size_t c = 0; c < mChannels; ++c) {
            e = std::max(e, mInterp.PushAbsMax(c, in[c][i]));
        }
        const float g = NextGain(e);
        mGain = g;
        mMinGain = std::min(mMinGain, g);
        blockMin = std::min(blockMin, g);
        for (size_t c = 0; c < mChannels; ++c) {
            auto& d = mDelay[c];
            const float delayed = d[mDelayPos];
            d[mDelayPos] = in[c][i];
            const float y = delayed * g;
            out[c][i] = y;
            outPeak = std::max(outPeak, mOutMeter.PushAbsMax(c, y));
        }
        mDelayPos = delayLen ? (mDelayPos + 1) % delayLen : 0;
    }
    mBlockMinGain = blockMin;
    mBlockOutPeak = outPeak;
}

namespace {
float linToDb(float v, float floorDb)
{
    return v > 0.0f ? std::max(floorDb, 20.0f * std::log10(v)) : floorDb;
}
}

void LimiterMeterState::Publish(float blockMinGain, float blockOutPeak) noexcept
{
    const float gr = std::max(0.0f, -linToDb(blockMinGain, FloorDb));
    const float out = linToDb(blockOutPeak, FloorDb);
    const bool reset = resetRequested.exchange(false, std::memory_order_relaxed);
    currentGrDb.store(gr, std::memory_order_relaxed);
    outPeakDb.store(out, std::memory_order_relaxed);
    // only the audio thread writes the maxima, so load/store is race-free
    const float prevGr = reset ? 0.0f : maxGrDb.load(std::memory_order_relaxed);
    maxGrDb.store(std::max(prevGr, gr), std::memory_order_relaxed);
    const float prevOut = reset ? FloorDb : maxOutPeakDb.load(std::memory_order_relaxed);
    maxOutPeakDb.store(std::max(prevOut, out), std::memory_order_relaxed);
    updates.fetch_add(1, std::memory_order_relaxed);
}
