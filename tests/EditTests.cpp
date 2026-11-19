#include "engine/Engine.h"
#include "host/PluginHost.h"
#include "model/Edit.h"
#include "model/Project.h"
#include "model/Undo.h"
#include "ui/ColourDice.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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

static Track audioTrack(const std::string& id) {
    Track t;
    t.id = id;
    t.name = id;
    t.type = TrackType::Audio;
    t.output = "master";
    return t;
}

TEST_CASE("UI-22 seeded EQ dice repeats a ramp and stays inside 2 to 10 stops") {
    const HsvLimit gold{0.08f, 0.14f, 0.45f, 0.85f, 0.35f, 0.95f};
    const DiceRamp a = rollEqRamp(0xC0FFEEULL, gold, true);
    const DiceRamp b = rollEqRamp(0xC0FFEEULL, gold, true);
    REQUIRE(a.count >= 2);
    REQUIRE(a.count <= 10);
    REQUIRE(a.count == b.count);
    REQUIRE(a.stops[0].pos == 0.f);
    REQUIRE(a.stops[a.count - 1].pos == 1.f);
    for (int i = 0; i < a.count; ++i) {
        REQUIRE(a.stops[i].argb == b.stops[i].argb);
        const float expected = static_cast<float>(i) / static_cast<float>(a.count - 1);
        REQUIRE(std::fabs(a.stops[i].pos - expected) < 1e-5f);
    }
    const DiceRamp other = rollEqRamp(0xC0FFEEULL ^ 0x9E3779B97F4A7C15ULL, gold, true);
    bool differs = other.count != a.count;
    for (int i = 0; i < a.count && i < other.count; ++i)
        differs = differs || other.stops[i].argb != a.stops[i].argb;
    REQUIRE(differs);

    const DiceRamp loose = rollEqRamp(42, gold, false);
    REQUIRE(loose.count >= 2);
    REQUIRE(loose.count <= 10);
    REQUIRE(loose.stops[0].pos == 0.f);
    REQUIRE(loose.stops[loose.count - 1].pos == 1.f);
}

TEST_CASE("BRW-01 ARR AUD MID warp edits reject a missing id") {
    Project project = Project::makeEmpty();
    project.tracks.push_back(audioTrack("drums"));
    REQUIRE(dropMedia(project, "missing", 0, "kick", 4) == false);
    REQUIRE(dropMedia(project, "drums", 4, "", 4) == false);
    REQUIRE(dropMedia(project, "drums", 4, "kick", 8));
    REQUIRE(project.tracks[0].clips.size() == 1);
    REQUIRE(project.tracks[0].clips[0].startQ == 4);
    REQUIRE(project.tracks[0].clips[0].mediaId == "kick");
    const std::string id = project.tracks[0].clips[0].id;

    REQUIRE(addNote(project, "drums", id, Note{0.25, 1.0, 36, 0.9f}));
    REQUIRE(addNote(project, "drums", id, Note{1.2, 0.5, 38, 0.8f}));
    REQUIRE(moveNote(project, "drums", id, 1, 1.4, 40));
    REQUIRE(project.tracks[0].clips[0].notes[1].pitch == 40);
    REQUIRE(quantizeNotes(project, "drums", id, 0.5) >= 1);
    const double snapped = project.tracks[0].clips[0].notes[0].startQ;
    REQUIRE(std::fabs(snapped - 0.5) < 1e-6);
    REQUIRE(deleteNote(project, "drums", id, 0));
    REQUIRE(deleteNote(project, "drums", id, 9) == false);
    REQUIRE(quantizeNotes(project, "nope", id, 0.25) == -1);

    REQUIRE(slipClip(project, "drums", id, 1.5));
    REQUIRE(project.tracks[0].clips[0].offsetQ == 1.5);
    REQUIRE(resizeClip(project, "drums", id, 6));
    REQUIRE(project.tracks[0].clips[0].lengthQ == 6);
    REQUIRE(duplicateClip(project, "drums", id));
    REQUIRE(project.tracks[0].clips.size() == 2);
    REQUIRE(project.tracks[0].clips[1].startQ == 10);

    REQUIRE(splitClip(project, "drums", id, 7));
    REQUIRE(project.tracks[0].clips[0].lengthQ == 3);
    REQUIRE(project.tracks[0].clips.size() == 3);
    REQUIRE(splitClip(project, "drums", id, 4) == false);

    REQUIRE(setWarp(project, "drums", id, {WarpMark{4, 8}, WarpMark{0, 0}}));
    REQUIRE(project.tracks[0].clips[0].warp.front().outputQ == 0);
    REQUIRE(std::fabs(mapWarp(project.tracks[0].clips[0].warp, 4) - 2.0) < 1e-6);
    REQUIRE(mapWarp({}, 3.5) == 3.5);
    REQUIRE(setWarp(project, "missing", id, {}) == false);
    REQUIRE(captureSlot(project, "drums", 2));
    REQUIRE(project.tracks[0].slots.size() == 3);
    REQUIRE(project.tracks[0].slots[2].has_value());
    REQUIRE(captureSlot(project, "missing", 0) == false);
}

