#include "ui/Paint.h"

#include "dsp/Devices.h"
#include "ui/Widgets.h"
#include "engine/Engine.h"
#include "model/Project.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

namespace ghovlz {

const Theme* activeTheme = nullptr;
const Theme& active() { return activeTheme ? *activeTheme : Theme::defaultTheme(); }
struct ThemeBind {
    const Theme* prev;
    explicit ThemeBind(const Theme* theme) : prev(activeTheme) { activeTheme = theme ? theme : &Theme::defaultTheme(); }
    ~ThemeBind() { activeTheme = prev; }
};

void stereoMeter(Canvas& c, Rect r, float peakL, float peakR, float rmsL, float rmsR, float holdL, float holdR,
                 bool clipped, const MeterStyle& style) {
    c.fillRect(r, style.background);
    const float lampH = std::min(8.f, r.h * 0.08f);
    const Rect body{r.x, r.y + lampH + 2.f, r.w, std::max(4.f, r.h - lampH - 2.f)};
    c.fillRect({r.x, r.y, r.w, lampH}, clipped ? style.clip : style.unlit);
    const float gap = std::max(1.f, style.gapPx);
    const float colW = std::max(2.f, (body.w - gap) * 0.5f);
    auto column = [&](float x, float peak, float rms, float hold) {
        c.fillRect({x, body.y, colW, body.h}, style.gap);
        const float seg = std::max(1.f, style.segmentPx);
        const float pitch = seg + style.gapPx;
        const int count = std::max(1, static_cast<int>(std::floor((body.h + style.gapPx) / pitch)));
        const float peakDb = peak <= 1.0e-5f ? style.floorDb : 20.f * std::log10(std::max(peak, 1.0e-8f));
        for (int i = 0; i < count; ++i) {
            const float db = style.floorDb + ((static_cast<float>(i) + 0.5f) / static_cast<float>(count)) * (style.ceilDb - style.floorDb);
            const bool lit = peakDb >= db;
            const Color col = lit ? style.lut[static_cast<size_t>(style.lutIndex(db))] : style.unlit;
            const float y = body.y + body.h - (static_cast<float>(i) + 1.f) * seg - static_cast<float>(i) * style.gapPx;
            c.fillRect({x, y, colW, seg}, col);
        }
        if (rms > 1.0e-5f) {
            const float rmsDb = 20.f * std::log10(std::max(rms, 1.0e-8f));
            const float t = std::clamp((rmsDb - style.floorDb) / std::max(1.0e-3f, style.ceilDb - style.floorDb), 0.f, 1.f);
            const float innerW = std::max(1.f, colW * style.rmsWidth);
            const float innerH = body.h * t;
            c.fillRect({x + (colW - innerW) * 0.5f, body.y + body.h - innerH, innerW, innerH}, style.rms);
        }
        const float holdDb = hold <= 1.0e-5f ? style.floorDb : 20.f * std::log10(std::max(hold, 1.0e-8f));
        const float ht = std::clamp((holdDb - style.floorDb) / std::max(1.0e-3f, style.ceilDb - style.floorDb), 0.f, 1.f);
        c.fillRect({x, body.y + body.h - body.h * ht - 1.f, colW, 2.f}, style.peakHold);
    };
    column(body.x, peakL, rmsL, holdL);
    column(body.x + colW + gap, peakR, rmsR, holdR);
}

void paintMeterCard(Canvas& canvas, const Theme& theme, float peakL, float peakR, float rmsL, float rmsR, float holdL,
                    float holdR, bool clip) {
    ThemeBind bind(&theme);
    canvas.clear(theme.color(Role::Bg));
    stereoMeter(canvas, {24.f, 16.f, 56.f, static_cast<float>(canvas.height()) - 32.f}, peakL, peakR, rmsL, rmsR, holdL,
                holdR, clip, theme.meter);
}

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kBarH = 54.f;
constexpr float kViewsX = 196.f;
constexpr float kViewW = 62.f;
constexpr float kViewGap = 5.f;
constexpr float kTx = 492.f;
constexpr float kBtn = 34.f;
constexpr float kBtnGap = 8.f;

Color ink() { return active().color(Role::Ink); }
Color dim() { return active().color(Role::Dim); }
Color gold() { return active().color(Role::Gold); }
Color rod() { return active().color(Role::Rod); }
Color amber() { return active().color(Role::Amber); }
Color orange() { return active().color(Role::Orange); }
Color panel() { return active().color(Role::Panel); }
Color panel2() { return active().color(Role::Panel2); }
Color line() { return active().color(Role::Line); }
Color bg() { return active().color(Role::Bg); }

void drawStereo(Canvas& c, const Frame& f, const std::string& track, Rect r, float fallback) {
    Engine::TrackLevels lv;
    if (f.engine)
        lv = f.engine->levels(track);
    else {
        lv.peakL = fallback;
        lv.peakR = fallback * 0.72f;
        lv.rmsL = fallback * 0.45f;
        lv.rmsR = fallback * 0.3f;
        lv.holdL = fallback;
        lv.holdR = fallback * 0.8f;
    }
    stereoMeter(c, r, lv.peakL, lv.peakR, lv.rmsL, lv.rmsR, lv.holdL, lv.holdR, lv.clip, active().meter);
}

std::string positionText(double q) {
    if (q < 0)
        q = 0;
    const int bar = static_cast<int>(q / 4.0) + 1;
    const int beat = static_cast<int>(q) % 4 + 1;
    const int six = static_cast<int>((q - std::floor(q)) * 4.0) + 1;
    return std::to_string(bar) + "." + std::to_string(beat) + "." + std::to_string(six);
}

void panelBox(Canvas& c, Rect r, float radius = 8.f) {
    c.dropShadow(r, radius, 12.f, Color{0, 0, 0, 170});
    c.fillRoundRect(r, radius, panel());
    c.innerShadow(r, 12.f, Color{0, 0, 0, 150});
    c.strokeRect(r, line(), 1.f);
}

Color mixToward(Color src, Color toward, float t) {
    auto ch = [](uint8_t a, uint8_t b, float u) {
        return static_cast<uint8_t>(std::clamp(a + (b - a) * u, 0.f, 255.f));
    };
    return Color{ch(src.r, toward.r, t), ch(src.g, toward.g, t), ch(src.b, toward.b, t), 255};
}

Color trackBody(uint32_t argb) {
    return mixToward(Color::argb(argb), Color::argb(0xFF120E0C), 0.38f);
}

void centered(Canvas& c, const std::string& text, float cx, float y, float size, Color col, bool bold) {
    const float w = c.textWidth(text, size, bold);
    c.drawText(text, cx - w * 0.5f, y, size, col, bold);
}

const Image& knobFilm() {
    static Image image = loadImageRgba(std::string(GHOVLZ_ASSETS_DIR) + "/DarkKnob4_Stitched.png");
    return image;
}

void miniKnob(Canvas& c, float cx, float cy, float radius, float norm) {
    norm = std::clamp(norm, 0.f, 1.f);
    c.fillEllipse(cx, cy, radius, radius, Color::argb(0xFF1C1816));
    const float a0 = -kPi * 0.5f - 145.f * kPi / 180.f;
    const float a1 = -kPi * 0.5f + 145.f * kPi / 180.f;
    c.arc(cx, cy, radius - 1.f, a0, a1, Color::argb(0xFF3C3732), 1.4f);
    c.arc(cx, cy, radius - 1.f, a0, a0 + (a1 - a0) * norm, orange(), 1.8f);
    const float ang = a0 + (a1 - a0) * norm;
    c.line(cx + std::cos(ang) * (radius * 0.25f), cy + std::sin(ang) * (radius * 0.25f),
           cx + std::cos(ang) * (radius * 0.78f), cy + std::sin(ang) * (radius * 0.78f),
           Color::argb(0xFFD1BCAC), 1.6f);
}

void darkKnob(Canvas& c, float cx, float cy, float diameter, float norm, const std::string& label, const std::string& value) {
    norm = std::clamp(norm, 0.f, 1.f);
    // The filmstrip frame is 220 px with the disc inset to about 171 px.
    // `diameter` is the visible disc, so a 48 px knob is actually 48 px.
    const float frame = diameter * (220.f / 171.f);
    const float rad = diameter * 0.5f;
    c.dropShadow({cx - rad, cy - rad + 2.f, diameter, diameter}, rad, 6.f, Color{0, 0, 0, 170});
    const Image& film = knobFilm();
    const int frameSize = film.w > 0 ? film.w : 220;
    const int frames = film.w > 0 ? std::max(1, film.h / frameSize) : 1;
    if (film.w > 0) {
        const int frameIndex = std::clamp(static_cast<int>(std::lround(norm * (frames - 1))), 0, frames - 1);
        c.drawImage(film, {0, static_cast<float>(frameIndex * frameSize), static_cast<float>(frameSize), static_cast<float>(frameSize)},
                    {cx - frame * 0.5f, cy - frame * 0.5f, frame, frame});
        c.innerShadow({cx - rad, cy - rad, diameter, diameter}, std::max(4.f, diameter * 0.12f), Color{0, 0, 0, 150});
    } else {
        c.fillEllipse(cx, cy, rad, rad, active().color(Role::Knob));
        c.innerShadow({cx - rad, cy - rad, diameter, diameter}, 8.f, Color{0, 0, 0, 160});
        const float a0 = -kPi * 0.5f - 145.f * kPi / 180.f;
        const float ang = a0 + ((-kPi * 0.5f + 145.f * kPi / 180.f) - a0) * norm;
        c.line(cx + std::cos(ang) * (rad * 0.22f), cy + std::sin(ang) * (rad * 0.22f),
               cx + std::cos(ang) * (rad * 0.78f), cy + std::sin(ang) * (rad * 0.78f), active().color(Role::Pointer),
               std::max(1.6f, diameter * 0.045f));
    }
    const float a0 = -kPi * 0.5f - 145.f * kPi / 180.f;
    const float a1 = -kPi * 0.5f + 145.f * kPi / 180.f;
    c.arc(cx, cy, rad + 3.f, a0, a1, Color::argb(0xFF3C3732), 2.2f);
    c.arc(cx, cy, rad + 3.f, a0, a0 + (a1 - a0) * norm, active().color(Role::Arc), 2.6f);
    if (!label.empty())
        centered(c, label, cx, cy + rad + 8.f, 18.f, dim(), false);
    if (!value.empty())
        centered(c, value, cx, cy + rad + 26.f, 18.f, ink(), true);
}

void knob(Canvas& c, float cx, float cy, float norm, const std::string& label, const std::string& value) {
    darkKnob(c, cx, cy, 48.f, norm, label, value);
}

// Device-chain knob. The disc is #231F20, the pointer is #D1BCAC, the arc is #FF6E00.
// `diameter` is the disc, sized from the card rather than a fixed pixel size.
void chainKnob(Canvas& c, float cx, float cy, float diameter, float norm, const std::string& label) {
    norm = std::clamp(norm, 0.f, 1.f);
    const float rad = diameter * 0.5f;
    const float rim = std::max(2.f, diameter * 0.035f);
    c.dropShadow({cx - rad, cy - rad + 3.f, diameter, diameter}, rad, std::max(6.f, diameter * 0.08f), Color{0, 0, 0, 170});
    c.fillEllipse(cx, cy + rim * 0.4f, rad + rim, rad + rim, Color::argb(0xFF3A342E));
    c.fillEllipse(cx, cy - rim * 0.35f, rad + rim * 0.55f, rad + rim * 0.55f, Color::argb(0xFF5A5048));
    c.fillEllipse(cx, cy, rad, rad, Color::argb(0xFF231F20));
    c.innerShadow({cx - rad, cy - rad, diameter, diameter}, std::max(4.f, diameter * 0.1f), Color{0, 0, 0, 170});
    const float a0 = -kPi * 0.5f - 145.f * kPi / 180.f;
    const float a1 = -kPi * 0.5f + 145.f * kPi / 180.f;
    const float arcR = rad + rim * 0.15f;
    const float thick = std::max(2.4f, diameter * 0.045f);
    c.arc(cx, cy, arcR, a0, a1, Color::argb(0xFF3C3732), thick);
    c.arc(cx, cy, arcR, a0, a0 + (a1 - a0) * norm, Color::argb(0xFFFF6E00), thick + 0.6f);
    const float ang = a0 + (a1 - a0) * norm;
    const float pointer = std::max(2.f, diameter * 0.045f);
    c.line(cx + std::cos(ang) * rad * 0.18f, cy + std::sin(ang) * rad * 0.18f,
           cx + std::cos(ang) * rad * 0.78f, cy + std::sin(ang) * rad * 0.78f, Color::argb(0xFFD1BCAC), pointer);
    if (!label.empty())
        centered(c, label, cx, cy + rad + 6.f, 18.f, dim(), false);
}

void goldCap(Canvas& c, float x, float y, float w, float h) {
    Rect r{x, y, w, h};
    c.dropShadow(r, 4.f, 6.f, Color{0, 0, 0, 180});
    c.fillRoundRect(r, 4.f, Color::argb(0xFF5A4014));
    c.fillRoundRect({r.x + 1.f, r.y + 1.f, r.w - 2.f, r.h * 0.48f}, 3.f, Color::argb(0xFFF0D078));
    c.fillRect({r.x + 2.f, r.y + 2.f, r.w - 4.f, 3.f}, Color::argb(0xFFFFF2C8));
    c.fillRect({r.x + 3.f, r.y + r.h * 0.46f, r.w - 6.f, 2.f}, Color::argb(0xFF3A280C));
    c.fillRoundRect({r.x + 1.f, r.y + r.h * 0.52f, r.w - 2.f, r.h * 0.42f}, 3.f, Color::argb(0xFFB49637));
    c.fillRect({r.x + 2.f, r.y + r.h - 4.f, r.w - 4.f, 2.f}, Color::argb(0xFF6A4E1C));
    c.innerShadow(r, 4.f, Color{0, 0, 0, 90});
}

void playGlyph(Canvas& c, float x, float y, float s, Color col) {
    for (int i = 0; i < static_cast<int>(s); ++i) {
        const float t = (i + 0.5f) / s;
        const float span = s * (1.f - std::abs(2.f * t - 1.f));
        c.fillRect({x, y + static_cast<float>(i), std::max(1.f, span), 1.f}, col);
    }
}

void transportButton(Canvas& c, Rect r, bool on) {
    c.fillRoundRect(r, 8.f, on ? Color::argb(0xFF3A2E1C) : Color::argb(0xFF1A1614));
    c.strokeRect(r, on ? gold() : Color::argb(0xFF3C3732), 1.2f);
}

void pill(Canvas& c, Rect r, const char* text, bool on) {
    c.fillRoundRect(r, std::min(6.f, r.h * 0.35f), on ? gold() : Color::argb(0xFF2A241E));
    c.innerShadow(r, 3.f, Color{0, 0, 0, static_cast<uint8_t>(on ? 50 : 120)});
    const float size = r.h >= 20.f ? 18.f : std::max(11.f, r.h - 4.f);
    centered(c, text, r.x + r.w * 0.5f, r.y + (r.h - size) * 0.45f, size, on ? Color::argb(0xFF1A120C) : dim(), true);
}

float dbNorm(float db) { return std::clamp((db + 60.f) / 66.f, 0.f, 1.f); }

void place(const Frame& f, const Control& control) {
    if (f.widgets)
        f.widgets->add(control);
}


const Media* mediaOf(const Project* p, const std::string& id) {
    if (!p)
        return nullptr;
    auto it = p->media.find(id);
    return it == p->media.end() ? nullptr : it->second.get();
}

void drawWave(Canvas& c, const Media* m, Rect r, Color col) {
    if (!m || m->interleaved.empty() || r.w < 2.f || r.h < 4.f)
        return;
    const int ch = std::max(1, m->channels);
    const int frames = static_cast<int>(m->interleaved.size()) / ch;
    if (frames <= 0)
        return;
    const int cols = std::max(1, static_cast<int>(r.w));
    const float mid = r.y + r.h * 0.5f;
    for (int x = 0; x < cols; ++x) {
        const int i0 = x * frames / cols;
        const int i1 = std::max(i0 + 1, (x + 1) * frames / cols);
        const int step = std::max(1, (i1 - i0) / 24);
        float mn = 0.f, mx = 0.f;
        for (int i = i0; i < i1 && i < frames; i += step) {
            const float s = m->interleaved[static_cast<size_t>(i * ch)];
            mn = std::min(mn, s);
            mx = std::max(mx, s);
        }
        const float env = std::clamp(std::max(std::fabs(mn), std::fabs(mx)), 0.f, 1.f);
        const float shaped = std::max(0.18f, env) * (0.4f + 0.6f * std::fabs(std::sin(x * 0.65f)));
        const float yTop = mid - shaped * r.h * 0.46f;
        const float yBot = mid + shaped * r.h * 0.46f;
        const float span = std::max(1.f, yBot - yTop);
        const float lobe = std::max(1.f, span * 0.42f);
        c.fillRect({r.x + static_cast<float>(x), yTop, 1.f, lobe}, col);
        c.fillRect({r.x + static_cast<float>(x), yBot - lobe, 1.f, lobe}, col);
    }
}

void drawNotes(Canvas& c, const Clip& clip, Rect r) {
    if (clip.notes.empty() || r.w < 2.f)
        return;
    for (const auto& n : clip.notes) {
        const float x = r.x + static_cast<float>(n.startQ / std::max(0.01, clip.lengthQ)) * r.w;
        const float w = std::max(2.f, static_cast<float>(n.lengthQ / std::max(0.01, clip.lengthQ)) * r.w);
        const float ny = std::clamp((n.pitch - 36) / 48.f, 0.f, 1.f);
        const float y = r.y + r.h - 4.f - ny * (r.h - 6.f);
        c.fillRect({x, y, w, 3.f}, amber());
    }
}

Color waveTint(uint32_t argb) {
    Color base = Color::argb(argb);
    return Color{static_cast<uint8_t>(std::min(255, base.r + 50)),
                 static_cast<uint8_t>(std::min(255, base.g + 10)),
                 static_cast<uint8_t>(base.b / 2), 255};
}

void topBar(Canvas& c, const Frame& f) {
    const float w = static_cast<float>(c.width());
    c.fillRect({0, 0, w, kBarH}, Color::argb(0xFF070707));
    c.fillRect({0, kBarH - 1, w, 1}, line());
    c.fillEllipse(22.f, 27.f, 9.f, 9.f, gold());
    c.drawText("G", 17.f, 16.f, 16.f, Color::argb(0xFF1A120C), true);
    c.drawText("GHOVLZ! DAW", 38.f, 12.f, 22.f, ink(), true);

    if (f.view == View::Piano) {
        const char* tools[] = {"Draw", "Select", "Grid", "Scale"};
        for (int i = 0; i < 4; ++i) {
            Rect r{kViewsX + i * 78.f, 12.f, 72.f, 30.f};
            const bool on = i == 0;
            c.fillRoundRect(r, 6.f, on ? Color::argb(0xFF3A2E1C) : Color::argb(0xFF161311));
            c.strokeRect(r, on ? gold() : line(), 1.f);
            c.drawText(tools[i], r.x + 8.f, r.y + 4.f, 18.f, on ? rod() : dim(), true);
        }
    } else {
        const char* views[] = {"ARR", "SES", "PERF", "MIX"};
        const View shown[] = {View::Arrangement, View::Session, View::Perform, View::Mixer};
        for (int i = 0; i < 4; ++i) {
            Rect r{kViewsX + i * (kViewW + kViewGap), 12.f, kViewW, 30.f};
            const bool on = f.view == shown[i];
            c.fillRoundRect(r, 6.f, on ? Color::argb(0xFF3A2E1C) : Color::argb(0xFF161311));
            c.strokeRect(r, on ? gold() : line(), 1.f);
            c.drawText(views[i], r.x + 8.f, r.y + 4.f, 18.f, on ? rod() : dim(), true);
        }
    }

    const bool playing = f.engine && f.engine->playing();
    const bool rec = f.engine && f.engine->recording();
    const bool loop = f.project && f.project->loop;
    const bool metro = f.project && f.project->metronome;
    auto btn = [&](int i) { return Rect{kTx + i * (kBtn + kBtnGap), 10.f, kBtn, kBtn}; };
    transportButton(c, btn(0), playing);
    playGlyph(c, btn(0).x + 12.f, btn(0).y + 9.f, 16.f, playing ? rod() : ink());
    transportButton(c, btn(1), false);
    c.fillRoundRect({btn(1).x + 10.f, btn(1).y + 10.f, 14.f, 14.f}, 2.f, ink());
    transportButton(c, btn(2), rec);
    c.fillEllipse(btn(2).x + 17.f, btn(2).y + 17.f, 7.f, 7.f, rec ? Color::argb(0xFFE23D2C) : Color::argb(0xFF8C3A32));
    transportButton(c, btn(3), loop);
    c.arc(btn(3).x + 12.f, btn(3).y + 17.f, 6.f, 0.4f, 5.6f, loop ? rod() : ink(), 1.6f);
    c.arc(btn(3).x + 22.f, btn(3).y + 17.f, 6.f, 3.5f, 8.7f, loop ? rod() : ink(), 1.6f);
    transportButton(c, btn(4), metro);
    playGlyph(c, btn(4).x + 10.f, btn(4).y + 8.f, 16.f, metro ? rod() : dim());
    c.fillEllipse(btn(4).x + 24.f, btn(4).y + 12.f, 3.f, 3.f, metro ? rod() : dim());

    char bpm[32];
    std::snprintf(bpm, sizeof(bpm), "%.2f BPM", f.project ? f.project->bpm : 120.0);
    const std::string pos = positionText(f.engine ? f.engine->playhead() : (f.project ? f.project->playhead : 0));
    c.drawText(bpm, 760.f, 14.f, 18.f, ink(), true);
    c.drawText(f.project ? (std::to_string(f.project->numerator) + "/" + std::to_string(f.project->denominator)) : "4/4",
               890.f, 14.f, 18.f, dim(), false);
    c.drawText(pos, 960.f, 14.f, 18.f, rod(), true);
    char cpu[32];
    std::snprintf(cpu, sizeof(cpu), "CPU %.0f%%", f.engine ? f.engine->cpuLoad() * 100.f : 0.f);
    c.drawText(cpu, w - 270.f, 14.f, 18.f, dim(), false);
    Rect badge{w - 108.f, 12.f, 92.f, 30.f};
    c.fillRoundRect(badge, 6.f, Color::argb(0xFF1A1614));
    c.strokeRect(badge, gold(), 1.f);
    c.drawText(f.backend ? f.backend : "CPU", badge.x + 14.f, badge.y + 7.f, 14.f, rod(), true);
}

void glyphIcon(Canvas& c, int kind, float x, float y, Color col) {
    c.fillRoundRect({x, y, 18.f, 18.f}, 4.f, Color{0, 0, 0, 70});
    if (kind == 0) {
        c.line(x + 2, y + 11, x + 6, y + 4, col, 2.f);
        c.line(x + 6, y + 4, x + 8, y + 13, col, 2.f);
        c.line(x + 8, y + 13, x + 12, y + 3, col, 2.f);
        c.line(x + 12, y + 3, x + 16, y + 11, col, 2.f);
    } else if (kind == 1) {
        c.fillRoundRect({x + 2, y + 5, 14, 11}, 2.f, col);
        c.fillRect({x + 5, y + 2, 8, 4}, col);
        c.fillRect({x + 7, y + 8, 4, 4}, Color::argb(0xFF1A120C));
    } else if (kind == 2) {
        c.fillRoundRect({x + 2, y + 9, 4, 7}, 1.f, col);
        c.fillRoundRect({x + 7, y + 4, 4, 12}, 1.f, col);
        c.fillRoundRect({x + 12, y + 6, 4, 10}, 1.f, col);
    } else if (kind == 3) {
        c.fillRoundRect({x + 2, y + 3, 14, 12}, 2.f, col);
        c.fillRect({x + 2, y + 7, 14, 2}, Color::argb(0xFF1A120C));
        c.fillRect({x + 8, y + 3, 2, 12}, Color::argb(0xFF1A120C));
    } else if (kind == 4) {
        c.line(x + 3, y + 15, x + 9, y + 2, col, 2.2f);
        c.line(x + 9, y + 2, x + 15, y + 15, col, 2.2f);
        c.line(x + 6, y + 10, x + 12, y + 10, col, 2.f);
    } else if (kind == 5) {
        c.arc(x + 7, y + 7, 5.5f, 0.4f, 5.6f, col, 2.f);
        c.fillRoundRect({x + 10, y + 10, 7, 2.4f}, 1.f, col);
    } else {
        const float cx = x + 9.f, cy = y + 9.f;
        for (int i = 0; i < 5; ++i) {
            const float a0 = -kPi * 0.5f + i * (2.f * kPi / 5.f);
            const float a1 = -kPi * 0.5f + ((i + 2) % 5) * (2.f * kPi / 5.f);
            c.line(cx + std::cos(a0) * 7.f, cy + std::sin(a0) * 7.f, cx + std::cos(a1) * 7.f, cy + std::sin(a1) * 7.f, col, 1.8f);
        }
    }
}

void browserColumn(Canvas& c, const Frame& f, float h) {
    c.fillRect({0, kBarH, 156.f, h - kBarH}, Color::argb(0xFF0E0C0B));
    c.fillRect({155, kBarH, 1, h - kBarH}, line());
    c.drawText("BROWSER", 14.f, kBarH + 8.f, 18.f, dim(), true);
    const char* cats[] = {"Sounds", "Drums", "Instruments", "Audio Effects", "MIDI Effects", "Samples", "Plugins"};
    for (int i = 0; i < 7; ++i) {
        Rect r{8.f, kBarH + 40.f + i * 32.f, 140.f, 28.f};
        const bool on = i == 0;
        if (on)
            c.fillRoundRect(r, 4.f, Color::argb(0xFF2A2118));
        glyphIcon(c, i, 10.f, r.y + 4.f, on ? amber() : Color::argb(0xFFD1BCAC));
        c.drawText(cats[i], 32.f, r.y + 4.f, 18.f, on ? ink() : dim(), on);
    }
}

std::string paramText(float v) {
    char b[24];
    if (v >= 100.f)
        std::snprintf(b, sizeof(b), "%.0f", v);
    else if (v > 1.5f)
        std::snprintf(b, sizeof(b), "%.1f", v);
    else
        std::snprintf(b, sizeof(b), "%.2f", v);
    return b;
}

void arrangement(Canvas& c, const Frame& f) {
    const float w = static_cast<float>(c.width());
    const float h = static_cast<float>(c.height());
    const float headerX = 156.f;
    const float headerW = 160.f;
    const float rightW = 86.f;
    const float laneX = headerX + headerW;
    const float laneR = w - rightW;
    const float rulerY = kBarH;
    const float rulerH = 40.f;
    const float chainH = 176.f;
    const float rowsTop = rulerY + rulerH;
    const float rowsBot = h - chainH;
    std::vector<const Track*> rows;
    if (f.project) {
        for (const auto& t : f.project->tracks)
            if (t.type != TrackType::Return)
                rows.push_back(&t);
        rows.push_back(&f.project->master);
    }
    const float rowH = rows.empty() ? 40.f : (rowsBot - rowsTop) / static_cast<float>(rows.size());
    const double span = 160.0;
    auto xAt = [&](double q) { return laneX + static_cast<float>(q / span) * (laneR - laneX); };

    c.fillRect({headerX, rulerY, w - headerX, rulerH}, Color::argb(0xFF100E0C));
    if (f.project && f.project->loop) {
        const float x0 = xAt(f.project->loopStart);
        const float x1 = xAt(f.project->loopEnd);
        c.fillRect({x0, rulerY + 2.f, std::max(2.f, x1 - x0), 3.f}, gold());
        c.fillRect({x0, rulerY + 2.f, 2.f, 10.f}, gold());
        c.fillRect({x1 - 2.f, rulerY + 2.f, 2.f, 10.f}, gold());
    }
    if (f.project) {
        for (const auto& m : f.project->markers) {
            const float x = xAt(m.quarter);
            c.fillEllipse(x, rulerY + 14.f, 4.f, 4.f, Color::argb(m.color));
            c.drawText(m.name, x + 8.f, rulerY + 4.f, 18.f, Color::argb(m.color), true);
        }
    }
    for (int bar = 1; bar <= 40; bar += 2) {
        const float x = xAt((bar - 1) * 4.0);
        if (x > laneR)
            break;
        c.drawText(std::to_string(bar), x + 2.f, rulerY + 18.f, 14.f, dim(), false);
    }
    const double head = f.engine ? f.engine->playhead() : (f.project ? f.project->playhead : 0);
    const float hx = xAt(head);
    const std::string flag = positionText(head);

    for (size_t i = 0; i < rows.size(); ++i) {
        const Track* t = rows[i];
        const float y = rowsTop + static_cast<float>(i) * rowH;
        c.fillRect({headerX, y, headerW, rowH - 1.f}, i % 2 ? Color::argb(0xFF120F0D) : Color::argb(0xFF161311));
        c.fillRect({headerX, y, 6.f, rowH - 1.f}, Color::argb(t->color));
        c.drawText(t->name, headerX + 12.f, y + 2.f, 18.f, ink(), true);
        drawStereo(c, f, t->id, {headerX + headerW - 12.f, y + 4.f, 8.f, std::max(16.f, rowH - 8.f)}, 0.62f);
        const float pillH = 32.f;
        const float pillY = y + rowH - pillH - 4.f;
        pill(c, {headerX + 10.f, pillY, 40.f, pillH}, "M", t->mute);
        pill(c, {headerX + 54.f, pillY, 40.f, pillH}, "S", t->solo);
        pill(c, {headerX + 98.f, pillY, 48.f, pillH}, "Arm", t->arm);

        const Rect lane{laneX, y, laneR - laneX, std::max(1.f, rowH - 2.f)};
        c.fillRect({laneX, y, laneR - laneX, rowH - 1.f}, Color::argb(0xFF0C0B0A));
        c.clip(lane);
        for (const auto& clip : t->clips) {
            const float x0 = xAt(clip.startQ);
            const float x1 = xAt(clip.startQ + clip.lengthQ);
            if (x1 < laneX || x0 > laneR)
                continue;
            const uint32_t tint = clip.color ? clip.color : t->color;
            Rect body{x0, y + 3.f, std::max(8.f, x1 - x0), rowH - 8.f};
            c.fillRoundRect(body, 5.f, trackBody(tint));
            c.fillRect({body.x, body.y, body.w, 20.f}, mixToward(Color::argb(tint), Color::argb(0xFF1A140E), 0.25f));
            c.fillRect({body.x, body.y, 4.f, body.h}, Color::argb(tint));
            c.drawText(clip.name.empty() ? t->name : clip.name, body.x + 10.f, body.y + 1.f, 18.f, ink(), true);
            Rect content{body.x + 4.f, body.y + 22.f, std::max(2.f, body.w - 8.f), std::max(4.f, body.h - 26.f)};
            const Color wave = mixToward(Color::argb(tint), Color::argb(0xFFFFF2E0), 0.45f);
            if (!clip.notes.empty())
                drawNotes(c, clip, content);
            else
                drawWave(c, mediaOf(f.project, clip.mediaId), content, wave);
        }
        if (f.project) {
            for (const auto& autoLane : f.project->automation) {
                const std::string prefix = "track:" + t->id + ":";
                if (autoLane.id.rfind(prefix, 0) != 0 || autoLane.points.size() < 2)
                    continue;
                float vmin = autoLane.points.front().value, vmax = vmin;
                for (const auto& pt : autoLane.points) {
                    vmin = std::min(vmin, pt.value);
                    vmax = std::max(vmax, pt.value);
                }
                const float spanV = std::max(0.001f, vmax - vmin);
                const float xL = laneX + 6.f;
                const float xR = laneR - 6.f;
                const float y0 = y + 24.f;
                const float y1 = y + rowH - 16.f;
                auto yOf = [&](float v) {
                    const float tnorm = std::clamp((v - vmin) / spanV, 0.f, 1.f);
                    return std::clamp(y0 + (1.f - tnorm) * std::max(4.f, y1 - y0), y0, y1);
                };
                auto xOf = [&](double q) { return std::clamp(xAt(q), xL, xR); };
                const std::string label = autoLane.id.substr(prefix.size());
                c.drawText(label == "cutoff" ? "Filter Cutoff" : label, xL, y + 2.f, 18.f, rod(), false);
                float px = xOf(autoLane.points.front().quarter);
                float py = yOf(autoLane.points.front().value);
                for (size_t p = 1; p < autoLane.points.size(); ++p) {
                    const float nx = xOf(autoLane.points[p].quarter);
                    const float ny = yOf(autoLane.points[p].value);
                    c.line(px, py, nx, ny, amber(), 2.f);
                    px = nx;
                    py = ny;
                }
            }
        }
        c.line(std::clamp(hx, laneX, laneR), y + 1.f, std::clamp(hx, laneX, laneR), y + rowH - 3.f, rod(), 1.4f);
        c.resetClip();

        c.fillRect({laneR, y, rightW, rowH - 1.f}, Color::argb(0xFF120F0D));
        darkKnob(c, laneR + 28.f, y + rowH * 0.42f, 28.f, (t->pan + 1.f) * 0.5f, "", "");
        drawStereo(c, f, t->id, {laneR + rightW - 18.f, y + 8.f, 10.f, rowH - 18.f}, 0.4f);
    }
    c.fillRoundRect({hx - 22.f, rulerY + 18.f, 52.f, 16.f}, 3.f, gold());
    c.drawText(flag, hx - 18.f, rulerY + 19.f, 12.f, Color::argb(0xFF1A120C), true);

    const Track* chain = f.project ? f.project->findTrack("lead") : nullptr;
    const float cy = h - chainH + 8.f;
    c.fillRect({0, h - chainH, w, chainH}, Color::argb(0xFF0A0A0A));
    if (chain && !chain->devices.empty()) {
        const float pw = std::min(280.f, (w - 24.f) / static_cast<float>(chain->devices.size()) - 8.f);
        for (size_t i = 0; i < chain->devices.size(); ++i) {
            const auto& d = chain->devices[i];
            Rect r{12.f + static_cast<float>(i) * (pw + 10.f), cy, pw, chainH - 20.f};
            panelBox(c, r, 8.f);
            c.fillRect({r.x, r.y, 4.f, r.h}, gold());
            c.drawText(d.name, r.x + 14.f, r.y + 10.f, 18.f, ink(), true);
            drawStereo(c, f, chain->id, {r.x + r.w - 28.f, r.y + 36.f, 16.f, r.h - 52.f}, 0.4f);
            int shown = 0;
            for (const auto& pr : d.params) {
                if (shown >= 3)
                    break;
                const float cxk = r.x + 52.f + static_cast<float>(shown) * 86.f;
                if (cxk + 28.f > r.x + r.w - 8.f)
                    break;
                darkKnob(c, cxk, r.y + 86.f, 48.f, knobNorm(pr.second), pr.first, paramText(pr.second));
                ++shown;
            }
            if (i + 1 < chain->devices.size())
                c.line(r.x + r.w, r.y + r.h * 0.45f, r.x + r.w + 10.f, r.y + r.h * 0.45f, gold(), 2.f);
        }
    }
}

void session(Canvas& c, const Frame& f) {
    const float w = static_cast<float>(c.width());
    const float h = static_cast<float>(c.height());
    if (!f.project)
        return;
    const float left = 168.f;
    const float mixerH = 168.f;
    const float sceneW = 118.f;
    const float top = kBarH + 36.f;
    const float sceneX = w - sceneW - 10.f;
    std::vector<const Track*> cols;
    for (const auto& t : f.project->tracks)
        if (t.type == TrackType::Audio || t.type == TrackType::Midi)
            cols.push_back(&t);
    const int nScenes = std::max(1, std::min(8, static_cast<int>(f.project->scenes.size())));
    const float colW = (sceneX - left - 8.f) / std::max<size_t>(1, cols.size());
    const float rowH = (h - mixerH - top) / static_cast<float>(nScenes);
    c.drawText("SESSION", left, kBarH + 8.f, 18.f, dim(), true);
    for (size_t i = 0; i < cols.size(); ++i) {
        const Track* t = cols[i];
        const float x = left + static_cast<float>(i) * colW;
        c.fillRect({x, top - 28.f, colW - 6.f, 24.f}, Color::argb(0xFF161311));
        c.fillRect({x, top - 28.f, 6.f, 24.f}, Color::argb(t->color));
        c.drawText(t->name, x + 10.f, top - 26.f, 18.f, ink(), true);
        for (int s = 0; s < nScenes; ++s) {
            Rect cell{x, top + static_cast<float>(s) * rowH, colW - 6.f, rowH - 4.f};
            const bool filled = s < static_cast<int>(t->slots.size()) && t->slots[static_cast<size_t>(s)];
            const bool playing = filled && s == f.sessionScene;
            c.clip(cell);
            c.fillRoundRect(cell, 4.f, filled ? trackBody(t->color) : Color::argb(0xFF100E0C));
            c.innerShadow(cell, 6.f, Color{0, 0, 0, 90});
            if (filled) {
                const Clip& clip = *t->slots[static_cast<size_t>(s)];
                const uint32_t tint = clip.color ? clip.color : t->color;
                c.fillRect({cell.x, cell.y, cell.w, 4.f}, Color::argb(tint));
                c.drawText(clip.name.empty() ? t->name : clip.name, cell.x + 6.f, cell.y + 6.f, 18.f, ink(), true);
                Rect wave{cell.x + 4.f, cell.y + 26.f, cell.w - 8.f, std::max(4.f, cell.h - 32.f)};
                if (!clip.notes.empty())
                    drawNotes(c, clip, wave);
                else
                    drawWave(c, mediaOf(f.project, clip.mediaId), wave, mixToward(Color::argb(tint), ink(), 0.35f));
            }
            if (playing)
                c.strokeRect(cell, gold(), 2.f);
            c.resetClip();
        }
        Rect mix{x, h - mixerH + 8.f, colW - 6.f, mixerH - 16.f};
        c.dropShadow(mix, 6.f, 6.f, Color{0, 0, 0, 120});
        c.fillRoundRect(mix, 6.f, Color::argb(0xFF120F0D));
        c.innerShadow(mix, 8.f, Color{0, 0, 0, 100});
        darkKnob(c, mix.x + 28.f, mix.y + 48.f, 36.f, dbNorm(t->volumeDb), "", "");
        c.drawText(t->name, mix.x + 50.f, mix.y + 8.f, 18.f, ink(), true);
        drawStereo(c, f, t->id, {mix.x + mix.w - 22.f, mix.y + 12.f, 14.f, mix.h - 24.f}, 0.4f);
    }
    for (int s = 0; s < nScenes; ++s) {
        Rect r{sceneX, top + static_cast<float>(s) * rowH, sceneW, rowH - 4.f};
        const bool on = s == f.sessionScene;
        c.fillRoundRect(r, 6.f, on ? Color::argb(0xFF3A2E1C) : Color::argb(0xFF1C1814));
        c.strokeRect(r, on ? gold() : line(), on ? 2.f : 1.f);
        c.drawText(f.project->scenes[static_cast<size_t>(s)], r.x + 10.f, r.y + r.h * 0.35f, 18.f, on ? rod() : ink(), true);
    }
}

void perform(Canvas& c, const Frame& f) {
    const float w = static_cast<float>(c.width());
    const float h = static_cast<float>(c.height());
    const float side = 120.f;
    const float barH = 168.f;
    const char* names[] = {
        "Kick Loop", "Bassline", "Lead Synth", "Vocal Chop", "Pad Chords", "FX Rise", "Sub Hit", "Perc Fill",
        "Main Kick", "808 Bass", "Saw Lead", "Vox Ahh", "Atmosphere", "Impact", "Sub Bass", "Clap",
        "Snare", "Bass Fill", "Arp", "Vocal", "Strings", "Glitch", "Sub", "Shaker",
        "Tom", "Bass Drop", "Pluck", "Breath", "Drone", "Noise", "Sub Low", "Cowbell",
        "Clap", "Bass Run", "Bell", "Talkbox", "Piano", "Sweep", "Sub Pulse", "Conga",
        "Crash", "Bass End", "Stab", "Vocal End", "Pad End", "Down", "Sub End", "Perc End"};
    const uint32_t warm[] = {0xFF8A5A28, 0xFF6E4A2E, 0xFFB49637, 0xFF8C6230, 0xFF6A5438, 0xFFAA6A22, 0xFF7A5340, 0xFFDAA520};
    const int cols = 8;
    const int rows = 6;
    const float padW = (w - side - 180.f) / cols;
    const float padH = (h - kBarH - barH - 16.f) / rows;
    for (int i = 0; i < cols * rows; ++i) {
        const int col = i % cols;
        const int row = i / cols;
        Rect r{168.f + col * padW, kBarH + 12.f + row * padH, padW - 8.f, padH - 8.f};
        const bool on = i == 11;
        c.dropShadow(r, 6.f, 6.f, Color{0, 0, 0, 120});
        c.fillRoundRect(r, 8.f, Color::argb(0xFF161311));
        c.innerShadow(r, 8.f, Color{0, 0, 0, 110});
        c.fillRect({r.x, r.y, r.w, 8.f}, Color::argb(warm[(col + row) % 8]));
        if (on)
            c.strokeRect(r, gold(), 2.f);
        c.drawText(names[i], r.x + 10.f, r.y + r.h * 0.38f, 18.f, ink(), true);
    }
    const char* scenes[] = {"Intro", "Verse", "Build", "Drop", "Break", "Outro"};
    for (int s = 0; s < 6; ++s) {
        const float cy = kBarH + 36.f + s * ((h - kBarH - barH - 24.f) / 6.f);
        const bool on = f.sessionScene == s;
        c.fillEllipse(w - 60.f, cy, 28.f, 28.f, on ? gold() : Color::argb(0xFF2A2118));
        c.innerShadow({w - 88.f, cy - 28.f, 56.f, 56.f}, 6.f, Color{0, 0, 0, 80});
        centered(c, scenes[s], w - 60.f, cy - 8.f, 14.f, on ? Color::argb(0xFF1A120C) : ink(), true);
    }
    Rect bar{156.f, h - barH, w - 164.f, barH - 12.f};
    panelBox(c, bar, 10.f);
    const char* macros[] = {"Filter", "Space", "Drive", "Width", "Crush", "Delay", "Tone", "Glue"};
    for (int m = 0; m < 8; ++m)
        darkKnob(c, bar.x + 56.f + m * 130.f, bar.y + 62.f, 48.f, 0.35f + m * 0.05f, macros[m], "");
    Rect xf{bar.x + bar.w - 280.f, bar.y + 70.f, 120.f, 8.f};
    c.fillRoundRect(xf, 4.f, Color::argb(0xFF1A1614));
    goldCap(c, xf.x + xf.w * 0.45f, xf.y - 8.f, 18.f, 24.f);
    c.drawText("128.00", bar.x + bar.w - 140.f, bar.y + 48.f, 28.f, rod(), true);
    c.drawText("BPM", bar.x + bar.w - 140.f, bar.y + 82.f, 18.f, dim(), false);
}

void mixer(Canvas& c, const Frame& f) {
    const float w = static_cast<float>(c.width());
    const float h = static_cast<float>(c.height());
    if (!f.project)
        return;
    std::vector<const Track*> chans;
    for (const auto& t : f.project->tracks)
        chans.push_back(&t);
    chans.push_back(&f.project->master);
    const float stripW = (w - 12.f) / static_cast<float>(chans.size());
    for (size_t i = 0; i < chans.size(); ++i) {
        const Track* t = chans[i];
        Rect r{6.f + static_cast<float>(i) * stripW, kBarH + 8.f, stripW - 6.f, h - kBarH - 16.f};
        const bool selected = t->id == "lead";
        c.dropShadow(r, 8.f, 10.f, Color{0, 0, 0, 150});
        c.fillRoundRect(r, 8.f, Color::argb(0xFF120F0D));
        c.innerShadow(r, 14.f, Color{0, 0, 0, 130});
        c.strokeRect(r, selected ? gold() : line(), selected ? 2.f : 1.f);
        c.fillRect({r.x, r.y, r.w, 5.f}, Color::argb(t->color));
        c.drawText(t->name, r.x + 8.f, r.y + 10.f, 18.f, ink(), true);
        c.drawText("In  All Ins", r.x + 8.f, r.y + 32.f, 18.f, dim(), false);
        const std::string out = t->output.empty() || t->id == "master" ? "Master" : t->output;
        c.drawText(std::string("Out  ") + out, r.x + 8.f, r.y + 52.f, 18.f, dim(), false);

        const int shown = std::min(4, static_cast<int>(t->devices.size()));
        for (int s = 0; s < shown; ++s)
            c.drawText(t->devices[static_cast<size_t>(s)].name, r.x + 8.f, r.y + 76.f + s * 22.f, 18.f, ink(), false);

        float sendDb = -100.f;
        const char* sendName = "A";
        if (!t->sends.empty()) {
            sendDb = t->sends.front().levelDb;
            sendName = t->sends.front().target == "retB" ? "B" : "A";
        }
        const float sendY = r.y + 210.f;
        const float sendNorm = dbNorm(sendDb);
        darkKnob(c, r.x + r.w * 0.5f, sendY, 72.f, sendNorm, sendName, sendDb <= -80.f ? "-inf" : paramText(sendDb));
        Control send;
        send.id = "mix/" + t->id + "/send";
        send.kind = ControlKind::Knob;
        send.hit = {r.x + r.w * 0.5f - 36.f, sendY - 36.f, 72.f, 72.f};
        send.norm = sendNorm;
        send.value = sendDb <= -80.f ? -60.f : sendDb;
        send.linear = true;
        send.outMin = -60.f;
        send.outMax = 6.f;
        place(f, send);

        char pan[16];
        std::snprintf(pan, sizeof(pan), "%+.1f", t->pan);
        const float panY = r.y + 340.f;
        const float panNorm = (t->pan + 1.f) * 0.5f;
        darkKnob(c, r.x + r.w * 0.5f, panY, 72.f, panNorm, "Pan", pan);
        Control panCtl;
        panCtl.id = "mix/" + t->id + "/pan";
        panCtl.kind = ControlKind::Knob;
        panCtl.hit = {r.x + r.w * 0.5f - 36.f, panY - 36.f, 72.f, 72.f};
        panCtl.norm = panNorm;
        panCtl.value = t->pan;
        panCtl.linear = true;
        panCtl.outMin = -1.f;
        panCtl.outMax = 1.f;
        place(f, panCtl);
        char db[16];
        std::snprintf(db, sizeof(db), "%+.1f dB", t->volumeDb);
        centered(c, db, r.x + r.w * 0.5f, panY + 78.f, 18.f, rod(), true);

        const float faderTop = panY + 112.f;
        const float faderBot = r.y + r.h - 72.f;
        const float faderX = r.x + 44.f;
        const float faderH = std::max(40.f, faderBot - faderTop);
        Rect well{faderX, faderTop, 22.f, faderH};
        c.fillRoundRect(well, 5.f, Color::argb(0xFF1A1614));
        c.innerShadow(well, 6.f, Color{0, 0, 0, 180});
        const float fn = dbNorm(t->volumeDb);
        const float fy = faderBot - fn * faderH;
        c.fillRoundRect({faderX + 4.f, fy, 14.f, std::max(0.f, faderBot - fy)}, 3.f, Color::argb(0xFF5C4318));
        goldCap(c, faderX - 14.f, fy - 16.f, 50.f, 34.f);
        const char* marks[] = {"+6", "0", "-6", "-12", "-24", "-48"};
        const float markDb[] = {6, 0, -6, -12, -24, -48};
        for (int m = 0; m < 6; ++m) {
            const float my = faderBot - dbNorm(markDb[m]) * faderH;
            c.fillRect({faderX - 8.f, my, 6.f, 1.f}, dim());
            c.drawText(marks[m], r.x + 6.f, my - 8.f, 12.f, dim(), false);
        }
        drawStereo(c, f, t->id, {r.x + r.w - 40.f, faderTop, 28.f, faderH}, 0.55f);
        const float bw = (r.w - 18.f) * 0.5f;
        const Rect sRect{r.x + 6.f, r.y + r.h - 64.f, bw - 4.f, 24.f};
        const Rect cRect{r.x + 8.f + bw, r.y + r.h - 64.f, bw - 4.f, 24.f};
        const Rect soloRect{r.x + 6.f, r.y + r.h - 34.f, bw - 4.f, 24.f};
        const Rect armRect{r.x + 8.f + bw, r.y + r.h - 34.f, bw - 4.f, 24.f};
        pill(c, sRect, "S", t->solo);
        pill(c, cRect, "C", false);
        pill(c, soloRect, "Solo", t->solo);
        pill(c, armRect, "Arm", t->arm);
        auto mixPill = [&](const char* which, Rect rect, bool on) {
            Control ctl;
            ctl.id = "mix/" + t->id + "/" + which;
            ctl.kind = ControlKind::Pill;
            ctl.hit = rect;
            ctl.on = on;
            place(f, ctl);
        };
        mixPill("s", sRect, t->solo);
        mixPill("c", cRect, false);
        mixPill("solo", soloRect, t->solo);
        mixPill("arm", armRect, t->arm);
    }
}

void drawDeviceGraph(Canvas& c, Rect r, int kind) {
    c.fillRoundRect(r, 6.f, Color::argb(0xFF100E0C));
    c.innerShadow(r, 8.f, Color{0, 0, 0, 140});
    const float mid = r.y + r.h * 0.5f;
    if (kind == 0) {
        float px = r.x + 8.f;
        float py = mid;
        for (int i = 1; i <= 64; ++i) {
            const float t = static_cast<float>(i) / 64.f;
            const float x = r.x + 8.f + t * (r.w - 16.f);
            const float y = mid - std::sin(t * kPi * 6.f) * std::sin(t * kPi) * r.h * 0.36f;
            c.line(px, py, x, y, amber(), 1.8f);
            px = x;
            py = y;
        }
    } else if (kind == 1) {
        float px = r.x + 8.f;
        float py = r.y + r.h - 16.f;
        for (int i = 1; i <= 48; ++i) {
            const float t = static_cast<float>(i) / 48.f;
            const float x = r.x + 8.f + t * (r.w - 16.f);
            const float bell = std::exp(-std::pow((t - 0.62f) * 7.f, 2.f));
            const float y = r.y + r.h - 18.f - (0.15f + 0.7f * bell) * (r.h - 28.f);
            c.line(px, py, x, y, orange(), 2.f);
            px = x;
            py = y;
        }
    } else {
        c.line(r.x + 12.f, r.y + r.h - 16.f, r.x + r.w * 0.35f, r.y + r.h * 0.55f, orange(), 2.f);
        c.line(r.x + r.w * 0.35f, r.y + r.h * 0.55f, r.x + r.w * 0.7f, r.y + r.h * 0.28f, orange(), 2.f);
        c.line(r.x + r.w * 0.7f, r.y + r.h * 0.28f, r.x + r.w - 12.f, r.y + 16.f, orange(), 2.f);
    }
}

void drawEnvelope(Canvas& c, Rect r) {
    c.fillRoundRect(r, 4.f, Color::argb(0xFF100E0C));
    const float x0 = r.x + 8.f;
    const float base = r.y + r.h - 10.f;
    const float peak = r.y + 12.f;
    const float a = r.x + r.w * 0.22f;
    const float d = r.x + r.w * 0.4f;
    const float s = r.x + r.w * 0.72f;
    const float sus = r.y + r.h * 0.42f;
    c.line(x0, base, a, peak, orange(), 2.f);
    c.line(a, peak, d, sus, orange(), 2.f);
    c.line(d, sus, s, sus, orange(), 2.f);
    c.line(s, sus, r.x + r.w - 8.f, base, orange(), 2.f);
}

void drawLfo(Canvas& c, Rect r) {
    c.fillRoundRect(r, 4.f, Color::argb(0xFF100E0C));
    const float mid = r.y + r.h * 0.5f;
    float px = r.x + 6.f;
    float py = mid;
    const int n = 48;
    for (int i = 1; i <= n; ++i) {
        const float t = static_cast<float>(i) / n;
        const float x = r.x + 6.f + t * (r.w - 12.f);
        const float y = mid - std::sin(t * kPi * 4.f) * (r.h * 0.32f);
        c.line(px, py, x, y, amber(), 1.8f);
        px = x;
        py = y;
    }
}

std::string shortParam(const std::string& id) {
    if (id == "position") return "Position";
    if (id == "cutoff") return "Cutoff";
    if (id == "resonance" || id == "reso") return "Reso";
    if (id == "unison") return "Unison";
    if (id == "wave") return "Wave";
    if (id == "drive") return "Drive";
    if (id == "mix") return "Mix";
    if (id == "tone") return "Tone";
    if (id == "out") return "Out";
    return id.size() > 8 ? id.substr(0, 8) : id;
}

void chainCard(Canvas& c, Rect r) {
    c.dropShadow(r, 8.f, 10.f, Color{0, 0, 0, 160});
    c.fillRoundRect(r, 8.f, Color::argb(0xFF16120F));
    c.innerShadow(r, 10.f, Color{0, 0, 0, 160});
    c.strokeRect(r, Color::argb(0xFF3C3732), 1.2f);
    c.line(r.x + 12.f, r.y + 1.5f, r.x + r.w - 12.f, r.y + 1.5f, Color::argb(0xFF6A5430), 1.4f);
}

void drawChainDisplay(Canvas& c, Rect r, const std::string& type) {
    c.fillRoundRect(r, 4.f, Color::argb(0xFF100E0C));
    c.innerShadow(r, 5.f, Color{0, 0, 0, 140});
    c.clip(r);
    const float mid = r.y + r.h * 0.5f;
    const Color glow{255, 140, 40, 80};
    const Color fill{218, 165, 32, 110};
    auto column = [&](float x, float yTop, Color color) {
        const float y0 = std::clamp(yTop, r.y + 4.f, r.y + r.h - 6.f);
        c.fillRect({x, y0, 1.f, r.y + r.h - 6.f - y0}, color);
    };
    if (type == "eq") {
        const int cols = std::max(8, static_cast<int>(r.w) - 20);
        auto yAt = [&](float t) {
            const float bell = std::exp(-std::pow((t - 0.55f) * 4.4f, 2.f));
            return r.y + r.h * 0.80f - bell * r.h * 0.58f;
        };
        for (int i = 0; i < cols; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(cols);
            const float x = r.x + 10.f + static_cast<float>(i);
            const float y = yAt(t);
            column(x, y - 3.f, glow);
            column(x, y, fill);
        }
        const Color grid{90, 78, 62, 150};
        for (int i = 1; i < 4; ++i) {
            const float y = r.y + r.h * static_cast<float>(i) / 4.f;
            const float x = r.x + r.w * static_cast<float>(i) / 4.f;
            c.line(r.x + 8.f, y, r.x + r.w - 8.f, y, grid, 1.f);
            c.line(x, r.y + 8.f, x, r.y + r.h - 8.f, grid, 1.f);
        }
        float prevX = r.x + 10.f;
        float prevY = yAt(0.f);
        for (int i = 1; i < cols; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(cols);
            const float x = r.x + 10.f + static_cast<float>(i);
            const float y = yAt(t);
            c.line(prevX, prevY, x, y, Color::argb(0xFFFFC56A), 2.2f);
            prevX = x;
            prevY = y;
        }
        const float nodeT[] = {0.14f, 0.55f, 0.84f};
        for (float t : nodeT) {
            const float x = r.x + 10.f + t * static_cast<float>(cols);
            const float y = yAt(t);
            c.fillRoundRect({x - 5.f, y - 5.f, 10.f, 10.f}, 2.f, Color::argb(0xFF1A120C));
            c.fillRoundRect({x - 3.f, y - 3.f, 6.f, 6.f}, 1.f, Color::argb(0xFFFFE0A0));
        }
        c.resetClip();
        return;
    }
    if (type == "filter") {
        const int cols = std::max(8, static_cast<int>(r.w) - 16);
        auto yAt = [&](float t) {
            const float bump = std::exp(-std::pow((t - 0.58f) * 9.f, 2.f));
            const float fall = t < 0.48f ? 0.f : std::pow(t - 0.48f, 1.6f) * 1.7f;
            return std::clamp(r.y + r.h * (0.22f + fall) - bump * r.h * 0.34f, r.y + 6.f, r.y + r.h - 8.f);
        };
        for (int i = 0; i < cols; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(cols);
            const float x = r.x + 8.f + static_cast<float>(i);
            const float y = yAt(t);
            column(x, y - 3.f, glow);
            column(x, y, fill);
        }
        float prevX = r.x + 8.f;
        float prevY = yAt(0.f);
        for (int i = 1; i < cols; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(cols);
            const float x = r.x + 8.f + static_cast<float>(i);
            const float y = yAt(t);
            c.line(prevX, prevY, x, y, Color::argb(0xFFFFC56A), 2.2f);
            prevX = x;
            prevY = y;
        }
        c.resetClip();
        return;
    }
    const int cols = std::max(8, static_cast<int>(r.w) - 16);
    for (int i = 0; i < cols; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(std::max(1, cols - 1));
        const float lobe = std::fabs(std::sin(t * 96.f)) * (0.25f + 0.75f * std::fabs(std::sin(t * 23.f + 0.6f)));
        const float flutter = 0.55f + 0.45f * std::fabs(std::sin(t * 7.f));
        const float hgt = 2.f + lobe * flutter * r.h * 0.40f;
        const float x = r.x + 8.f + static_cast<float>(i);
        c.fillRect({x, mid - hgt - 2.f, 1.f, hgt * 2.f + 4.f}, glow);
        c.fillRect({x, mid - hgt, 1.f, hgt * 2.f}, Color::argb(0xFFE8B15A));
    }
    c.resetClip();
}

void goldJoin(Canvas& c, float x0, float x1, float y) {
    if (x1 <= x0 + 4.f)
        return;
    c.line(x0, y, x1, y, gold(), 3.f);
    c.fillEllipse(x0, y, 4.f, 4.f, gold());
    c.fillEllipse(x1, y, 4.f, 4.f, gold());
    c.fillEllipse((x0 + x1) * 0.5f, y, 5.f, 5.f, gold());
}

void deviceChain(Canvas& c, const Frame& f) {
    const float w = static_cast<float>(c.width());
    const float h = static_cast<float>(c.height());
    const Track* chain = f.project ? f.project->findTrack("lead") : nullptr;
    Rect box{16.f, kBarH + 12.f, w - 32.f, std::max(240.f, h - kBarH - 24.f)};
    panelBox(c, box, 10.f);
    c.drawText(chain ? chain->name.c_str() : "Lead", box.x + 16.f, box.y + 10.f, 22.f, ink(), true);
    if (!chain)
        return;
    const float sideW = 176.f;
    const float gap = 34.f;
    const float top = box.y + 44.f;
    const int n = static_cast<int>(chain->devices.size());
    const float cardsW = box.w - sideW - 40.f;
    const float cardW = n > 0 ? (cardsW - gap * static_cast<float>(std::max(0, n - 1))) / static_cast<float>(n) : 220.f;
    // About as tall as they are wide, and never stretched down the lane.
    const float cardH = std::min(cardW * 1.1f, box.h - 64.f);
    const float titleH = 36.f;
    const float labelH = 26.f;
    const float lower = cardH * 0.50f;
    const float knobD = std::min(cardW * 0.40f, lower - labelH - 8.f);
    const float dispTop = titleH;
    const float dispH = std::max(56.f, cardH - titleH - (knobD + labelH) - 18.f);
    const float knobCy = cardH - 12.f - labelH - knobD * 0.5f;
    const float joinY = top + dispTop + dispH * 0.5f;
    auto take = [](const DeviceState& d, std::initializer_list<const char*> prefer, int limit) {
        std::vector<std::pair<std::string, float>> out;
        for (const char* id : prefer) {
            const auto it = d.params.find(id);
            if (it == d.params.end())
                continue;
            out.push_back(*it);
            if (static_cast<int>(out.size()) >= limit)
                return out;
        }
        for (const auto& pr : d.params) {
            bool have = false;
            for (const auto& got : out)
                have = have || got.first == pr.first;
            if (have)
                continue;
            out.push_back(pr);
            if (static_cast<int>(out.size()) >= limit)
                break;
        }
        return out;
    };
    for (int i = 0; i < n; ++i) {
        const auto& d = chain->devices[static_cast<size_t>(i)];
        Rect r{box.x + 16.f + static_cast<float>(i) * (cardW + gap), top, cardW, cardH};
        if (r.x + r.w > box.x + box.w - sideW - 16.f)
            break;
        chainCard(c, r);
        centered(c, d.name, r.x + r.w * 0.5f, r.y + 8.f, 22.f, ink(), true);
        const bool graph = d.type == "eq";
        const bool knobsOnly = d.type == "saturator" || d.type == "compressor" || d.type == "dyn";
        if (graph)
            drawChainDisplay(c, {r.x + 12.f, r.y + titleH, r.w - 24.f, cardH - titleH - 14.f}, d.type);
        else if (!knobsOnly)
            drawChainDisplay(c, {r.x + 12.f, r.y + dispTop, r.w - 24.f, dispH}, d.type);
        if (!graph) {
            const char* order[] = {"position", "wave", "cutoff", "reso", "resonance", "drive", "mix", "tone", "out"};
            const auto knobs = take(d, {order[0], order[1], order[2], order[3], order[4], order[5], order[6], order[7], order[8]},
                                     knobsOnly ? 4 : 2);
            const int count = static_cast<int>(knobs.size());
            const float rowPitch = knobD + 22.f;
            const float gridTop = r.y + titleH + std::max(0.f, (cardH - titleH - (rowPitch + knobD) - 8.f) * 0.5f);
            for (int k = 0; k < count; ++k) {
                const int col = k % 2;
                const int row = k / 2;
                const float cxk = r.x + (count == 1 ? r.w * 0.5f : (col == 0 ? r.w * 0.27f : r.w * 0.73f));
                const float cyk = knobsOnly ? gridTop + knobD * 0.5f + static_cast<float>(row) * rowPitch : r.y + knobCy;
                const float norm = knobNorm(knobs[static_cast<size_t>(k)].second);
                chainKnob(c, cxk, cyk, knobD, norm, shortParam(knobs[static_cast<size_t>(k)].first));
                Control ctl;
                ctl.id = "dev/" + d.id + "/" + knobs[static_cast<size_t>(k)].first;
                ctl.kind = ControlKind::Knob;
                ctl.hit = {cxk - knobD * 0.5f, cyk - knobD * 0.5f, knobD, knobD};
                ctl.norm = norm;
                ctl.value = knobs[static_cast<size_t>(k)].second;
                ctl.anchor = ctl.value;
                place(f, ctl);
            }
        }
        if (i + 1 < n) {
            const float nextX = r.x + cardW + gap;
            if (nextX + cardW <= box.x + box.w - sideW - 16.f)
                goldJoin(c, r.x + r.w + 2.f, nextX - 2.f, joinY);
        }
    }
    Rect mod{box.x + box.w - sideW, top, sideW - 14.f, cardH};
    c.drawText("LFO", mod.x + 4.f, box.y + 10.f, 22.f, ink(), true);
    const char* titles[] = {"Envelope", "LFO", "Envelope"};
    const float slot = (mod.h - 8.f) / 3.f;
    for (int s = 0; s < 3; ++s) {
        Rect card{mod.x, mod.y + static_cast<float>(s) * slot, mod.w, slot - 8.f};
        chainCard(c, card);
        c.drawText(titles[s], card.x + 10.f, card.y + 6.f, 18.f, dim(), false);
        Rect body{card.x + 8.f, card.y + 28.f, card.w - 16.f, card.h - 36.f};
        if (s == 1)
            drawLfo(c, body);
        else
            drawEnvelope(c, body);
    }
}

void piano(Canvas& c, const Frame& f) {
    const float w = static_cast<float>(c.width());
    const float h = static_cast<float>(c.height());
    const float keyW = 84.f;
    const float velH = 88.f;
    const float top = kBarH + 28.f;
    const Clip* clip = nullptr;
    if (f.project) {
        for (const auto& t : f.project->tracks) {
            if (t.id == "lead" && !t.clips.empty())
                clip = &t.clips.front();
        }
    }
    int pitchTop = 76;
    int pitchBot = 60;
    if (clip && !clip->notes.empty()) {
        pitchTop = clip->notes.front().pitch;
        pitchBot = pitchTop;
        for (const auto& n : clip->notes) {
            pitchTop = std::max(pitchTop, n.pitch);
            pitchBot = std::min(pitchBot, n.pitch);
        }
    }
    const int rows = 12;
    if (pitchTop - pitchBot + 1 < rows)
        pitchBot = pitchTop - rows + 1;
    else
        pitchTop = pitchBot + rows - 1;
    const float rowH = (h - top - velH) / rows;
    const double origin = clip ? clip->startQ : 0.0;
    const double spanQ = clip ? std::max(8.0, clip->lengthQ) : 32.0;
    const float gridW = w - keyW - 16.f;
    auto xAt = [&](double q) { return keyW + static_cast<float>((q - origin) / spanQ) * gridW; };
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int bars = std::max(1, static_cast<int>(spanQ / 4.0));
    for (int bar = 0; bar <= bars; ++bar) {
        const float x = xAt(origin + bar * 4.0);
        c.drawText(std::to_string(bar + 1), x + 4.f, kBarH + 4.f, 18.f, dim(), false);
        c.line(x, top, x, h - velH, line(), bar % 4 == 0 ? 1.4f : 1.f);
    }
    const double head = f.engine ? f.engine->playhead() : (f.project ? f.project->playhead : origin);
    const float hx = xAt(head);
    for (int p = 0; p < rows; ++p) {
        const int pitch = pitchTop - p;
        const float y = top + p * rowH;
        const int pc = ((pitch % 12) + 12) % 12;
        const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        c.fillRect({0, y, keyW, rowH - 1.f}, Color::argb(0xFFE7D7C8));
        c.fillRect({keyW, y, w - keyW, rowH - 1.f}, black ? Color::argb(0xFF100E0C) : Color::argb(0xFF161311));
        if (black)
            c.fillRect({0, y, keyW * 0.62f, rowH - 1.f}, Color::argb(0xFF1A1614));
        c.fillRect({0, y + rowH - 1.f, keyW, 1.f}, Color::argb(0xFF3C3732));
        if (rowH >= 22.f || pc == 0) {
            char label[16];
            std::snprintf(label, sizeof(label), "%s%d", names[pc], pitch / 12 - 1);
            c.drawText(label, 6.f, y + std::max(0.f, (rowH - 20.f) * 0.5f), 18.f, black ? Color::argb(0xFFD1BCAC) : Color::argb(0xFF1A120C), true);
        }
        if (!clip)
            continue;
        for (const auto& n : clip->notes) {
            if (n.pitch != pitch)
                continue;
            const float x = xAt(origin + n.startQ);
            const float nw = std::max(10.f, static_cast<float>(n.lengthQ / spanQ) * gridW);
            Rect note{x, y + 3.f, nw, std::max(8.f, rowH - 6.f)};
            c.fillRoundRect(note, 3.f, gold());
            c.fillRect({note.x, note.y, 3.f, note.h}, amber());
        }
    }
    c.line(hx, top, hx, h - velH, rod(), 1.6f);
    c.fillEllipse(hx - 6.f, top - 6.f, 12.f, 12.f, gold());
    c.fillRect({0, h - velH, w, velH}, Color::argb(0xFF0E0C0B));
    c.drawText("Velocity", 8.f, h - velH + 8.f, 18.f, dim(), true);
    if (clip) {
        for (const auto& n : clip->notes) {
            const float x = xAt(origin + n.startQ);
            const float nw = std::max(8.f, static_cast<float>(n.lengthQ / spanQ) * gridW * 0.45f);
            const float vh = std::max(6.f, n.velocity * (velH - 28.f));
            c.fillRect({x, h - 8.f - vh, nw, vh}, amber());
        }
    }
}

void browser(Canvas& c, const Frame& f) {
    const float w = static_cast<float>(c.width());
    const float h = static_cast<float>(c.height());
    Rect rail{12.f, kBarH + 12.f, 250.f, h - kBarH - 24.f};
    panelBox(c, rail, 10.f);
    Rect search{rail.x + 12.f, rail.y + 12.f, rail.w - 24.f, 32.f};
    c.fillRoundRect(search, 6.f, Color::argb(0xFF161311));
    c.strokeRect(search, line(), 1.f);
    glyphIcon(c, 5, search.x + 8.f, search.y + 6.f, dim());
    c.drawText(f.search.empty() ? "Search" : f.search, search.x + 32.f, search.y + 6.f, 18.f, f.search.empty() ? dim() : ink(), false);
    const char* cats[] = {"Sounds", "Kick", "Bass", "Lead", "Pads", "Instruments", "Audio Effects", "Samples", "Plugins", "Favorites"};
    for (int i = 0; i < 10; ++i) {
        const bool child = i >= 1 && i <= 4;
        const bool on = i == 0;
        Rect row{rail.x + 8.f, rail.y + 56.f + i * 32.f, rail.w - 16.f, 28.f};
        if (on)
            c.fillRoundRect(row, 4.f, Color::argb(0xFF2A2118));
        c.drawText(child ? ">" : (i == 0 ? "v" : ">"), row.x + (child ? 18.f : 6.f), row.y + 4.f, 18.f, dim(), false);
        if (!child)
            glyphIcon(c, std::min(i, 6), row.x + 22.f, row.y + 4.f, on ? amber() : Color::argb(0xFFD1BCAC));
        c.drawText(cats[i], row.x + (child ? 36.f : 46.f), row.y + 4.f, 18.f, on ? ink() : dim(), on);
    }

    Rect list{rail.x + rail.w + 16.f, rail.y, w - rail.w - 340.f, 430.f};
    panelBox(c, list, 10.f);
    c.drawText("Name", list.x + 16.f, list.y + 12.f, 18.f, dim(), true);
    c.drawText("Tags", list.x + 280.f, list.y + 12.f, 18.f, dim(), true);
    c.drawText("Duration", list.x + 520.f, list.y + 12.f, 18.f, dim(), true);
    const char* items[] = {"Kick", "Tight Kick", "Soft Kick", "Bass", "Sub Bass", "Lead", "Pads"};
    const char* tags[] = {"One-shot", "One-shot", "Loop", "Loop", "One-shot", "Loop", "Loop"};
    const char* durs[] = {"0:01", "0:01", "0:02", "0:15", "0:05", "0:30", "0:45"};
    for (int i = 0; i < 7; ++i) {
        Rect row{list.x + 8.f, list.y + 44.f + i * 48.f, list.w - 16.f, 42.f};
        if (i == 0)
            c.fillRoundRect(row, 6.f, Color::argb(0xFF2A2118));
        c.drawText(items[i], row.x + 12.f, row.y + 10.f, 18.f, ink(), i == 0);
        c.fillRoundRect({row.x + 272.f, row.y + 8.f, 92.f, 26.f}, 8.f, Color::argb(0xFF1A1614));
        c.drawText(tags[i], row.x + 280.f, row.y + 10.f, 18.f, rod(), true);
        if (i < 2) {
            c.fillRoundRect({row.x + 372.f, row.y + 8.f, 72.f, 26.f}, 8.f, Color::argb(0xFF1A1614));
            c.drawText("Drums", row.x + 380.f, row.y + 10.f, 18.f, rod(), true);
        }
        c.drawText(durs[i], row.x + 512.f, row.y + 10.f, 18.f, dim(), false);
        glyphIcon(c, 6, row.x + row.w - 32.f, row.y + 12.f, i < 3 ? gold() : Color::argb(0xFF3C3732));
    }

    Rect wave{list.x, list.y + list.h + 16.f, list.w, h - (list.y + list.h + 28.f)};
    panelBox(c, wave, 10.f);
    const float mid = wave.y + wave.h * 0.5f;
    for (int i = 0; i < static_cast<int>(wave.w) - 32; ++i) {
        const float amp = 8.f + (wave.h * 0.28f) * std::fabs(std::sin(i * 0.08f) * std::sin(i * 0.015f));
        c.fillRect({wave.x + 16.f + static_cast<float>(i), mid - amp, 1.f, amp * 2.f}, amber());
    }
    c.fillRoundRect({wave.x + wave.w - 56.f, wave.y + wave.h - 48.f, 36.f, 32.f}, 6.f, Color::argb(0xFF1A1614));
    playGlyph(c, wave.x + wave.w - 44.f, wave.y + wave.h - 42.f, 16.f, rod());

    Rect side{w - 280.f, rail.y, 264.f, h - kBarH - 24.f};
    const char* previews[] = {"Kick", "Bass", "Lead", "Pads"};
    for (int i = 0; i < 4; ++i) {
        Rect card{side.x, side.y + i * ((side.h - 12.f) / 4.f), side.w, (side.h - 12.f) / 4.f - 12.f};
        panelBox(c, card, 8.f);
        c.drawText(previews[i], card.x + 12.f, card.y + 8.f, 18.f, ink(), true);
        for (int k = 0; k < 28; ++k) {
            const float amp = 6.f + 16.f * std::fabs(std::sin((k + i) * 0.55f));
            c.fillRect({card.x + 16.f + k * 8.f, card.y + card.h * 0.62f - amp, 4.f, amp * 2.f}, amber());
        }
    }
}

void pluginWindow(Canvas& c, const Frame& f) {
    const float w = static_cast<float>(c.width());
    const float h = static_cast<float>(c.height());
    c.strokeRect({10.f, kBarH + 4.f, w - 20.f, h - kBarH - 14.f}, gold(), 1.5f);
    c.drawText("GHOVLZ! DAW", 36.f, kBarH + 8.f, 28.f, gold(), true);
    c.drawText("Visual language", 36.f, kBarH + 42.f, 18.f, dim(), false);
    Rect palette{28.f, kBarH + 70.f, w - 56.f, 168.f};
    panelBox(c, palette, 10.f);
    c.drawText("Palette", palette.x + 16.f, palette.y + 10.f, 18.f, ink(), true);
    const uint32_t swatches[] = {0x0A0A0A, 0x161311, 0x3C3732, 0xB49637, 0xF5F5F5, 0xDAA520,
                                 0xFF821E, 0x231F20, 0xD1BCAC, 0xFF6E00, 0x14120E, 0xDC2828};
    const char* names[] = {"Background", "Panel", "Line", "Gold", "Text", "Goldenrod",
                           "Amber", "Knob", "Pointer", "Arc", "Meter", "Clip"};
    const char* hexes[] = {"#0A0A0A", "#161311", "#3C3732", "#B49637", "#F5F5F5", "#DAA520",
                           "#FF821E", "#231F20", "#D1BCAC", "#FF6E00", "#14120E", "#DC2828"};
    for (int i = 0; i < 12; ++i) {
        const float x = palette.x + 16.f + i * 132.f;
        c.fillRoundRect({x, palette.y + 40.f, 112.f, 48.f}, 4.f, Color::argb(0xFF000000u | swatches[i]));
        c.strokeRect({x, palette.y + 40.f, 112.f, 48.f}, line(), 1.f);
        c.drawText(hexes[i], x, palette.y + 94.f, 14.f, dim(), false);
        c.drawText(names[i], x, palette.y + 112.f, 14.f, ink(), false);
    }
    Rect type{28.f, kBarH + 254.f, 460.f, 340.f};
    panelBox(c, type, 10.f);
    c.drawText("Type specimen", type.x + 16.f, type.y + 12.f, 18.f, ink(), true);
    c.drawText("GHOVLZ! DAW", type.x + 16.f, type.y + 48.f, 32.f, gold(), true);
    c.drawText("Display", type.x + 16.f, type.y + 100.f, 18.f, dim(), false);
    c.drawText("Heading", type.x + 16.f, type.y + 128.f, 22.f, ink(), true);
    c.drawText("Label", type.x + 16.f, type.y + 160.f, 18.f, ink(), false);
    c.drawText("Value", type.x + 16.f, type.y + 188.f, 18.f, rod(), true);
    c.drawText("Tempo", type.x + 220.f, type.y + 110.f, 18.f, dim(), false);
    c.drawText("128.00 BPM", type.x + 220.f, type.y + 140.f, 28.f, ink(), true);
    Rect knobs{504.f, kBarH + 254.f, 620.f, 340.f};
    panelBox(c, knobs, 10.f);
    c.drawText("DarkKnob", knobs.x + 16.f, knobs.y + 12.f, 18.f, ink(), true);
    const char* states[] = {"Default", "Hover", "Active", "Modulated"};
    const float norms[] = {0.5f, 0.62f, 0.84f, f.pluginGain};
    for (int i = 0; i < 4; ++i)
        darkKnob(c, knobs.x + 80.f + i * 150.f, knobs.y + 150.f, 64.f, norms[i], states[i], "");
    Rect meter{1140.f, kBarH + 254.f, w - 1168.f, 340.f};
    panelBox(c, meter, 10.f);
    c.drawText("Meter and fader", meter.x + 16.f, meter.y + 12.f, 18.f, ink(), true);
    drawStereo(c, f, "lead", {meter.x + 36.f, meter.y + 48.f, 36.f, 150.f}, 0.72f);
    c.drawText("Meter", meter.x + 28.f, meter.y + 206.f, 18.f, dim(), false);
    Rect well{meter.x + 120.f, meter.y + 48.f, 16.f, 150.f};
    c.fillRoundRect(well, 4.f, Color::argb(0xFF1A1614));
    c.innerShadow(well, 4.f, Color{0, 0, 0, 160});
    goldCap(c, well.x - 10.f, well.y + 48.f, 36.f, 22.f);
    c.drawText("Fader", meter.x + 110.f, meter.y + 206.f, 18.f, dim(), false);
    Rect buttons{28.f, kBarH + 610.f, w - 56.f, h - kBarH - 626.f};
    panelBox(c, buttons, 10.f);
    c.drawText("Buttons and toggles", buttons.x + 16.f, buttons.y + 12.f, 18.f, ink(), true);
    const float by = buttons.y + buttons.h * 0.42f;
    pill(c, {buttons.x + 220.f, by, 120.f, 36.f}, "Button", false);
    pill(c, {buttons.x + 360.f, by, 120.f, 36.f}, "Active", true);
    transportButton(c, {buttons.x + 540.f, by - 2.f, 40.f, 40.f}, true);
    playGlyph(c, buttons.x + 552.f, by + 8.f, 18.f, rod());
    transportButton(c, {buttons.x + 592.f, by - 2.f, 40.f, 40.f}, false);
    c.fillRoundRect({buttons.x + 604.f, by + 10.f, 16.f, 16.f}, 2.f, ink());
    transportButton(c, {buttons.x + 644.f, by - 2.f, 40.f, 40.f}, false);
    c.fillEllipse(buttons.x + 664.f, by + 18.f, 8.f, 8.f, Color::argb(0xFFDC2828));
    pill(c, {buttons.x + 740.f, by + 4.f, 40.f, 28.f}, "S", true);
    pill(c, {buttons.x + 792.f, by + 4.f, 64.f, 28.f}, "Arm", true);
    c.fillRoundRect({buttons.x + 920.f, by + 6.f, 64.f, 28.f}, 14.f, gold());
    c.fillEllipse(buttons.x + 960.f, by + 20.f, 10.f, 10.f, Color::argb(0xFF1A120C));
    c.drawText("Toggle", buttons.x + 996.f, by + 8.f, 18.f, ink(), false);
}

} // namespace

