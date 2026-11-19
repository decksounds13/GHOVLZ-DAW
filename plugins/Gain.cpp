#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "pluginterfaces/base/ibstream.h"

#include <cstring>

namespace Steinberg {
namespace Vst {

static constexpr ParamID kGainId = 100;

class GainProcessor final : public SingleComponentEffect {
public:
    static FUnknown* createInstance(void*) { return static_cast<IAudioProcessor*>(new GainProcessor); }

    tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE {
        const tresult r = SingleComponentEffect::initialize(context);
        if (r != kResultOk)
            return r;
        addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo);
        addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
        parameters.addParameter(STR16("Gain"), nullptr, 0, 0.5, ParameterInfo::kCanAutomate, kGainId);
        return kResultOk;
    }
    tresult PLUGIN_API setProcessing(TBool) SMTG_OVERRIDE { return kResultOk; }
    tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue value) SMTG_OVERRIDE {
        const tresult r = SingleComponentEffect::setParamNormalized(id, value);
        if (id == kGainId)
            gain_ = static_cast<float>(value) * 2.f;
        return r;
    }
    tresult PLUGIN_API process(ProcessData& data) SMTG_OVERRIDE {
        if (auto* p = parameters.getParameter(kGainId))
            gain_ = static_cast<float>(p->getNormalized()) * 2.f;
        if (data.numSamples <= 0 || !data.inputs || !data.outputs)
            return kResultOk;
        const int32 chs = data.inputs[0].numChannels;
        for (int32 c = 0; c < chs && c < data.outputs[0].numChannels; ++c) {
            float* in = data.inputs[0].channelBuffers32 ? data.inputs[0].channelBuffers32[c] : nullptr;
            float* out = data.outputs[0].channelBuffers32 ? data.outputs[0].channelBuffers32[c] : nullptr;
            if (!in || !out)
                continue;
            for (int32 i = 0; i < data.numSamples; ++i)
                out[i] = in[i] * gain_;
        }
        return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream* stream) SMTG_OVERRIDE {
        if (!stream)
            return kInvalidArgument;
        int32 written = 0;
        stream->write(&gain_, static_cast<int32>(sizeof(gain_)), &written);
        return kResultOk;
    }
    tresult PLUGIN_API setState(IBStream* stream) SMTG_OVERRIDE {
        if (!stream)
            return kInvalidArgument;
        int32 read = 0;
        float g = 1.f;
        stream->read(&g, static_cast<int32>(sizeof(g)), &read);
        gain_ = g;
        if (auto* p = parameters.getParameter(kGainId))
            p->setNormalized(g * 0.5);
        return kResultOk;
    }

private:
    float gain_ = 1.f;
};

} // namespace Vst
} // namespace Steinberg

BEGIN_FACTORY_DEF("Decksounds", "https://decksounds.com", "mailto:dev@decksounds.com")
DEF_CLASS2(INLINE_UID(0xA1B2C3D4, 0x11112222, 0x33334444, 0x55556666),
           PClassInfo::kManyInstances,
           kVstAudioEffectClass,
           "GHOVLZ Test Gain",
           0,
           "Fx",
           "1.0.0",
           kVstVersionString,
           Steinberg::Vst::GainProcessor::createInstance)
END_FACTORY
