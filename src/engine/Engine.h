#pragma once

#include "model/Project.h"
#include "core/Types.h"

#include <atomic>
#include <map>
#include <memory>
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
    void pushMidi(int sampleOffset, uint8_t status, uint8_t data1, uint8_t data2);

    void play();
    void stop();
    void setRecording(bool recording);
    void seek(double quarters);
    bool playing() const { return playing_.load(); }
    bool recording() const { return recording_.load(); }
    double playhead() const { return playhead_.load(); }
    float cpuLoad() const { return cpu_.load(); }
    float meter(const std::string& trackId) const;
    float holdMeter(const std::string& trackId) const;
    bool clipLamp(const std::string& trackId) const;

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
    struct RecSlot {
        std::vector<float> samples;
        std::atomic<int> used {0};
        double startQ = 0;
        int rate = 48000;
        int channels = 2;
    };

private:
    void publish();
    void finalizeTakes();
    std::shared_ptr<Device> makeLive(const DeviceState& state);

    Graph* acquire() const;
    void releaseGraph() const;

    std::shared_ptr<Project> project_;
    mutable std::atomic<Graph*> published_ {nullptr};
    mutable std::atomic<int> readers_ {0};
    std::vector<std::unique_ptr<Graph>> owned_;
    std::shared_ptr<PluginHost> plugins_;
    double sampleRate_ = 48000;
    int block_ = 512;
    std::atomic<bool> playing_ {false};
    std::atomic<bool> recording_ {false};
    std::atomic<double> playhead_ {0};
    std::atomic<float> cpu_ {0};
    bool offline_ = false;

    struct LiveMidi {
        int offset = 0;
        uint8_t status = 0;
        uint8_t d1 = 0;
        uint8_t d2 = 0;
    };
    AudioBuffer mix_;
    MidiBuffer silentMidi_;
    LiveMidi midiRing_[256] {};
    std::atomic<int> midiW_ {0};
    std::atomic<int> midiR_ {0};
    std::map<std::string, DelayLineState> delays_;
    std::map<std::string, std::unique_ptr<RecSlot>> recs_;
};

} // namespace ghovlz
