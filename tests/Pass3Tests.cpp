#include "audio/AppSettings.h"
#include "audio/AsioConvert.h"
#include "core/RtGuard.h"
#include "dsp/Devices.h"
#include "engine/Engine.h"
#include "host/PluginHost.h"
#include "midi/MidiIO.h"
#include "model/Project.h"
#include "model/Undo.h"
#include "ui/Canvas.h"
#include "ui/Paint.h"
#include "ui/Renderer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace ghovlz;

static fs::path exeDir() {
#if defined(_WIN32)
    wchar_t buf[4096];
    const DWORD n = GetModuleFileNameW(nullptr, buf, 4096);
    if (n == 0)
        return fs::current_path();
    return fs::path(buf).parent_path();
#else
    return fs::canonical("/proc/self/exe").parent_path();
#endif
}

static fs::path findUp(const std::string& name) {
    fs::path dir = exeDir();
    for (int i = 0; i < 6; ++i) {
        if (fs::exists(dir)) {
            for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied);
                 it != fs::recursive_directory_iterator(); ++it) {
                if (it->path().filename() == name)
                    return it->path();
#if defined(_WIN32)
                if (it->path().filename() == name + ".exe")
                    return it->path();
#endif
                if (it.depth() > 3)
                    it.disable_recursion_pending();
            }
        }
        if (!dir.has_parent_path() || dir == dir.root_path())
            break;
        dir = dir.parent_path();
    }
    return {};
}

static std::shared_ptr<Media> dcMedia(int frames, float value) {
    auto media = std::make_shared<Media>();
    media->id = "dc";
    media->sampleRate = 48000;
    media->channels = 2;
    media->interleaved.assign(static_cast<size_t>(frames) * 2, value);
    return media;
}

TEST_CASE("set during playback stays off the smoothed value and the analyzer is readable") {
    auto project = std::make_shared<Project>(Project::makeEmpty());
    Track track;
    track.id = "a";
    track.arm = true;
    DeviceState util;
    util.id = "u";
    util.type = "utility";
    DeviceState analyzer;
    analyzer.id = "an";
    analyzer.type = "analyzer";
    track.devices.push_back(util);
    track.devices.push_back(analyzer);
    project->tracks.push_back(track);
    Engine engine;
    engine.setProject(project);
    engine.prepare(48000, 256);
    engine.play();
    const int before = rtGuardHits().load();
    std::atomic<bool> run {true};
    std::thread ui([&] {
        for (int i = 0; i < 20000; ++i) {
            engine.setDeviceParam("u", "gain", (i & 1) ? 0.f : -6.f);
            if (auto* device = engine.device("an"))
                (void)readAnalyzer(*device);
        }
        run.store(false);
    });
    std::vector<float> out(512, 0.f);
    std::vector<float> in(512, 0.2f);
    while (run.load())
        engine.process(out.data(), in.data(), 256);
    ui.join();
    engine.stop();
    REQUIRE(rtGuardHits().load() == before);
    REQUIRE(std::isfinite(out[0]));
}

TEST_CASE("republish keeps the device and the reverb tail") {
    auto project = std::make_shared<Project>(Project::makeEmpty());
    project->bpm = 120;
    Track track;
    track.id = "a";
    track.arm = true;
    track.monitor = MonitorMode::In;
    DeviceState reverb;
    reverb.id = "rev";
    reverb.type = "reverb";
    reverb.params["mix"] = 1.f;
    reverb.params["size"] = 0.9f;
    track.devices.push_back(reverb);
    project->tracks.push_back(track);
    Engine engine;
    engine.setProject(project);
    engine.prepare(48000, 256);
    engine.play();
    Device* first = engine.device("rev");
    REQUIRE(first != nullptr);
    std::vector<float> impulse(512, 0.f);
    impulse[0] = impulse[1] = 1.f;
    std::vector<float> out(512);
    engine.process(out.data(), impulse.data(), 256);
    std::vector<float> silent(512, 0.f);
    for (int i = 0; i < 8; ++i)
        engine.process(out.data(), silent.data(), 256);
    float before = 0.f;
    for (float v : out)
        before = std::max(before, std::abs(v));
    project->playhead = engine.playhead();
    for (int i = 0; i < 1000; ++i)
        engine.setProject(project);
    REQUIRE(engine.device("rev") == first);
    engine.armDeadlines(true);
    std::atomic<int> edits {0};
    std::thread ui([&] {
        while (edits.load() < 1000) {
            engine.setDeviceParam("rev", "mix", 1.f);
            edits.fetch_add(1);
        }
    });
    for (int i = 0; i < 1000; ++i)
        engine.process(out.data(), silent.data(), 64);
    ui.join();
    float after = 0.f;
    for (float v : out)
        after = std::max(after, std::abs(v));
    REQUIRE(before > 0.001f);
    REQUIRE(after > 0.0001f);
#ifndef GHOVLZ_SANITIZER
    REQUIRE(engine.deadlineMisses() == 0);
#endif
}

