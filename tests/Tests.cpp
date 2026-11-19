#include "core/Types.h"
#include "dsp/Devices.h"
#include "engine/Engine.h"
#include "host/PluginHost.h"
#include "io/WavFile.h"
#include "model/Project.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
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
                if (it.depth() > 3) it.disable_recursion_pending();
            }
        }
        if (!dir.has_parent_path() || dir == dir.root_path()) break;
        dir = dir.parent_path();
    }
    return {};
}
static float rmsRange(const AudioBuffer& a, int from, int to) {
    double s = 0; int n = 0;
    for (int i = from; i < to && i < a.frames; ++i) { s += double(a.ch(0)[i]) * a.ch(0)[i]; ++n; }
    return n ? float(std::sqrt(s / n)) : 0.f;
}
TEST_CASE("bell at 1 kHz boosts about 12 dB") {
    auto eq = createDevice("eq");
    for (int i = 1; i <= 8; ++i) eq->set("on" + std::to_string(i), 0.f);
    eq->set("on1", 1.f); eq->set("type1", 0.f); eq->set("freq1", 1000.f); eq->set("gain1", 12.f); eq->set("q1", 0.707f);
    eq->prepare(48000, 4096);
    AudioBuffer wet(2, 4096), dry(2, 4096);
    for (int i = 0; i < 4096; ++i) {
        float s = 0.2f * std::sin(2.f * 3.14159265f * 1000.f * i / 48000.f);
        wet.ch(0)[i] = wet.ch(1)[i] = dry.ch(0)[i] = dry.ch(1)[i] = s;
    }
    MidiBuffer midi; eq->process(wet, midi);
    float ratio = rmsRange(wet, 2000, 4000) / rmsRange(dry, 2000, 4000);
    REQUIRE(ratio > 3.2f);
    REQUIRE(ratio < 4.6f);
}
TEST_CASE("linear phase eq delays an impulse by about 512 samples") {
    auto eq = createDevice("eq");
    for (int i = 1; i <= 8; ++i) eq->set("on" + std::to_string(i), 0.f);
    eq->set("phase", 1.f);
    eq->prepare(48000, 2048);
    REQUIRE(eq->latency() == 512);
    AudioBuffer a(2, 2048); a.clear(); a.ch(0)[0] = a.ch(1)[0] = 1.f;
    MidiBuffer midi; eq->process(a, midi);
    int peak = 0; float best = 0;
    for (int i = 0; i < a.frames; ++i) if (std::abs(a.ch(0)[i]) > best) { best = std::abs(a.ch(0)[i]); peak = i; }
    REQUIRE(std::isfinite(best));
    REQUIRE(std::abs(peak - 512) < 8);
}
TEST_CASE("compressor pulls a hot sine down") {
    auto d = createDevice("compressor"); d->prepare(48000, 8000);
    AudioBuffer a(2, 8000);
    for (int i = 0; i < 8000; ++i) a.ch(0)[i] = a.ch(1)[i] = 0.8f * std::sin(2.f * 3.14159265f * 200.f * i / 48000.f);
    MidiBuffer midi; d->process(a, midi);
    REQUIRE(rmsRange(a, 4000, 8000) < 0.35f);
}
TEST_CASE("gate silences a whisper") {
    auto d = createDevice("gate"); d->prepare(48000, 8000);
    AudioBuffer a(2, 8000);
    for (int i = 0; i < 8000; ++i) a.ch(0)[i] = a.ch(1)[i] = 0.0001f;
    MidiBuffer midi; d->process(a, midi);
    REQUIRE(rmsRange(a, 4000, 8000) < 0.001f);
}
TEST_CASE("saturator stays finite") {
    auto d = createDevice("saturator"); d->set("drive", 24.f); d->prepare(48000, 2048);
    AudioBuffer a(2, 2048);
    for (int i = 0; i < 2048; ++i) a.ch(0)[i] = a.ch(1)[i] = std::sin(2.f * 3.14159265f * 220.f * i / 48000.f);
    MidiBuffer midi; d->process(a, midi);
    REQUIRE(a.peak() <= 1.05f);
}
TEST_CASE("instruments make sound") {
    for (const char* type : {"wavetable", "sampler", "drumrack"}) {
        auto d = createDevice(type); d->prepare(48000, 8000);
        AudioBuffer a(2, 8000); a.clear();
        MidiBuffer midi; midi.add(0, 0x90, std::string(type)=="drumrack"?36:60, 100);
        d->process(a, midi);
        REQUIRE(a.peak() > 0.02f);
    }
}
TEST_CASE("transport and metronome") {
    auto project = std::make_shared<Project>(Project::makeEmpty());
    project->bpm = 120; project->metronome = true; project->loop = true; project->loopStart = 0; project->loopEnd = 8;
    Engine engine; engine.setProject(project); engine.prepare(48000, 512); engine.seek(0); engine.play();
    std::vector<float> out(48000 * 2);
    engine.process(out.data(), nullptr, 48000);
    REQUIRE(engine.playhead() == Catch::Approx(2.0).margin(0.05));
    float start = 0, beat = 0;
    for (int i = 0; i < 200; ++i) start = std::max(start, std::abs(out[i*2]));
    for (int i = 23900; i < 24200; ++i) beat = std::max(beat, std::abs(out[i*2]));
    REQUIRE(start > 0.1f); REQUIRE(beat > 0.1f);
}
TEST_CASE("arrangement audio plays") {
    auto project = std::make_shared<Project>(Project::makeEmpty());
    project->bpm = 120; project->metronome = false;
    auto m = std::make_shared<Media>(); m->id="tone"; m->sampleRate=48000; m->channels=2; m->interleaved.resize(48000);
    for (int i = 0; i < 24000; ++i) { float s = 0.6f * std::sin(2.f*3.14159265f*220.f*i/48000.f); m->interleaved[i*2]=m->interleaved[i*2+1]=s; }
    project->media[m->id]=m;
    Track t; t.id="a"; t.type=TrackType::Audio; t.output="master";
    Clip c; c.lengthQ=8; c.mediaId="tone"; t.clips.push_back(c); project->tracks.push_back(t);
    Engine engine; engine.setProject(project); engine.prepare(48000,512); engine.play();
    std::vector<float> out(1024); engine.process(out.data(), nullptr, 512);
    float peak=0; for (float s: out) peak=std::max(peak, std::abs(s));
    REQUIRE(peak > 0.4f);
}
TEST_CASE("pre-fader send versus post-fader send") {
    auto run = [](bool pre) {
        auto project = std::make_shared<Project>(Project::makeEmpty());
        project->bpm=120; project->metronome=false;
        auto m = std::make_shared<Media>(); m->id="tone"; m->sampleRate=48000; m->channels=2; m->interleaved.assign(96000, 0.5f);
        project->media[m->id]=m;
        Track src; src.id="src"; src.type=TrackType::Audio; src.volumeDb=-100.f; src.output="master";
        Clip c; c.lengthQ=8; c.mediaId="tone"; src.clips.push_back(c); src.sends.push_back(SendState{"ret", 0.f, pre});
        Track ret; ret.id="ret"; ret.type=TrackType::Return; ret.output="master";
        project->tracks={src, ret};
        Engine engine; engine.setProject(project); engine.prepare(48000,1024); engine.play();
        std::vector<float> out(2048); engine.process(out.data(), nullptr, 1024);
        float peak=0; for (float s: out) peak=std::max(peak, std::abs(s));
        return peak;
    };
    REQUIRE(run(true) > 0.2f);
    REQUIRE(run(false) < 0.01f);
}
TEST_CASE("delay compensation aligns limiter lookahead") {
    auto project = std::make_shared<Project>(Project::makeEmpty());
    project->bpm=120; project->metronome=false; project->loop=false;
    auto m = std::make_shared<Media>(); m->id="imp"; m->sampleRate=48000; m->channels=2; m->interleaved.assign(96000, 0.f); m->interleaved[0]=m->interleaved[1]=0.5f;
    project->media[m->id]=m;
    Track dry; dry.id="dry"; dry.type=TrackType::Audio; dry.pan=-1.f; dry.output="master";
    Clip c; c.lengthQ=4; c.mediaId="imp"; dry.clips.push_back(c);
    Track wet=dry; wet.id="wet"; wet.pan=1.f;
    DeviceState lim; lim.id="lim"; lim.type="limiter"; lim.name="Limiter"; lim.params["lookahead"]=10.f; lim.params["ceiling"]=0.f;
    wet.devices.push_back(lim);
    project->tracks={dry, wet};
    Engine engine; engine.setProject(project); engine.prepare(48000,2048); engine.seek(0); engine.play();
    std::vector<float> out(4096); engine.process(out.data(), nullptr, 2048);
    int peakL=0, peakR=0; float bL=0, bR=0;
    for (int i=0;i<2048;++i){ if(std::abs(out[i*2])>bL){bL=std::abs(out[i*2]); peakL=i;} if(std::abs(out[i*2+1])>bR){bR=std::abs(out[i*2+1]); peakR=i;} }
    REQUIRE(bL>0.2f); REQUIRE(bR>0.2f); REQUIRE(peakL==480); REQUIRE(peakR==480);
}
TEST_CASE("project round trip") {
    auto project = std::make_shared<Project>(Project::makeEmpty());
    project->name="Round"; project->tracks.resize(1); project->tracks[0].id="t"; project->tracks[0].volumeDb=-6.f;
    DeviceState d; d.id="eq1"; d.type="eq"; d.name="EQ Eight"; d.params["gain3"]=4.5f; project->tracks[0].devices.push_back(d);
    auto dir = fs::temp_directory_path()/"ghovlz-round"; fs::remove_all(dir);
    REQUIRE(project->saveToFolder(dir.string()));
    Project loaded; REQUIRE(loaded.loadFromFolder(dir.string()));
    REQUIRE(loaded.name=="Round");
    REQUIRE(loaded.tracks.at(0).volumeDb == Catch::Approx(-6.f));
    REQUIRE(loaded.tracks.at(0).devices.at(0).params.at("gain3") == Catch::Approx(4.5f));
    fs::remove_all(dir);
}
TEST_CASE("offline wav render") {
    auto project = std::make_shared<Project>(Project::makeShowcase());
    project->loopEnd = 4;
    Engine engine; engine.setProject(project); engine.prepare(48000, 256);
    auto wav = fs::temp_directory_path()/"ghovlz-render.wav";
    REQUIRE(engine.renderToWav(wav.string()));
    WavData data; REQUIRE(readWav(wav.string(), data));
    REQUIRE(data.sampleRate==48000);
    double s=0; for (float v: data.interleaved) s += double(v)*v;
    REQUIRE(std::sqrt(s/data.interleaved.size())>0.001);
    fs::remove(wav);
}
TEST_CASE("vst3 scan blacklist and gain") {
    auto gain = findUp("GHOVLZ Test Gain.vst3");
    auto crash = findUp("GHOVLZ Crash.vst3");
    auto scan = findUp("GhovlzPluginScan");
    REQUIRE(fs::is_directory(gain));
    REQUIRE(fs::is_directory(crash));
    REQUIRE(fs::is_regular_file(scan));
    PluginHost host; host.setScannerExecutable(scan.string());
    auto black = fs::temp_directory_path()/"ghovlz-black.txt"; fs::remove(black);
    host.setBlacklistFile(black.string());
    std::string err;
    REQUIRE(host.scan(gain.string(), err));
    REQUIRE(host.lastName().find("Gain") != std::string::npos);
    REQUIRE_FALSE(host.scan(crash.string(), err));
    REQUIRE(host.isBlacklisted(crash.string()));
    auto dev = host.instantiate(gain.string(), err);
    REQUIRE(dev);
    dev->set("Gain", 0.25f);
    dev->prepare(48000, 512);
    AudioBuffer a(2, 512);
    for (int i=0;i<512;++i) a.ch(0)[i]=a.ch(1)[i]=0.5f*std::sin(2.f*3.14159265f*440.f*i/48000.f);
    MidiBuffer midi; dev->process(a, midi);
    REQUIRE(a.peak() == Catch::Approx(0.25f).margin(0.03f));
    REQUIRE_FALSE(dev->pluginState().empty());
}
