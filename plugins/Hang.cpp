#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"

#include <chrono>
#include <thread>

namespace Steinberg {
namespace Vst {

class HangProcessor final : public SingleComponentEffect {
public:
    static FUnknown* createInstance(void*) {
        for (;;)
            std::this_thread::sleep_for(std::chrono::hours(1));
        return static_cast<IAudioProcessor*>(new HangProcessor);
    }
    tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE { return SingleComponentEffect::initialize(context); }
    tresult PLUGIN_API process(ProcessData&) SMTG_OVERRIDE { return kResultOk; }
};

} // namespace Vst
} // namespace Steinberg

BEGIN_FACTORY_DEF("Decksounds", "https://decksounds.com", "mailto:dev@decksounds.com")
DEF_CLASS2(INLINE_UID(0xAABBCCDD, 0x00001111, 0x22223333, 0x44445555),
           PClassInfo::kManyInstances,
           kVstAudioEffectClass,
           "GHOVLZ Hang",
           0,
           "Fx",
           "1.0.0",
           kVstVersionString,
           Steinberg::Vst::HangProcessor::createInstance)
END_FACTORY
