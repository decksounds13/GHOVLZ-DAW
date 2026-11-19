#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ghovlz {

struct WavData {
    int sampleRate = 48000;
    int channels = 2;
    std::vector<float> interleaved;
};

bool writeWav24(const std::string& path, const float* interleaved, int channels, int frames, int sampleRate);
bool readWav(const std::string& path, WavData& out);

} // namespace ghovlz