TEST_CASE("warp markers change which media sample the arrangement reads") {
    auto project = std::make_shared<Project>(Project::makeEmpty());
    project->bpm = 120;
    Track track = audioTrack("a");
    Clip clip;
    clip.id = "c";
    clip.startQ = 0;
    clip.lengthQ = 8;
    clip.mediaId = "ramp";
    clip.warp = {WarpMark{0, 0}, WarpMark{4, 8}};
    track.clips.push_back(clip);
    project->tracks.push_back(track);
    auto media = std::make_shared<Media>();
    media->id = "ramp";
    media->sampleRate = 48000;
    media->channels = 2;
    media->interleaved.assign(200000 * 2, 0.f);
    media->interleaved[48000 * 2] = 0.25f;
    media->interleaved[48000 * 2 + 1] = 0.25f;
    media->interleaved[96000 * 2] = 0.9f;
    media->interleaved[96000 * 2 + 1] = 0.9f;
    project->media["ramp"] = media;

    Engine engine;
    engine.setProject(project);
    engine.prepare(48000, 64);
    engine.seek(4);
    engine.play();
    std::vector<float> out(128, 0.f);
    std::vector<float> in(128, 0.f);
    engine.process(out.data(), in.data(), 64);
    engine.stop();
    REQUIRE(std::fabs(out[0]) > 0.05f);
    REQUIRE(std::fabs(out[0]) < 0.45f);
}

TEST_CASE("SES-01 to SES-04 launch, quantize, stop, and toggle a slot") {
    auto project = std::make_shared<Project>(Project::makeEmpty());
    project->bpm = 120;
    Track track = audioTrack("a");
    Clip clip;
    clip.id = "slot";
    clip.lengthQ = 4;
    clip.mediaId = "dc";
    track.slots.push_back(clip);
    project->tracks.push_back(track);
    auto media = std::make_shared<Media>();
    media->id = "dc";
    media->sampleRate = 48000;
    media->channels = 2;
    media->interleaved.assign(48000 * 2, 0.5f);
    project->media["dc"] = media;

    Engine engine;
    engine.setProject(project);
    engine.prepare(48000, 64);
    engine.launchScene(0);
    REQUIRE(engine.playingScene("a") == 0);
    std::vector<float> out(128, 0.f);
    std::vector<float> in(128, 0.f);
    engine.process(out.data(), in.data(), 64);
    REQUIRE(std::fabs(out[0]) > 0.05f);

    engine.stopSession();
    REQUIRE(engine.playingScene("a") == -1);
    std::fill(out.begin(), out.end(), 0.f);
    engine.process(out.data(), in.data(), 64);
    REQUIRE(std::fabs(out[0]) < 1e-4f);

    engine.seek(1.5);
    engine.launchSceneQuantized(0);
    REQUIRE(engine.playingScene("a") == 0);
    REQUIRE(std::fabs(engine.slotOrigin("a") - 4.0) < 1e-6);

    engine.launchSlot("a", 0);
    REQUIRE(engine.playingScene("a") == -1);
    engine.launchSlot("a", 0);
    REQUIRE(engine.playingScene("a") == 0);
    engine.stopSlot("a");
    REQUIRE(engine.playingScene("a") == -1);
    engine.stop();
}

TEST_CASE("STB-01 undo during playback restores the project and stays finite") {
    auto project = std::make_shared<Project>(Project::makeEmpty());
    project->tracks.push_back(audioTrack("a"));
    const uint64_t before = projectHash(*project);
    Engine engine;
    engine.setProject(project);
    engine.prepare(48000, 128);
    engine.play();
    std::atomic<bool> run {true};
    std::thread audio([&] {
        std::vector<float> out(256, 0.f);
        std::vector<float> in(256, 0.1f);
        while (run.load())
            engine.process(out.data(), in.data(), 128);
    });
    UndoStack stack;
    for (int i = 0; i < 200; ++i)
        stack.setVolume(*project, "a", (i & 1) ? -6.f : 0.f);
    run.store(false);
    audio.join();
    engine.stop();
    while (stack.undo(*project)) {
    }
    REQUIRE(projectHash(*project) == before);
    REQUIRE(std::isfinite(project->tracks[0].volumeDb));
}

TEST_CASE("STB-02 and STB-03 save the edit and ignore a missing id") {
    Project project = Project::makeEmpty();
    project.tracks.push_back(audioTrack("a"));
    REQUIRE(dropMedia(project, "a", 0, "hat", 2));
    const uint64_t hash = projectHash(project);
    const auto dir = fs::temp_directory_path() / "ghovlz-stb-edit";
    fs::remove_all(dir);
    REQUIRE(project.saveToFolder(dir.string()));
    Project loaded = Project::makeEmpty();
    REQUIRE(loaded.loadFromFolder(dir.string()));
    REQUIRE(projectHash(loaded) == hash);
    REQUIRE(loaded.tracks[0].clips.size() == 1);
    fs::remove_all(dir);

    REQUIRE(splitClip(project, "gone", "nope", 1) == false);
    REQUIRE(resizeClip(project, "a", "nope", 1) == false);
    REQUIRE(duplicateClip(project, "a", "nope") == false);
    REQUIRE(slipClip(project, "a", "nope", 1) == false);
    REQUIRE(addNote(project, "a", "nope", Note{}) == false);
    REQUIRE(moveNote(project, "a", "nope", 0, 0, 60) == false);
    REQUIRE(project.tracks[0].clips.size() == 1);
}

TEST_CASE("HOST-07 the gain plugin view attaches and reports a size") {
    const fs::path gain = findUp("GHOVLZ Test Gain.vst3");
    REQUIRE(fs::exists(gain));
    PluginHost host;
    std::string error;
    auto device = host.instantiate(gain.string(), error);
    REQUIRE(device);
    PluginHost::EditorAttach attached;
    void* parent = reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    REQUIRE(host.openEditor(*device, parent, attached, error));
    REQUIRE(attached.open);
    REQUIRE(attached.width == 480);
    REQUIRE(attached.height == 300);
    REQUIRE(host.openEditor(*device, parent, attached, error) == false);
    host.closeEditor(*device);
    REQUIRE(host.openEditor(*device, parent, attached, error));
    host.closeEditor(*device);
}
