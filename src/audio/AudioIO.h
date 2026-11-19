#pragma once

#include "audio/AudioSettings.h"

namespace ghovlz {

class Engine;

class AudioIO {
public:
    AudioIO();
    ~AudioIO();
    AudioIO(const AudioIO&) = delete;
    AudioIO& operator=(const AudioIO&) = delete;

    bool open(Engine* engine, int sampleRate = 48000);
    bool open(Engine* engine, const AudioSettings& settings);
    void close();
    void pump(const float* input = nullptr);
    void noteStopped();
    bool recover();
    bool running() const { return running_; }
    const char* backendName() const { return backend_; }
    const AudioSettings& settings() const { return settings_; }

private:
    struct Impl;
    Impl* impl_ = nullptr;
    Engine* engine_ = nullptr;
    AudioSettings settings_;
    bool running_ = false;
    const char* backend_ = "Off";
};

} // namespace ghovlz