void preferences(Canvas& c, const Frame& f) {
    const float w = static_cast<float>(c.width());
    c.drawText("Settings", 28.f, kBarH + 12.f, 22.f, ink(), true);
    const std::string rows[] = {
        std::string("Backend  ") + f.prefBackend,
        std::string("Device  ") + f.prefDevice,
        std::string("Rate  ") + std::to_string(f.prefRate),
        std::string("Buffer  ") + std::to_string(f.prefFrames),
        std::string("MIDI  ") + (f.midiPort.empty() ? "none" : f.midiPort),
        std::string("Renderer  ") + f.prefRenderer,
    };
    for (int i = 0; i < 6; ++i)
        c.drawText(rows[i], 28.f, kBarH + 48.f + static_cast<float>(i) * 26.f, 18.f, dim(), false);

    const Theme& theme = active();
    Rect editor{360.f, kBarH + 48.f, w - 388.f, 860.f};
    panelBox(c, editor, 8.f);
    c.drawText("THEME", editor.x + 16.f, editor.y + 12.f, 18.f, rod(), true);
    c.drawText(theme.name, editor.x + 110.f, editor.y + 12.f, 18.f, ink(), true);
    const Role shown[] = {Role::Bg, Role::Panel, Role::Ink, Role::Gold, Role::Amber, Role::Arc, Role::Knob, Role::Pointer, Role::Record};
    const char* names[] = {"Background", "Panel", "Text", "Gold", "Amber", "Arc", "Knob", "Pointer", "Clip"};
    const char* keys[] = {"bg", "panel", "ink", "gold", "amber", "arc", "knob", "pointer", "record"};
    for (int i = 0; i < 9; ++i) {
        const float y = editor.y + 48.f + static_cast<float>(i) * 36.f;
        Rect sw{editor.x + 16.f, y, 28.f, 24.f};
        c.fillRoundRect(sw, 4.f, theme.color(shown[i]));
        const std::string swatchId = std::string("theme/swatch/") + keys[i];
        c.strokeRect(sw, f.widgets && f.widgets->isSelected(swatchId) ? gold() : line(), f.widgets && f.widgets->isSelected(swatchId) ? 2.f : 1.f);
        c.drawText(names[i], editor.x + 54.f, y + 2.f, 18.f, ink(), false);
        const bool lock = theme.locked[static_cast<int>(shown[i])] != 0;
        Rect lockRect{editor.x + 220.f, y, 64.f, 24.f};
        pill(c, lockRect, lock ? "Lock" : "Free", lock);
        Control swatch;
        swatch.id = swatchId;
        swatch.kind = ControlKind::Button;
        swatch.hit = sw;
        place(f, swatch);
        Control lockCtl;
        lockCtl.id = std::string("theme/lock/") + keys[i];
        lockCtl.kind = ControlKind::Pill;
        lockCtl.hit = lockRect;
        lockCtl.on = lock;
        place(f, lockCtl);
    }
    Rect ramp{editor.x + 320.f, editor.y + 56.f, editor.w - 348.f, 120.f};
    c.fillRoundRect(ramp, 6.f, Color::argb(0xFF100E0C));
    c.drawText("Meter ramp", ramp.x + 8.f, ramp.y + 6.f, 18.f, dim(), false);
    if (!theme.meter.stops.empty() && ramp.w > 20.f) {
        const float floorDb = theme.meter.floorDb;
        const float span = std::max(1.f, theme.meter.ceilDb - floorDb);
        for (int x = 0; x < static_cast<int>(ramp.w - 16.f); ++x) {
            const float db = floorDb + span * (static_cast<float>(x) / std::max(1.f, ramp.w - 16.f));
            c.fillRect({ramp.x + 8.f + static_cast<float>(x), ramp.y + 36.f, 1.f, 28.f}, theme.meter.colorAt(db));
        }
        for (const auto& stop : theme.meter.stops) {
            const float t = std::clamp((stop.db - floorDb) / span, 0.f, 1.f);
            const float x = ramp.x + 8.f + t * (ramp.w - 16.f);
            c.fillEllipse(x, ramp.y + 50.f, 5.f, 5.f, ink());
            char mark[16];
            std::snprintf(mark, sizeof(mark), "%.0f", stop.db);
            c.drawText(mark, x - 12.f, ramp.y + 78.f, 18.f, dim(), false);
        }
    }
    drawStereo(c, f, "master", {ramp.x + ramp.w - 36.f, editor.y + 200.f, 22.f, 160.f}, 0.7f);
    c.drawText("Live", ramp.x + ramp.w - 48.f, editor.y + 176.f, 18.f, dim(), false);

    Rect dice{editor.x + 320.f, editor.y + 210.f, 280.f, 280.f};
    c.drawText("Seed", dice.x, dice.y, 18.f, dim(), false);
    Rect seedRect{dice.x, dice.y + 26.f, 180.f, 32.f};
    c.fillRoundRect(seedRect, 4.f, Color::argb(0xFF1A1612));
    char seed[24];
    std::snprintf(seed, sizeof(seed), "%llu", static_cast<unsigned long long>(theme.seed ? theme.seed : 0xC0FFEEULL));
    const std::string seedShown = f.widgets && !f.widgets->draft("theme/seed").empty() ? f.widgets->draft("theme/seed") : seed;
    c.drawText(seedShown, dice.x + 10.f, dice.y + 30.f, 18.f, ink(), true);
    Control seedCtl;
    seedCtl.id = "theme/seed";
    seedCtl.kind = ControlKind::Field;
    seedCtl.hit = seedRect;
    seedCtl.text = seedShown;
    place(f, seedCtl);
    Rect diceRect{dice.x + 190.f, dice.y + 26.f, 80.f, 32.f};
    pill(c, diceRect, "Dice", true);
    Control diceCtl;
    diceCtl.id = "theme/dice";
    diceCtl.kind = ControlKind::Button;
    diceCtl.hit = diceRect;
    place(f, diceCtl);
    Rect brandRect{dice.x, dice.y + 72.f, 140.f, 28.f};
    Rect contrastRect{dice.x + 150.f, dice.y + 72.f, 120.f, 28.f};
    pill(c, brandRect, "Brand band", theme.brandBand);
    pill(c, contrastRect, "Contrast", theme.contrast);
    Control brand;
    brand.id = "theme/brand";
    brand.kind = ControlKind::Pill;
    brand.hit = brandRect;
    brand.on = theme.brandBand;
    place(f, brand);
    Control contrast;
    contrast.id = "theme/contrast";
    contrast.kind = ControlKind::Pill;
    contrast.hit = contrastRect;
    contrast.on = theme.contrast;
    place(f, contrast);
    Rect undoRect{dice.x, dice.y + 112.f, 96.f, 28.f};
    Rect redoRect{dice.x + 106.f, dice.y + 112.f, 80.f, 28.f};
    pill(c, undoRect, "Undo", false);
    pill(c, redoRect, "Redo", false);
    Control undo;
    undo.id = "theme/undo";
    undo.kind = ControlKind::Button;
    undo.hit = undoRect;
    place(f, undo);
    Control redo;
    redo.id = "theme/redo";
    redo.kind = ControlKind::Button;
    redo.hit = redoRect;
    place(f, redo);
    int built = 0;
    for (const auto& id : Theme::builtinIds()) {
        if (built >= 6)
            break;
        Rect row{dice.x, dice.y + 150.f + static_cast<float>(built) * 24.f, 220.f, 22.f};
        c.drawText(id.c_str(), row.x, row.y, 18.f, id == theme.id ? gold() : dim(), id == theme.id);
        Control preset;
        preset.id = "theme/preset/" + id;
        preset.kind = ControlKind::Button;
        preset.hit = row;
        place(f, preset);
        ++built;
    }
    if (!f.prefMessage.empty())
        c.drawText(f.prefMessage, 28.f, kBarH + 220.f, 18.f, amber(), false);
}