TEST_CASE("a stepped gain change lands on sample N") {
    const int sizes[] = {32, 64, 256, 1024, 2048};
    for (int frames : sizes) {
        const int n = frames / 2;
        auto project = std::make_shared<Project>(Project::makeEmpty());
        project->bpm = 120;
        project->metronome = false;
        auto media = dcMedia(frames + 8, 1.f);
        project->media[media->id] = media;
        Track track;
        track.id = "a";
        Clip clip;
        clip.id = "c";
        clip.mediaId = media->id;
        clip.startQ = 0;
        clip.lengthQ = 64;
        track.clips.push_back(clip);
        project->tracks.push_back(track);
        Lane lane;
        lane.id = "track:a:gain";
        lane.steps = true;
        const double spq = 48000.0 * 60.0 / 120.0;
        lane.points.push_back(AutoPoint{0, -100.f});
        lane.points.push_back(AutoPoint{n / spq, 0.f});
        project->automation.push_back(lane);
        Engine engine;
        engine.setProject(project);
        engine.prepare(48000, frames);
        engine.seek(0);
        engine.play();
        std::vector<float> out(static_cast<size_t>(frames) * 2);
        engine.process(out.data(), nullptr, frames);
        REQUIRE(std::abs(out[static_cast<size_t>(n - 1) * 2]) < 0.02f);
        REQUIRE(out[static_cast<size_t>(n) * 2] == Catch::Approx(0.7071).margin(0.08));
    }
}

TEST_CASE("sixteen tracks record a bit-exact take and can re-arm") {
    const char* secondsEnv = std::getenv("GHOVLZ_RECORD_SECONDS");
    const int seconds = secondsEnv ? std::max(1, std::atoi(secondsEnv)) : 1;
    auto project = std::make_shared<Project>(Project::makeEmpty());
    project->bpm = 120;
    project->metronome = false;
    for (int i = 0; i < 16; ++i) {
        Track track;
        track.id = "t" + std::to_string(i);
        track.arm = true;
        project->tracks.push_back(track);
    }
    Engine engine;
    engine.setProject(project);
    engine.prepare(48000, 64);
    engine.seek(4);
    engine.play();
    engine.setRecording(true);
    const int frames = 48000 * seconds;
    std::vector<float> block(64 * 2);
    for (int i = 0; i < 64 * 2; ++i)
        block[static_cast<size_t>(i)] = 0.25f;
    for (int done = 0; done < frames; done += 64)
        engine.process(block.data(), block.data(), 64);
    engine.setRecording(false);
    engine.setRecording(true);
    engine.process(block.data(), block.data(), 64);
    engine.stop();
    REQUIRE(engine.recordDrops() == 0);
    REQUIRE(project->tracks[0].clips.size() == 2);
    const int expect = static_cast<int>(0.25f * 8388607.f);
    const float decoded = expect / 8388608.f;
    const auto& mediaId = project->tracks[0].clips[0].mediaId;
    const auto media = project->media.at(mediaId);
    REQUIRE(static_cast<int>(media->interleaved.size() / 2) == frames);
    REQUIRE(media->interleaved[0] == Catch::Approx(decoded).margin(1.0e-6));
    REQUIRE(media->interleaved[100] == Catch::Approx(decoded).margin(1.0e-6));
}

