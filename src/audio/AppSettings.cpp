#include "audio/AppSettings.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>

namespace ghovlz {
namespace {
using json = nlohmann::json;
}

AudioSettings AppSettings::audio() const {
    AudioSettings out;
    out.backend = backend;
    out.device = device;
    out.sampleRate = sampleRate;
    out.frames = frames;
    out.exclusive = exclusive;
    return out;
}

bool AppSettings::save(const std::string& path) const {
    json midi = json::array();
    for (const auto& port : midiPorts)
        midi.push_back(port);
    const json root = {{"backend", backend}, {"device", device}, {"sampleRate", sampleRate},
                       {"frames", frames}, {"exclusive", exclusive}, {"midi", midi},
                       {"pluginFolder", pluginFolder}, {"renderer", renderer},
                       {"themeScale", themeScale}, {"theme", themeId}};
    std::ofstream out(path);
    if (!out)
        return false;
    out << root.dump(2);
    return static_cast<bool>(out);
}

AppSettings AppSettings::load(const std::string& path) {
    AppSettings settings;
    std::ifstream in(path);
    if (!in)
        return settings;
    json root;
    try {
        in >> root;
    } catch (...) {
        return settings;
    }
    settings.backend = root.value("backend", settings.backend);
    settings.device = root.value("device", settings.device);
    settings.sampleRate = root.value("sampleRate", settings.sampleRate);
    settings.frames = root.value("frames", settings.frames);
    settings.exclusive = root.value("exclusive", settings.exclusive);
    settings.pluginFolder = root.value("pluginFolder", settings.pluginFolder);
    settings.renderer = root.value("renderer", settings.renderer);
    settings.themeScale = root.value("themeScale", settings.themeScale);
    settings.themeId = root.value("theme", settings.themeId);
    if (root.contains("midi"))
        for (const auto& port : root["midi"])
            settings.midiPorts.push_back(port.get<std::string>());
    return settings;
}

std::string AppSettings::userPath() {
#if defined(_WIN32)
    const char* profile = std::getenv("USERPROFILE");
    const std::string root = profile ? profile : ".";
    return root + "/AppData/Roaming/GHOVLZ/settings.json";
#else
    const char* home = std::getenv("HOME");
    const std::string root = home ? home : ".";
    return root + "/.config/ghovlz/settings.json";
#endif
}

OpenFallback openOrFallback(const AppSettings& settings, const std::function<bool(const AudioSettings&)>& tryOpen) {
    OpenFallback result;
    result.used = settings.audio();
    if (tryOpen(result.used)) {
        result.ok = true;
        return result;
    }
    result.used.device.clear();
    result.used.exclusive = false;
    if (result.used.backend == "asio")
        result.used.backend = "wasapi";
    result.message = "Could not open " + settings.backend + ". Using " + result.used.backend + " defaults.";
    result.ok = tryOpen(result.used);
    if (!result.ok)
        result.message = "Audio device failed to open.";
    return result;
}

int recoverBackoffMs(int attempt) {
    const int shift = std::clamp(attempt, 0, 5);
    return std::min(2000, 50 * (1 << shift));
}

} // namespace ghovlz
