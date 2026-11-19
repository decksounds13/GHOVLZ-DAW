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
    void pushMidi(int sampleOffset, uint8_t status, uint8_t data1, uint8_t data2, int size = 3);
    int midiDelivered() const { return midiDelivered_.load(); }

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

    // Per-channel levels written by the audio thread. The UI reads them; the theme does not.
    struct TrackLevels {
        float peakL = 0, peakR = 0, rmsL = 0, rmsR = 0, holdL = 0, holdR = 0;
        bool clip = false;
    };
    TrackLevels levels(const std::string& trackId) const;

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
    void launchSceneQuantized(int scene);
    void launchSlot(const std::string& track, int scene);
    void stopSession();
    void stopSlot(const std::string& track);
    int playingScene(const std::string& track);
    double slotOrigin(const std::string& track);
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
        std::vector<float> ring;
        std::atomic<uint32_t> w {0};
        std::atomic<uint32_t> r {0};
        uint32_t mask = 0;
        std::atomic<int> drops {0};
        std::atomic<bool> retired {false};
        std::atomic<bool> drained {false};
        double startQ = 0;
        int rate = 48000;
        int channels = 2;
        int sessionScene = -1;
        std::string track;
        std::string path;
        void* stream = nullptr;
    };

    void armDeadlines(bool on) { deadlines_.store(on); }
    int deadlineMisses() const { return deadlineMisses_.load(); }
    int recordDrops() const { return recordDrops_.load(); }

private:
    void publish();
    void finalizeTakes();
    void ensureWriter();
    std::shared_ptr<Device> makeLive(const DeviceState& state);

    Graph* acquire() const;
    void releaseGraph() const;
    bool graphPinned(std::uintptr_t stamp) const;
    static std::uintptr_t packGraph(Graph* graph, uint32_t seq);
    static Graph* unpackGraph(std::uintptr_t stamp);

    std::shared_ptr<Project> project_;
    // High 16 bits are a publish sequence. Reusing a graph address must not
    // look like the pointer a reader already loaded.
    mutable std::atomic<std::uintptr_t> published_ {0};
    uint32_t publishSeq_ = 1;
    mutable std::atomic<int> readers_ {0};
    // Hazard slots. A reader stores the stamped graph it is using here before touching it.
    // publish() will not destroy a graph while any slot still names that stamp.
    struct Hazard {
        std::atomic<std::uintptr_t> ptr {0};
    };
    mutable Hazard hazards_[8];
    static constexpr std::uintptr_t kClaimed = 1;
    struct PinState {
        Hazard* slot = nullptr;
        Graph* held = nullptr;
        std::uintptr_t stamp = 0;
        int depth = 0;
    };
    static PinState& pinState();
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
        uint8_t size = 3;
    };
    AudioBuffer mix_;
    MidiBuffer silentMidi_;
    LiveMidi midiRing_[256] {};
    std::atomic<int> midiW_ {0};
    std::atomic<int> midiR_ {0};
    std::atomic<int> midiDelivered_ {0};
    std::atomic<bool> deadlines_ {false};
    std::atomic<int> deadlineMisses_ {0};
    std::atomic<bool> writerRun_ {false};
    void* writer_ = nullptr;
    std::map<std::string, DelayLineState> delays_;
    std::map<std::string, std::unique_ptr<RecSlot>> recs_;
    std::vector<std::unique_ptr<RecSlot>> retired_;
    std::mutex recMu_;
    std::atomic<int> recordDrops_ {0};
};

} // namespace ghovlz
