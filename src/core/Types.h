#pragma once

#include "core/RtGuard.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace ghovlz {

struct AudioBuffer {
    int channels = 0;
    int frames = 0;
    std::vector<float> data;

    AudioBuffer() = default;
    AudioBuffer(int ch, int n) { resize(ch, n); }

    void resize(int ch, int n) {
        channels = std::max(0, ch);
        frames = std::max(0, n);
        const size_t need = static_cast<size_t>(channels) * static_cast<size_t>(frames);
        if (data.size() < need)
            data.resize(need);
        std::fill(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(need), 0.f);
    }
    void clear() {
        const size_t need = static_cast<size_t>(channels) * static_cast<size_t>(frames);
        std::fill(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(std::min(need, data.size())), 0.f);
    }
    float* ch(int c) { return data.data() + static_cast<size_t>(c) * static_cast<size_t>(frames); }
    const float* ch(int c) const { return data.data() + static_cast<size_t>(c) * static_cast<size_t>(frames); }
    void addFrom(const AudioBuffer& src, float gain = 1.f) {
        const int nch = std::min(channels, src.channels);
        const int n = std::min(frames, src.frames);
        for (int c = 0; c < nch; ++c) {
            float* d = ch(c);
            const float* s = src.ch(c);
            for (int i = 0; i < n; ++i)
                d[i] += s[i] * gain;
        }
    }
    float peak() const {
        float p = 0.f;
        for (float s : data)
            p = std::max(p, std::abs(s));
        return p;
    }
};

struct MidiEvent {
    int sample = 0;
    uint8_t status = 0;
    uint8_t data1 = 0;
    uint8_t data2 = 0;
    bool isNoteOn() const { return (status & 0xF0) == 0x90 && data2 > 0; }
    bool isNoteOff() const { return (status & 0xF0) == 0x80 || ((status & 0xF0) == 0x90 && data2 == 0); }
};

struct MidiBuffer {
    std::vector<MidiEvent> events;
    void clear() { events.clear(); }
    void add(int sample, uint8_t status, uint8_t d1, uint8_t d2) { events.push_back({sample, status, d1, d2}); }
};

struct Param {
    std::string id;
    std::string name;
    float minV = 0.f;
    float maxV = 1.f;
    float value = 0.f;
    float skew = 1.f;
    // Audio thread reads target and writes smoothed. The message thread writes
    // target only. smoothed is snapped from set() when no callback is running.
    std::atomic<float> target {0.f};
    float smoothed = 0.f;
    std::vector<std::string> choices;

    Param() = default;
    Param(const Param& o)
        : id(o.id), name(o.name), minV(o.minV), maxV(o.maxV), value(o.value), skew(o.skew),
          target(o.target.load(std::memory_order_relaxed)), smoothed(o.smoothed), choices(o.choices) {}
    Param& operator=(const Param& o) {
        if (this != &o) {
            id = o.id;
            name = o.name;
            minV = o.minV;
            maxV = o.maxV;
            value = o.value;
            skew = o.skew;
            target.store(o.target.load(std::memory_order_relaxed), std::memory_order_relaxed);
            smoothed = o.smoothed;
            choices = o.choices;
        }
        return *this;
    }
    Param(Param&& o) noexcept
        : id(std::move(o.id)), name(std::move(o.name)), minV(o.minV), maxV(o.maxV), value(o.value), skew(o.skew),
          target(o.target.load(std::memory_order_relaxed)), smoothed(o.smoothed), choices(std::move(o.choices)) {}
    Param& operator=(Param&& o) noexcept {
        if (this != &o) {
            id = std::move(o.id);
            name = std::move(o.name);
            minV = o.minV;
            maxV = o.maxV;
            value = o.value;
            skew = o.skew;
            target.store(o.target.load(std::memory_order_relaxed), std::memory_order_relaxed);
            smoothed = o.smoothed;
            choices = std::move(o.choices);
        }
        return *this;
    }

    float norm() const {
        if (!choices.empty()) {
            const float span = static_cast<float>(std::max<int>(1, static_cast<int>(choices.size()) - 1));
            return std::clamp(value / span, 0.f, 1.f);
        }
        const float span = maxV - minV;
        float t = span == 0.f ? 0.f : (value - minV) / span;
        t = std::clamp(t, 0.f, 1.f);
        if (skew != 1.f && skew > 0.f)
            t = std::pow(t, skew);
        return t;
    }
    void setNorm(float t) {
        t = std::clamp(t, 0.f, 1.f);
        if (!choices.empty()) {
            value = std::round(t * static_cast<float>(choices.size() - 1));
            return;
        }
        if (skew != 1.f && skew > 0.f)
            t = std::pow(t, 1.f / skew);
        value = minV + t * (maxV - minV);
    }
};

inline float dbToGain(float db) {
    if (db <= -100.f)
        return 0.f;
    return std::pow(10.f, db / 20.f);
}
inline float gainToDb(float g) {
    return 20.f * std::log10(std::max(g, 1.0e-8f));
}

class Device {
public:
    virtual ~Device() = default;
    std::string id;
    std::string type;
    std::string name;
    std::atomic<bool> bypass {false};
    std::vector<Param> params;

    Param* find(const char* pid) {
        if (!pid)
            return nullptr;
        for (auto& p : params)
            if (p.id == pid)
                return &p;
        return nullptr;
    }
    const Param* find(const char* pid) const {
        if (!pid)
            return nullptr;
        for (const auto& p : params)
            if (p.id == pid)
                return &p;
        return nullptr;
    }
    Param* find(const std::string& pid) { return find(pid.c_str()); }
    const Param* find(const std::string& pid) const { return find(pid.c_str()); }
    // Message-thread lookup. The audio callback uses at(), which does not
    // compare names. Calling get() from the callback is a real-time hit.
    float get(const char* pid) const {
        if (rtGuardActive())
            rtGuardHit("param-name");
        if (const auto* p = find(pid))
            return p->smoothed;
        return 0.f;
    }
    float get(const std::string& pid) const { return get(pid.c_str()); }
    float at(int index) {
        if (index < 0 || index >= static_cast<int>(params.size()))
            return 0.f;
        auto& p = params[static_cast<size_t>(index)];
        const float t = p.target.load(std::memory_order_relaxed);
        const float c = (p.smoothed == t) ? 1.f : 0.35f;
        p.smoothed += (t - p.smoothed) * c;
        return p.smoothed;
    }
    void set(const char* pid, float v) {
        if (auto* p = find(pid)) {
            p->value = v;
            p->target.store(v, std::memory_order_relaxed);
            if (!rtGuardActive())
                p->smoothed = v;
        }
    }
    void set(const std::string& pid, float v) { set(pid.c_str(), v); }

    virtual void prepare(double sampleRate, int maxBlock) = 0;
    virtual void process(AudioBuffer& audio, MidiBuffer& midi) = 0;
    virtual int latency() const { return 0; }
    virtual void loadSample(const float*, int, int) {}
    virtual std::string pluginState() const { return {}; }
    virtual void setPluginState(const std::string&) {}
};

struct AnalyzerSnapshot {
    float spectrum[256] {};
    float scope[512] {};
    float correlation = 0.f;
    float momentaryLufs = -70.f;
    float peakDb = -100.f;
};

} // namespace ghovlz
