#include "engine/Engine.h"
#include "model/Project.h"
#include "ui/Canvas.h"
#include "ui/Paint.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>

using namespace ghovlz;

static bool same(Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

static int countGreen(const uint8_t* px, int n) {
    int hits = 0;
    for (int i = 0; i < n; ++i) {
        const int r = px[i * 4], g = px[i * 4 + 1], b = px[i * 4 + 2];
        if (g > 90 && g > r + 25 && g > b + 25)
            ++hits;
    }
    return hits;
}

static int columnEnergy(const uint8_t* px, int w, int h, int x0, int x1) {
    int sum = 0;
    for (int y = 16; y < h - 8; ++y) {
        for (int x = x0; x < x1 && x < w; ++x) {
            const uint8_t* p = px + (static_cast<size_t>(y) * w + x) * 4;
            sum += int(p[0]) + int(p[1]) + int(p[2]);
        }
    }
    return sum;
}

TEST_CASE("six meter profiles bake the stop list") {
    const auto ids = Theme::builtinIds();
    REQUIRE(ids.size() == 6);
    REQUIRE(ids.front() == "ghovlz-gold");
    for (const auto& id : ids) {
        const Theme* theme = Theme::findBuiltin(id);
        REQUIRE(theme != nullptr);
        REQUIRE(theme->meter.lut.size() == 256);
        const Color floor = theme->meter.colorAt(theme->meter.floorDb);
        const Color ceil = theme->meter.colorAt(theme->meter.ceilDb);
        REQUIRE(same(theme->meter.lut.front(), floor));
        REQUIRE(same(theme->meter.lut.back(), ceil));
        REQUIRE(same(theme->meter.lut[static_cast<size_t>(theme->meter.lutIndex(-60.f))], theme->meter.colorAt(-60.f)));
    }
    const Theme* gold = Theme::findBuiltin("ghovlz-gold");
    REQUIRE(same(gold->meter.colorAt(-60.f), Color::argb(0xFF5C4B1F)));
    REQUIRE(same(gold->meter.colorAt(6.f), Color::argb(0xFFDC2828)));
    const Theme* mono = Theme::findBuiltin("mono-gold");
    REQUIRE(same(mono->meter.colorAt(-20.f), Color::argb(0xFFB49637)));
    REQUIRE(same(mono->meter.colorAt(3.f), Color::argb(0xFFDC2828)));
    const Theme* contrast = Theme::findBuiltin("high-contrast");
    REQUIRE(same(contrast->meter.colorAt(-30.f), Color::argb(0xFFF5F5F5)));
    REQUIRE(same(contrast->meter.colorAt(-10.f), Color::argb(0xFFFFD400)));
    REQUIRE(same(contrast->meter.colorAt(3.f), Color::argb(0xFFFF2D2D)));
}

TEST_CASE("theme provider commits undo and keeps preview off the stack") {
    ThemeProvider provider;
    const uint64_t start = provider.version();
    Theme led = *Theme::findBuiltin("led-classic");
    provider.set(led, ThemeCommit::Preview);
    REQUIRE(provider.current().id == "led-classic");
    REQUIRE(provider.historyDepth() == 0);
    REQUIRE(provider.version() == start + 1);
    provider.set(*Theme::findBuiltin("ember"), ThemeCommit::Commit);
    REQUIRE(provider.historyDepth() == 1);
    REQUIRE(provider.undo());
    REQUIRE(provider.current().id == "ghovlz-gold");
    REQUIRE(provider.redo());
    REQUIRE(provider.current().id == "ember");
}

TEST_CASE("a theme seed is stable and the audio thread is refused") {
    const Theme a = Theme::fromSeed("42");
    const Theme b = Theme::fromSeed("42");
    REQUIRE(a.seed == b.seed);
    REQUIRE(a.seed != 0);
    REQUIRE(same(a.meter.lut[10], b.meter.lut[10]));
    REQUIRE(a.seed != Theme::fromSeed("43").seed);
    const int before = rtGuardHits().load();
    {
        RtGuardSilence quiet;
        RtScope audio;
        ThemeProvider provider;
        provider.current();
    }
    REQUIRE(rtGuardHits().load() > before);
}

TEST_CASE("each built-in meter renders and the columns are independent") {
    auto canvas = makeSoftwareCanvas(160, 360);
    const Theme* gold = Theme::findBuiltin("ghovlz-gold");
    const Theme* led = Theme::findBuiltin("led-classic");
    paintMeterCard(*canvas, *gold, 0.5f, 0.5f, 0.2f, 0.2f, 0.5f, 0.5f, false);
    REQUIRE(countGreen(softwarePixels(*canvas), canvas->width() * canvas->height()) == 0);
    paintMeterCard(*canvas, *led, 0.5f, 0.5f, 0.2f, 0.2f, 0.5f, 0.5f, false);
    REQUIRE(countGreen(softwarePixels(*canvas), canvas->width() * canvas->height()) > 20);
    for (const auto& id : Theme::builtinIds()) {
        paintMeterCard(*canvas, *Theme::findBuiltin(id), 0.8f, 0.05f, 0.f, 0.f, 0.f, 0.f, id == "ghovlz-gold");
        const auto* px = softwarePixels(*canvas);
        const int left = columnEnergy(px, canvas->width(), canvas->height(), 26, 48);
        const int right = columnEnergy(px, canvas->width(), canvas->height(), 58, 78);
        REQUIRE(left > right);
    }
}

TEST_CASE("a meter ramp file loads the profile stops") {
    const auto path = std::filesystem::temp_directory_path() / "ghovlz-theme-one.json";
    {
        std::ofstream out(path);
        out << R"({"id":"custom-gold","name":"Custom","stops":[[-60,"#5C4B1F"],[6,"#DC2828"]],"bg":"#14120E","clip":"#DC2828"})";
    }
    const Theme loaded = Theme::loadFile(path.string());
    REQUIRE(loaded.id == "custom-gold");
    REQUIRE(same(loaded.meter.colorAt(-60.f), Color::argb(0xFF5C4B1F)));
    REQUIRE(same(loaded.meter.colorAt(6.f), Color::argb(0xFFDC2828)));
    std::filesystem::remove(path);
}

TEST_CASE("UI-23 contrast holds across 10000 seeds") {
    const Theme base = Theme::defaultTheme();
    for (int i = 1; i <= 10000; ++i) {
        const uint64_t seed = static_cast<uint64_t>(i) * 0x9E3779B97F4A7C15ULL;
        const bool brand = (i % 2) == 0;
        const Theme rolled = Theme::roll(base, seed == 0 ? 1 : seed, brand, true);
        REQUIRE(contrastHolds(rolled));
        REQUIRE(rolled.seed == (seed == 0 ? 1 : seed));
    }
    const Theme quiet = Theme::roll(base, 7, true, false);
    REQUIRE(quiet.contrast == false);
}

static void clickControl(Frame& frame, Project* project, ThemeProvider* themes, const Control& control) {
    const float x = control.hit.x + control.hit.w * 0.5f;
    const float y = control.hit.y + control.hit.h * 0.5f;
    const std::shared_ptr<Project> keep;
    uiPointer(frame, project, nullptr, keep, themes, PointerEvent{PointerEvent::Phase::Down, x, y, false});
    uiPointer(frame, project, nullptr, keep, themes, PointerEvent{PointerEvent::Phase::Up, x, y, false});
}

TEST_CASE("widgets drag knobs and drive the theme editor") {
    auto project = std::make_shared<Project>(Project::makeShowcase());
    WidgetLayer layer;
    Frame frame;
    frame.project = project.get();
    frame.widgets = &layer;
    frame.view = View::Device;
    auto canvas = makeSoftwareCanvas(1680, 980);
    paintFrame(*canvas, frame);
    const Control* knob = layer.find("dev/d-wt/position");
    REQUIRE(knob);
    REQUIRE(std::fabs(knob->hit.w - 48.f) < 0.1f);
    REQUIRE(knob->hit.y > canvas->height() * 0.45f);
    REQUIRE(knob->hit.y + knob->hit.h < canvas->height() - 80.f);
    const Control* drive = layer.find("dev/d-sat/drive");
    const Control* mix = layer.find("dev/d-sat/mix");
    const Control* out = layer.find("dev/d-sat/out");
    const Control* tone = layer.find("dev/d-sat/tone");
    REQUIRE(drive);
    REQUIRE(mix);
    REQUIRE(out);
    REQUIRE(tone);
    REQUIRE(std::fabs(drive->hit.y - mix->hit.y) < 1.f);
    REQUIRE(std::fabs(out->hit.y - tone->hit.y) < 1.f);
    REQUIRE(std::fabs(drive->hit.x - out->hit.x) < 1.f);
    REQUIRE(mix->hit.x > drive->hit.x + 40.f);
    REQUIRE(out->hit.y > drive->hit.y + 40.f);
    REQUIRE(out->hit.y - drive->hit.y < 140.f);
    const float cx = knob->hit.x + knob->hit.w * 0.5f;
    const float cy = knob->hit.y + knob->hit.h * 0.5f;
    Track* lead = project->findTrack("lead");
    REQUIRE(lead);
    const float before = lead->devices[0].params.at("position");
    const std::shared_ptr<Project> keep;
    REQUIRE(uiPointer(frame, project.get(), nullptr, keep, nullptr, PointerEvent{PointerEvent::Phase::Down, cx, cy, false}));
    REQUIRE(uiPointer(frame, project.get(), nullptr, keep, nullptr, PointerEvent{PointerEvent::Phase::Move, cx, cy - 80.f, false}));
    const float coarse = lead->devices[0].params.at("position");
    REQUIRE(coarse > before + 0.3f);
    uiPointer(frame, project.get(), nullptr, keep, nullptr, PointerEvent{PointerEvent::Phase::Up, cx, cy - 80.f, false});
    lead->devices[0].params["position"] = before;
    paintFrame(*canvas, frame);
    REQUIRE(uiPointer(frame, project.get(), nullptr, keep, nullptr, PointerEvent{PointerEvent::Phase::Down, cx, cy, false}));
    REQUIRE(uiPointer(frame, project.get(), nullptr, keep, nullptr, PointerEvent{PointerEvent::Phase::Move, cx, cy - 80.f, true}));
    const float fine = lead->devices[0].params.at("position");
    REQUIRE(fine > before);
    REQUIRE(fine < coarse);
    uiPointer(frame, project.get(), nullptr, keep, nullptr, PointerEvent{PointerEvent::Phase::Up, cx, cy - 80.f, true});

    frame.view = View::Mixer;
    paintFrame(*canvas, frame);
    const Control* pan = layer.find("mix/lead/pan");
    REQUIRE(pan);
    REQUIRE(std::fabs(pan->hit.w - 72.f) < 0.1f);
    const float px = pan->hit.x + pan->hit.w * 0.5f;
    const float py = pan->hit.y + pan->hit.h * 0.5f;
    const float panBefore = lead->pan;
    REQUIRE(uiPointer(frame, project.get(), nullptr, keep, nullptr, PointerEvent{PointerEvent::Phase::Down, px, py, false}));
    REQUIRE(uiPointer(frame, project.get(), nullptr, keep, nullptr, PointerEvent{PointerEvent::Phase::Move, px, py - 40.f, false}));
    REQUIRE(lead->pan > panBefore);
    uiPointer(frame, project.get(), nullptr, keep, nullptr, PointerEvent{PointerEvent::Phase::Up, px, py - 40.f, false});
    const Control* solo = layer.find("mix/lead/solo");
    REQUIRE(solo);
    REQUIRE(lead->solo == false);
    clickControl(frame, project.get(), nullptr, *solo);
    REQUIRE(lead->solo == true);

    ThemeProvider themes;
    frame.theme = &themes.current();
    frame.view = View::Prefs;
    paintFrame(*canvas, frame);
    const Control* lock = layer.find("theme/lock/gold");
    REQUIRE(lock);
    REQUIRE(lock->on);
    clickControl(frame, project.get(), &themes, *lock);
    REQUIRE(themes.current().locked[static_cast<int>(Role::Gold)] == 0);
    paintFrame(*canvas, frame);
    const Control* seed = layer.find("theme/seed");
    REQUIRE(seed);
    clickControl(frame, project.get(), &themes, *seed);
    REQUIRE(layer.focused() == "theme/seed");
    for (int i = 0; i < 16; ++i)
        uiKey(frame, project.get(), nullptr, keep, &themes, KeyEvent{0, true});
    REQUIRE(uiKey(frame, project.get(), nullptr, keep, &themes, KeyEvent{'4', false}));
    REQUIRE(uiKey(frame, project.get(), nullptr, keep, &themes, KeyEvent{'2', false}));
    REQUIRE(layer.find("theme/seed")->text == "42");
    const Control* dice = layer.find("theme/dice");
    REQUIRE(dice);
    clickControl(frame, project.get(), &themes, *dice);
    REQUIRE(themes.current().seed == 42);
    REQUIRE(contrastHolds(themes.current()));
    const Control* undo = layer.find("theme/undo");
    REQUIRE(undo);
    clickControl(frame, project.get(), &themes, *undo);
    REQUIRE(themes.current().seed != 42);
    const Control* redo = layer.find("theme/redo");
    REQUIRE(redo);
    clickControl(frame, project.get(), &themes, *redo);
    REQUIRE(themes.current().seed == 42);
    paintFrame(*canvas, frame);
    const Control* preset = layer.find("theme/preset/led-classic");
    REQUIRE(preset);
    clickControl(frame, project.get(), &themes, *preset);
    REQUIRE(themes.current().id == "led-classic");
    const Control* swatch = layer.find("theme/swatch/amber");
    REQUIRE(swatch);
    clickControl(frame, project.get(), &themes, *swatch);
    REQUIRE(layer.isSelected("theme/swatch/amber"));
}
