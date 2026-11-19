#pragma once

#include <string>

namespace ghovlz {

// Internal builds may use Steinberg's ASIO SDK. That SDK is proprietary for
// closed source; the GPLv3 alternative is not used. It is not fetched and it
// is not linked in this tree. Windows audio is WASAPI through miniaudio.
// A later configure step can compile an ASIO backend when ASIO_SDK_DIR is set.
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
        return "Steinberg ASIO SDK: proprietary license for closed-source builds. "
               "Not redistributed here. WASAPI is the default Windows backend.";
    }
    std::string open() const {
        if (!sdkAvailable())
            return "ASIO SDK not configured (set ASIO_SDK_DIR). Using WASAPI.";
        return "ASIO backend compiled";
    }
};

} // namespace ghovlz
