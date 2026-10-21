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

double NoiseSuppressionViewModel::mix() const { return effect<NoiseSuppressionEffect>().mMixPercent; }

void NoiseSuppressionViewModel::setMix(double v)
{
    auto& e = effect<NoiseSuppressionEffect>();
    if (e.mMixPercent != v) {
        e.mMixPercent = v;
        emit mixChanged();
    }
}

void NoiseSuppressionViewModel::doReload() { emit mixChanged(); }

BreathRemovalViewModel::BreathRemovalViewModel(QObject* parent, int instanceId)
    : BuiltinEffectModel(parent, instanceId) {}

QString BreathRemovalViewModel::effectTitle() const { return muse::qtrc("effects/breathremoval", "Breath removal"); }

double BreathRemovalViewModel::reduction() const { return effect<BreathRemovalEffect>().mReductionDb; }
void BreathRemovalViewModel::setReduction(double v)
{
    auto& e = effect<BreathRemovalEffect>();
    if (e.mReductionDb != v) {
        e.mReductionDb = v;
        emit changed();
    }
}

bool BreathRemovalViewModel::silence() const { return effect<BreathRemovalEffect>().mSilence; }
void BreathRemovalViewModel::setSilence(bool v)
{
    auto& e = effect<BreathRemovalEffect>();
    if (e.mSilence != v) {
        e.mSilence = v;
        emit changed();
    }
}

double BreathRemovalViewModel::fadeMs() const { return effect<BreathRemovalEffect>().mFadeMs; }
void BreathRemovalViewModel::setFadeMs(double v)
{
    auto& e = effect<BreathRemovalEffect>();
    if (e.mFadeMs != v) {
        e.mFadeMs = v;
        emit changed();
    }
}

double BreathRemovalViewModel::sensitivity() const { return effect<BreathRemovalEffect>().mSensitivity; }
void BreathRemovalViewModel::setSensitivity(double v)
{
    auto& e = effect<BreathRemovalEffect>();
    if (e.mSensitivity != v) {
        e.mSensitivity = v;
        emit changed();
    }
}

void BreathRemovalViewModel::doReload() { emit changed(); }
}
