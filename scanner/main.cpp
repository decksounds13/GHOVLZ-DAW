#include "public.sdk/source/vst/hosting/module.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: GhovlzPluginScan <plugin.vst3>\n";
        return 2;
    }
    std::string error;
    auto module = VST3::Hosting::Module::create(argv[1], error);
    if (!module) {
        std::cerr << error << "\n";
        return 3;
    }
    bool any = false;
    for (const auto& ci : module->getFactory().classInfos()) {
        if (ci.category() != kVstAudioEffectClass)
            continue;
        any = true;
        std::cout << "NAME\t" << ci.name() << "\n";
        // Instantiating is what surfaces a plugin that aborts in its factory.
        auto component = module->getFactory().createInstance<Steinberg::Vst::IComponent>(ci.ID());
        if (!component) {
            std::cerr << "create failed\n";
            return 4;
        }
        std::cout << "OK\n";
    }
    return any ? 0 : 3;
}
