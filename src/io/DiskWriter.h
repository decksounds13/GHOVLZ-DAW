#pragma once

#include <cstdint>
#include <string>

namespace ghovlz {

// Streaming 24-bit WAV. The writer thread owns it. The audio thread never touches the file.
class WavStream {
public:
    bool open(const std::string& path, int channels, int sampleRate);
    void write(const float* interleaved, int frames);
    void close();
    bool ok() const { return file_ != nullptr || closed_; }
    int frames() const { return frames_; }

private:
    void* file_ = nullptr;
    int channels_ = 2;
    int rate_ = 48000;
    int frames_ = 0;
    bool closed_ = false;
};

} // namespace ghovlz
