/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace au::record {
//! How to change the active take of a track with take lanes
enum class TakeSwitch {
    Next,      //!< rotate lanes: the main track plays take 1, take 1 gets take 2, ...
    Previous,  //!< inverse rotation
    Activate,  //!< swap the focused take lane with its main track
    CompSelection, //!< make the selected time range of the focused take lane active on its main track
    AutoComp,  //!< pick the best take per segment automatically (see au3wrap/internal/autocomp.h)
};

//! Level summary of a recorded clip (Artist mode "Last take")
struct TakeStats {
    double duration = 0.0;  //!< seconds
    double peakDb = -144.0; //!< sample peak, dBFS
    bool clipped = false;   //!< at least one sample at full scale
    size_t clippedSamples = 0;
};

//! Project-level musical key (see au3wrap/internal/projectkey.h)
struct ProjectKeyInfo {
    int tonic = 0;            //!< pitch class, 0 = C
    bool minor = false;
    double tuningCents = 0.0; //!< offset from A4 = 440 Hz
    bool operator==(const ProjectKeyInfo&) const = default;
};

//! Result of "Detect key" (see au3wrap/internal/keydetect.h)
struct KeyDetection {
    bool valid = false;
    std::string keyName;          //!< e.g. "A minor"
    int tonic = 0;                //!< pitch class, 0 = C
    bool minor = false;
    double confidence = 0.0;      //!< 0..1
    double scaleConfidence = 0.0; //!< 0..1, ignoring the relative major/minor ambiguity
    double tuningCents = 0.0;
    double tuningHz = 440.0;
    bool fromPitchTrack = false;  //!< monophonic analysis (YIN) rather than spectral
    std::vector<std::pair<std::string, double> > alternatives; //!< key name, correlation
    double correlation = 0.0;
};
}
