#pragma once

namespace ghovlz {

class Engine;

class AudioIO {
public:
    AudioIO();
    ~AudioIO();
    AudioIO(const AudioIO&) = delete;
    AudioIO& operator=(const AudioIO&) = delete;

    bool open(Engine* engine, int sampleRate = 48000);
    void close();
    bool running() const { return running_; }
    const char* backendName() const { return backend_; }

private:
    struct Impl;
    Impl* impl_ = nullptr;
    Engine* engine_ = nullptr;
    bool running_ = false;
    const char* backend_ = "Off";
};

} // namespace ghovlz
