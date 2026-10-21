/*
 * Audacity: A Digital Audio Editor
 */
#pragma once

#include <cstddef>
#include <complex>
#include <memory>
#include <vector>

struct DenoiseState;

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

// ---------------------------------------------------------------------------------------------
// Streaming (realtime) building blocks. The offline functions above are built on the same code.
// No allocation happens in process() once an object is constructed (for blocks up to
// MAX_BLOCK samples; longer blocks are split internally).

constexpr size_t MAX_BLOCK = 1024;

//! Band-limited resampler that keeps its state across blocks (Hann-windowed sinc, tabulated).
//! Output sample j is the input interpolated at time j / factor; it is emitted as soon as the
//! input it needs (up to halfWidth() samples ahead) has arrived.
class StreamResampler
{
public:
    explicit StreamResampler(double factor);
    double factor() const { return m_factor; }
    bool bypass() const { return m_factor == 1.0; }
    //! one-sided kernel width in input samples (0 in bypass)
    double halfWidth() const { return m_bypass ? 0.0 : m_hw; }
    void reset();
    //! Feed n samples; appends produced samples to out[*outLen ...], at most outCap in total
    void push(const float* in, size_t n, float* out, size_t& outLen, size_t outCap);

private:
    double m_factor = 1.0;
    bool m_bypass = true;
    double m_cutoff = 1.0;
    double m_hw = 0.0;
    static constexpr int TABLE_RES = 512; // kernel samples per input sample
    std::vector<float> m_table;           // kernel for d >= 0
    std::vector<float> m_hist;            // ring of past input
    size_t m_mask = 0;
    long long m_inCount = 0;              // input samples received
    long long m_outCount = 0;             // output samples emitted
    float kernel(double d) const;
};

//! Per-channel streaming RNNoise with resampling to/from 48 kHz and wet/dry mix.
//! Output is the processed input delayed by exactly latency() samples (zeros first).
class StreamSuppressor
{
public:
    //! wantAudio false: only run the model (VAD), output is not produced
    StreamSuppressor(double rate, bool wantAudio = true);
    ~StreamSuppressor();
    static size_t latencyFor(double rate);
    size_t latency() const { return m_latency; }
    void reset();
    //! mix 0..1 (smoothed over ~10 ms); out may equal in. out may be null when !wantAudio.
    void process(const float* in, float* out, size_t n, double mix);
    //! RNNoise frames processed so far and the VAD of frame f (valid for the last 256 frames).
    //! Frame f is centred on input time f * 10 ms.
    size_t vadFrames() const { return m_vadCount; }
    float vad(size_t f) const { return m_vadRing[f & 255]; }
    //! Every VAD value is also appended here when set (offline use)
    std::vector<float>* vadLog = nullptr;
    //! Times the output queue ran dry (would mean the latency is too small; should stay 0)
    size_t underflows() const { return m_underflows; }

private:
    void processChunk(const float* in, float* out, size_t n, double mix);
    double m_rate;
    bool m_wantAudio;
    size_t m_latency;
    StreamResampler m_up, m_down;
    struct Fifo {
        std::vector<float> buf;
        size_t r = 0, w = 0, mask = 0;
        void init(size_t cap);
        void clear() { r = w = 0; }
        size_t size() const { return w - r; }
        void push(float v) { buf[w++ & mask] = v; }
        float pop() { return buf[r++ & mask]; }
    };
    Fifo m_dry, m_wet;
    std::vector<float> m_tmp48, m_frame, m_tmpOut;
    size_t m_frameFill = 0;
    size_t m_dropped = 0;
    DenoiseState* m_st = nullptr;
    float m_vadRing[256] {};
    size_t m_vadCount = 0;
    double m_mixNow = -1.0;
    size_t m_underflows = 0;
};

//! Causal breath removal with a fixed lookahead (reported as latency). Running noise-floor and
//! speech-level estimates replace the whole-selection statistics of detectBreaths(); the same
//! frame features and candidate rules are used. All channels get the same gain.
class StreamBreathGate
{
public:
    StreamBreathGate(double rate, unsigned channels);
    static size_t latencyFor(double rate);
    size_t latency() const { return m_latency; }
    void reset();
    //! settings may change between calls; fades and gain changes are smoothed
    void process(const float* const* in, float* const* out, size_t n, const BreathSettings& s);
    //! frames whose label was not decided in time (should stay 0 after the first latency)
    size_t undecided() const { return m_undecided; }
    size_t breathsStarted() const { return m_breaths; }

private:
    void analyseReady(const BreathSettings& s);
    void decideReady(const BreathSettings& s);
    float gainAt(long long i, const BreathSettings& s);
    double m_rate;
    unsigned m_channels;
    size_t m_hop, m_fftN, m_latency;
    StreamSuppressor m_vad;
    std::vector<float> m_mono, m_vadTmp;     // ring of mono input
    size_t m_monoMask = 0;
    std::vector<std::vector<float> > m_delay; // per channel delay rings
    size_t m_delayMask = 0;
    long long m_count = 0;                   // input samples received
    // frame rings
    static constexpr size_t FR = 256;
    float m_rms[FR] {}, m_flat[FR] {};
    char m_voiced[FR] {}, m_cand[FR] {}, m_label[FR] {}, m_candLoose[FR] {};
    long long m_startOf[FR] {};
    long long m_analysed = 0, m_decided = 0;
    long long m_lastVoiced = -1000000, m_breathStart = -1, m_breathLen = 0;
    bool m_inBreath = false, m_blockUntilBreak = false;
    float m_peakDb = -200.0f, m_speechDb = -200.0f, m_floorDb = 0.0f;
    bool m_haveVoice = false, m_haveFloor = false, m_prevVoiced = false;
    std::vector<std::complex<float> > m_fftBuf;
    double m_target = 1.0;  // smoothed attenuation
    size_t m_undecided = 0, m_breaths = 0;
};
}
