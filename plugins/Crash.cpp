#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/base/funknown.h"

#include <cstdlib>

static Steinberg::FUnknown* crashCreate(void*) {
    std::abort();
    return nullptr;
}

BEGIN_FACTORY_DEF("Decksounds", "https://decksounds.com", "mailto:dev@decksounds.com")
DEF_CLASS2(INLINE_UID(0x0C0A5C0A, 0x0BAD0BAD, 0x0DEAD10C, 0x0C0FFEE0),
           PClassInfo::kManyInstances,
           kVstAudioEffectClass,
           "GHOVLZ Crash",
           0,
           "Fx",
           "1.0.0",
           kVstVersionString,
           crashCreate)
END_FACTORY
