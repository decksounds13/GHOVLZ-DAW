#include "io/DiskWriter.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace ghovlz {
namespace {

void writeLe16(FILE* f, unsigned v) {
    unsigned char b[2] = {static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8)};
    std::fwrite(b, 1, 2, f);
}
void writeLe32(FILE* f, unsigned v) {
    unsigned char b[4] = {static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8),
                          static_cast<unsigned char>(v >> 16), static_cast<unsigned char>(v >> 24)};
    std::fwrite(b, 1, 4, f);
}

} // namespace

bool WavStream::open(const std::string& path, int channels, int sampleRate) {
    close();
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    channels_ = std::max(1, channels);
    rate_ = sampleRate > 0 ? sampleRate : 48000;
    frames_ = 0;
    closed_ = false;
    std::fwrite("RIFF", 1, 4, f);
    writeLe32(f, 36);
    std::fwrite("WAVEfmt ", 1, 8, f);
    writeLe32(f, 16);
    writeLe16(f, 1);
    writeLe16(f, static_cast<unsigned>(channels_));
    writeLe32(f, static_cast<unsigned>(rate_));
    writeLe32(f, static_cast<unsigned>(rate_ * channels_ * 3));
    writeLe16(f, static_cast<unsigned>(channels_ * 3));
    writeLe16(f, 24);
    std::fwrite("data", 1, 4, f);
    writeLe32(f, 0);
    file_ = f;
    return true;
}

void WavStream::write(const float* interleaved, int frames) {
    auto* f = static_cast<FILE*>(file_);
    if (!f || frames <= 0 || !interleaved)
        return;
    const int n = frames * channels_;
    std::vector<unsigned char> bytes(static_cast<size_t>(n) * 3);
    for (int i = 0; i < n; ++i) {
        const float x = std::clamp(interleaved[i], -1.f, 1.f);
        const int v = static_cast<int>(x * 8388607.f);
        bytes[static_cast<size_t>(i) * 3] = static_cast<unsigned char>(v);
        bytes[static_cast<size_t>(i) * 3 + 1] = static_cast<unsigned char>(v >> 8);
        bytes[static_cast<size_t>(i) * 3 + 2] = static_cast<unsigned char>(v >> 16);
    }
    std::fwrite(bytes.data(), 1, bytes.size(), f);
    frames_ += frames;
}

void WavStream::close() {
    auto* f = static_cast<FILE*>(file_);
    if (!f)
        return;
    const unsigned dataBytes = static_cast<unsigned>(frames_) * static_cast<unsigned>(channels_) * 3u;
    std::fseek(f, 4, SEEK_SET);
    writeLe32(f, 36u + dataBytes);
    std::fseek(f, 40, SEEK_SET);
    writeLe32(f, dataBytes);
    std::fclose(f);
    file_ = nullptr;
    closed_ = true;
}

} // namespace ghovlz
