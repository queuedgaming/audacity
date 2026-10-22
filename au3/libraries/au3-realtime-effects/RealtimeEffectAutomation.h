/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

 Audacity: A Digital Audio Editor

 @file RealtimeEffectAutomation.h

 Parameter automation hook for realtime effect states (Audacity 4)

 **********************************************************************/
#pragma once

class EffectSettings;

//! Writes automated parameter values into an effect's settings for a given project time.
/*! Apply() runs on the audio thread (playback) or the export thread: it must not allocate,
    lock or throw. Objects are immutable once installed; editing installs a new object. */
class RealtimeEffectAutomation
{
public:
    virtual ~RealtimeEffectAutomation() = default;
    virtual void Apply(EffectSettings& settings, double time) const noexcept = 0;
};
