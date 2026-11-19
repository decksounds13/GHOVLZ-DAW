#pragma once

#include "ui/Canvas.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ghovlz {

// Colour roles. The index is the offset into Theme::colors, which is what
// JSON, locks and the editor list walk. Meters are a MeterStyle, not a role.
enum class RoleGroup { Surface, Text, Accent, State, Track };

enum class Role : int {
    Bg = 0,
    Panel,
    Panel2,
    Line,
    Well,
    Ink,
    Dim,
    InkOnAccent,
    Gold,
    Rod,
    Amber,
    Orange,
    Play,
    Record,
    Armed,
    Mute,
    Solo,
    Selection,
    Knob,
    Pointer,
    Arc,
    Button,
    Track0,
    Count = Track0 + 16
};

struct RoleInfo {
    Role role;
    const char* id;
    const char* key;
    RoleGroup group;
};

// id, JSON key, group. Track slots are track0..track15.
inline constexpr RoleInfo kRoles[] = {
    {Role::Bg, "bg", "bg", RoleGroup::Surface},
    {Role::Panel, "panel", "panel", RoleGroup::Surface},
    {Role::Panel2, "panel2", "panel2", RoleGroup::Surface},
    {Role::Line, "line", "line", RoleGroup::Surface},
    {Role::Well, "well", "well", RoleGroup::Surface},
    {Role::Ink, "ink", "ink", RoleGroup::Text},
    {Role::Dim, "dim", "dim", RoleGroup::Text},
    {Role::InkOnAccent, "inkOnAccent", "inkOnAccent", RoleGroup::Text},
    {Role::Gold, "gold", "gold", RoleGroup::Accent},
    {Role::Rod, "rod", "rod", RoleGroup::Accent},
    {Role::Amber, "amber", "amber", RoleGroup::Accent},
    {Role::Orange, "orange", "orange", RoleGroup::Accent},
    {Role::Play, "play", "play", RoleGroup::State},
    {Role::Record, "record", "record", RoleGroup::State},
    {Role::Armed, "armed", "armed", RoleGroup::State},
    {Role::Mute, "mute", "mute", RoleGroup::State},
    {Role::Solo, "solo", "solo", RoleGroup::State},
    {Role::Selection, "selection", "selection", RoleGroup::State},
    {Role::Knob, "knob", "knob", RoleGroup::State},
    {Role::Pointer, "pointer", "pointer", RoleGroup::State},
    {Role::Arc, "arc", "arc", RoleGroup::State},
    {Role::Button, "button", "button", RoleGroup::State},
};

struct TypeScale {
    float display = 30.f;
    float heading = 22.f;
    float label = 18.f;
    float value = 18.f;
    float mono = 18.f;
};

enum class RampInterp { Hard, Linear, Smooth };

struct RampStop {
    float db = -60.f;
    Color color {};
};

// dB stops plus a baked 256-entry LUT. stereoMeter indexes the LUT; it does not
// interpolate per frame. bake() runs on the UI thread.
struct MeterStyle {
    std::string id;
    std::vector<RampStop> stops;
    RampInterp interp = RampInterp::Smooth;
    Color peakHold {};
    Color clip {};
    Color rms {};
    Color background {};
    Color unlit {};
    Color gap {};
    float segmentPx = 4.f;
    float gapPx = 1.f;
    float rmsWidth = 0.45f;
    float clipDb = 0.f;
    float floorDb = -60.f;
    float ceilDb = 6.f;
    float minContrastLitVsBg = 1.4f;
    std::array<Color, 256> lut {};

    void bake();
    Color colorAt(float db) const;
    int lutIndex(float db) const;
};

struct Theme {
    std::string id = "ghovlz-gold";
    std::string name = "GHOVLZ Gold (default)";
    int version = 1;
    uint64_t seed = 0;
    std::array<Color, static_cast<int>(Role::Count)> colors {};
    MeterStyle meter;
    TypeScale type;

    Color color(Role role) const { return colors[static_cast<int>(role)]; }
    Color& color(Role role) { return colors[static_cast<int>(role)]; }

    static const Theme& defaultTheme();
    static const Theme* findBuiltin(std::string_view id);
    static std::vector<std::string> builtinIds();
    static Theme loadFile(const std::string& path);
    static Theme fromSeed(std::string_view seedText);
    void bake() { meter.bake(); }
};

enum class ThemeCommit { Preview, Commit };

// UI thread only. set() bakes the LUT and bumps version so the next frame
// paints the new theme. Preview does not enter the history; Commit does.
class ThemeProvider {
public:
    ThemeProvider();
    const Theme& current() const;
    uint64_t version() const { return version_; }
    void set(Theme theme, ThemeCommit how);
    bool undo();
    bool redo();
    int historyDepth() const { return static_cast<int>(undo_.size()); }

private:
    void guard() const;
    Theme current_;
    Theme previewBase_;
    bool previewing_ = false;
    uint64_t version_ = 1;
    std::vector<Theme> undo_;
    std::vector<Theme> redo_;
};

} // namespace ghovlz
