#include "public.sdk/source/vst/hosting/module.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include <fstream>
#include <iostream>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdlib.h>
#endif

int main(int argc, char** argv) {
#if defined(_WIN32)
    // A plugin that abort()s must exit the scanner. The default CRT abort
    // report would otherwise wait on a dialog.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    if (argc < 2 || !argv[1] || argv[1][0] == '\0') {
        std::cerr << "usage: GhovlzPluginScan <plugin.vst3>\n";
        return 2;
    }
    const std::string bundle = argv[1];
    const char* infos[] = {"/Contents/Resources/moduleinfo.json", "/Contents/moduleinfo.json", "/moduleinfo.json"};
    for (const char* rel : infos) {
        std::ifstream in(bundle + rel);
        if (!in)
            continue;
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const auto key = text.find("\"Name\"");
        if (key == std::string::npos)
            continue;
        const auto colon = text.find(':', key);
        const auto q1 = colon == std::string::npos ? std::string::npos : text.find('"', colon + 1);
        const auto q2 = q1 == std::string::npos ? std::string::npos : text.find('"', q1 + 1);
        if (q1 == std::string::npos || q2 == std::string::npos)
            continue;
        std::cout << "NAME\t" << text.substr(q1 + 1, q2 - q1 - 1) << std::endl;
        std::cout << "OK\n";
        return 0;
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
        std::cout << "NAME\t" << ci.name() << std::endl;
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
