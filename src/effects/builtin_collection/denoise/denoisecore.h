/*
 * Audacity: A Digital Audio Editor
 */
#pragma once

#include <cstddef>
#include <vector>

//! Signal processing behind the "Noise suppression" and "Breath removal" built-in effects.
//! Free of Qt and of the effects framework so it can be unit tested on synthetic signals.
namespace au::effects::denoise {
//! RNNoise works on 10 ms frames at 48 kHz
constexpr double RNNOISE_RATE = 48000.0;
constexpr double FRAME_SEC = 0.01;

struct SuppressResult {
    std::vector<float> audio; //!< same length and timing as the input
    std::vector<float> vad;   //!< RNNoise voice probability per 10 ms frame of the input, [0, 1]
};

//! Run RNNoise over one channel. Resamples to 48 kHz and back when needed and compensates the
//! model's latency, so output sample i lines up with input sample i.
//! `mix` 0..1 blends the denoised signal with the original (1 = fully denoised).
SuppressResult suppress(const float* x, size_t n, double rate, double mix, bool wantAudio = true);

struct BreathSettings {
    double reductionDb = -12.0;   //!< gain applied inside breaths
    bool silence = false;         //!< silence breaths entirely instead
    double fadeSec = 0.01;        //!< fade in/out around each breath
    double minLenSec = 0.08;      //!< shorter noise bursts are not breaths
    double maxLenSec = 1.0;       //!< longer ones are background noise/room tone, left alone
    double maxLevelBelowSpeechDb = 6.0;  //!< breaths are at least this much quieter than the speech level
    double minLevelBelowSpeechDb = 55.0; //!< ... but louder than speech level minus this (above the noise floor)
    double minAboveFloorDb = 8.0; //!< breaths stand this far above the noise floor
    double minFlatness = 0.2;     //!< spectral flatness (0 tonal .. 1 white): breaths are broadband
    double vadThreshold = 0.5;    //!< RNNoise voice probability above which a frame is voice
    double maxGapToVoiceSec = 1.5; //!< a breath sits next to a phrase
};

struct Breath {
    double t0 = 0.0;
    double t1 = 0.0;
    float levelDb = 0.0f;  //!< RMS level relative to full scale
    float flatness = 0.0f;
};

struct FrameFeatures {
    std::vector<float> rmsDb;
    std::vector<float> flatness;
    std::vector<float> vad;
    std::vector<char> voiced;
    float speechDb = -100.0f;
};

//! 10 ms frame features of a (mono) signal; `vad` per frame as returned by suppress(), or empty to compute it
FrameFeatures analyse(const float* x, size_t n, double rate, const std::vector<float>& vad, double vadThreshold);

//! Breaths: short, quiet, broadband, unvoiced stretches next to voiced audio
std::vector<Breath> detectBreaths(const float* x, size_t n, double rate, const BreathSettings& settings,
                                  const std::vector<float>& vad = {});

//! Gain per sample that attenuates `breaths` with `settings.fadeSec` fades (1 elsewhere)
std::vector<float> breathGain(size_t n, double rate, const std::vector<Breath>& breaths, const BreathSettings& settings);

//! Multiply `x` by breathGain()
void applyGain(float* x, const std::vector<float>& gain);

//! dB of RMS over [i0, i1)
double rmsDb(const float* x, size_t i0, size_t i1);
}
