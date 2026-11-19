#include "audio/AudioIO.h"

#include "audio/AsioBackend.h"
#include "engine/Engine.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <string>
#include <vector>

namespace ghovlz {

struct DeviceBridge {
    Engine* engine = nullptr;
    bool* needsReopen = nullptr;
};

struct AudioIO::Impl {
    ma_context context {};
    ma_device device {};
    bool contextReady = false;
    bool inited = false;
    bool needsReopen = false;
    DeviceBridge bridge;
    FakeBackend fake;
    AsioBackend asio;
    ma_device_id playback {};
    ma_device_id capture {};
    bool havePlayback = false;
    bool haveCapture = false;
};

static void audioCallback(ma_device* device, void* output, const void* input, ma_uint32 frameCount) {
    auto* bridge = static_cast<DeviceBridge*>(device->pUserData);
    if (!bridge || !bridge->engine || !output)
        return;
    bridge->engine->process(static_cast<float*>(output), static_cast<const float*>(input), static_cast<int>(frameCount));
}

static void notifyCallback(const ma_device_notification* notification) {
    auto* bridge = static_cast<DeviceBridge*>(notification->pDevice->pUserData);
    if (!bridge || !bridge->needsReopen)
        return;
    if (notification->type == ma_device_notification_type_stopped || notification->type == ma_device_notification_type_rerouted)
        *bridge->needsReopen = true;
}

AudioIO::AudioIO() : impl_(new Impl) {}

AudioIO::~AudioIO() {
    close();
    delete impl_;
}

bool AudioIO::open(Engine* engine, int sampleRate) {
    AudioSettings settings;
    settings.sampleRate = sampleRate;
    settings.backend = "wasapi";
    return open(engine, settings);
}

bool AudioIO::open(Engine* engine, const AudioSettings& settings) {
    close();
    engine_ = engine;
    settings_ = settings;
    if (settings_.backend == "fake") {
        running_ = impl_->fake.open(engine, settings_);
        backend_ = "Fake";
        return running_;
    }
    if (settings_.backend == "asio") {
        const std::string err = impl_->asio.open(engine, settings_.device, settings_.sampleRate, settings_.frames);
        running_ = err.empty();
        backend_ = running_ ? impl_->asio.name() : "Off";
        return running_;
    }
    if (ma_context_init(nullptr, 0, nullptr, &impl_->context) != MA_SUCCESS) {
        backend_ = "Off";
        return false;
    }
    impl_->contextReady = true;
    ma_device_info* playback = nullptr;
    ma_uint32 playbackCount = 0;
    ma_device_info* capture = nullptr;
    ma_uint32 captureCount = 0;
    ma_context_get_devices(&impl_->context, &playback, &playbackCount, &capture, &captureCount);
    auto match = [&](ma_device_info* list, ma_uint32 count, ma_device_id& id) {
        if (settings_.device.empty())
            return false;
        for (ma_uint32 i = 0; i < count; ++i) {
            if (settings_.device == list[i].name) {
                id = list[i].id;
                return true;
            }
        }
        return false;
    };
    impl_->havePlayback = match(playback, playbackCount, impl_->playback);
    impl_->haveCapture = match(capture, captureCount, impl_->capture);

    ma_device_config config = ma_device_config_init(ma_device_type_duplex);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.capture.format = ma_format_f32;
    config.capture.channels = 2;
    config.sampleRate = static_cast<ma_uint32>(settings_.sampleRate);
    config.dataCallback = audioCallback;
    config.notificationCallback = notifyCallback;
    impl_->bridge.engine = engine;
    impl_->bridge.needsReopen = &impl_->needsReopen;
    config.pUserData = &impl_->bridge;
    config.periodSizeInFrames = static_cast<ma_uint32>(settings_.frames);
    config.playback.shareMode = settings_.exclusive ? ma_share_mode_exclusive : ma_share_mode_shared;
    config.capture.shareMode = settings_.exclusive ? ma_share_mode_exclusive : ma_share_mode_shared;
#if defined(MA_HAS_WASAPI)
    config.wasapi.usage = ma_wasapi_usage_pro_audio;
#endif
    if (impl_->havePlayback)
        config.playback.pDeviceID = &impl_->playback;
    if (impl_->haveCapture)
        config.capture.pDeviceID = &impl_->capture;
    if (ma_device_init(&impl_->context, &config, &impl_->device) != MA_SUCCESS) {
        config.deviceType = ma_device_type_playback;
        config.capture.channels = 0;
        config.capture.pDeviceID = nullptr;
        if (ma_device_init(&impl_->context, &config, &impl_->device) != MA_SUCCESS) {
            ma_context_uninit(&impl_->context);
            impl_->contextReady = false;
            backend_ = "Off";
            return false;
        }
    }
    impl_->inited = true;
    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        ma_device_uninit(&impl_->device);
        ma_context_uninit(&impl_->context);
        impl_->inited = false;
        impl_->contextReady = false;
        backend_ = "Off";
        return false;
    }
    running_ = true;
    backend_ = ma_get_backend_name(impl_->device.pContext->backend);
    if (engine)
        engine->prepare(impl_->device.sampleRate, static_cast<int>(config.periodSizeInFrames));
    return true;
}

void AudioIO::close() {
    impl_->fake.close();
    impl_->asio.close();
    if (impl_->inited) {
        ma_device_uninit(&impl_->device);
        impl_->inited = false;
    }
    if (impl_->contextReady) {
        ma_context_uninit(&impl_->context);
        impl_->contextReady = false;
    }
    running_ = false;
    backend_ = "Off";
}

void AudioIO::pump(const float* input) { impl_->fake.pump(input); }
void AudioIO::noteStopped() {
    impl_->needsReopen = true;
    impl_->fake.noteStopped();
}
bool AudioIO::recover() {
    if (!impl_->needsReopen && !impl_->fake.needsReopen())
        return true;
    impl_->needsReopen = false;
    return open(engine_, settings_);
}

} // namespace ghovlz
