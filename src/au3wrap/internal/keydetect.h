/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "pitch.h"

//! Pure key / scale and tuning-reference detection (no project types), reusable by analysis features.
//! Pitch classes: 0 = C, 1 = C#, ..., 9 = A, 11 = B.
namespace au::au3::keydetect {
enum class Profile {
    KrumhanslKessler, //!< probe-tone ratings (Krumhansl & Kessler 1982)
    Temperley,        //!< corpus-derived (Temperley 2007, Kostka-Payne)
};

enum class Source {
    Auto,     //!< pitch track (YIN) for monophonic material, spectral peaks otherwise
    Pitch,    //!< YIN f0 only (monophonic: vocals, solo instruments)
    Spectral, //!< STFT peaks folded into 12 bins (chords, mixes)
};

struct Settings {
    Profile profile = Profile::KrumhanslKessler;
    Source source = Source::Auto;
    double minHz = 60.0;              //!< spectral peaks outside [minHz, maxHz] are ignored
    double maxHz = 2000.0;
    double silenceDb = -45.0;
    double monophonicVoiced = 0.6;    //!< Auto: share of non-silent frames YIN must call voiced (with confidence)
    double monophonicConfidence = 0.9;
    double tuningPriorRatio = 0.8;    //!< keep A440 unless another offset is clearly more common
    size_t alternatives = 4;          //!< runners-up reported in Result::alternatives
};

struct KeyCandidate {
    int tonic = 0;          //!< pitch class
    bool minor = false;
    double correlation = 0.0; //!< Pearson r between the chroma and the rotated profile
};

using Chroma = std::array<double, 12>;

struct Result {
    bool valid = false;           //!< false when there was too little pitched material
    KeyCandidate key;
    std::vector<KeyCandidate> alternatives; //!< next best keys, best first
    double tonality = 0.0;        //!< 0..1: share of the chroma inside the key's scale, rescaled so a flat chroma is 0
    double confidence = 0.0;      //!< 0..1: tonality times the best key's margin over the runner-up (key incl. tonic)
    double scaleConfidence = 0.0; //!< 0..1: tonality times the best correlation (note set only: relative keys agree)
    double tuningCents = 0.0;     //!< reference offset from A4 = 440 Hz (e.g. +7.9 for A = 442)
    double tuningHz = 440.0;
    bool usedPitchTrack = false;  //!< chroma came from YIN (monophonic) rather than spectral peaks
    Chroma chroma {};             //!< normalised pitch-class profile (sums to 1)

    std::string keyName() const;  //!< e.g. "Eb major", "F# minor"
};

//! Tonic name, ASCII ("C", "C#", "D", "Eb", ...)
std::string pitchClassName(int pc);

//! Pitch classes of the key's scale as a 12-bit mask (bit pc). Minor allows both the natural and
//! the raised (harmonic/melodic) 6th and 7th degrees, so leading tones are not punished.
unsigned scaleMask(const KeyCandidate& key);

//! Nearest note to `cents` (relative to A4 = 440 Hz) on the grid shifted by `tuningCents`, restricted
//! to pitch classes in `mask` (0xFFF = chromatic). Returns cents relative to A4 = 440 Hz.
double snapCents(double cents, unsigned mask, double tuningCents);

//! Tuning offset in cents (-50..50) from (deviation from nearest semitone, weight) pairs:
//! peak of a smoothed circular histogram, with a prior towards 0 (A440).
double estimateTuning(const std::vector<std::pair<double, double> >& deviations, double priorRatio = 0.8);

//! Match a chroma vector against the 24 major/minor profiles
Result matchKey(const Chroma& chroma, const Settings& s = {});

//! Chroma + tuning from YIN tracks (one or more lanes, e.g. all takes of a vocal)
Result fromPitchTracks(const std::vector<std::vector<pitch::Frame> >& tracks, const Settings& s = {});

//! Chroma + tuning from spectral peaks of `x`
Result fromSpectrum(const std::vector<float>& x, double rate, const Settings& s = {});

//! Full detection on mono audio, choosing the source per Settings::source
Result detect(const std::vector<float>& x, double rate, const Settings& s = {});
}
