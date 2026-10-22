/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

 Audacity: A Digital Audio Editor

 @file RealtimeEffectAutomation.h

 Parameter automation hook for realtime effect states (Audacity 4)

 **********************************************************************/
#pragma once

class EffectSettings;
class EffectInstance;

//! Writes automated parameter values into an effect's settings for a given project time.
/*! Apply() runs on the audio thread (playback) or the export thread: it must not allocate,
    lock or throw. Objects are immutable once installed; editing installs a new object. */
class RealtimeEffectAutomation
{
public:
    virtual ~RealtimeEffectAutomation() = default;
    //! @param instance the instance about to process (may be null): plugin formats whose
    //! parameters are not plain settings fields (VST3) queue their changes there
    virtual void Apply(EffectSettings& settings, double time, EffectInstance* instance) const noexcept = 0;
};
