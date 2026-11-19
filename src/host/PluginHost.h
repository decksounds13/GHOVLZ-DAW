#pragma once

#include "core/Types.h"

#include <memory>
#include <string>

namespace ghovlz {

// Hosts VST 3 modules through the Steinberg SDK (MIT since 3.8).
// Scanning runs out of process so a plugin that aborts cannot take the DAW down.
class PluginHost {
public:
    void setScannerExecutable(std::string path);
    void setBlacklistFile(std::string path);

    bool scan(const std::string& pluginPath, std::string& error);
    bool isBlacklisted(const std::string& pluginPath) const;
    std::shared_ptr<Device> instantiate(const std::string& pluginPath, std::string& error);
    std::string lastName() const { return lastName_; }

    void setState(Device& device, const std::string& encoded);
    std::string stateOf(const Device& device) const;

    struct EditorAttach {
        int width = 0;
        int height = 0;
        bool open = false;
    };
    // HOST-07. Asks the plugin for an IPlugView, checks the platform, and calls attached().
    // The view stores the parent. It does not create a native child window.
    bool openEditor(Device& device, void* parent, EditorAttach& out, std::string& error);
    bool resizeEditor(Device& device, int width, int height, std::string& error);
    void closeEditor(Device& device);

private:
    std::string scanner_;
    std::string blacklist_;
    std::string lastName_;
};

} // namespace ghovlz
