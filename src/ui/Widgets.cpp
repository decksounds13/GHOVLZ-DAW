#include "ui/Widgets.h"

#include "engine/Engine.h"
#include "model/Project.h"
#include "ui/Paint.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ghovlz {
namespace {

Track* trackById(Project& project, const std::string& id) {
    if (Track* t = project.findTrack(id))
        return t;
    return nullptr;
}

uint64_t parseSeed(const std::string& text, uint64_t fallback) {
    if (text.empty())
        return fallback;
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text.c_str(), &end, 0);
    if (end == text.c_str())
        return fallback;
    return static_cast<uint64_t>(value);
}

} // namespace

float knobNorm(float v) {
    if (v >= 0.f && v <= 1.f)
        return v;
    if (v > 1.f && v <= 36.f)
        return v / 36.f;
    if (v > 36.f)
        return std::clamp(std::log(std::max(20.f, v) / 20.f) / std::log(1000.f), 0.f, 1.f);
    return std::clamp((v + 60.f) / 66.f, 0.f, 1.f);
}

float knobFromNorm(float norm, float anchor) {
    norm = std::clamp(norm, 0.f, 1.f);
    if (anchor >= 0.f && anchor <= 1.f)
        return norm;
    if (anchor > 1.f && anchor <= 36.f)
        return norm * 36.f;
    if (anchor > 36.f)
        return 20.f * std::pow(1000.f, norm);
    return norm * 66.f - 60.f;
}

void WidgetLayer::begin() {
    drafts_.clear();
    for (const auto& control : controls_) {
        if (control.kind == ControlKind::Field)
            drafts_.emplace_back(control.id, control.text);
    }
    controls_.clear();
}

void WidgetLayer::add(Control control) {
    if (control.kind == ControlKind::Field) {
        for (const auto& draft : drafts_) {
            if (draft.first == control.id && !draft.second.empty()) {
                control.text = draft.second;
                break;
            }
        }
    }
    if (drag_.active && drag_.id == control.id && control.kind == ControlKind::Knob) {
        control.norm = drag_.norm;
        control.value = drag_.raw;
    }
    controls_.push_back(std::move(control));
}

const Control* WidgetLayer::find(std::string_view id) const {
    for (const auto& control : controls_)
        if (control.id == id)
            return &control;
    return nullptr;
}

Control* WidgetLayer::find(std::string_view id) {
    for (auto& control : controls_)
        if (control.id == id)
            return &control;
    return nullptr;
}

std::string WidgetLayer::draft(std::string_view id) const {
    if (const Control* control = find(id))
        return control->text;
    for (const auto& draft : drafts_)
        if (draft.first == id)
            return draft.second;
    return {};
}

const Control* WidgetLayer::hit(float x, float y) const {
    for (auto it = controls_.rbegin(); it != controls_.rend(); ++it)
        if (it->hit.contains(x, y))
            return &*it;
    return nullptr;
}

WidgetLayer::PointerResult WidgetLayer::pointer(const PointerEvent& event) {
    PointerResult result;
    if (event.phase == PointerEvent::Phase::Down) {
        const Control* control = hit(event.x, event.y);
        if (!control)
            return result;
        result.handled = true;
        drag_.active = true;
        drag_.id = control->id;
        drag_.kind = control->kind;
        drag_.startY = event.y;
        drag_.startNorm = control->norm;
        drag_.anchor = control->anchor;
        drag_.linear = control->linear;
        drag_.outMin = control->outMin;
        drag_.outMax = control->outMax;
        drag_.norm = control->norm;
        drag_.raw = control->value;
        if (control->kind == ControlKind::Field) {
            focus_ = control->id;
            ControlAction action;
            action.id = control->id;
            action.kind = ControlAction::Kind::Text;
            action.text = control->text;
            result.action = action;
        }
        return result;
    }
    if (!drag_.active)
        return result;
    result.handled = true;
    if (event.phase == PointerEvent::Phase::Move) {
        if (drag_.kind != ControlKind::Knob)
            return result;
        const float span = event.fine ? 800.f : 160.f;
        const float norm = std::clamp(drag_.startNorm + (drag_.startY - event.y) / span, 0.f, 1.f);
        const float raw = drag_.linear ? drag_.outMin + norm * (drag_.outMax - drag_.outMin) : knobFromNorm(norm, drag_.anchor);
        drag_.norm = norm;
        drag_.raw = raw;
        if (Control* control = find(drag_.id)) {
            control->norm = norm;
            control->value = raw;
        }
        ControlAction action;
        action.id = drag_.id;
        action.kind = ControlAction::Kind::Value;
        action.value = raw;
        action.norm = norm;
        result.action = action;
        return result;
    }
    const std::string id = drag_.id;
    const ControlKind kind = drag_.kind;
    drag_.active = false;
    const Control* control = find(id);
    if (!control || !control->hit.contains(event.x, event.y))
        return result;
    if (kind == ControlKind::Knob)
        return result;
    if (kind == ControlKind::Field)
        return result;
    ControlAction action;
    action.id = id;
    if (kind == ControlKind::Pill) {
        if (Control* live = find(id))
            live->on = !live->on;
        action.kind = ControlAction::Kind::Toggle;
        action.on = find(id) ? find(id)->on : !control->on;
    } else {
        action.kind = ControlAction::Kind::Click;
        selected_ = id;
    }
    result.action = action;
    return result;
}

