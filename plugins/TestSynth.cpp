#include "pluginterfaces/base/ibstream.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <cstring>

namespace Steinberg {
namespace Vst {

static constexpr ParamID kLevelId = 100;

class TestSynthProcessor final : public AudioEffect {
public:
    TestSynthProcessor() { setControllerClass(FUID(0x55555555, 0x66666666, 0x77777777, 0x88888888)); }
    static FUnknown* createInstance(void*) { return static_cast<IAudioProcessor*>(new TestSynthProcessor); }

    tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE {
        const tresult r = AudioEffect::initialize(context);
        if (r != kResultOk)
            return r;
        addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
        addEventInput(STR16("Event In"), 1);
        return kResultOk;
    }
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* inputs, int32 numIns, SpeakerArrangement* outputs, int32 numOuts) SMTG_OVERRIDE {
        if (numOuts > 0)
            outputs[0] = SpeakerArr::kStereo;
        return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    }
    tresult PLUGIN_API canProcessSampleSize(int32 symbolicSampleSize) SMTG_OVERRIDE {
        return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
    }
    tresult PLUGIN_API process(ProcessData& data) SMTG_OVERRIDE {
        if (data.numSamples <= 0 || !data.outputs || data.outputs[0].numChannels < 1 || !data.outputs[0].channelBuffers32)
            return kResultOk;
        float* left = data.outputs[0].channelBuffers32[0];
        float* right = data.outputs[0].numChannels > 1 ? data.outputs[0].channelBuffers32[1] : nullptr;
        std::memset(left, 0, sizeof(float) * static_cast<size_t>(data.numSamples));
        if (right)
            std::memset(right, 0, sizeof(float) * static_cast<size_t>(data.numSamples));
        if (data.inputParameterChanges) {
            int points = 0;
            const int32 queues = data.inputParameterChanges->getParameterCount();
            for (int32 i = 0; i < queues; ++i) {
                auto* queue = data.inputParameterChanges->getParameterData(i);
                if (!queue)
                    continue;
                points += queue->getPointCount();
                if (queue->getParameterId() == kLevelId && queue->getPointCount() > 0) {
                    int32 offset = 0;
                    ParamValue value = 0;
                    queue->getPoint(queue->getPointCount() - 1, offset, value);
                    level_ = static_cast<float>(value);
                }
            }
            if (points > 0)
                left[0] += static_cast<float>(points);
        }
        if (data.inputEvents && right) {
            const int32 count = data.inputEvents->getEventCount();
            for (int32 i = 0; i < count; ++i) {
                Event ev {};
                if (data.inputEvents->getEvent(i, ev) != kResultOk || ev.type != Event::kNoteOnEvent)
                    continue;
                if (ev.sampleOffset >= 0 && ev.sampleOffset < data.numSamples)
                    right[ev.sampleOffset] = 0.5f;
                held_ = true;
            }
        }
        if (data.processContext && data.numSamples > 1)
            left[1] = static_cast<float>(data.processContext->tempo) * 0.001f;
        if (held_) {
            for (int32 i = 2; i < data.numSamples; ++i)
                left[i] += 0.2f * level_;
        }
        return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream* stream) SMTG_OVERRIDE {
        if (!stream)
            return kInvalidArgument;
        int32 written = 0;
        stream->write(&level_, static_cast<int32>(sizeof(level_)), &written);
        return kResultOk;
    }
    tresult PLUGIN_API setState(IBStream* stream) SMTG_OVERRIDE {
        if (!stream)
            return kInvalidArgument;
        int32 read = 0;
        float level = level_;
        stream->read(&level, static_cast<int32>(sizeof(level)), &read);
        level_ = level;
        return kResultOk;
    }

private:
    float level_ = 0.5f;
    bool held_ = false;
};

class TestSynthController final : public EditController {
public:
    static FUnknown* createInstance(void*) { return static_cast<IEditController*>(new TestSynthController); }
    tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE {
        const tresult r = EditController::initialize(context);
        if (r != kResultOk)
            return r;
        parameters.addParameter(STR16("Level"), nullptr, 0, 0.5, ParameterInfo::kCanAutomate, kLevelId);
        return kResultOk;
    }
    tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue value) SMTG_OVERRIDE {
        if (auto* handler = getComponentHandler()) {
            handler->beginEdit(id);
            handler->performEdit(id, value);
            handler->endEdit(id);
        }
        return EditController::setParamNormalized(id, value);
    }
    tresult PLUGIN_API setComponentState(IBStream* stream) SMTG_OVERRIDE {
        if (!stream)
            return kResultFalse;
        float level = 0.5f;
        int32 read = 0;
        stream->read(&level, static_cast<int32>(sizeof(level)), &read);
        if (read == static_cast<int32>(sizeof(level)))
            setParamNormalized(kLevelId, level);
        return kResultOk;
    }
};

} // namespace Vst
} // namespace Steinberg

BEGIN_FACTORY_DEF("Decksounds", "https://decksounds.com", "mailto:dev@decksounds.com")
DEF_CLASS2(INLINE_UID(0x11111111, 0x22222222, 0x33333333, 0x44444444),
           PClassInfo::kManyInstances,
           kVstAudioEffectClass,
           "GHOVLZ Test Synth",
           Vst::kDistributable,
           "Instrument|Synth",
           "1.0.0",
           kVstVersionString,
           Steinberg::Vst::TestSynthProcessor::createInstance)
DEF_CLASS2(INLINE_UID(0x55555555, 0x66666666, 0x77777777, 0x88888888),
           PClassInfo::kManyInstances,
           kVstComponentControllerClass,
           "GHOVLZ Test Synth Controller",
           0,
           "",
           "1.0.0",
           kVstVersionString,
           Steinberg::Vst::TestSynthController::createInstance)
END_FACTORY