TEST_CASE("ten thousand edits undo back to the same project hash") {
    Project project = Project::makeEmpty();
    Track track;
    track.id = "a";
    Clip clip;
    clip.id = "c";
    clip.startQ = 1;
    clip.lengthQ = 4;
    track.clips.push_back(clip);
    track.sends.push_back(SendState{"ret", -12.f, false});
    DeviceState device;
    device.id = "u";
    device.type = "utility";
    device.params["gain"] = 0.f;
    track.devices.push_back(device);
    project.tracks.push_back(track);
    const uint64_t before = projectHash(project);
    UndoStack stack;
    for (int i = 0; i < 10000; ++i) {
        const int kind = i % 5;
        if (kind == 0)
            stack.setVolume(project, "a", -static_cast<float>(i % 12));
        else if (kind == 1)
            stack.setParam(project, "u", "gain", static_cast<float>(i % 7));
        else if (kind == 2)
            stack.setSend(project, "a", "ret", -static_cast<float>(i % 20));
        else if (kind == 3)
            stack.moveClip(project, "a", "c", 1.0 + (i % 9));
        else
            stack.addDevice(project, "a", "utility");
    }
    while (stack.undo(project)) {
    }
    REQUIRE(projectHash(project) == before);
    REQUIRE(stack.depth() == 0);
}

TEST_CASE("settings fall back and asio selectors convert every sample width") {
    auto dir = fs::temp_directory_path() / "ghovlz-settings.json";
    AppSettings settings;
    settings.backend = "asio";
    settings.device = "missing-box";
    settings.exclusive = true;
    settings.sampleRate = 96000;
    settings.frames = 64;
    settings.midiPorts = {"a", "b"};
    settings.renderer = "warp";
    REQUIRE(settings.save(dir.string()));
    const AppSettings loaded = AppSettings::load(dir.string());
    REQUIRE(loaded.backend == "asio");
    REQUIRE(loaded.frames == 64);
    REQUIRE(loaded.midiPorts.size() == 2);
    int attempts = 0;
    const OpenFallback opened = openOrFallback(settings, [&](const AudioSettings& audio) {
        ++attempts;
        return audio.backend != "asio" && audio.device.empty() && !audio.exclusive;
    });
    REQUIRE(opened.ok);
    REQUIRE(attempts == 2);
    REQUIRE(opened.used.backend == "wasapi");
    REQUIRE_FALSE(opened.message.empty());
    REQUIRE(recoverBackoffMs(0) == 50);
    REQUIRE(recoverBackoffMs(5) == 1600);
    REQUIRE(asioInt16ToFloat(16384) == Catch::Approx(0.5f).margin(0.001));
    const uint8_t bytes[3] = {0, 0, 0x40};
    REQUIRE(asioInt24LsbToFloat(bytes) == Catch::Approx(0.5f).margin(0.001));
    REQUIRE(asioInt32ToFloat(1073741824) == Catch::Approx(0.5f).margin(0.001));
    REQUIRE(asioFloat64ToFloat(0.25) == Catch::Approx(0.25f));
    REQUIRE(asioSelectorReply(static_cast<int>(AsioSelector::Supported)) == 1);
    REQUIRE(asioSelectorNeedsReset(static_cast<int>(AsioSelector::ResetRequest)));
    REQUIRE_FALSE(asioSelectorNeedsReset(static_cast<int>(AsioSelector::Supported)));
    fs::remove(dir);
}

TEST_CASE("midi offsets fan out and short messages pass") {
    REQUIRE(sampleOffsetFromSeconds(0.001, 48000.0) == 48);
    auto project = std::make_shared<Project>(Project::makeEmpty());
    for (int i = 0; i < 2; ++i) {
        Track track;
        track.id = "m" + std::to_string(i);
        track.type = TrackType::Midi;
        track.arm = true;
        DeviceState device;
        device.id = "w" + std::to_string(i);
        device.type = "wavetable";
        track.devices.push_back(device);
        project->tracks.push_back(track);
    }
    Engine engine;
    engine.setProject(project);
    engine.prepare(48000, 256);
    engine.play();
    engine.pushMidi(10, 0xF0, 1, 2, 3);
    engine.pushMidi(4, 0xC0, 2, 0, 2);
    std::vector<float> out(512);
    engine.process(out.data(), nullptr, 256);
    REQUIRE(engine.midiDelivered() == 2);
}

