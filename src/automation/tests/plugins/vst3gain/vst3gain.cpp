/*
 * Audacity: A Digital Audio Editor
 * Minimal VST3 test plugin for parameter automation tests: gain in dB plus a few
 * parameters with flags the host must handle (list, bypass, read-only meter).
 */
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <cmath>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {
enum : ParamID { kGainId = 100, kModeId = 200, kBypassId = 300, kMeterId = 400 };
constexpr double kMinDb = -40.0;
constexpr double kMaxDb = 24.0;
const FUID kTestGainUID(0x6a1f3c21, 0x4b7d4e2a, 0x9c11d0a5, 0x3e8b7f42);

class TestGain : public SingleComponentEffect
{
public:
    static FUnknown* create(void*) { return static_cast<IAudioProcessor*>(new TestGain); }

    tresult PLUGIN_API initialize(FUnknown* context) override
    {
        const tresult r = SingleComponentEffect::initialize(context);
        if (r != kResultOk) {
            return r;
        }
        addAudioInput(STR16("In"), SpeakerArr::kMono);
        addAudioOutput(STR16("Out"), SpeakerArr::kMono);

        auto* gain = new RangeParameter(STR16("Gain"), kGainId, STR16("dB"), kMinDb, kMaxDb, 0.0);
        parameters.addParameter(gain);
        auto* mode = new StringListParameter(STR16("Mode"), kModeId);
        mode->appendString(STR16("Clean"));
        mode->appendString(STR16("Warm"));
        mode->appendString(STR16("Hot"));
        parameters.addParameter(mode);
        parameters.addParameter(STR16("Bypass"), nullptr, 1, 0, ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass, kBypassId);
        parameters.addParameter(STR16("Meter"), nullptr, 0, 0, ParameterInfo::kIsReadOnly, kMeterId);
        return kResultOk;
    }

    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* in, int32 nIn, SpeakerArrangement* out, int32 nOut) override
    {
        if (nIn == 1 && nOut == 1 && in[0] == SpeakerArr::kMono && out[0] == SpeakerArr::kMono) {
            return kResultTrue;
        }
        return kResultFalse;
    }

    tresult PLUGIN_API canProcessSampleSize(int32 size) override
    {
        return size == kSample32 ? kResultTrue : kResultFalse;
    }

    tresult PLUGIN_API process(ProcessData& data) override
    {
        if (auto* changes = data.inputParameterChanges) {
            for (int32 i = 0, n = changes->getParameterCount(); i < n; ++i) {
                IParamValueQueue* q = changes->getParameterData(i);
                if (!q || q->getPointCount() <= 0) {
                    continue;
                }
                int32 offset = 0;
                ParamValue v = 0;
                if (q->getPoint(q->getPointCount() - 1, offset, v) != kResultOk) {
                    continue;
                }
                if (q->getParameterId() == kGainId) {
                    mGainNorm = v;
                } else if (q->getParameterId() == kBypassId) {
                    mBypass = v > 0.5;
                }
            }
        }
        if (data.numSamples <= 0 || data.numInputs < 1 || data.numOutputs < 1) {
            return kResultOk;
        }
        const float k = mBypass ? 1.0f : float(std::pow(10.0, (kMinDb + (kMaxDb - kMinDb) * mGainNorm) / 20.0));
        const float* in = data.inputs[0].channelBuffers32[0];
        float* out = data.outputs[0].channelBuffers32[0];
        for (int32 s = 0; s < data.numSamples; ++s) {
            out[s] = in[s] * k;
        }
        return kResultOk;
    }

    tresult PLUGIN_API setState(IBStream* state) override
    {
        double v = 0;
        int32 read = 0;
        if (state && state->read(&v, sizeof(v), &read) == kResultOk && read == sizeof(v)) {
            mGainNorm = v;
            setParamNormalized(kGainId, v);
        }
        return kResultOk;
    }

    tresult PLUGIN_API getState(IBStream* state) override
    {
        double v = mGainNorm;
        int32 written = 0;
        return state ? state->write(&v, sizeof(v), &written) : kResultFalse;
    }

private:
    double mGainNorm = (0.0 - kMinDb) / (kMaxDb - kMinDb);
    bool mBypass = false;
};
}

BEGIN_FACTORY_DEF("Audacity Tests", "https://www.audacityteam.org", "mailto:none@example.com")
DEF_CLASS2(INLINE_UID_FROM_FUID(kTestGainUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
           "Automation Test Gain", Vst::kDistributable, "Fx", "1.0.0", kVstVersionString, TestGain::create)
END_FACTORY