std::optional<ControlAction> WidgetLayer::key(const KeyEvent& event) {
    if (focus_.empty())
        return std::nullopt;
    Control* control = find(focus_);
    if (!control || control->kind != ControlKind::Field)
        return std::nullopt;
    if (event.backspace) {
        if (!control->text.empty())
            control->text.pop_back();
    } else if (event.codepoint >= 32 && event.codepoint < 127 && control->text.size() < 16) {
        control->text.push_back(static_cast<char>(event.codepoint));
    } else {
        return std::nullopt;
    }
    for (auto& draft : drafts_) {
        if (draft.first == control->id) {
            draft.second = control->text;
            break;
        }
    }
    ControlAction action;
    action.id = control->id;
    action.kind = ControlAction::Kind::Text;
    action.text = control->text;
    return action;
}

bool applyControl(const ControlAction& action, Project* project, Engine* engine, const std::shared_ptr<Project>& keep,
                  ThemeProvider* themes, WidgetLayer& widgets) {
    bool edited = false;
    const std::string& id = action.id;
    if (project && id.rfind("dev/", 0) == 0 && action.kind == ControlAction::Kind::Value) {
        const std::string rest = id.substr(4);
        const auto slash = rest.find('/');
        if (slash != std::string::npos) {
            const std::string device = rest.substr(0, slash);
            const std::string param = rest.substr(slash + 1);
            auto write = [&](Track& track) {
                for (auto& state : track.devices) {
                    if (state.id != device)
                        continue;
                    state.params[param] = action.value;
                    edited = true;
                }
            };
            for (auto& track : project->tracks)
                write(track);
            write(project->master);
        }
    } else if (project && id.rfind("mix/", 0) == 0) {
        const std::string rest = id.substr(4);
        const auto slash = rest.find('/');
        if (slash != std::string::npos) {
            Track* track = trackById(*project, rest.substr(0, slash));
            const std::string which = rest.substr(slash + 1);
            if (track && which == "pan" && action.kind == ControlAction::Kind::Value) {
                track->pan = std::clamp(action.value, -1.f, 1.f);
                edited = true;
            } else if (track && which == "send" && action.kind == ControlAction::Kind::Value) {
                if (track->sends.empty())
                    track->sends.push_back(SendState{"retA", action.value, false});
                else
                    track->sends.front().levelDb = action.value;
                edited = true;
            } else if (track && (which == "solo" || which == "s") && action.kind == ControlAction::Kind::Toggle) {
                track->solo = action.on;
                edited = true;
            } else if (track && which == "arm" && action.kind == ControlAction::Kind::Toggle) {
                track->arm = action.on;
                edited = true;
            }
        }
    } else if (themes && id.rfind("theme/", 0) == 0) {
        Theme next = themes->current();
        const std::string rest = id.substr(6);
        if (rest.rfind("lock/", 0) == 0 && action.kind == ControlAction::Kind::Toggle) {
            const std::string key = rest.substr(5);
            for (const auto& info : kRoles) {
                if (info.key == key)
                    next.locked[static_cast<int>(info.role)] = action.on ? 1 : 0;
            }
            themes->set(next, ThemeCommit::Commit);
        } else if (rest == "dice" && action.kind == ControlAction::Kind::Click) {
            const uint64_t seed = parseSeed(widgets.draft("theme/seed"), next.seed ? next.seed : 0xC0FFEEULL);
            themes->set(Theme::roll(next, seed, next.brandBand, next.contrast), ThemeCommit::Commit);
        } else if ((rest == "brand" || rest == "contrast") && action.kind == ControlAction::Kind::Toggle) {
            if (rest == "brand")
                next.brandBand = action.on;
            else
                next.contrast = action.on;
            const uint64_t seed = parseSeed(widgets.draft("theme/seed"), next.seed ? next.seed : 0xC0FFEEULL);
            themes->set(Theme::roll(next, seed, next.brandBand, next.contrast), ThemeCommit::Commit);
        } else if (rest == "undo" && action.kind == ControlAction::Kind::Click) {
            themes->undo();
        } else if (rest == "redo" && action.kind == ControlAction::Kind::Click) {
            themes->redo();
        } else if (rest.rfind("preset/", 0) == 0 && action.kind == ControlAction::Kind::Click) {
            if (const Theme* builtin = Theme::findBuiltin(rest.substr(7)))
                themes->set(*builtin, ThemeCommit::Commit);
        }
    }
    if (edited && engine && keep)
        engine->setProject(keep);
    return edited;
}

bool uiPointer(Frame& frame, Project* project, Engine* engine, const std::shared_ptr<Project>& keep, ThemeProvider* themes,
               const PointerEvent& event) {
    if (!frame.widgets)
        return false;
    const WidgetLayer::PointerResult result = frame.widgets->pointer(event);
    if (result.action)
        applyControl(*result.action, project, engine, keep, themes, *frame.widgets);
    return result.handled;
}

bool uiKey(Frame& frame, Project* project, Engine* engine, const std::shared_ptr<Project>& keep, ThemeProvider* themes,
           const KeyEvent& event) {
    if (!frame.widgets || frame.widgets->focused().empty())
        return false;
    const std::optional<ControlAction> action = frame.widgets->key(event);
    if (!action)
        return false;
    applyControl(*action, project, engine, keep, themes, *frame.widgets);
    return true;
}

} // namespace ghovlz