TEST_CASE("format 1 loads, missing media is listed, and ten projects round trip") {
    auto root = fs::temp_directory_path() / "ghovlz-fmt";
    fs::remove_all(root);
    fs::create_directories(root);
    {
        std::ofstream out(root / "project.json");
        out << R"({"format":1,"name":"Old","bpm":100,"tracks":[{"id":"a","name":"A","type":"audio"}],"media":[{"id":"gone","file":"audio/gone.wav"}]})";
    }
    Project old;
    REQUIRE(old.loadFromFolder(root.string()));
    REQUIRE(old.format == 2);
    REQUIRE(old.missingMedia.size() == 1);
    for (int i = 0; i < 10; ++i) {
        Project project = Project::makeShowcase();
        project.name = "Corpus " + std::to_string(i);
        project.bpm = 100 + i;
        auto dir1 = root / ("p" + std::to_string(i) + "a");
        auto dir2 = root / ("p" + std::to_string(i) + "b");
        REQUIRE(project.saveToFolder(dir1.string()));
        Project loaded;
        REQUIRE(loaded.loadFromFolder(dir1.string()));
        REQUIRE(loaded.saveToFolder(dir2.string()));
        std::ifstream a(dir1 / "project.json");
        std::ifstream b(dir2 / "project.json");
        const std::string ta((std::istreambuf_iterator<char>(a)), {});
        const std::string tb((std::istreambuf_iterator<char>(b)), {});
        REQUIRE(ta == tb);
    }
    fs::remove_all(root);
}

TEST_CASE("paint scale matches the 100 percent hit targets") {
    for (float scale : {1.f, 1.5f, 2.f}) {
        const Rect play = hitRect(Hit::Play, scale);
        const Rect base = hitRect(Hit::Play, 1.f);
        REQUIRE(std::abs(play.x - base.x * scale) <= 1.f);
        REQUIRE(std::abs(play.y - base.y * scale) <= 1.f);
        REQUIRE(std::abs(play.w - base.w * scale) <= 1.f);
        REQUIRE(std::abs(play.h - base.h * scale) <= 1.f);
        REQUIRE(hitTest(play.x + 1.f, play.y + 1.f, 1680, 980, scale) == Hit::Play);
    }
    auto canvas = makeSoftwareCanvas(400, 80);
    canvas->setPaintScale(2.f);
    canvas->clear(Color::argb(0xFF000000));
    canvas->fillRect({10.f, 10.f, 20.f, 20.f}, Color::argb(0xFFFF0000));
    const uint8_t* px = softwarePixels(*canvas);
    const uint8_t* unscaled = px + (10 * 400 + 10) * 4;
    const uint8_t* scaled = px + (25 * 400 + 25) * 4;
    REQUIRE(unscaled[0] == 0);
    REQUIRE(scaled[0] == 255);
}

