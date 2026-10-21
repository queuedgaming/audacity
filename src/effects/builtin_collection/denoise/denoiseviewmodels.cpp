/*
 * Audacity: A Digital Audio Editor
 */
#include "denoiseviewmodels.h"
#include "denoiseeffects.h"

#include "global/translation.h"

namespace au::effects {
NoiseSuppressionViewModel::NoiseSuppressionViewModel(QObject* parent, int instanceId)
    : BuiltinEffectModel(parent, instanceId) {}

QString NoiseSuppressionViewModel::effectTitle() const { return muse::qtrc("effects/noisesuppression", "Noise suppression"); }

double NoiseSuppressionViewModel::mix() const { return settings<NoiseSuppressionSettings>().mMixPercent; }

void NoiseSuppressionViewModel::setMix(double v)
{
    if (mix() == v) {
        return;
    }
    // goes through the settings access, so a realtime instance picks it up while playing
    modifySettings<NoiseSuppressionSettings>([v](EffectSettings& s) { s.cast<NoiseSuppressionSettings>()->mMixPercent = v; });
    emit mixChanged();
}

void NoiseSuppressionViewModel::doReload() { emit mixChanged(); }

BreathRemovalViewModel::BreathRemovalViewModel(QObject* parent, int instanceId)
    : BuiltinEffectModel(parent, instanceId) {}

QString BreathRemovalViewModel::effectTitle() const { return muse::qtrc("effects/breathremoval", "Breath removal"); }

double BreathRemovalViewModel::reduction() const { return settings<BreathRemovalSettings>().mReductionDb; }
void BreathRemovalViewModel::setReduction(double v)
{
    if (reduction() == v) {
        return;
    }
    modifySettings<BreathRemovalSettings>([v](EffectSettings& s) { s.cast<BreathRemovalSettings>()->mReductionDb = v; });
    emit changed();
}

bool BreathRemovalViewModel::silence() const { return settings<BreathRemovalSettings>().mSilence; }
void BreathRemovalViewModel::setSilence(bool v)
{
    if (silence() == v) {
        return;
    }
    modifySettings<BreathRemovalSettings>([v](EffectSettings& s) { s.cast<BreathRemovalSettings>()->mSilence = v; });
    emit changed();
}

double BreathRemovalViewModel::fadeMs() const { return settings<BreathRemovalSettings>().mFadeMs; }
void BreathRemovalViewModel::setFadeMs(double v)
{
    if (fadeMs() == v) {
        return;
    }
    modifySettings<BreathRemovalSettings>([v](EffectSettings& s) { s.cast<BreathRemovalSettings>()->mFadeMs = v; });
    emit changed();
}

double BreathRemovalViewModel::sensitivity() const { return settings<BreathRemovalSettings>().mSensitivity; }
void BreathRemovalViewModel::setSensitivity(double v)
{
    if (sensitivity() == v) {
        return;
    }
    modifySettings<BreathRemovalSettings>([v](EffectSettings& s) { s.cast<BreathRemovalSettings>()->mSensitivity = v; });
    emit changed();
}

void BreathRemovalViewModel::doReload() { emit changed(); }
}
