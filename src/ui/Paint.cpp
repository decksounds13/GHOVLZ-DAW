#include "ui/Paint.h"

#include "dsp/Devices.h"
#include "engine/Engine.h"
#include "model/Project.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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
    const float rad = diameter * 0.5f;
    c.dropShadow({cx - rad, cy - rad + 2.f, diameter, diameter}, rad, 6.f, Color{0, 0, 0, 170});
    const Image& film = knobFilm();
    const int frameSize = film.w > 0 ? film.w : 220;
    const int frames = film.w > 0 ? std::max(1, film.h / frameSize) : 1;
    if (film.w > 0) {
        const int frame = std::clamp(static_cast<int>(std::lround(norm * (frames - 1))), 0, frames - 1);
        c.drawImage(film, {0, static_cast<float>(frame * frameSize), static_cast<float>(frameSize), static_cast<float>(frameSize)},
                    {cx - rad, cy - rad, diameter, diameter});
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

void goldCap(Canvas& c, float x, float y, float w, float h) {
    Rect r{x, y, w, h};
    c.dropShadow(r, 3.f, 4.f, Color{0, 0, 0, 160});
    c.fillRoundRect(r, 3.f, Color::argb(0xFF7A5A22));
    c.fillRoundRect({r.x + 1.f, r.y + 1.f, r.w - 2.f, r.h * 0.42f}, 2.f, Color::argb(0xFFE6C56A));
    c.fillRect({r.x + 3.f, r.y + r.h * 0.46f, r.w - 6.f, 2.f}, Color::argb(0xFF4A3414));
    c.fillRoundRect({r.x + 1.f, r.y + r.h * 0.55f, r.w - 2.f, r.h * 0.38f}, 2.f, Color::argb(0xFFB49637));
    c.innerShadow(r, 3.f, Color{0, 0, 0, 80});
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
float knobNorm(float v) {
    if (v >= 0.f && v <= 1.f)
        return v;
    if (v > 1.f && v <= 36.f)
        return v / 36.f;
    if (v > 36.f)
        return std::clamp(std::log(std::max(20.f, v) / 20.f) / std::log(1000.f), 0.f, 1.f);
    return dbNorm(v);
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

    const char* views[] = {"ARR", "SES", "PERF", "MIX"};
    const View shown[] = {View::Arrangement, View::Session, View::Perform, View::Mixer};
    for (int i = 0; i < 4; ++i) {
        Rect r{kViewsX + i * (kViewW + kViewGap), 12.f, kViewW, 30.f};
        const bool on = f.view == shown[i];
        c.fillRoundRect(r, 6.f, on ? Color::argb(0xFF3A2E1C) : Color::argb(0xFF161311));
        c.strokeRect(r, on ? gold() : line(), 1.f);
        c.drawText(views[i], r.x + 8.f, r.y + 4.f, 18.f, on ? rod() : dim(), true);
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
    if (kind == 0) {
        c.line(x + 2, y + 10, x + 6, y + 4, col, 1.6f);
        c.line(x + 6, y + 4, x + 8, y + 12, col, 1.6f);
        c.line(x + 8, y + 12, x + 12, y + 2, col, 1.6f);
        c.line(x + 12, y + 2, x + 16, y + 10, col, 1.6f);
    } else if (kind == 1) {
        c.fillRoundRect({x + 2, y + 4, 12, 10}, 2.f, col);
        c.fillRect({x + 4, y + 2, 6, 4}, col);
    } else if (kind == 2) {
        c.fillRect({x + 3, y + 8, 3, 8}, col);
        c.fillRect({x + 8, y + 4, 3, 12}, col);
        c.fillRect({x + 13, y + 6, 3, 10}, col);
    } else if (kind == 3) {
        c.strokeRect({x + 2, y + 3, 14, 12}, col, 1.4f);
        c.line(x + 2, y + 7, x + 16, y + 7, col, 1.4f);
    } else if (kind == 4) {
        c.line(x + 4, y + 14, x + 8, y + 3, col, 1.6f);
        c.line(x + 8, y + 3, x + 12, y + 14, col, 1.6f);
        c.line(x + 6, y + 10, x + 10, y + 10, col, 1.4f);
    } else if (kind == 5) {
        c.arc(x + 7, y + 7, 5.f, 0.2f, 5.8f, col, 1.6f);
        c.line(x + 11, y + 11, x + 16, y + 16, col, 1.8f);
    } else {
        const float cx = x + 8.f, cy = y + 8.f;
        for (int i = 0; i < 5; ++i) {
            const float a0 = -kPi * 0.5f + i * (2.f * kPi / 5.f);
            const float a1 = -kPi * 0.5f + ((i + 2) % 5) * (2.f * kPi / 5.f);
            c.line(cx + std::cos(a0) * 7.f, cy + std::sin(a0) * 7.f, cx + std::cos(a1) * 7.f, cy + std::sin(a1) * 7.f, col, 1.4f);
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
        c.drawText(t->name, headerX + 12.f, y + 4.f, 18.f, ink(), true);
        drawStereo(c, f, t->id, {headerX + headerW - 20.f, y + 4.f, 16.f, std::max(16.f, rowH - 8.f)}, 0.62f);
        const float pillY = y + std::max(26.f, rowH - 28.f);
        pill(c, {headerX + 12.f, pillY, 30.f, 22.f}, "M", t->mute);
        pill(c, {headerX + 46.f, pillY, 30.f, 22.f}, "S", t->solo);
        pill(c, {headerX + 80.f, pillY, 48.f, 22.f}, "Arm", t->arm);

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
                const float y0 = y + 6.f;
                const float y1 = y + rowH - 8.f;
                auto yOf = [&](float v) {
                    const float tnorm = std::clamp((v - vmin) / spanV, 0.f, 1.f);
                    return std::clamp(y0 + (1.f - tnorm) * std::max(4.f, y1 - y0), y + 4.f, y + rowH - 6.f);
                };
                const std::string label = autoLane.id.substr(prefix.size());
                c.drawText(label == "cutoff" ? "Filter Cutoff" : label, laneX + 8.f, y + 4.f, 18.f, rod(), false);
                float px = std::clamp(xAt(autoLane.points.front().quarter), laneX, laneR);
                float py = yOf(autoLane.points.front().value);
                for (size_t p = 1; p < autoLane.points.size(); ++p) {
                    const float nx = std::clamp(xAt(autoLane.points[p].quarter), laneX, laneR);
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
        miniKnob(c, laneR + 22.f, y + rowH * 0.42f, 14.f, (t->pan + 1.f) * 0.5f);
        drawStereo(c, f, t->id, {laneR + rightW - 22.f, y + 8.f, 14.f, rowH - 18.f}, 0.4f);
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
                const float cxk = r.x + 40.f + static_cast<float>(shown) * 78.f;
                if (cxk + 24.f > r.x + r.w - 20.f)
                    break;
                darkKnob(c, cxk, r.y + 70.f, 48.f, knobNorm(pr.second), pr.first, paramText(pr.second));
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
        c.drawText(t->name, r.x + 8.f, r.y + 12.f, 18.f, ink(), true);
        c.drawText("In  All Ins", r.x + 8.f, r.y + 36.f, 14.f, dim(), false);
        const std::string out = t->output.empty() || t->id == "master" ? "Master" : t->output;
        c.drawText(std::string("Out  ") + out, r.x + 8.f, r.y + 54.f, 14.f, dim(), false);

        const int slots = 4;
        for (int s = 0; s < slots; ++s) {
            Rect slot{r.x + 8.f, r.y + 80.f + s * 26.f, r.w - 16.f, 22.f};
            c.fillRoundRect(slot, 4.f, Color::argb(0xFF1A1612));
            c.innerShadow(slot, 3.f, Color{0, 0, 0, 110});
            std::string owned = "—";
            if (s < static_cast<int>(t->devices.size()))
                owned = t->devices[static_cast<size_t>(s)].name;
            c.drawText(owned, slot.x + 6.f, slot.y + 2.f, 14.f, owned == "—" ? dim() : ink(), false);
        }

        float a = -100.f, b = -100.f;
        for (const auto& s : t->sends) {
            if (s.target == "retA")
                a = s.levelDb;
            if (s.target == "retB")
                b = s.levelDb;
        }
        const float sendY = r.y + 214.f;
        darkKnob(c, r.x + r.w * 0.32f, sendY, 44.f, dbNorm(a), "A", a <= -80.f ? "-inf" : paramText(a));
        darkKnob(c, r.x + r.w * 0.72f, sendY, 44.f, dbNorm(b), "B", b <= -80.f ? "-inf" : paramText(b));

        char pan[16];
        std::snprintf(pan, sizeof(pan), "%+.1f", t->pan);
        const float panY = r.y + 320.f;
        darkKnob(c, r.x + r.w * 0.5f, panY, 48.f, (t->pan + 1.f) * 0.5f, "Pan", pan);
        char db[16];
        std::snprintf(db, sizeof(db), "%+.1f dB", t->volumeDb);
        centered(c, db, r.x + r.w * 0.5f, panY + 78.f, 18.f, rod(), true);

        const float faderTop = panY + 108.f;
        const float faderBot = r.y + r.h - 72.f;
        const float faderX = r.x + 46.f;
        const float faderH = std::max(40.f, faderBot - faderTop);
        Rect well{faderX, faderTop, 14.f, faderH};
        c.fillRoundRect(well, 4.f, Color::argb(0xFF1A1614));
        c.innerShadow(well, 4.f, Color{0, 0, 0, 180});
        const float fn = dbNorm(t->volumeDb);
        const float fy = faderBot - fn * faderH;
        c.fillRoundRect({faderX + 3.f, fy, 8.f, std::max(0.f, faderBot - fy)}, 2.f, Color::argb(0xFF5C4318));
        goldCap(c, faderX - 8.f, fy - 12.f, 30.f, 22.f);
        const char* marks[] = {"+6", "0", "-6", "-12", "-24", "-48"};
        const float markDb[] = {6, 0, -6, -12, -24, -48};
        for (int m = 0; m < 6; ++m) {
            const float my = faderBot - dbNorm(markDb[m]) * faderH;
            c.fillRect({faderX - 8.f, my, 6.f, 1.f}, dim());
            c.drawText(marks[m], r.x + 6.f, my - 8.f, 12.f, dim(), false);
        }
        drawStereo(c, f, t->id, {r.x + r.w - 40.f, faderTop, 28.f, faderH}, 0.55f);
        const float bw = (r.w - 18.f) * 0.5f;
        pill(c, {r.x + 6.f, r.y + r.h - 64.f, bw - 4.f, 24.f}, "S", t->solo);
        pill(c, {r.x + 8.f + bw, r.y + r.h - 64.f, bw - 4.f, 24.f}, "C", false);
        pill(c, {r.x + 6.f, r.y + r.h - 34.f, bw - 4.f, 24.f}, "Solo", t->solo);
        pill(c, {r.x + 8.f + bw, r.y + r.h - 34.f, bw - 4.f, 24.f}, "Arm", t->arm);
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

void deviceChain(Canvas& c, const Frame& f) {
    const float w = static_cast<float>(c.width());
    const float h = static_cast<float>(c.height());
    const Track* chain = f.project ? f.project->findTrack("lead") : nullptr;
    c.drawText("SYNTH LEAD", 24.f, kBarH + 12.f, 22.f, ink(), true);
    if (!chain)
        return;
    const float side = 300.f;
    const float top = kBarH + 52.f;
    const float pw = (w - side - 48.f) / std::max<size_t>(1, chain->devices.size()) - 12.f;
    for (size_t i = 0; i < chain->devices.size(); ++i) {
        const auto& d = chain->devices[i];
        Rect r{20.f + static_cast<float>(i) * (pw + 12.f), top, pw, h - top - 24.f};
        panelBox(c, r, 10.f);
        c.fillRect({r.x, r.y, 5.f, r.h}, gold());
        c.drawText(d.name, r.x + 18.f, r.y + 14.f, 22.f, ink(), true);
        drawDeviceGraph(c, {r.x + 16.f, r.y + 52.f, r.w - 52.f, std::max(80.f, r.h - 340.f)}, static_cast<int>(i % 3));
        drawStereo(c, f, chain->id, {r.x + r.w - 28.f, r.y + 52.f, 16.f, std::max(80.f, r.h - 340.f)}, 0.45f);
        int shown = 0;
        for (const auto& pr : d.params) {
            if (shown >= 4)
                break;
            const float cxk = r.x + 56.f + static_cast<float>(shown % 2) * 120.f;
            const float cyk = r.y + r.h - 210.f + static_cast<float>(shown / 2) * 110.f;
            darkKnob(c, cxk, cyk, 48.f, knobNorm(pr.second), pr.first, paramText(pr.second));
            ++shown;
        }
        if (i + 1 < chain->devices.size())
            c.line(r.x + r.w, r.y + 40.f, r.x + r.w + 12.f, r.y + 40.f, gold(), 2.f);
    }
    Rect mod{w - side - 8.f, top, side, h - top - 24.f};
    panelBox(c, mod, 10.f);
    c.fillRect({mod.x, mod.y, mod.w, 36.f}, Color::argb(0xFF1C1814));
    c.drawText("MODULATION", mod.x + 16.f, mod.y + 8.f, 18.f, rod(), true);
    c.drawText("LFO", mod.x + 16.f, mod.y + 48.f, 18.f, ink(), true);
    drawLfo(c, {mod.x + 16.f, mod.y + 76.f, mod.w - 32.f, 64.f});
    darkKnob(c, mod.x + 56.f, mod.y + 200.f, 48.f, 0.42f, "Rate", "1/8");
    darkKnob(c, mod.x + 160.f, mod.y + 200.f, 48.f, 0.65f, "Depth", "65%");
    c.drawText("ENVELOPE", mod.x + 16.f, mod.y + 280.f, 18.f, ink(), true);
    drawEnvelope(c, {mod.x + 16.f, mod.y + 308.f, mod.w - 32.f, 72.f});
    const char* env[] = {"A", "D", "S", "R"};
    const float envV[] = {0.18f, 0.36f, 0.62f, 0.4f};
    const char* envT[] = {"12", "180", "70", "240"};
    for (int i = 0; i < 4; ++i)
        darkKnob(c, mod.x + 40.f + i * 68.f, mod.y + 450.f, 44.f, envV[i], env[i], envT[i]);
}

void piano(Canvas& c, const Frame& f) {
    const float w = static_cast<float>(c.width());
    const float h = static_cast<float>(c.height());
    const float keyW = 72.f;
    const float velH = 72.f;
    const float top = kBarH + 56.f;
    const char* tools[] = {"Draw", "Select", "Grid", "Scale"};
    for (int i = 0; i < 4; ++i) {
        Rect r{16.f + i * 88.f, kBarH + 8.f, 80.f, 28.f};
        c.fillRoundRect(r, 4.f, i == 0 ? Color::argb(0xFF3A2E1C) : Color::argb(0xFF161311));
        c.strokeRect(r, i == 0 ? gold() : line(), 1.f);
        c.drawText(tools[i], r.x + 8.f, r.y + 4.f, 18.f, i == 0 ? rod() : ink(), true);
    }
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
        c.drawText(std::to_string(bar + 1), x + 4.f, kBarH + 36.f, 18.f, dim(), false);
        c.line(x, top, x, h - velH, line(), bar % 4 == 0 ? 1.4f : 1.f);
    }
    const double head = f.engine ? f.engine->playhead() : (f.project ? f.project->playhead : origin);
    const float hx = xAt(head);
    for (int p = 0; p < rows; ++p) {
        const int pitch = pitchTop - p;
        const float y = top + p * rowH;
        const int pc = ((pitch % 12) + 12) % 12;
        const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        c.fillRect({0, y, keyW, rowH - 1.f}, black ? Color::argb(0xFF1A1614) : Color::argb(0xFFD1BCAC));
        c.fillRect({keyW, y, w - keyW, rowH - 1.f}, black ? Color::argb(0xFF100E0C) : Color::argb(0xFF161311));
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
    browserColumn(c, f, h);
    Rect search{176.f, kBarH + 10.f, 420.f, 32.f};
    c.fillRoundRect(search, 6.f, Color::argb(0xFF161311));
    c.innerShadow(search, 4.f, Color{0, 0, 0, 120});
    c.strokeRect(search, line(), 1.f);
    glyphIcon(c, 5, search.x + 8.f, search.y + 6.f, dim());
    c.drawText(f.search.empty() ? "Search" : f.search, search.x + 32.f, search.y + 6.f, 18.f,
               f.search.empty() ? dim() : ink(), false);
    c.drawText("Name", 180.f, kBarH + 52.f, 18.f, dim(), true);
    c.drawText("Tags", 520.f, kBarH + 52.f, 18.f, dim(), true);
    c.drawText("Duration", 760.f, kBarH + 52.f, 18.f, dim(), true);
    const char* items[] = {"Warm Pad", "Analog Bass", "Dusty Kick", "Room Snare", "Tape Lead", "Vinyl Hat"};
    const char* tags[] = {"Pad", "Bass", "Drum", "Drum", "Lead", "Hat"};
    const char* durs[] = {"0:08", "0:04", "0:01", "0:01", "0:06", "0:01"};
    for (int i = 0; i < 6; ++i) {
        Rect r{176.f, kBarH + 80.f + i * 52.f, w - 520.f, 44.f};
        c.fillRoundRect(r, 6.f, Color::argb(0xFF161311));
        c.fillRect({r.x, r.y, 6.f, r.h}, i == 0 ? amber() : gold());
        c.drawText(items[i], r.x + 16.f, r.y + 10.f, 18.f, ink(), false);
        c.fillRoundRect({520.f, r.y + 8.f, 64.f, 26.f}, 8.f, Color::argb(0xFF2A2118));
        c.drawText(tags[i], 528.f, r.y + 10.f, 18.f, rod(), true);
        c.drawText(durs[i], 760.f, r.y + 10.f, 18.f, dim(), false);
        glyphIcon(c, 6, r.x + r.w - 36.f, r.y + 12.f, i < 2 ? gold() : Color::argb(0xFF3C3732));
    }
    Rect preview{w - 320.f, kBarH + 80.f, 300.f, 220.f};
    panelBox(c, preview, 8.f);
    c.drawText("Warm Pad", preview.x + 16.f, preview.y + 12.f, 18.f, ink(), true);
    for (int i = 0; i < 48; ++i) {
        const float amp = 8.f + 28.f * std::fabs(std::sin(i * 0.45f));
        c.fillRect({preview.x + 16.f + i * 5.5f, preview.y + 90.f - amp, 3.f, amp * 2.f}, amber());
    }
    c.fillRoundRect({preview.x + 16.f, preview.y + 150.f, 36.f, 28.f}, 4.f, Color::argb(0xFF1A1614));
    playGlyph(c, preview.x + 28.f, preview.y + 156.f, 16.f, rod());
    c.drawText("Recent", preview.x, preview.y + 240.f, 18.f, dim(), true);
    for (int i = 0; i < 3; ++i) {
        Rect tile{preview.x + i * 100.f, preview.y + 268.f, 92.f, 64.f};
        c.fillRoundRect(tile, 6.f, Color::argb(0xFF161311));
        c.innerShadow(tile, 4.f, Color{0, 0, 0, 100});
        c.drawText(items[i], tile.x + 8.f, tile.y + 20.f, 18.f, ink(), false);
    }
    c.drawText("Collections", 180.f, kBarH + 410.f, 18.f, dim(), true);
    const char* cols[] = {"Drums", "Bass", "Leads", "Vocals", "FX", "House"};
    for (int i = 0; i < 6; ++i) {
        Rect tile{176.f + (i % 6) * 150.f, kBarH + 444.f, 140.f, 120.f};
        c.dropShadow(tile, 6.f, 6.f, Color{0, 0, 0, 120});
        c.fillRoundRect(tile, 8.f, Color::argb(0xFF161311));
        c.innerShadow(tile, 8.f, Color{0, 0, 0, 110});
        c.fillRect({tile.x, tile.y, tile.w, 6.f}, i % 2 ? amber() : gold());
        for (int k = 0; k < 16; ++k) {
            const float amp = 8.f + 18.f * std::fabs(std::sin((k + i) * 0.7f));
            c.fillRect({tile.x + 12.f + k * 7.f, tile.y + 58.f - amp, 4.f, amp * 2.f}, amber());
        }
        c.drawText(cols[i], tile.x + 12.f, tile.y + 88.f, 18.f, ink(), true);
    }
}

void pluginWindow(Canvas& c, const Frame& f) {
    const float w = static_cast<float>(c.width());
    const float h = static_cast<float>(c.height());
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
    c.drawText("Settings", 28.f, kBarH + 20.f, 22.f, ink(), true);
    c.drawText(std::string("Theme  ") + active().name, 28.f, kBarH + 58.f, 16.f, gold(), true);
    const std::string rows[] = {
        std::string("Backend  ") + f.prefBackend,
        std::string("Device  ") + f.prefDevice,
        std::string("Rate  ") + std::to_string(f.prefRate),
        std::string("Buffer  ") + std::to_string(f.prefFrames),
        std::string("MIDI  ") + (f.midiPort.empty() ? "none" : f.midiPort),
        std::string("Renderer  ") + f.prefRenderer,
    };
    for (int i = 0; i < 6; ++i)
        c.drawText(rows[i], 28.f, kBarH + 100.f + static_cast<float>(i) * 28.f, 16.f, dim(), false);
    if (!f.prefMessage.empty())
        c.drawText(f.prefMessage, 28.f, kBarH + 280.f, 16.f, amber(), false);
}

void paintFrame(Canvas& c, const Frame& frame) {
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

Hit hitTest(float x, float y, int, int, float scale) {
    const Hit order[] = {Hit::Arr, Hit::Ses, Hit::Perf, Hit::Mix, Hit::Play, Hit::Stop, Hit::Rec, Hit::Loop, Hit::Metro};
    for (Hit hit : order)
        if (hitRect(hit, scale).contains(x, y))
            return hit;
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