TEST_CASE("arrangement clips and automation stay inside their lane") {
    Project project = Project::makeShowcase();
    auto canvas = makeSoftwareCanvas(1680, 980);
    Frame frame;
    frame.project = &project;
    frame.view = View::Arrangement;
    paintFrame(*canvas, frame);
    const uint8_t* px = softwarePixels(*canvas);
    const float headerX = 156.f;
    const float laneX = headerX + 160.f;
    const float laneR = 1680.f - 86.f;
    const float rowsTop = 54.f + 40.f;
    const float rowsBot = 980.f - 176.f;
    int rows = 1;
    int lead = -1;
    int pads = -1;
    int index = 0;
    for (const auto& t : project.tracks) {
        if (t.type == TrackType::Return)
            continue;
        if (t.id == "lead")
            lead = index;
        if (t.id == "pads")
            pads = index;
        ++index;
        ++rows;
    }
    REQUIRE(lead >= 0);
    REQUIRE(pads == lead + 1);
    const float rowH = (rowsBot - rowsTop) / static_cast<float>(rows);
    auto inLane = [&](int y, int lane) {
        const float top = rowsTop + static_cast<float>(lane) * rowH;
        return y >= static_cast<int>(top) && y < static_cast<int>(top + rowH);
    };
    int amber = 0;
    int body = 0;
    int escaped = 0;
    const int kickY = static_cast<int>(rowsTop + rowH * 0.55f);
    const float padsTop = rowsTop + static_cast<float>(pads) * rowH;
    for (int y = static_cast<int>(rowsTop); y < static_cast<int>(rowsBot); ++y) {
        for (int x = static_cast<int>(laneX); x < 1680; x += 2) {
            const uint8_t* p = px + (y * 1680 + x) * 4;
            if (p[0] != 0xFF || p[1] != 0x82 || p[2] != 0x1E)
                continue;
            if (x >= static_cast<int>(laneR)) {
                ++escaped;
                continue;
            }
            REQUIRE((inLane(y, lead) || inLane(y, pads)));
            if (inLane(y, pads)) {
                REQUIRE(y > static_cast<int>(padsTop) + 8);
                REQUIRE(y < static_cast<int>(padsTop + rowH) - 8);
            }
            ++amber;
        }
        if (y == kickY) {
            for (int x = static_cast<int>(laneX) + 30; x < static_cast<int>(laneX) + 200; x += 2) {
                const uint8_t* p = px + (y * 1680 + x) * 4;
                if (p[0] > 70)
                    ++body;
            }
        }
    }
    REQUIRE(amber > 20);
    REQUIRE(body > 10);
    REQUIRE(escaped == 0);
}

TEST_CASE("moduleinfo is scanned without loading and a hang is blacklisted") {
    const fs::path scanner = findUp("GhovlzPluginScan");
    REQUIRE_FALSE(scanner.empty());
    auto bundle = fs::temp_directory_path() / "ghovlz-info.vst3";
    fs::remove_all(bundle);
    fs::create_directories(bundle / "Contents/Resources");
    {
        std::ofstream out(bundle / "Contents/Resources/moduleinfo.json");
        out << R"({"Name":"Listed Synth","Classes":[{"Name":"Listed Synth","Category":"Audio Module Class"}]})";
    }
    auto blacklist = fs::temp_directory_path() / "ghovlz-info-black.txt";
    fs::remove(blacklist);
    PluginHost host;
    host.setScannerExecutable(scanner.string());
    host.setBlacklistFile(blacklist.string());
    std::string error;
    REQUIRE(host.scan(bundle.string(), error));
    REQUIRE(host.lastName() == "Listed Synth");
    host.setScannerExecutable("/no/such/scanner");
    REQUIRE(host.scan(bundle.string(), error));
    REQUIRE(host.lastName() == "Listed Synth");
    REQUIRE_FALSE(host.scan("", error));
    const fs::path hang = findUp("GHOVLZ Hang.vst3");
    if (!hang.empty()) {
        auto hangList = fs::temp_directory_path() / "ghovlz-hang-black.txt";
        fs::remove(hangList);
        PluginHost killer;
        killer.setScannerExecutable(scanner.string());
        killer.setBlacklistFile(hangList.string());
        const auto started = std::chrono::steady_clock::now();
        REQUIRE_FALSE(killer.scan(hang.string(), error));
        const auto elapsed = std::chrono::steady_clock::now() - started;
        REQUIRE(elapsed < std::chrono::seconds(30));
        REQUIRE(killer.isBlacklisted(hang.string()));
    }
    fs::remove_all(bundle);
}

