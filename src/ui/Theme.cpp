#include "ui/Theme.h"

#include "core/RtGuard.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>

namespace ghovlz {
namespace {

Color hex(uint32_t rgb) { return Color::argb(0xFF000000u | (rgb & 0x00FFFFFFu)); }

Color parseHex(const std::string& text) {
    std::string s = text;
    if (!s.empty() && s[0] == '#')
        s.erase(s.begin());
    unsigned value = 0;
    try {
        value = static_cast<unsigned>(std::stoul(s, nullptr, 16));
    } catch (...) {
        return hex(0xB49637);
    }
    if (s.size() <= 6)
        value |= 0xFF000000u;
    return Color::argb(value);
}

float smootherstep(float t) {
    t = std::clamp(t, 0.f, 1.f);
    return t * t * t * (t * (t * 6.f - 15.f) + 10.f);
}

Color lerp(Color a, Color b, float t) {
    auto ch = [](uint8_t x, uint8_t y, float u) {
        return static_cast<uint8_t>(std::lround(x + (static_cast<float>(y) - static_cast<float>(x)) * u));
    };
    return Color{ch(a.r, b.r, t), ch(a.g, b.g, t), ch(a.b, b.b, t), 255};
}

void paintDefaults(Theme& t) {
    t.color(Role::Bg) = hex(0x0A0A0A);
    t.color(Role::Panel) = hex(0x161311);
    t.color(Role::Panel2) = hex(0x221E1A);
    t.color(Role::Line) = hex(0x3C3732);
    t.color(Role::Well) = hex(0x100E0C);
    t.color(Role::Ink) = hex(0xF5F5F5);
    t.color(Role::Dim) = hex(0xA39890);
    t.color(Role::InkOnAccent) = hex(0x1A120C);
    t.color(Role::Gold) = hex(0xB49637);
    t.color(Role::Rod) = hex(0xDAA520);
    t.color(Role::Amber) = hex(0xFF821E);
    t.color(Role::Orange) = hex(0xFF6E00);
    t.color(Role::Play) = hex(0xB49637);
    t.color(Role::Record) = hex(0xDC2828);
    t.color(Role::Armed) = hex(0xFF821E);
    t.color(Role::Mute) = hex(0x3C3223);
    t.color(Role::Solo) = hex(0xB49637);
    t.color(Role::Selection) = hex(0xDAA520);
    t.color(Role::Knob) = hex(0x231F20);
    t.color(Role::Pointer) = hex(0xD1BCAC);
    t.color(Role::Arc) = hex(0xFF6E00);
    t.color(Role::Button) = hex(0x3C3223);
    const uint32_t tracks[] = {0x8A5A28, 0x6E4A2E, 0x7A5340, 0x5C4632, 0xB49637, 0x6A5438, 0x8C6230, 0xAA6A22,
                               0x3C3732, 0x3C3732, 0xB49637, 0x8A5A28, 0x6E4A2E, 0x7A5340, 0x5C4632, 0xDAA520};
    for (int i = 0; i < 16; ++i)
        t.color(static_cast<Role>(static_cast<int>(Role::Track0) + i)) = hex(tracks[i]);
}

MeterStyle style(const char* id, std::initializer_list<std::pair<float, uint32_t>> stops, uint32_t bg, uint32_t unlit,
                 uint32_t gap, uint32_t hold, uint32_t clip, uint32_t rms, float seg, float gapPx, float contrast) {
    MeterStyle m;
    m.id = id;
    for (const auto& s : stops)
        m.stops.push_back(RampStop{s.first, hex(s.second)});
    m.background = hex(bg);
    m.unlit = hex(unlit);
    m.gap = hex(gap);
    m.peakHold = hex(hold);
    m.clip = hex(clip);
    m.rms = hex(rms);
    m.segmentPx = seg;
    m.gapPx = gapPx;
    m.minContrastLitVsBg = contrast;
    m.bake();
    return m;
}

Theme themed(const char* id, const char* name, MeterStyle meter) {
    Theme t;
    t.id = id;
    t.name = name;
    paintDefaults(t);
    t.meter = std::move(meter);
    t.meter.bake();
    return t;
}

const std::vector<Theme>& builtins() {
    static const std::vector<Theme> all = {
        themed("ghovlz-gold", "GHOVLZ Gold (default)",
               style("ghovlz-gold",
                     {{-60.f, 0x5C4B1F}, {-30.f, 0x8C7430}, {-18.f, 0xB49637}, {-9.f, 0xC9A43A}, {-3.f, 0xDAA520},
                      {0.f, 0xFF821E}, {6.f, 0xDC2828}},
                     0x14120E, 0x221D16, 0x0A0A0A, 0xE5BD80, 0xDC2828, 0xD1BCAC, 4.f, 1.f, 2.24f)),
        themed("led-classic", "LED Classic (mixer concept)",
               style("led-classic",
                     {{-60.f, 0x2E7D32}, {-24.f, 0x43A047}, {-12.f, 0xC0CA33}, {-6.f, 0xF2B134}, {-3.f, 0xF57C00},
                      {0.f, 0xDC2828}, {6.f, 0xDC2828}},
                     0x14120E, 0x1B1A15, 0x0A0A0A, 0xF5F5F5, 0xDC2828, 0xF5F5F5, 4.f, 1.f, 3.65f)),
        themed("mono-gold", "Mono Gold",
               style("mono-gold", {{-60.f, 0xB49637}, {0.f, 0xB49637}, {0.01f, 0xDC2828}, {6.f, 0xDC2828}}, 0x14120E,
                     0x201B14, 0x0A0A0A, 0xDAA520, 0xDC2828, 0x6E5C24, 4.f, 1.f, 3.9f)),
        themed("ember", "Ember",
               style("ember",
                     {{-60.f, 0x5E2412}, {-36.f, 0x7A2410}, {-18.f, 0xA8361A}, {-9.f, 0xD2501E}, {-3.f, 0xFF821E},
                      {0.f, 0xFFB43C}, {6.f, 0xDC2828}},
                     0x120D0A, 0x1E1511, 0x070707, 0xFFE2B0, 0xDC2828, 0xFFB43C, 4.f, 1.f, 1.61f)),
        themed("high-contrast", "High Contrast (accessibility)",
               style("high-contrast",
                     {{-60.f, 0xF5F5F5}, {-18.f, 0xF5F5F5}, {-17.99f, 0xFFD400}, {-6.f, 0xFFD400}, {-5.99f, 0xFF8C00},
                      {0.f, 0xFF8C00}, {0.01f, 0xFF2D2D}, {6.f, 0xFF2D2D}},
                     0x000000, 0x1A1A1A, 0x000000, 0xFFFFFF, 0xFF2D2D, 0x3A3A3A, 5.f, 2.f, 5.67f)),
        themed("amber-glow", "Amber Glow (EQ preset)",
               style("amber-glow",
                     {{-60.f, 0x4A2A08}, {-50.f, 0x5A320A}, {-30.3f, 0xA0500A}, {-10.5f, 0xF0A028}, {6.f, 0xFFF0B4}},
                     0x14120E, 0x1C1812, 0x0A0A0A, 0xFFF0B4, 0xDC2828, 0xF0A028, 4.f, 1.f, 1.46f)),
    };
    return all;
}

MeterStyle meterFromJson(const nlohmann::json& j) {
    MeterStyle m;
    m.id = j.value("id", "custom");
    if (j.contains("stops")) {
        for (const auto& s : j["stops"]) {
            if (s.size() < 2)
                continue;
            m.stops.push_back(RampStop{s[0].get<float>(), parseHex(s[1].get<std::string>())});
        }
    }
    if (j.contains("bg"))
        m.background = parseHex(j["bg"].get<std::string>());
    if (j.contains("unlit"))
        m.unlit = parseHex(j["unlit"].get<std::string>());
    if (j.contains("gap"))
        m.gap = parseHex(j["gap"].get<std::string>());
    if (j.contains("hold"))
        m.peakHold = parseHex(j["hold"].get<std::string>());
    if (j.contains("clip"))
        m.clip = parseHex(j["clip"].get<std::string>());
    if (j.contains("rms"))
        m.rms = parseHex(j["rms"].get<std::string>());
    m.gapPx = j.value("gapPx", 1.f);
    m.segmentPx = j.value("segPx", 4.f);
    m.minContrastLitVsBg = j.value("minContrastLitVsBg", 1.4f);
    if (j.contains("floorDb"))
        m.floorDb = j["floorDb"].get<float>();
    if (j.contains("ceilDb"))
        m.ceilDb = j["ceilDb"].get<float>();
    std::sort(m.stops.begin(), m.stops.end(), [](const RampStop& a, const RampStop& b) { return a.db < b.db; });
    m.bake();
    return m;
}

uint64_t fnv(std::string_view text) {
    uint64_t h = 14695981039346656037ull;
    for (unsigned char c : text) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

} // namespace

int MeterStyle::lutIndex(float db) const {
    const float span = std::max(1.0e-3f, ceilDb - floorDb);
    const float t = std::clamp((db - floorDb) / span, 0.f, 1.f);
    return std::clamp(static_cast<int>(std::lround(t * 255.f)), 0, 255);
}

Color MeterStyle::colorAt(float db) const {
    if (stops.empty())
        return clip;
    if (db <= stops.front().db)
        return stops.front().color;
    if (db >= stops.back().db)
        return stops.back().color;
    for (size_t i = 1; i < stops.size(); ++i) {
        if (db > stops[i].db)
            continue;
        const float span = stops[i].db - stops[i - 1].db;
        float t = span <= 1.0e-6f ? 1.f : (db - stops[i - 1].db) / span;
        if (span < 0.05f)
            t = db >= stops[i].db - 1.0e-4f ? 1.f : 0.f;
        else if (interp == RampInterp::Smooth)
            t = smootherstep(t);
        else if (interp == RampInterp::Hard)
            t = t >= 1.f ? 1.f : 0.f;
        return lerp(stops[i - 1].color, stops[i].color, t);
    }
    return stops.back().color;
}

void MeterStyle::bake() {
    for (int i = 0; i < 256; ++i) {
        const float db = floorDb + (static_cast<float>(i) / 255.f) * (ceilDb - floorDb);
        lut[static_cast<size_t>(i)] = colorAt(db);
    }
}

const Theme& Theme::defaultTheme() { return builtins().front(); }

const Theme* Theme::findBuiltin(std::string_view id) {
    for (const auto& t : builtins())
        if (t.id == id)
            return &t;
    return nullptr;
}

std::vector<std::string> Theme::builtinIds() {
    std::vector<std::string> ids;
    for (const auto& t : builtins())
        ids.push_back(t.id);
    return ids;
}

Theme Theme::loadFile(const std::string& path) {
    std::ifstream in(path);
    if (!in)
        return defaultTheme();
    nlohmann::json root;
    try {
        in >> root;
    } catch (...) {
        return defaultTheme();
    }
    Theme t = defaultTheme();
    if (root.contains("format") && root["format"] == "ghovlz-theme") {
        t.id = root.value("id", t.id);
        t.name = root.value("name", t.name);
        t.seed = root.value("seed", 0ull);
        if (root.contains("roles")) {
            for (const auto& info : kRoles) {
                if (root["roles"].contains(info.key))
                    t.color(info.role) = parseHex(root["roles"][info.key].get<std::string>());
            }
        }
        if (root.contains("meter"))
            t.meter = meterFromJson(root["meter"]);
        t.bake();
        return t;
    }
    if (root.contains("profiles") && root["profiles"].is_array() && !root["profiles"].empty()) {
        const auto* chosen = &root["profiles"][0];
        const std::string use = root.value("use", "");
        if (!use.empty()) {
            for (const auto& p : root["profiles"]) {
                if (p.value("id", "") == use) {
                    chosen = &p;
                    break;
                }
            }
        }
        t.meter = meterFromJson(*chosen);
        if (root.contains("floorDb"))
            t.meter.floorDb = root["floorDb"].get<float>();
        if (root.contains("ceilDb"))
            t.meter.ceilDb = root["ceilDb"].get<float>();
        t.meter.bake();
        t.id = t.meter.id;
        t.name = chosen->value("name", t.id);
        return t;
    }
    if (root.contains("stops")) {
        t.meter = meterFromJson(root);
        t.id = t.meter.id;
        t.name = root.value("name", t.id);
    }
    return t;
}

Theme Theme::fromSeed(std::string_view seedText) {
    Theme t = defaultTheme();
    t.seed = fnv(seedText);
    t.id = std::string("seed-") + std::string(seedText);
    t.name = std::string("Seed ") + std::string(seedText);
    const float k = static_cast<float>(t.seed % 97) / 97.f;
    const Color warm = hex(0xFF821E);
    for (auto& stop : t.meter.stops) {
        if (stop.db >= t.meter.ceilDb - 0.01f)
            continue;
        stop.color = lerp(stop.color, warm, k * 0.35f);
    }
    if (!t.meter.stops.empty())
        t.meter.stops.back().color = t.meter.clip;
    t.meter.bake();
    return t;
}

ThemeProvider::ThemeProvider() : current_(Theme::defaultTheme()) {}

void ThemeProvider::guard() const {
    if (rtGuardActive())
        rtGuardHit("theme");
}

const Theme& ThemeProvider::current() const {
    guard();
    return current_;
}

void ThemeProvider::set(Theme theme, ThemeCommit how) {
    guard();
    theme.bake();
    if (how == ThemeCommit::Preview) {
        if (!previewing_) {
            previewBase_ = current_;
            previewing_ = true;
        }
        current_ = std::move(theme);
        ++version_;
        return;
    }
    Theme before = previewing_ ? previewBase_ : current_;
    previewing_ = false;
    if (undo_.size() >= 50)
        undo_.erase(undo_.begin());
    undo_.push_back(std::move(before));
    redo_.clear();
    current_ = std::move(theme);
    ++version_;
}

bool ThemeProvider::undo() {
    guard();
    if (previewing_) {
        current_ = previewBase_;
        previewing_ = false;
        ++version_;
        return true;
    }
    if (undo_.empty())
        return false;
    redo_.push_back(current_);
    current_ = std::move(undo_.back());
    undo_.pop_back();
    current_.bake();
    ++version_;
    return true;
}

bool ThemeProvider::redo() {
    guard();
    if (redo_.empty())
        return false;
    undo_.push_back(current_);
    current_ = std::move(redo_.back());
    redo_.pop_back();
    current_.bake();
    ++version_;
    return true;
}

} // namespace ghovlz
