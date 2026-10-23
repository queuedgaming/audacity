/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <optional>
#include <string>
#include <vector>

class AudacityProject;
class WaveTrack;

//! Gain staging: per-track input trim and level analysis (peak, true peak, RMS, integrated LUFS)
namespace au::au3::gainstaging {
constexpr double MinTrimDb = -24.0;
constexpr double MaxTrimDb = 24.0;
constexpr double SilenceDb = -150.0;

struct Levels {
    long long trackId = -1;
    std::string name;
    int channels = 0;
    double peakDb = SilenceDb;     //!< sample peak, dBFS
    double truePeakDb = SilenceDb; //!< 4x oversampled true peak, dBTP
    double rmsDb = SilenceDb;      //!< RMS over all channels, dBFS
    double lufs = SilenceDb;       //!< integrated loudness (EBU R128 / BS.1770), LUFS
    double trimDb = 0.0;           //!< current input trim
    bool silent = true;
};

enum class TargetKind {
    Rms,  //!< target in dBFS RMS
    Lufs, //!< target in LUFS integrated
    Peak, //!< target in dBTP true peak
};

//! Levels of the samples of a mono/stereo channel buffer set (rate needed for LUFS)
Levels measure(const std::vector<std::vector<float> >& channels, double rate);

//! Levels of a wave track's audio as stored (before trim, envelope and effects)
Levels analyzeTrack(const WaveTrack& track);

//! Every wave track that is not a bus, in track order
std::vector<Levels> analyze(AudacityProject& project);

//! Trim that brings `levels` to `target` (clamped to the trim range); nullopt for silence
std::optional<double> suggestTrimDb(const Levels& levels, TargetKind kind, double target);

double trimDb(const WaveTrack& track);
//! Sets the input trim (clamped); returns false if unchanged
bool setTrimDb(WaveTrack& track, double db);
}