#if !defined(_WIN32)
TEST_CASE("a scanner name keeps no carriage return") {
    const auto dir = fs::temp_directory_path();
    const auto script = dir / "ghovlz-cr-scan.sh";
    const auto bundle = dir / "ghovlz-cr-plugin.vst3";
    const auto blacklist = dir / "ghovlz-cr-black.txt";
    {
        std::ofstream out(script);
        out << "#!/bin/sh\nprintf 'NAME\\tListed Synth\\r\\nOK\\r\\n'\n";
    }
    fs::permissions(script, fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec);
    fs::remove_all(bundle);
    fs::create_directories(bundle);
    fs::remove(blacklist);
    fs::remove(fs::path(blacklist.string() + ".scan"));
    PluginHost host;
    host.setScannerExecutable(script.string());
    host.setBlacklistFile(blacklist.string());
    std::string error;
    const bool scanned = host.scan(bundle.string(), error);
    INFO(error);
    REQUIRE(scanned);
    REQUIRE(host.lastName() == "Listed Synth");
    host.setScannerExecutable("/no/such/scanner");
    REQUIRE(host.scan(bundle.string(), error));
    REQUIRE(host.lastName() == "Listed Synth");
    fs::remove(script);
    fs::remove_all(bundle);
    fs::remove(blacklist);
    fs::remove(fs::path(blacklist.string() + ".scan"));
}
#endif

TEST_CASE("test synth notes land once and state reaches the controller") {
    const fs::path bundle = findUp("GHOVLZ Test Synth.vst3");
    if (bundle.empty())
        return;
    auto project = std::make_shared<Project>(Project::makeEmpty());
    project->bpm = 120;
    project->metronome = false;
    Track track;
    track.id = "keys";
    track.type = TrackType::Midi;
    track.arm = true;
    DeviceState device;
    device.id = "synth";
    device.type = "vst3";
    device.pluginPath = bundle.string();
    track.devices.push_back(device);
    project->tracks.push_back(track);
    auto plugins = std::make_shared<PluginHost>();
    Engine engine;
    engine.setPluginHost(plugins);
    engine.setProject(project);
    engine.prepare(48000, 64);
    Device* synth = engine.device("synth");
    REQUIRE(synth != nullptr);
    REQUIRE(synth->editParam("Level"));
    synth->set("Level", 0.2f);
    engine.play();
    engine.pushMidi(80, 0x90, 60, 100, 3);
    std::vector<float> out(256 * 2, 0.f);
    engine.process(out.data(), nullptr, 256);
    REQUIRE(out[80 * 2 + 1] == Catch::Approx(0.5f * 0.7071f).margin(0.02));
    int spikes = 0;
    for (int i = 0; i < 256; ++i)
        if (std::abs(out[static_cast<size_t>(i) * 2 + 1]) > 0.2f)
            ++spikes;
    REQUIRE(spikes == 1);
    REQUIRE(out[2] == Catch::Approx(0.12f * 0.7071f).margin(0.02));
    const std::string state = synth->pluginState();
    Project second = Project::makeEmpty();
    Track other = track;
    other.devices[0].state = state;
    second.tracks.push_back(other);
    Engine again;
    again.setPluginHost(plugins);
    again.setProject(std::make_shared<Project>(second));
    again.prepare(48000, 64);
    Device* restored = again.device("synth");
    REQUIRE(restored != nullptr);
    REQUIRE(restored->params[0].value == Catch::Approx(0.2f).margin(0.001));
}

#if defined(_WIN32)
TEST_CASE("UI-20 WARP readback matches a painted fill") {
    RendererDesc desc;
    desc.width = 32;
    desc.height = 16;
    desc.preference = RendererPreference::Warp;
    auto canvas = openRenderer(desc);
    REQUIRE(canvas);
    REQUIRE(std::string(canvas->backendName()) == "WARP");
    canvas->begin();
    canvas->clear(Color::argb(0xFFB49637));
    canvas->end();
    const auto path = std::filesystem::temp_directory_path() / "ghovlz-warp-readback.png";
    REQUIRE(canvas->savePng(path.string()));
    const Image image = loadImageRgba(path.string());
    std::filesystem::remove(path);
    REQUIRE(image.w == 32);
    REQUIRE(image.h == 16);
    REQUIRE(image.rgba.size() >= 4);
    REQUIRE(image.rgba[0] > 150);
    REQUIRE(image.rgba[1] > 120);
    REQUIRE(image.rgba[2] < 90);
}
#endif
