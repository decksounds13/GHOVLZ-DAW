#pragma once

#include <string>

namespace ghovlz {

class Engine;

// Our ASIO host. The Steinberg SDK is not in this repository. CMake downloads
// it into the build tree, or GHOVLZ_ASIO_SDK_DIR points at a local copy.
class AsioBackend {
public:
    static bool sdkAvailable() {
#if defined(GHOVLZ_WITH_ASIO)
        return true;
#else
        return false;
#endif
    }
    static const char* licenseNote() {
        return "Steinberg ASIO SDK 2.3.4 is fetched at configure time or read from "
               "GHOVLZ_ASIO_SDK_DIR. It is not committed. A closed-source release "
               "still needs Steinberg's proprietary licence.";
    }
    std::string open(Engine* engine, const std::string& device, int sampleRate, int frames);
    void close();
    bool running() const { return opened_; }
    const char* name() const;

private:
    bool opened_ = false;
    std::string name_;
};

} // namespace ghovlz
