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

private:
    std::string scanner_;
    std::string blacklist_;
    std::string lastName_;
};

} // namespace ghovlz
