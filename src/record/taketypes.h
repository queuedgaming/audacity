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
