#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <cstring>

namespace Steinberg {
namespace Vst {

static constexpr ParamID kGainId = 100;

// HOST-07. Accepts the embed handshake for HWND and X11. The parent is stored.
// A native child window is not created here; the host still drives attached() and removed().
class GainView final : public IPlugView {
public:
    GainView() { FUNKNOWN_CTOR }
    ~GainView() { FUNKNOWN_DTOR }

    DECLARE_FUNKNOWN_METHODS

    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) SMTG_OVERRIDE {
        if (!type)
            return kResultFalse;
        if (std::strcmp(type, kPlatformTypeHWND) == 0 || std::strcmp(type, kPlatformTypeX11EmbedWindowID) == 0)
            return kResultTrue;
        return kResultFalse;
    }
    tresult PLUGIN_API attached(void* parent, FIDString type) SMTG_OVERRIDE {
        if (!parent || isPlatformTypeSupported(type) != kResultTrue)
            return kResultFalse;
        parent_ = parent;
        return kResultOk;
    }
    tresult PLUGIN_API removed() SMTG_OVERRIDE {
        parent_ = nullptr;
        return kResultOk;
    }
    tresult PLUGIN_API onWheel(float) SMTG_OVERRIDE { return kResultFalse; }
    tresult PLUGIN_API onKeyDown(char16, int16, int16) SMTG_OVERRIDE { return kResultFalse; }
    tresult PLUGIN_API onKeyUp(char16, int16, int16) SMTG_OVERRIDE { return kResultFalse; }
    tresult PLUGIN_API getSize(ViewRect* size) SMTG_OVERRIDE {
        if (!size)
            return kInvalidArgument;
        size->left = 0;
        size->top = 0;
        size->right = 480;
        size->bottom = 300;
        return kResultOk;
    }
    tresult PLUGIN_API onSize(ViewRect* newSize) SMTG_OVERRIDE {
        if (!newSize)
            return kInvalidArgument;
        rect_ = *newSize;
        return kResultOk;
    }
    tresult PLUGIN_API onFocus(TBool) SMTG_OVERRIDE { return kResultOk; }
    tresult PLUGIN_API setFrame(IPlugFrame* frame) SMTG_OVERRIDE {
        frame_ = frame;
        return kResultOk;
    }
    tresult PLUGIN_API canResize() SMTG_OVERRIDE { return kResultFalse; }
    tresult PLUGIN_API checkSizeConstraint(ViewRect* rect) SMTG_OVERRIDE {
        if (!rect)
            return kInvalidArgument;
        rect->right = rect->left + 480;
        rect->bottom = rect->top + 300;
        return kResultOk;
    }

private:
    void* parent_ = nullptr;
    IPlugFrame* frame_ = nullptr;
    ViewRect rect_ {0, 0, 480, 300};
};

tresult PLUGIN_API GainView::queryInterface(const TUID iid, void** obj) {
    QUERY_INTERFACE(iid, obj, IPlugView::iid, IPlugView)
    QUERY_INTERFACE(iid, obj, FUnknown::iid, IPlugView)
    *obj = nullptr;
    return kNoInterface;
}
IMPLEMENT_REFCOUNT(GainView)

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
        if (data.inputParameterChanges) {
            const int32 queues = data.inputParameterChanges->getParameterCount();
            for (int32 i = 0; i < queues; ++i) {
                auto* queue = data.inputParameterChanges->getParameterData(i);
                if (!queue || queue->getParameterId() != kGainId || queue->getPointCount() < 1)
                    continue;
                int32 offset = 0;
                ParamValue value = 0;
                queue->getPoint(queue->getPointCount() - 1, offset, value);
                gain_ = static_cast<float>(value) * 2.f;
                setParamNormalized(kGainId, value);
            }
        }
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
    IPlugView* PLUGIN_API createView(FIDString name) SMTG_OVERRIDE {
        if (name && std::strcmp(name, ViewType::kEditor) == 0)
            return new GainView;
        return nullptr;
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
