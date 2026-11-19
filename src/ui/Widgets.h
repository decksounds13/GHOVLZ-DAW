#pragma once

#include "ui/Canvas.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ghovlz {

class Engine;
struct Project;
class ThemeProvider;
struct Frame;

// Map a stored parameter into 0..1 and back. The anchor keeps the domain:
// 0..1 stays linear, 1..36 stays a 36-count, above 36 stays a log frequency,
// and a negative anchor stays a dB value.
float knobNorm(float value);
float knobFromNorm(float norm, float anchor);

enum class ControlKind { Button, Pill, Knob, Field };

struct Control {
    std::string id;
    ControlKind kind = ControlKind::Button;
    Rect hit {};
    float norm = 0.f;
    float value = 0.f;
    float anchor = 0.f;
    bool linear = false;
    float outMin = 0.f;
    float outMax = 1.f;
    bool on = false;
    std::string text;
};

struct ControlAction {
    std::string id;
    enum class Kind { Click, Toggle, Value, Text } kind = Kind::Click;
    float value = 0.f;
    float norm = 0.f;
    bool on = false;
    std::string text;
};

struct PointerEvent {
    enum class Phase { Down, Move, Up };
    Phase phase = Phase::Down;
    float x = 0.f;
    float y = 0.f;
    bool fine = false;
};

struct KeyEvent {
    uint32_t codepoint = 0;
    bool backspace = false;
};

// Retained controls for one view. begin() drops the rects from the last paint
// and keeps field text, the selection, and an in-progress drag.
class WidgetLayer {
public:
    void begin();
    void add(Control control);
    const Control* find(std::string_view id) const;
    Control* find(std::string_view id);
    const std::vector<Control>& controls() const { return controls_; }
    std::string draft(std::string_view id) const;
    bool isSelected(std::string_view id) const { return !id.empty() && selected_ == id; }
    const std::string& focused() const { return focus_; }
    bool captured() const { return drag_.active; }

    struct PointerResult {
        bool handled = false;
        std::optional<ControlAction> action;
    };
    PointerResult pointer(const PointerEvent& event);
    std::optional<ControlAction> key(const KeyEvent& event);

private:
    const Control* hit(float x, float y) const;
    std::vector<Control> controls_;
    std::vector<std::pair<std::string, std::string>> drafts_;
    std::string selected_;
    std::string focus_;
    struct Drag {
        bool active = false;
        std::string id;
        ControlKind kind = ControlKind::Button;
        float startY = 0.f;
        float startNorm = 0.f;
        float anchor = 0.f;
        bool linear = false;
        float outMin = 0.f;
        float outMax = 1.f;
        float norm = 0.f;
        float raw = 0.f;
    } drag_;
};

// Writes the action into the project or the theme provider. A project edit
// republishes the graph when an engine is passed.
// `keep` is the owning pointer republished to the engine after a project edit.
bool applyControl(const ControlAction& action, Project* project, Engine* engine, const std::shared_ptr<Project>& keep,
                  ThemeProvider* themes, WidgetLayer& widgets);

bool uiPointer(Frame& frame, Project* project, Engine* engine, const std::shared_ptr<Project>& keep, ThemeProvider* themes,
               const PointerEvent& event);
bool uiKey(Frame& frame, Project* project, Engine* engine, const std::shared_ptr<Project>& keep, ThemeProvider* themes,
           const KeyEvent& event);

} // namespace ghovlz
