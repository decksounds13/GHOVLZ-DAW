#pragma once

#include "core/Types.h"

#include <memory>
#include <string>

namespace Steinberg {
namespace Linux {
class IRunLoop;
}
} // namespace Steinberg

namespace ghovlz {

// Hosts VST 3 modules through the Steinberg SDK (MIT since 3.8).
// Scanning runs out of process so a plugin that aborts cannot take the DAW down.
class PluginHost {
public:
    PluginHost();
    ~PluginHost();

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
    // HOST-07. Attaches the plug-in view to a native parent and gives it an IPlugFrame.
    // The frame also implements the Linux IRunLoop. setEditorScale talks to
    // IPlugViewContentScaleSupport and resizeView follows with onSize.
    bool openEditor(Device& device, void* parent, EditorAttach& out, std::string& error);
    bool resizeEditor(Device& device, int width, int height, std::string& error);
    bool setEditorScale(Device& device, float factor, int& width, int& height, std::string& error);
    void pumpEditorRunLoop();
    Steinberg::Linux::IRunLoop* editorRunLoop() const;
    void closeEditor(Device& device);

private:
    struct EditorHost;
    std::unique_ptr<EditorHost> editors_;
    std::string scanner_;
    std::string blacklist_;
    std::string lastName_;
};

} // namespace ghovlz
