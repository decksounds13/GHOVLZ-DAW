#pragma once

#include "audio/AudioSettings.h"

#include <functional>
#include <string>
#include <vector>

namespace ghovlz {

struct AppSettings {
    std::string backend = "wasapi";
    std::string device;
    int sampleRate = 48000;
    int frames = 256;
    bool exclusive = false;
    std::vector<std::string> midiPorts;
    std::string pluginFolder;
    std::string renderer = "auto";
    float themeScale = 1.f;
    std::string themeId = "ghovlz-gold";

    AudioSettings audio() const;
    bool save(const std::string& path) const;
    static AppSettings load(const std::string& path);
    static std::string userPath();
};

struct OpenFallback {
    bool ok = false;
    std::string message;
    AudioSettings used;
};

// First try the requested settings. On failure, clear the device, drop exclusive,
// and map an ASIO request onto WASAPI, then try once more.
OpenFallback openOrFallback(const AppSettings& settings, const std::function<bool(const AudioSettings&)>& tryOpen);

// UI-thread backoff after a device asks to be reopened. Attempt 0 waits 50 ms.
int recoverBackoffMs(int attempt);

} // namespace ghovlz
