#include "ui/Theme.h"

#include "core/RtGuard.h"
#include "ui/ColourDice.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

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
    t.locked[static_cast<int>(Role::Gold)] = 1;
    t.locked[static_cast<int>(Role::Record)] = 1;
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
        t.brandBand = root.value("brandBand", true);
        t.contrast = root.value("contrast", true);
        if (root.contains("roles")) {
            for (const auto& info : kRoles) {
                if (root["roles"].contains(info.key))
                    t.color(info.role) = parseHex(root["roles"][info.key].get<std::string>());
            }
        }
        if (root.contains("locked")) {
            for (const auto& key : root["locked"]) {
                for (const auto& info : kRoles)
                    if (key.get<std::string>() == info.key)
                        t.locked[static_cast<int>(info.role)] = 1;
            }
        }
        if (root.contains("imported")) {
            for (auto it = root["imported"].begin(); it != root["imported"].end(); ++it) {
                const Color c = parseHex(it.value().get<std::string>());
                t.imported[it.key()] = (uint32_t(c.a) << 24) | (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | c.b;
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

namespace {

uint32_t pack(Color c) {
    return (uint32_t(c.a) << 24) | (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | c.b;
}

std::string hex6(uint32_t argb) {
    char b[8];
    std::snprintf(b, sizeof(b), "%06X", static_cast<unsigned>(argb & 0xFFFFFFu));
    return b;
}

nlohmann::json themeJson(const Theme& t) {
    nlohmann::json roles = nlohmann::json::object();
    nlohmann::json locked = nlohmann::json::array();
    for (const auto& info : kRoles) {
        roles[info.key] = "#" + hex6(pack(t.color(info.role)));
        if (t.locked[static_cast<int>(info.role)])
            locked.push_back(info.key);
    }
    nlohmann::json stops = nlohmann::json::array();
    for (const auto& s : t.meter.stops)
        stops.push_back({s.db, "#" + hex6(pack(s.color))});
    nlohmann::json imported = nlohmann::json::object();
    for (const auto& kv : t.imported)
        imported[kv.first] = "#" + hex6(kv.second);
    return {{"format", "ghovlz-theme"},
            {"version", t.version},
            {"id", t.id},
            {"name", t.name},
            {"seed", t.seed},
            {"brandBand", t.brandBand},
            {"contrast", t.contrast},
            {"roles", roles},
            {"locked", locked},
            {"imported", imported},
            {"meter", {{"id", t.meter.id},
                       {"stops", stops},
                       {"bg", "#" + hex6(pack(t.meter.background))},
                       {"clip", "#" + hex6(pack(t.meter.clip))},
                       {"hold", "#" + hex6(pack(t.meter.peakHold))},
                       {"rms", "#" + hex6(pack(t.meter.rms))},
                       {"floorDb", t.meter.floorDb},
                       {"ceilDb", t.meter.ceilDb}}}};
}

const Role* roleForEqAttr(const std::string& attr) {
    static const std::pair<const char*, Role> map[] = {
        {"PluginBackground", Role::Bg},
        {"MenuBackgroundGradientColor1", Role::Bg},
        {"PluginBackground2", Role::Panel},
        {"MenuBackgroundGradientColor2", Role::Panel2},
        {"MenuThinBorderColor", Role::Line},
        {"MenuTabBarBorderColor", Role::Line},
        {"GraphBackground", Role::Well},
        {"MenuButtonTextColor1", Role::Ink},
        {"PluginButtonText", Role::Ink},
        {"MenuLabelTextColor1", Role::Dim},
        {"GraphAxisText", Role::Dim},
        {"PluginBrandText", Role::Gold},
        {"GraphSumCurve", Role::Rod},
        {"PluginButtonAccent", Role::Amber},
        {"KnobArc", Role::Arc},
        {"KnobTint", Role::Knob},
        {"GraphHandleText", Role::Pointer},
        {"MeterClip", Role::Record},
        {"MenuListBoxSelectionColor1", Role::Selection},
        {"PluginButtonBackground", Role::Button},
    };
    for (const auto& row : map)
        if (attr == row.first)
            return &row.second;
    return nullptr;
}

struct XmlNode {
    std::string name;
    std::map<std::string, std::string> attrs;
    std::vector<XmlNode> kids;
};

std::vector<XmlNode> parseXml(const std::string& xml) {
    std::vector<XmlNode> roots;
    std::vector<XmlNode*> stack;
    for (size_t i = 0; i < xml.size(); ++i) {
        if (xml[i] != '<')
            continue;
        if (i + 1 < xml.size() && (xml[i + 1] == '?' || xml[i + 1] == '!'))
            continue;
        const bool closing = i + 1 < xml.size() && xml[i + 1] == '/';
        size_t j = i + 1 + (closing ? 1 : 0);
        while (j < xml.size() && xml[j] != '>' && !std::isspace(static_cast<unsigned char>(xml[j])))
            ++j;
        const std::string name = xml.substr(i + 1 + (closing ? 1 : 0), j - (i + 1 + (closing ? 1 : 0)));
        XmlNode node;
        node.name = name;
        bool self = false;
        while (j < xml.size() && xml[j] != '>') {
            while (j < xml.size() && std::isspace(static_cast<unsigned char>(xml[j])))
                ++j;
            if (j >= xml.size() || xml[j] == '>')
                break;
            if (xml[j] == '/') {
                self = true;
                ++j;
                continue;
            }
            const size_t k = j;
            while (j < xml.size() && xml[j] != '=' && !std::isspace(static_cast<unsigned char>(xml[j])))
                ++j;
            const std::string key = xml.substr(k, j - k);
            while (j < xml.size() && xml[j] != '"' && xml[j] != '\'')
                ++j;
            if (j >= xml.size())
                break;
            const char q = xml[j];
            const size_t v = ++j;
            while (j < xml.size() && xml[j] != q)
                ++j;
            node.attrs[key] = xml.substr(v, j - v);
            if (j < xml.size())
                ++j;
        }
        if (closing) {
            if (!stack.empty())
                stack.pop_back();
        } else if (self || (j < xml.size() && xml[j] == '>' && j > 0 && xml[j - 1] == '/')) {
            if (stack.empty())
                roots.push_back(std::move(node));
            else
                stack.back()->kids.push_back(std::move(node));
        } else {
            if (stack.empty()) {
                roots.push_back(std::move(node));
                stack.push_back(&roots.back());
            } else {
                stack.back()->kids.push_back(std::move(node));
                stack.push_back(&stack.back()->kids.back());
            }
        }
        if (j < xml.size() && xml[j] == '>')
            i = j;
    }
    return roots;
}

void walk(const XmlNode& node, const std::function<void(const XmlNode&, const XmlNode*)>& fn, const XmlNode* parent) {
    fn(node, parent);
    for (const auto& kid : node.kids)
        walk(kid, fn, &node);
}

uint32_t parseAttrColour(const std::string& text) {
    if (text.empty())
        return 0;
    bool hex = true;
    for (char c : text) {
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            hex = false;
    }
    if (hex && (text.size() == 6 || text.size() == 8)) {
        const Color c = parseHex(text);
        return pack(c);
    }
    const long value = std::strtol(text.c_str(), nullptr, 10);
    return static_cast<uint32_t>(value);
}

} // namespace

std::string Theme::document() const { return themeJson(*this).dump(2); }

Theme Theme::fromDocument(const std::string& text) {
    std::istringstream in(text);
    nlohmann::json root;
    try {
        in >> root;
    } catch (...) {
        return defaultTheme();
    }
    // Reuse the file parser by writing through the same object shape.
    Theme t = defaultTheme();
    t.id = root.value("id", t.id);
    t.name = root.value("name", t.name);
    t.seed = root.value("seed", 0ull);
    t.brandBand = root.value("brandBand", true);
    t.contrast = root.value("contrast", true);
    if (root.contains("roles")) {
        for (const auto& info : kRoles)
            if (root["roles"].contains(info.key))
                t.color(info.role) = parseHex(root["roles"][info.key].get<std::string>());
    }
    if (root.contains("locked")) {
        for (const auto& key : root["locked"])
            for (const auto& info : kRoles)
                if (key.get<std::string>() == info.key)
                    t.locked[static_cast<int>(info.role)] = 1;
    }
    if (root.contains("imported")) {
        for (auto it = root["imported"].begin(); it != root["imported"].end(); ++it) {
            const Color c = parseHex(it.value().get<std::string>());
            t.imported[it.key()] = pack(c);
        }
    }
    if (root.contains("meter"))
        t.meter = meterFromJson(root["meter"]);
    t.bake();
    return t;
}

bool Theme::saveFile(const std::string& path) const {
    std::ofstream out(path);
    if (!out)
        return false;
    out << document();
    return static_cast<bool>(out);
}

std::string Theme::profileDir() {
#if defined(_WIN32)
    const char* profile = std::getenv("USERPROFILE");
    const std::string root = profile ? profile : ".";
    return root + "/AppData/Roaming/GHOVLZ/themes";
#else
    const char* home = std::getenv("HOME");
    const std::string root = home ? home : ".";
    return root + "/.config/ghovlz/themes";
#endif
}

bool Theme::saveToProfile() const {
    std::error_code ec;
    std::filesystem::create_directories(profileDir(), ec);
    if (ec)
        return false;
    const std::string safe = id.empty() ? "theme" : id;
    return saveFile(profileDir() + "/" + safe + ".json");
}

Theme Theme::roll(const Theme& base, uint64_t seed, bool brandBandOn, bool contrastOn) {
    Theme t = base;
    t.seed = seed;
    t.brandBand = brandBandOn;
    t.contrast = contrastOn;
    Pcg32 rng(seed == 0 ? 1 : seed);
    const float h0 = brandBandOn ? 0.08f : 0.f;
    const float h1 = brandBandOn ? 0.14f : 1.f;
    auto paint = [&](float v0, float v1) {
        return Color::argb(hsvToArgb(h0 + rng.unit() * (h1 - h0), 0.35f + rng.unit() * 0.5f, v0 + rng.unit() * (v1 - v0)));
    };
    for (const auto& info : kRoles) {
        if (t.locked[static_cast<int>(info.role)])
            continue;
        if (contrastOn && (info.role == Role::Record || info.role == Role::Armed)) {
            t.color(info.role) = hex(info.role == Role::Record ? 0xDC2828 : 0xFF821E);
            continue;
        }
        if (info.group == RoleGroup::Surface)
            t.color(info.role) = paint(0.04f, 0.16f);
        else if (info.group == RoleGroup::Text) {
            if (contrastOn) {
                const float floor = info.role == Role::Dim ? 0.72f : 0.88f;
                const float hue = h0 + rng.unit() * (h1 - h0);
                t.color(info.role) = Color::argb(hsvToArgb(hue, rng.unit() * 0.18f, floor + rng.unit() * (0.98f - floor)));
            } else {
                t.color(info.role) = paint(0.35f, 0.98f);
            }
        } else
            t.color(info.role) = paint(0.45f, 0.9f);
    }
    for (auto& stop : t.meter.stops) {
        if (contrastOn && stop.db >= t.meter.ceilDb - 0.05f)
            stop.color = hex(0xDC2828);
        else
            stop.color = paint(0.25f, 0.85f);
    }
    if (contrastOn)
        t.meter.clip = hex(0xDC2828);
    t.id = "dice";
    t.name = "Dice";
    t.bake();
    return t;
}

namespace {

float linearChannel(uint8_t channel) {
    const float s = static_cast<float>(channel) / 255.f;
    return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
}

bool sameRgb(Color color, uint32_t rgb) {
    return color.r == static_cast<uint8_t>((rgb >> 16) & 255) && color.g == static_cast<uint8_t>((rgb >> 8) & 255) &&
           color.b == static_cast<uint8_t>(rgb & 255);
}

} // namespace

float contrastRatio(Color a, Color b) {
    const float la = 0.2126f * linearChannel(a.r) + 0.7152f * linearChannel(a.g) + 0.0722f * linearChannel(a.b);
    const float lb = 0.2126f * linearChannel(b.r) + 0.7152f * linearChannel(b.g) + 0.0722f * linearChannel(b.b);
    const float hi = std::max(la, lb);
    const float lo = std::min(la, lb);
    return (hi + 0.05f) / (lo + 0.05f);
}

bool contrastHolds(const Theme& theme) {
    if (!sameRgb(theme.color(Role::Record), 0xDC2828) || !sameRgb(theme.color(Role::Armed), 0xFF821E) ||
        !sameRgb(theme.meter.clip, 0xDC2828))
        return false;
    const Role text[] = {Role::Ink, Role::Dim};
    const Role ground[] = {Role::Bg, Role::Panel};
    for (Role fg : text) {
        for (Role bg : ground) {
            const float need = fg == Role::Dim ? 3.f : 4.5f;
            if (contrastRatio(theme.color(fg), theme.color(bg)) < need)
                return false;
        }
    }
    return true;
}

std::vector<Theme> importEqThemes(const std::string& xml) {
    std::vector<Theme> out;
    const auto roots = parseXml(xml);
    auto take = [&](const XmlNode& node) {
        if (node.name != "Theme")
            return;
        Theme t = Theme::defaultTheme();
        const auto name = node.attrs.count("Name") ? node.attrs.at("Name") : node.attrs.count("name") ? node.attrs.at("name") : "EQ";
        t.name = name;
        t.id = name;
        for (const auto& kv : node.attrs) {
            if (kv.first == "Name" || kv.first == "name" || kv.first == "Created" || kv.first == "Modified" ||
                kv.first == "ColorAttributeNames")
                continue;
            const uint32_t colour = parseAttrColour(kv.second);
            t.imported[kv.first] = colour;
            if (const Role* role = roleForEqAttr(kv.first))
                t.color(*role) = Color::argb(colour | 0xFF000000u);
            if (kv.first == "MeterClip")
                t.meter.clip = Color::argb(colour | 0xFF000000u);
            if (kv.first == "MeterBackground")
                t.meter.background = Color::argb(colour | 0xFF000000u);
            if (kv.first == "MeterFill" && !t.meter.stops.empty())
                t.meter.stops.front().color = Color::argb(colour | 0xFF000000u);
        }
        for (int i = 1; i <= 8; ++i) {
            const std::string key = "GraphBand" + std::to_string(i);
            const auto it = t.imported.find(key);
            if (it == t.imported.end())
                continue;
            t.color(static_cast<Role>(static_cast<int>(Role::Track0) + (i - 1))) = Color::argb(it->second | 0xFF000000u);
        }
        t.bake();
        out.push_back(std::move(t));
    };
    for (const auto& root : roots)
        walk(root, [&](const XmlNode& node, const XmlNode*) { take(node); }, nullptr);
    return out;
}

std::string exportEqThemes(const std::vector<Theme>& themes) {
    std::ostringstream xml;
    xml << "<Presets>\n";
    for (const auto& theme : themes) {
        xml << "  <Theme Name=\"" << theme.name << "\"";
        for (const auto& kv : theme.imported)
            xml << " " << kv.first << "=\"" << hex6(kv.second) << "\"";
        xml << "/>\n";
    }
    xml << "</Presets>\n";
    return xml.str();
}

std::vector<EqRamp> importEqRamps(const std::string& xml) {
    std::vector<EqRamp> out;
    const auto roots = parseXml(xml);
    auto takePreset = [&](const XmlNode& node) {
        if (node.name != "Preset")
            return;
        EqRamp ramp;
        ramp.name = node.attrs.count("name") ? node.attrs.at("name") : "Ramp";
        auto collect = [&](const XmlNode& n, auto&& self) -> void {
            if (n.name == "Stop") {
                EqRampStop stop;
                stop.pos = n.attrs.count("pos") ? std::strtof(n.attrs.at("pos").c_str(), nullptr) : 0.f;
                stop.argb = n.attrs.count("argb") ? parseAttrColour(n.attrs.at("argb")) : 0xFF000000u;
                ramp.stops.push_back(stop);
            }
            for (const auto& kid : n.kids)
                self(kid, self);
        };
        collect(node, collect);
        if (ramp.stops.size() >= 2)
            out.push_back(std::move(ramp));
    };
    for (const auto& root : roots)
        walk(root, [&](const XmlNode& node, const XmlNode*) { takePreset(node); }, nullptr);
    return out;
}

std::string exportEqRamps(const std::vector<EqRamp>& ramps) {
    std::ostringstream xml;
    xml << "<RampPresets>\n";
    for (const auto& ramp : ramps) {
        xml << "  <Preset name=\"" << ramp.name << "\">\n    <Ramp>\n";
        for (const auto& stop : ramp.stops)
            xml << "      <Stop pos=\"" << stop.pos << "\" argb=\"" << static_cast<int32_t>(stop.argb) << "\"/>\n";
        xml << "    </Ramp>\n  </Preset>\n";
    }
    xml << "</RampPresets>\n";
    return xml.str();
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
