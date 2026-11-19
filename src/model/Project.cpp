#include "model/Project.h"
#include "io/WavFile.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string_view>

namespace ghovlz {
namespace {

using json = nlohmann::json;

std::string colorHex(uint32_t c) {
    char b[16];
    std::snprintf(b, sizeof(b), "#%06X", static_cast<unsigned>(c & 0xFFFFFFu));
    return b;
}
uint32_t parseColor(const std::string& s, uint32_t fallback) {
    if (s.size() >= 7 && s[0] == '#')
        return static_cast<uint32_t>(std::strtoul(s.c_str() + 1, nullptr, 16)) | 0xFF000000u;
    return fallback;
}

json clipJson(const Clip& c) {
    json notes = json::array();
    for (const auto& n : c.notes)
        notes.push_back({{"start", n.startQ}, {"length", n.lengthQ}, {"pitch", n.pitch}, {"vel", n.velocity}});
    return {{"id", c.id}, {"name", c.name}, {"start", c.startQ}, {"length", c.lengthQ},
            {"offset", c.offsetQ}, {"media", c.mediaId}, {"color", colorHex(c.color)}, {"notes", notes}};
}
Clip clipFrom(const json& j) {
    Clip c;
    c.id = j.value("id", "");
    c.name = j.value("name", "");
    c.startQ = j.value("start", 0.0);
    c.lengthQ = j.value("length", 4.0);
    c.offsetQ = j.value("offset", 0.0);
    c.mediaId = j.value("media", "");
    c.color = parseColor(j.value("color", "#B49637"), 0xFFB49637);
    if (j.contains("notes")) {
        for (const auto& n : j["notes"])
            c.notes.push_back(Note{n.value("start", 0.0), n.value("length", 1.0), n.value("pitch", 60), n.value("vel", 0.8f)});
    }
    return c;
}
json deviceJson(const DeviceState& d) {
    json params = json::object();
    for (const auto& kv : d.params)
        params[kv.first] = kv.second;
    return {{"id", d.id}, {"type", d.type}, {"name", d.name}, {"bypass", d.bypass},
            {"pluginPath", d.pluginPath}, {"state", d.state}, {"params", params}};
}
DeviceState deviceFrom(const json& j) {
    DeviceState d;
    d.id = j.value("id", "");
    d.type = j.value("type", "missing");
    d.name = j.value("name", d.type);
    d.bypass = j.value("bypass", false);
    d.pluginPath = j.value("pluginPath", "");
    d.state = j.value("state", "");
    if (j.contains("params")) {
        for (auto it = j["params"].begin(); it != j["params"].end(); ++it)
            d.params[it.key()] = it.value().get<float>();
    }
    return d;
}
json trackJson(const Track& t) {
    json clips = json::array();
    for (const auto& c : t.clips) clips.push_back(clipJson(c));
    json slots = json::array();
    for (const auto& s : t.slots) slots.push_back(s ? clipJson(*s) : json(nullptr));
    json devices = json::array();
    for (const auto& d : t.devices) devices.push_back(deviceJson(d));
    json sends = json::array();
    for (const auto& s : t.sends)
        sends.push_back({{"target", s.target}, {"db", s.levelDb}, {"pre", s.pre}});
    const char* monitor = t.monitor == MonitorMode::In ? "in" : t.monitor == MonitorMode::Off ? "off" : "auto";
    return {{"id", t.id}, {"name", t.name}, {"type", trackTypeName(t.type)}, {"output", t.output},
            {"volumeDb", t.volumeDb}, {"pan", t.pan}, {"mute", t.mute}, {"solo", t.solo}, {"arm", t.arm},
            {"monitor", monitor},
            {"color", colorHex(t.color)}, {"devices", devices}, {"clips", clips}, {"slots", slots}, {"sends", sends}};
}
Track trackFrom(const json& j) {
    Track t;
    t.id = j.value("id", "");
    t.name = j.value("name", "Track");
    t.type = trackTypeFromName(j.value("type", "audio"));
    t.output = j.value("output", "");
    t.volumeDb = j.value("volumeDb", 0.f);
    t.pan = j.value("pan", 0.f);
    t.mute = j.value("mute", false);
    t.solo = j.value("solo", false);
    t.arm = j.value("arm", false);
    const std::string monitor = j.value("monitor", "auto");
    t.monitor = monitor == "in" ? MonitorMode::In : monitor == "off" ? MonitorMode::Off : MonitorMode::Auto;
    t.color = parseColor(j.value("color", "#3C3732"), 0xFF3C3732);
    if (j.contains("devices"))
        for (const auto& d : j["devices"]) t.devices.push_back(deviceFrom(d));
    if (j.contains("clips"))
        for (const auto& c : j["clips"]) t.clips.push_back(clipFrom(c));
    if (j.contains("slots"))
        for (const auto& s : j["slots"]) {
            if (s.is_null()) t.slots.push_back(std::nullopt);
            else t.slots.push_back(clipFrom(s));
        }
    if (j.contains("sends"))
        for (const auto& s : j["sends"])
            t.sends.push_back(SendState{s.value("target", ""), s.value("db", -100.f), s.value("pre", false)});
    return t;
}

MediaPtr makeTone(const std::string& id, int rate, int frames, const std::function<float(int)>& fn) {
    auto m = std::make_shared<Media>();
    m->id = id;
    m->sampleRate = rate;
    m->channels = 2;
    m->interleaved.resize(static_cast<size_t>(frames) * 2);
    for (int i = 0; i < frames; ++i) {
        const float s = fn(i);
        m->interleaved[static_cast<size_t>(i) * 2] = s;
        m->interleaved[static_cast<size_t>(i) * 2 + 1] = s;
    }
    return m;
}

} // namespace

const char* trackTypeName(TrackType t) {
    switch (t) {
        case TrackType::Audio: return "audio";
        case TrackType::Midi: return "midi";
        case TrackType::Group: return "group";
        case TrackType::Return: return "return";
        case TrackType::Master: return "master";
    }
    return "audio";
}
TrackType trackTypeFromName(const std::string& s) {
    if (s == "midi") return TrackType::Midi;
    if (s == "group") return TrackType::Group;
    if (s == "return") return TrackType::Return;
    if (s == "master") return TrackType::Master;
    return TrackType::Audio;
}

Track* Project::findTrack(const std::string& id) {
    if (master.id == id) return &master;
    for (auto& t : tracks) if (t.id == id) return &t;
    return nullptr;
}
const Track* Project::findTrack(const std::string& id) const {
    if (master.id == id) return &master;
    for (const auto& t : tracks) if (t.id == id) return &t;
    return nullptr;
}
DeviceState* Project::findDevice(const std::string& id) {
    auto search = [&](Track& t) -> DeviceState* {
        for (auto& d : t.devices) if (d.id == id) return &d;
        return nullptr;
    };
    if (auto* d = search(master)) return d;
    for (auto& t : tracks) if (auto* d = search(t)) return d;
    return nullptr;
}
std::string Project::newId(const char* prefix) {
    static int n = 1;
    return std::string(prefix) + std::to_string(n++);
}

bool Project::saveToFolder(const std::string& folder) const {
    constexpr std::string_view kStaging = ".ghovlz-tmp";
    if (folder.size() < kStaging.size() ||
        std::string_view(folder).substr(folder.size() - kStaging.size()) != kStaging) {
        namespace fs = std::filesystem;
        const std::string tmp = folder + ".ghovlz-tmp";
        std::error_code ec;
        fs::remove_all(tmp, ec);
        if (!saveToFolder(tmp))
            return false;
        const std::string bak = folder + ".ghovlz-bak";
        fs::remove_all(bak, ec);
        if (fs::exists(folder)) {
            fs::rename(folder, bak, ec);
            if (ec)
                return false;
        }
        fs::rename(tmp, folder, ec);
        if (ec)
            return false;
        fs::remove_all(bak, ec);
        return true;
    }
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(folder + "/audio", ec);
    json mediaList = json::array();
    for (const auto& kv : media) {
        const auto& m = kv.second;
        if (!m) continue;
        const std::string rel = "audio/" + kv.first + ".wav";
        const int frames = static_cast<int>(m->interleaved.size() / std::max(1, m->channels));
        if (!writeWav24(folder + "/" + rel, m->interleaved.data(), m->channels, frames, m->sampleRate))
            return false;
        mediaList.push_back({{"id", kv.first}, {"name", m->name}, {"bpm", m->bpm}, {"file", rel},
                             {"sampleRate", m->sampleRate}, {"channels", m->channels}});
    }
    json markersJ = json::array();
    for (const auto& m : markers)
        markersJ.push_back({{"name", m.name}, {"quarter", m.quarter}, {"color", colorHex(m.color)}});
    json lanes = json::array();
    for (const auto& lane : automation) {
        json pts = json::array();
        for (const auto& pt : lane.points) pts.push_back({{"q", pt.quarter}, {"v", pt.value}});
        lanes.push_back({{"id", lane.id}, {"steps", lane.steps}, {"points", pts}});
    }
    json tracksJ = json::array();
    for (const auto& t : tracks) tracksJ.push_back(trackJson(t));
    json macrosJ = json::array();
    for (float m : macros) macrosJ.push_back(m);
    json root = {{"format", format}, {"name", name}, {"bpm", bpm},
                 {"timeSignature", {numerator, denominator}}, {"loop", loop},
                 {"loopStart", loopStart}, {"loopEnd", loopEnd}, {"playhead", playhead},
                 {"metronome", metronome}, {"metronomeLevel", metronomeLevel},
                 {"scenes", scenes}, {"markers", markersJ}, {"macros", macrosJ},
                 {"master", trackJson(master)}, {"tracks", tracksJ},
                 {"automation", lanes}, {"media", mediaList}};
    std::ofstream o(folder + "/project.json");
    if (!o) return false;
    o << root.dump(2);
    return static_cast<bool>(o);
}

bool Project::collectAndSave(const std::string& folder) {
    for (auto& kv : media) {
        if (!kv.second || kv.second->sourcePath.empty() || !kv.second->interleaved.empty())
            continue;
        WavData wav;
        if (!readWav(kv.second->sourcePath, wav))
            continue;
        auto copy = std::make_shared<Media>(*kv.second);
        copy->sampleRate = wav.sampleRate;
        copy->channels = wav.channels;
        copy->interleaved = std::move(wav.interleaved);
        kv.second = copy;
    }
    return saveToFolder(folder);
}

bool Project::loadFromFolder(const std::string& folder) {
    std::ifstream in(folder + "/project.json");
    if (!in) return false;
    json root;
    try {
    in >> root;
    format = root.value("format", 1);
    if (format < 2)
        format = 2;
    missingMedia.clear();
    name = root.value("name", "Untitled");
    bpm = root.value("bpm", 120.0);
    if (root.contains("timeSignature") && root["timeSignature"].size() == 2) {
        numerator = root["timeSignature"][0].get<int>();
        denominator = root["timeSignature"][1].get<int>();
    }
    loop = root.value("loop", true);
    loopStart = root.value("loopStart", 0.0);
    loopEnd = root.value("loopEnd", 16.0);
    playhead = root.value("playhead", 0.0);
    metronome = root.value("metronome", true);
    metronomeLevel = root.value("metronomeLevel", 0.35f);
    scenes.clear();
    if (root.contains("scenes"))
        for (const auto& s : root["scenes"]) scenes.push_back(s.get<std::string>());
    markers.clear();
    if (root.contains("markers"))
        for (const auto& m : root["markers"])
            markers.push_back(Marker{m.value("name", ""), m.value("quarter", 0.0), parseColor(m.value("color", "#DAA520"), 0xFFDAA520)});
    for (float& m : macros) m = 0.f;
    if (root.contains("macros")) {
        int i = 0;
        for (const auto& m : root["macros"]) if (i < 8) macros[i++] = m.get<float>();
    }
    master = root.contains("master") ? trackFrom(root["master"]) : Track{};
    tracks.clear();
    if (root.contains("tracks"))
        for (const auto& t : root["tracks"]) tracks.push_back(trackFrom(t));
    automation.clear();
    if (root.contains("automation")) {
        for (const auto& lane : root["automation"]) {
            Lane L;
            L.id = lane.value("id", "");
            L.steps = lane.value("steps", false);
            if (lane.contains("points"))
                for (const auto& pt : lane["points"])
                    L.points.push_back(AutoPoint{pt.value("q", 0.0), pt.value("v", 0.f)});
            automation.push_back(std::move(L));
        }
    }
    media.clear();
    if (root.contains("media")) {
        for (const auto& m : root["media"]) {
            WavData wav;
            if (!readWav(folder + "/" + m.value("file", ""), wav)) {
                missingMedia.push_back(m.value("id", ""));
                continue;
            }
            auto mediaObj = std::make_shared<Media>();
            mediaObj->id = m.value("id", "");
            mediaObj->name = m.value("name", "");
            mediaObj->bpm = m.value("bpm", 120.0);
            mediaObj->sampleRate = wav.sampleRate;
            mediaObj->channels = wav.channels;
            mediaObj->interleaved = std::move(wav.interleaved);
            media[mediaObj->id] = mediaObj;
        }
    }
    return true;
    } catch (...) {
        return false;
    }
}

Project Project::makeEmpty() {
    Project p;
    p.name = "Empty";
    p.master.id = "master";
    p.master.name = "Master";
    p.master.type = TrackType::Master;
    p.scenes = {"Intro", "Verse", "Build", "Drop", "Break", "Drop 2", "Outro", "End"};
    return p;
}

Project Project::makeShowcase() {
    Project p = makeEmpty();
    p.name = "GHOVLZ Showcase";
    p.bpm = 128;
    p.loop = true;
    p.loopStart = 0;
    p.loopEnd = 128;
    p.playhead = 64;
    p.metronome = false;
    const float macrosIn[8] = {0.62f, 0.48f, 0.55f, 0.40f, 0.33f, 0.58f, 0.70f, 0.46f};
    for (int i = 0; i < 8; ++i) p.macros[i] = macrosIn[i];
    p.markers = {Marker{"Intro", 0, 0xFFFF821E}, Marker{"Verse", 32, 0xFFDAA520},
                 Marker{"Drop", 64, 0xFFFF6E00}, Marker{"Break", 96, 0xFFB49637}};
    const int rate = 48000;
    const double spq = rate * 60.0 / p.bpm;
    auto addMedia = [&](const std::string& id, int frames, const std::function<float(int)>& fn) {
        p.media[id] = makeTone(id, rate, frames, fn);
    };
    const int bar = static_cast<int>(spq * 4);
    addMedia("kick", bar, [&](int i) {
        const int period = std::max(1, static_cast<int>(spq));
        const int ph = i % period;
        return ph < 400 ? std::sin(6.2831853f * 70.f * ph / rate) * std::exp(-ph / 180.f) : 0.f;
    });
    addMedia("drums", bar * 2, [&](int i) {
        const int step = std::max(1, static_cast<int>(spq / 2));
        const int ph = i % step;
        const int which = (i / step) % 4;
        if (which == 0 || which == 2)
            return ph < 300 ? std::sin(6.2831853f * 90.f * ph / rate) * std::exp(-ph / 140.f) : 0.f;
        return ph < 180 ? ((i * 17 % 100) / 50.f - 1.f) * std::exp(-ph / 80.f) * 0.4f : 0.f;
    });
    addMedia("snare", bar, [&](int i) {
        const int period = std::max(1, static_cast<int>(spq * 2));
        const int ph = i % period;
        return ph < 500 ? ((i * 13 % 100) / 50.f - 1.f) * std::exp(-ph / 200.f) * 0.7f : 0.f;
    });
    addMedia("bass", bar * 2, [&](int i) {
        const int n = (i / std::max(1, static_cast<int>(spq))) % 4;
        const float f = n < 2 ? 55.f : 73.4f;
        return 0.45f * std::sin(6.2831853f * f * i / rate);
    });
    addMedia("vox", bar * 2, [&](int i) {
        return 0.18f * std::sin(6.2831853f * 440.f * i / rate) * (0.6f + 0.4f * std::sin(6.2831853f * 3.f * i / rate));
    });
    addMedia("riser", static_cast<int>(spq * 8), [&](int i) {
        const float t = i / static_cast<float>(spq * 8.0);
        const float f = 200.f * std::pow(20.f, t);
        return 0.9f * std::sin(6.2831853f * f * i / static_cast<float>(rate)) * t;
    });
    auto audioClip = [](const std::string& id, const std::string& name, double start, double len,
                        const std::string& mediaId, uint32_t col) {
        Clip c; c.id = id; c.name = name; c.startQ = start; c.lengthQ = len; c.mediaId = mediaId; c.color = col; return c;
    };
    struct Spec { const char* id; const char* name; TrackType type; float vol; uint32_t color; };
    const Spec specs[] = {
        {"kick", "Kick", TrackType::Audio, -3, 0xFF8A5A28},
        {"drums", "Drums", TrackType::Audio, -4, 0xFF6E4A2E},
        {"snare", "Snare", TrackType::Audio, -5, 0xFF7A5340},
        {"bass", "Bass", TrackType::Audio, -4, 0xFF5C4632},
        {"lead", "Synth Lead", TrackType::Midi, -6, 0xFFB49637},
        {"pads", "Pads", TrackType::Midi, -8, 0xFF6A5438},
        {"vox", "Vocals", TrackType::Audio, -5, 0xFF8C6230},
        {"fx", "FX", TrackType::Audio, -2, 0xFFAA6A22},
        {"retA", "A Reverb", TrackType::Return, -4, 0xFF3C3732},
        {"retB", "B Delay", TrackType::Return, -6, 0xFF3C3732},
    };
    for (const auto& s : specs) {
        Track t; t.id = s.id; t.name = s.name; t.type = s.type; t.volumeDb = s.vol; t.color = s.color; t.output = "master";
        p.tracks.push_back(std::move(t));
    }
    auto* kick = p.findTrack("kick");
    auto* drums = p.findTrack("drums");
    auto* snare = p.findTrack("snare");
    auto* bass = p.findTrack("bass");
    auto* lead = p.findTrack("lead");
    auto* pads = p.findTrack("pads");
    auto* vox = p.findTrack("vox");
    auto* fx = p.findTrack("fx");
    auto* retA = p.findTrack("retA");
    auto* retB = p.findTrack("retB");
    kick->clips.push_back(audioClip("c-kick", "Kick Loop", 0, 128, "kick", 0xFF8A5A28));
    drums->clips.push_back(audioClip("c-drums", "Drum Loop", 0, 128, "drums", 0xFF6E4A2E));
    snare->clips.push_back(audioClip("c-snare", "Snare", 0, 128, "snare", 0xFF7A5340));
    bass->clips.push_back(audioClip("c-bass", "Bass Loop", 0, 128, "bass", 0xFF5C4632));
    vox->clips.push_back(audioClip("c-vox", "Vocal", 32, 64, "vox", 0xFF8C6230));
    fx->clips.push_back(audioClip("c-fx", "Riser", 56, 8, "riser", 0xFFFF6E00));
    Clip leadClip;
    leadClip.id = "c-lead"; leadClip.name = "Lead"; leadClip.startQ = 64; leadClip.lengthQ = 32; leadClip.color = 0xFFDAA520;
    const int pitches[] = {60, 64, 67, 71, 69, 67, 65, 64, 62, 67, 71, 70, 69, 67, 64, 60};
    for (int i = 0; i < 16; ++i)
        leadClip.notes.push_back(Note{i * 2.0, 1.6, pitches[i], 0.45f + 0.5f * static_cast<float>((i * 5) % 7) / 6.f});
    lead->clips.push_back(leadClip);
    Clip padClip;
    padClip.id = "c-pad"; padClip.name = "Pad Chords"; padClip.startQ = 32; padClip.lengthQ = 64; padClip.color = 0xFFB49637;
    padClip.notes = {Note{0, 16, 48, 0.5f}, Note{0, 16, 55, 0.45f}, Note{0, 16, 60, 0.4f}, Note{16, 16, 50, 0.5f}, Note{16, 16, 57, 0.4f}};
    pads->clips.push_back(padClip);
    auto dev = [](const char* id, const char* type, const char* name) {
        DeviceState d; d.id = id; d.type = type; d.name = name; return d;
    };
    drums->devices.push_back(dev("d-deq", "eq", "EQ Eight"));
    drums->devices.push_back(dev("d-glue", "compressor", "Glue"));
    bass->devices.push_back(dev("d-satb", "saturator", "Saturator"));
    lead->devices.push_back(dev("d-wt", "wavetable", "Wavetable"));
    lead->devices.back().params = {{"position", 0.62f}, {"wave", 0.35f}, {"unison", 3.f}, {"cutoff", 4200.f}};
    lead->devices.push_back(dev("d-flt", "filter", "Filter"));
    lead->devices.back().params = {{"cutoff", 1800.f}, {"reso", 1.4f}, {"mix", 0.85f}};
    lead->devices.push_back(dev("d-sat", "saturator", "Saturator"));
    lead->devices.back().params = {{"drive", 12.f}, {"tone", 0.15f}, {"mix", 0.7f}};
    pads->devices.push_back(dev("d-pad", "wavetable", "Wavetable"));
    vox->devices.push_back(dev("d-veq", "eq", "EQ Eight"));
    retA->devices.push_back(dev("d-rev", "reverb", "Reverb"));
    retB->devices.push_back(dev("d-del", "delay", "Delay"));
    p.master.devices.push_back(dev("d-meq", "eq", "EQ Eight"));
    p.master.devices.push_back(dev("d-mdyn", "dyn", "DYN"));
    p.master.devices.push_back(dev("d-lim", "limiter", "Limiter"));
    p.master.devices.push_back(dev("d-an", "analyzer", "Analyzer"));
    p.master.volumeDb = -1.f;
    lead->sends.push_back(SendState{"retA", -8.f, false});
    pads->sends.push_back(SendState{"retA", -10.f, false});
    vox->sends.push_back(SendState{"retA", -12.f, false});
    vox->sends.push_back(SendState{"retB", -14.f, true});
    const char* sceneNames[] = {"Intro", "Verse", "Build", "Drop", "Break", "Drop 2", "Outro", "End"};
    for (Track* t : {kick, drums, snare, bass, lead, pads, vox, fx}) {
        t->slots.resize(8);
        for (int s = 0; s < 8; ++s) {
            if (t->clips.empty()) continue;
            Clip slot = t->clips.front();
            slot.id = t->id + std::string("-s") + std::to_string(s);
            slot.name = sceneNames[s];
            slot.startQ = 0;
            slot.lengthQ = 4;
            t->slots[static_cast<size_t>(s)] = slot;
        }
    }
    Lane lane;
    lane.id = "track:lead:gain";
    lane.points = {AutoPoint{64, -8.f}, AutoPoint{80, -2.f}, AutoPoint{96, -6.f}};
    p.automation.push_back(lane);
    Lane cutoff;
    cutoff.id = "track:pads:cutoff";
    cutoff.points = {AutoPoint{0, 0.22f}, AutoPoint{32, 0.55f}, AutoPoint{64, 0.92f}, AutoPoint{96, 0.38f}, AutoPoint{128, 0.6f}};
    p.automation.push_back(cutoff);
    return p;
}

} // namespace ghovlz
