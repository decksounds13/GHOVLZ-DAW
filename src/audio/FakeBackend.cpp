#include "audio/AudioSettings.h"

#include "engine/Engine.h"

#include <vector>

namespace ghovlz {

bool FakeBackend::open(Engine* engine, const AudioSettings& settings) {
    engine_ = engine;
    settings_ = settings;
    if (settings_.sampleRate < 8000)
        settings_.sampleRate = 48000;
    if (settings_.frames < 16)
        settings_.frames = 256;
    output_.assign(static_cast<size_t>(settings_.frames) * 2, 0.f);
    needsReopen_ = false;
    if (engine_)
        engine_->prepare(settings_.sampleRate, settings_.frames);
    return engine_ != nullptr;
}

void FakeBackend::close() { engine_ = nullptr; }

void FakeBackend::pump(const float* input) {
    if (!engine_)
        return;
    engine_->process(output_.data(), input, settings_.frames);
}

void FakeBackend::noteStopped() { needsReopen_ = true; }

bool FakeBackend::recover() {
    if (!needsReopen_)
        return true;
    return open(engine_, settings_);
}

} // namespace ghovlz
