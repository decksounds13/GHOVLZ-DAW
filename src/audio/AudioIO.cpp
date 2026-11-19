#include "audio/AudioIO.h"

#include "engine/Engine.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <vector>

namespace ghovlz {

struct AudioIO::Impl {
    ma_device device{};
    bool inited = false;
    Engine* engine = nullptr;
    std::vector<float> input;
};

static void audioCallback(ma_device* device, void* output, const void* input, ma_uint32 frameCount) {
    auto* engine = static_cast<Engine*>(device->pUserData);
    if (!engine || !output)
        return;
    engine->process(static_cast<float*>(output), static_cast<const float*>(input), static_cast<int>(frameCount));
}

AudioIO::AudioIO() : impl_(new Impl) {}

AudioIO::~AudioIO() {
    close();
    delete impl_;
}

bool AudioIO::open(Engine* engine, int sampleRate) {
    close();
    engine_ = engine;
    impl_->engine = engine;
    ma_device_config config = ma_device_config_init(ma_device_type_duplex);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.capture.format = ma_format_f32;
    config.capture.channels = 2;
    config.sampleRate = static_cast<ma_uint32>(sampleRate);
    config.dataCallback = audioCallback;
    config.pUserData = engine;
    config.periodSizeInFrames = 256;
    if (ma_device_init(nullptr, &config, &impl_->device) != MA_SUCCESS) {
        config.deviceType = ma_device_type_playback;
        config.capture.channels = 0;
        if (ma_device_init(nullptr, &config, &impl_->device) != MA_SUCCESS) {
            backend_ = "Off";
            running_ = false;
            return false;
        }
    }
    impl_->inited = true;
    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        ma_device_uninit(&impl_->device);
        impl_->inited = false;
        backend_ = "Off";
        return false;
    }
    running_ = true;
    backend_ = "Audio";
    if (impl_->device.pContext)
        backend_ = ma_get_backend_name(impl_->device.pContext->backend);
    if (engine)
        engine->prepare(impl_->device.sampleRate, static_cast<int>(config.periodSizeInFrames));
    return true;
}

void AudioIO::close() {
    if (impl_ && impl_->inited) {
        ma_device_uninit(&impl_->device);
        impl_->inited = false;
    }
    running_ = false;
    backend_ = "Off";
}

} // namespace ghovlz
