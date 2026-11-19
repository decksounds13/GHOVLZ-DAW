#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ghovlz {

enum class TrackType { Audio, Midi, Group, Return, Master };
enum class MonitorMode { Off, Auto, In };

struct Media {
    std::string id;
    std::string name;
    std::string sourcePath;
    double bpm = 120;
    int sampleRate = 48000;
    int channels = 2;
    std::vector<float> interleaved;
};
using MediaPtr = std::shared_ptr<const Media>;

struct Note {
    double startQ = 0;
    double lengthQ = 1;
    int pitch = 60;
    float velocity = 0.8f;
};

struct WarpMark {
    double sourceQ = 0;
    double outputQ = 0;
};

struct Clip {
    std::string id;
    std::string name;
    double startQ = 0;
    double lengthQ = 4;
    double offsetQ = 0;
    std::string mediaId;
    std::vector<Note> notes;
    std::vector<WarpMark> warp;
    uint32_t color = 0xFFB49637;
};

struct DeviceState {
    std::string id;
    std::string type;
    std::string name;
    bool bypass = false;
    std::string pluginPath;
    std::string state;
    std::map<std::string, float> params;
};

struct SendState {
    std::string target;
    float levelDb = -100.f;
    bool pre = false;
};

struct Track {
    std::string id;
    std::string name;
    TrackType type = TrackType::Audio;
    std::string output;
    float volumeDb = 0.f;
    float pan = 0.f;
    bool mute = false;
    bool solo = false;
    bool arm = false;
    MonitorMode monitor = MonitorMode::Auto;
    uint32_t color = 0xFF3C3732;
    std::vector<DeviceState> devices;
    std::vector<Clip> clips;
    std::vector<std::optional<Clip>> slots;
    std::vector<SendState> sends;
};

struct Marker {
    std::string name;
    double quarter = 0;
    uint32_t color = 0xFFDAA520;
};

struct AutoPoint {
    double quarter = 0;
    float value = 0;
};

struct Lane {
    std::string id;
    bool steps = false;
    std::vector<AutoPoint> points;
};

struct Project {
    int format = 2;
    std::vector<std::string> missingMedia;
    std::string name = "Untitled";
    double bpm = 120;
    int numerator = 4;
    int denominator = 4;
    bool loop = true;
    double loopStart = 0;
    double loopEnd = 16;
    double playhead = 0;
    bool metronome = true;
    float metronomeLevel = 0.35f;
    std::vector<std::string> scenes;
    std::vector<Marker> markers;
    float macros[8] {};
    Track master;
    std::vector<Track> tracks;
    std::vector<Lane> automation;
    std::map<std::string, MediaPtr> media;
    // UI-24. When set, this document wins over the profile theme. Omitted from JSON when off,
    // so projects that never set it stay byte-identical.
    bool themeOverride = false;
    std::string themeDocument;

    Track* findTrack(const std::string& id);
    const Track* findTrack(const std::string& id) const;
    DeviceState* findDevice(const std::string& id);
    std::string newId(const char* prefix);

    bool saveToFolder(const std::string& folder) const;
    bool loadFromFolder(const std::string& folder);
    bool collectAndSave(const std::string& folder);

    static Project makeEmpty();
    static Project makeShowcase();
};

const char* trackTypeName(TrackType t);
TrackType trackTypeFromName(const std::string& s);

} // namespace ghovlz
