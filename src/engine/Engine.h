#pragma once

#include "model/Project.h"
#include "core/Types.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ghovlz {

class PluginHost;

class Engine {
public:
    Engine();
    ~Engine();

    void setPluginHost(std::shared_ptr<PluginHost> host);
    void prepare(double sampleRate, int block);
    void release();
    void setProject(std::shared_ptr<Project> project);
    std::shared_ptr<Project> project() const;

    void process(float* interleavedOut, const float* interleavedIn, int frames);

    void play();
    void stop();
    void setRecording(bool recording);
    void seek(double quarters);
    bool playing() const { return playing_.load(); }
    bool recording() const { return recording_.load(); }
    double playhead() const { return playhead_.load(); }
    float cpuLoad() const { return cpu_.load(); }
    float meter(const std::string& trackId) const;

    void setVolumeDb(const std::string& track, float db);
    void setPan(const std::string& track, float pan);
    void setMute(const std::string& track, bool v);
    void setSolo(const std::string& track, bool v);
    void setArm(const std::string& track, bool v);
    void setSend(const std::string& track, const std::string& target, float db, bool pre);
    void addDevice(const std::string& track, const std::string& type);
    bool addPlugin(const std::string& track, const std::string& path, std::string& error);
    void setBypass(const std::string& deviceId, bool bypass);
    void setDeviceParam(const std::string& deviceId, const std::string& param, float value);
    Device* device(const std::string& id);

    void launchScene(int scene);
    void launchSlot(const std::string& track, int scene);
    bool saveProject(const std::string& folder);
    bool loadProject(const std::string& folder);
    bool renderToWav(const std::string& path);

    struct Graph;
    struct DelayLineState {
        int cap = 0;
        int w = 0;
        int delay = -1;
        std::vector<float> data[2];
    };

private:
    void publish();
    void finalizeTakes();
    std::shared_ptr<Device> makeLive(const DeviceState& state);

    std::shared_ptr<Project> project_;
    std::shared_ptr<Graph> graph_;
    std::shared_ptr<PluginHost> plugins_;
    double sampleRate_ = 48000;
    int block_ = 512;
    std::atomic<bool> playing_ {false};
    std::atomic<bool> recording_ {false};
    std::atomic<double> playhead_ {0};
    std::atomic<float> cpu_ {0};
    bool offline_ = false;

    std::map<std::string, DelayLineState> delays_;
    std::mutex recMu_;
    struct Take {
        std::vector<float> interleaved;
        int rate = 48000;
    };
    std::map<std::string, Take> takes_;
};

} // namespace ghovlz
