#include "io/WavFile.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <vector>

namespace ghovlz {
namespace {

void write16(std::ostream& o, uint16_t v) {
    char b[2] = {static_cast<char>(v & 255), static_cast<char>((v >> 8) & 255)};
    o.write(b, 2);
}
void write32(std::ostream& o, uint32_t v) {
    char b[4] = {static_cast<char>(v & 255), static_cast<char>((v >> 8) & 255),
                 static_cast<char>((v >> 16) & 255), static_cast<char>((v >> 24) & 255)};
    o.write(b, 4);
}
uint16_t read16(std::istream& in) {
    unsigned char b[2];
    in.read(reinterpret_cast<char*>(b), 2);
    return static_cast<uint16_t>(b[0] | (b[1] << 8));
}
uint32_t read32(std::istream& in) {
    unsigned char b[4];
    in.read(reinterpret_cast<char*>(b), 4);
    return static_cast<uint32_t>(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
}

} // namespace

bool writeWav24(const std::string& path, const float* interleaved, int channels, int frames, int sampleRate) {
    std::ofstream o(path, std::ios::binary);
    if (!o)
        return false;
    const uint32_t dataBytes = static_cast<uint32_t>(frames * channels * 3);
    o.write("RIFF", 4);
    write32(o, 36 + dataBytes);
    o.write("WAVE", 4);
    o.write("fmt ", 4);
    write32(o, 16);
    write16(o, 1);
    write16(o, static_cast<uint16_t>(channels));
    write32(o, static_cast<uint32_t>(sampleRate));
    write32(o, static_cast<uint32_t>(sampleRate * channels * 3));
    write16(o, static_cast<uint16_t>(channels * 3));
    write16(o, 24);
    o.write("data", 4);
    write32(o, dataBytes);
    const int n = frames * channels;
    for (int i = 0; i < n; ++i) {
        float s = std::max(-1.f, std::min(1.f, interleaved[i]));
        int v = static_cast<int>(std::lrint(s * 8388607.0));
        unsigned u = static_cast<unsigned>(v);
        char b[3] = {static_cast<char>(u & 255), static_cast<char>((u >> 8) & 255), static_cast<char>((u >> 16) & 255)};
        o.write(b, 3);
    }
    return static_cast<bool>(o);
}

bool readWav(const std::string& path, WavData& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    char riff[4];
    in.read(riff, 4);
    if (std::string(riff, 4) != "RIFF")
        return false;
    read32(in);
    char wave[4];
    in.read(wave, 4);
    if (std::string(wave, 4) != "WAVE")
        return false;
    uint16_t format = 1, channels = 2, bits = 16;
    uint32_t rate = 48000;
    uint32_t dataBytes = 0;
    bool haveData = false;
    while (in && !haveData) {
        char id[4];
        in.read(id, 4);
        if (!in)
            break;
        const uint32_t size = read32(in);
        const std::string sid(id, 4);
        if (sid == "fmt ") {
            format = read16(in);
            channels = read16(in);
            rate = read32(in);
            read32(in);
            read16(in);
            bits = read16(in);
            if (size > 16)
                in.seekg(size - 16, std::ios::cur);
        } else if (sid == "data") {
            dataBytes = size;
            haveData = true;
        } else {
            in.seekg(size, std::ios::cur);
        }
    }
    if (!haveData || channels < 1)
        return false;
    const int bytesPer = bits / 8;
    if (bytesPer <= 0)
        return false;
    const int frames = static_cast<int>(dataBytes / (bytesPer * channels));
    out.sampleRate = static_cast<int>(rate);
    out.channels = channels;
    out.interleaved.resize(static_cast<size_t>(frames) * channels);
    for (int i = 0; i < frames * channels; ++i) {
        if (format == 3 && bits == 32) {
            float s = 0;
            in.read(reinterpret_cast<char*>(&s), 4);
            out.interleaved[i] = s;
        } else if (bits == 16) {
            const int16_t s = static_cast<int16_t>(read16(in));
            out.interleaved[i] = s / 32768.f;
        } else if (bits == 24) {
            unsigned char b[3];
            in.read(reinterpret_cast<char*>(b), 3);
            int v = b[0] | (b[1] << 8) | (b[2] << 16);
            if (v & 0x800000)
                v |= ~0xFFFFFF;
            out.interleaved[i] = v / 8388608.f;
        } else if (bits == 32 && format == 1) {
            const uint32_t u = read32(in);
            out.interleaved[i] = static_cast<int32_t>(u) / 2147483648.f;
        } else {
            return false;
        }
    }
    return true;
}

} // namespace ghovlz
