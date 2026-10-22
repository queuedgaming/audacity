/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <cstddef>

//! Optional processing of the software playthrough (input monitoring) signal.
//! Called on the audio thread with the monitored input already copied into the
//! interleaved stereo output buffer; it must be realtime safe (no allocation,
//! no locks, no blocking). The recorded (captured) audio is never passed here,
//! so recordings stay dry.
class PlaythroughProcessor
{
public:
    virtual ~PlaythroughProcessor() = default;
    virtual void ProcessPlaythrough(float* interleavedStereo, size_t frames) noexcept = 0;
};
