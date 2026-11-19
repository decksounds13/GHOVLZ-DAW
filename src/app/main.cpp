#include "app/Platform.h"
#include "audio/AudioIO.h"
#include "engine/Engine.h"
#include "host/PluginHost.h"
#include "midi/MidiIO.h"
#include "model/Project.h"
#include "ui/Canvas.h"
#include "ui/Paint.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

fs::path exeDir() {
#if defined(_WIN32)
    wchar_t buf[4096];
    DWORD n = GetModuleFileNameW(nullptr, buf, 4096);
    if (n == 0)
        return fs::current_path();
    return fs::path(buf).parent_path();
#else
    std::error_code ec;
    auto p = fs::canonical("/proc/self/exe", ec);
    if (ec)
        return fs::current_path();
    return p.parent_path();
#endif
}

fs::path findBundle() {
    fs::path dir = exeDir();
    for (int up = 0; up < 5 && !dir.empty(); ++up) {
        std::error_code ec;
        if (fs::exists(dir, ec)) {
            for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
                 it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (ec)
                    break;
                if (it->path().filename() == "GHOVLZ Test Gain.vst3")
                    return it->path();
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

void warmMeters(ghovlz::Engine& engine) {
    engine.seek(0);
    engine.play();
    std::vector<float> block(256 * 2);
    for (int i = 0; i < 48; ++i)
        engine.process(block.data(), nullptr, 256);
    engine.setVolumeDb("kick", 6.f);
    for (int i = 0; i < 24; ++i)
        engine.process(block.data(), nullptr, 256);
}

} // namespace

int main(int argc, char** argv) {
    std::string shotDir;
    std::string themeArg;
    bool window = true;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--screenshot" && i + 1 < argc) {
            shotDir = argv[++i];
            window = false;
        } else if (arg == "--theme" && i + 1 < argc) {
            themeArg = argv[++i];
        } else if (arg == "--help") {
            std::cout << "GHOVLZ DAW\n"
                      << "  --screenshot <dir>          write the main views and exit\n"
                      << "  --theme <id|file|seed>      built-in id, theme JSON, or a seed\n";
            return 0;
        }
    }

    auto project = std::make_shared<ghovlz::Project>(ghovlz::Project::makeShowcase());
    ghovlz::Engine engine;
    auto hostPlugins = std::make_shared<ghovlz::PluginHost>();
    engine.setPluginHost(hostPlugins);
    engine.setProject(project);
    engine.prepare(48000, 256);
    const double showAt = project->playhead;
    warmMeters(engine);
    engine.seek(showAt);
    {
        std::vector<float> block(256 * 2);
        for (int i = 0; i < 2; ++i)
            engine.process(block.data(), nullptr, 256);
    }
    engine.seek(showAt);

    ghovlz::ThemeProvider themes;
    if (!themeArg.empty()) {
        if (const ghovlz::Theme* builtin = ghovlz::Theme::findBuiltin(themeArg)) {
            themes.set(*builtin, ghovlz::ThemeCommit::Commit);
            std::cout << "theme builtin " << builtin->id << "\n";
        } else if (fs::exists(themeArg)) {
            themes.set(ghovlz::Theme::loadFile(themeArg), ghovlz::ThemeCommit::Commit);
            std::cout << "theme file " << themes.current().id << "\n";
        } else {
            themes.set(ghovlz::Theme::fromSeed(themeArg), ghovlz::ThemeCommit::Commit);
            std::cout << "theme seed " << themeArg << "\n";
        }
    }

    ghovlz::Frame frame;
    frame.theme = &themes.current();
    frame.project = project.get();
    frame.engine = &engine;
    frame.playing = engine.playing();
    frame.playhead = engine.playhead();
    frame.cpu = std::max(0.04f, engine.cpuLoad());
    frame.backend = "CPU";
    frame.audioBackend = "Off";

    const fs::path bundle = findBundle();
    if (!bundle.empty()) {
        std::string error;
        auto plugin = hostPlugins->instantiate(bundle.string(), error);
        if (plugin) {
            frame.pluginLoaded = true;
            frame.pluginGain = plugin->get("Gain");
        }
    }

    if (!shotDir.empty()) {
        std::error_code ec;
        fs::create_directories(shotDir, ec);
        auto canvas = ghovlz::makeSoftwareCanvas(1680, 980);
        const ghovlz::View views[] = {ghovlz::View::Arrangement, ghovlz::View::Session, ghovlz::View::Perform,
                                      ghovlz::View::Mixer, ghovlz::View::Device, ghovlz::View::Piano,
                                      ghovlz::View::Browser, ghovlz::View::Plugin};
        const char* names[] = {"arrangement.png", "session.png", "perform.png", "mixer.png",
                               "device-chain.png", "piano-roll.png", "browser.png", "plugin.png"};
        for (int i = 0; i < 8; ++i) {
            frame.view = views[i];
            frame.playhead = engine.playhead();
            ghovlz::paintFrame(*canvas, frame);
            const fs::path out = fs::path(shotDir) / names[i];
            if (!canvas->savePng(out.string()))
                std::cerr << "failed to write " << out << "\n";
            else
                std::cout << "wrote " << out << "\n";
        }
        frame.view = ghovlz::View::Arrangement;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 30; ++i)
            ghovlz::paintFrame(*canvas, frame);
        const auto t1 = std::chrono::steady_clock::now();
        const double cold = std::chrono::duration<double, std::milli>(t1 - t0).count() / 30.0;
        const auto t2 = std::chrono::steady_clock::now();
        for (int i = 0; i < 200; ++i)
            ghovlz::paintFrame(*canvas, frame);
        const auto t3 = std::chrono::steady_clock::now();
        const double warm = std::chrono::duration<double, std::milli>(t3 - t2).count() / 200.0;
        std::ofstream bench(fs::path(shotDir) / "frame-times.txt");
        bench << "software canvas arrangement frames\n";
        bench << "first 30 ms " << cold << "\n";
        bench << "next 200 ms " << warm << "\n";
        std::cout << "frame ms cold " << cold << " warm " << warm << "\n";
        return 0;
    }

    if (!window)
        return 0;
    ghovlz::AppHost host;
    host.engine = &engine;
    host.project = project;
    host.frame = frame;
    return ghovlz::runWindow(host);
}
