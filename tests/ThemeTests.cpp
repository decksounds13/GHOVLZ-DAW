#include "engine/Engine.h"
#include "model/Project.h"
#include "ui/Canvas.h"
#include "ui/Paint.h"
#include "ui/Theme.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

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
