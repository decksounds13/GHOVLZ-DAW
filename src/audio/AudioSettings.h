#pragma once

#include <string>
#include <vector>

namespace ghovlz {

struct AudioSettings {
    std::string backend = "wasapi";
    std::string device;
    int sampleRate = 48000;
    int frames = 256;
    bool exclusive = false;
};

class Engine;

// In-process device used by tests and by the app when backend == "fake".
class FakeBackend {
public:
    bool open(Engine* engine, const AudioSettings& settings);
    void close();
    void pump(const float* input);
    void noteStopped();
    bool recover();
    bool needsReopen() const { return needsReopen_; }
    int sampleRate() const { return settings_.sampleRate; }
    int frames() const { return settings_.frames; }
    const std::string& backendName() const { return settings_.backend; }

private:
    Engine* engine_ = nullptr;
    AudioSettings settings_;
    std::vector<float> output_;
    bool needsReopen_ = false;
};

} // namespace ghovlz