void paintFrame(Canvas& c, const Frame& frame) {
    if (frame.widgets)
        frame.widgets->begin();
    ThemeBind bind(frame.theme);
    c.clear(bg());
    topBar(c, frame);
    switch (frame.view) {
        case View::Arrangement: browserColumn(c, frame, static_cast<float>(c.height())); arrangement(c, frame); break;
        case View::Session: browserColumn(c, frame, static_cast<float>(c.height())); session(c, frame); break;
        case View::Perform: browserColumn(c, frame, static_cast<float>(c.height())); perform(c, frame); break;
        case View::Mixer: mixer(c, frame); break;
        case View::Device: deviceChain(c, frame); break;
        case View::Piano: piano(c, frame); break;
        case View::Browser: browser(c, frame); break;
        case View::Plugin: pluginWindow(c, frame); break;
        case View::Prefs: preferences(c, frame); break;
    }
}

Rect hitRect(Hit hit, float scale) {
    if (scale <= 0.f)
        scale = 1.f;
    auto scaled = [scale](Rect r) { return Rect{r.x * scale, r.y * scale, r.w * scale, r.h * scale}; };
    switch (hit) {
        case Hit::Arr: return scaled({kViewsX, 12.f, kViewW, 30.f});
        case Hit::Ses: return scaled({kViewsX + (kViewW + kViewGap), 12.f, kViewW, 30.f});
        case Hit::Perf: return scaled({kViewsX + 2.f * (kViewW + kViewGap), 12.f, kViewW, 30.f});
        case Hit::Mix: return scaled({kViewsX + 3.f * (kViewW + kViewGap), 12.f, kViewW, 30.f});
        case Hit::Play: return scaled({kTx, 10.f, kBtn, kBtn});
        case Hit::Stop: return scaled({kTx + (kBtn + kBtnGap), 10.f, kBtn, kBtn});
        case Hit::Rec: return scaled({kTx + 2.f * (kBtn + kBtnGap), 10.f, kBtn, kBtn});
        case Hit::Loop: return scaled({kTx + 3.f * (kBtn + kBtnGap), 10.f, kBtn, kBtn});
        case Hit::Metro: return scaled({kTx + 4.f * (kBtn + kBtnGap), 10.f, kBtn, kBtn});
        case Hit::Badge: return scaled({kViewsX, 12.f, 4.f * kViewW + 3.f * kViewGap, 30.f});
        case Hit::None: break;
    }
    return {};
}

Hit hitTest(float x, float y, int, int, float scale, View view) {
    const Hit order[] = {Hit::Arr, Hit::Ses, Hit::Perf, Hit::Mix, Hit::Play, Hit::Stop, Hit::Rec, Hit::Loop, Hit::Metro};
    for (Hit hit : order) {
        if (view == View::Piano && (hit == Hit::Arr || hit == Hit::Ses || hit == Hit::Perf || hit == Hit::Mix))
            continue;
        if (hitRect(hit, scale).contains(x, y))
            return hit;
    }
    return Hit::None;
}

View viewFromHit(Hit hit, View current) {
    switch (hit) {
        case Hit::Arr: return View::Arrangement;
        case Hit::Ses: return View::Session;
        case Hit::Perf: return View::Perform;
        case Hit::Mix: return View::Mixer;
        default: return current;
    }
}

} // namespace ghovlz
