/*
* Audacity: A Digital Audio Editor
*/
#pragma once

namespace au::record {
//! How to change the active take of a track with take lanes
enum class TakeSwitch {
    Next,      //!< rotate lanes: the main track plays take 1, take 1 gets take 2, ...
    Previous,  //!< inverse rotation
    Activate,  //!< swap the focused take lane with its main track
};
}
