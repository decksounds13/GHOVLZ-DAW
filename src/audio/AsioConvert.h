#pragma once

#include <cstdint>
#include <cstring>

namespace ghovlz {

// Sample conversion and ASIO message selectors. The numeric values match the
// Steinberg SDK so the Windows host and the Linux tests share one table.
// A real ASIO driver at 64 samples stays unverified until a desktop log.

enum class AsioSelector : int {
    Supported = 1,
    EngineVersion = 2,
    ResetRequest = 3,
    BufferSizeChange = 4,
    ResyncRequest = 5,
    LatenciesChanged = 6
};

inline int asioSelectorReply(int selector) {
    if (selector == static_cast<int>(AsioSelector::Supported))
        return 1;
    if (selector == static_cast<int>(AsioSelector::EngineVersion))
        return 2;
    return 1;
}

inline bool asioSelectorNeedsReset(int selector) {
    return selector == static_cast<int>(AsioSelector::ResetRequest) ||
           selector == static_cast<int>(AsioSelector::BufferSizeChange) ||
           selector == static_cast<int>(AsioSelector::ResyncRequest) ||
           selector == static_cast<int>(AsioSelector::LatenciesChanged);
}

inline float asioInt16ToFloat(int16_t sample) { return static_cast<float>(sample) / 32768.f; }
inline float asioInt32ToFloat(int32_t sample) { return static_cast<float>(sample) / 2147483648.f; }
inline float asioFloat64ToFloat(double sample) { return static_cast<float>(sample); }

inline float asioInt24LsbToFloat(const uint8_t bytes[3]) {
    int32_t v = static_cast<int32_t>(bytes[0] | (bytes[1] << 8) | (bytes[2] << 16));
    if (v & 0x800000)
        v |= ~0xFFFFFF;
    return static_cast<float>(v) / 8388608.f;
}

inline int16_t asioFloatToInt16(float sample) {
    const float x = sample < -1.f ? -1.f : (sample > 1.f ? 1.f : sample);
    return static_cast<int16_t>(x * 32767.f);
}

} // namespace ghovlz
