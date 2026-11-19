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
    c.dropShadow(r, radius, 8.f, Color{0, 0, 0, 150});
    c.fillRoundRect(r, radius, panel());
    c.strokeRect(r, line(), 1.f);
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

void knob(Canvas& c, float cx, float cy, float norm, const std::string& label, const std::string& value) {
    norm = std::clamp(norm, 0.f, 1.f);
    const float size = 52.f;
    const Image& film = knobFilm();
    const int frameSize = film.w > 0 ? film.w : 220;
    const int frames = film.w > 0 ? std::max(1, film.h / frameSize) : 1;
    if (film.w > 0) {
        const int frame = std::clamp(static_cast<int>(std::lround(norm * (frames - 1))), 0, frames - 1);
        c.drawImage(film, {0, static_cast<float>(frame * frameSize), static_cast<float>(frameSize), static_cast<float>(frameSize)},
                    {cx - size * 0.5f, cy - size * 0.5f, size, size});
    } else {
        c.fillEllipse(cx, cy, size * 0.5f, size * 0.5f, Color::argb(0xFF231F20));
    }
    const float a0 = -kPi * 0.5f - 145.f * kPi / 180.f;
    const float a1 = -kPi * 0.5f + 145.f * kPi / 180.f;
    c.arc(cx, cy, size * 0.5f + 4.f, a0, a0 + (a1 - a0) * norm, orange(), 2.4f);
    const float ang = a0 + (a1 - a0) * norm;
    c.line(cx + std::cos(ang) * 6.f, cy + std::sin(ang) * 6.f, cx + std::cos(ang) * 18.f, cy + std::sin(ang) * 18.f,
           Color::argb(0xFFD1BCAC), 2.2f);
    c.drawText(label, cx - 28.f, cy + 30.f, 13.f, dim(), false);
    c.drawText(value, cx - 24.f, cy + 46.f, 13.f, ink(), false);
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
    c.fillRoundRect(r, 3.f, on ? amber() : Color::argb(0xFF2A241E));
    c.drawText(text, r.x + 4.f, r.y + 1.f, 12.f, on ? Color::argb(0xFF1A120C) : dim(), true);
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
        const float y0 = mid - mx * r.h * 0.46f;
        const float y1 = mid - mn * r.h * 0.46f;
        c.fillRect({r.x + static_cast<float>(x), y0, 1.f, std::max(1.f, y1 - y0)}, col);
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
        c.drawText(views[i], r.x + 14.f, r.y + 7.f, 13.f, on ? rod() : dim(), true);
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
    c.drawText(bpm, 760.f, 16.f, 16.f, ink(), true);
    c.drawText(f.project ? (std::to_string(f.project->numerator) + "/" + std::to_string(f.project->denominator)) : "4/4",
               890.f, 16.f, 16.f, dim(), false);
    c.drawText(pos, 960.f, 16.f, 16.f, rod(), true);
    char cpu[32];
    std::snprintf(cpu, sizeof(cpu), "CPU %.0f%%", f.engine ? f.engine->cpuLoad() * 100.f : 0.f);
    c.drawText(cpu, w - 250.f, 16.f, 14.f, dim(), false);
    Rect badge{w - 108.f, 12.f, 92.f, 30.f};
    c.fillRoundRect(badge, 6.f, Color::argb(0xFF1A1614));
    c.strokeRect(badge, gold(), 1.f);
    c.drawText(f.backend ? f.backend : "CPU", badge.x + 14.f, badge.y + 7.f, 14.f, rod(), true);
}

void browserColumn(Canvas& c, const Frame& f, float h) {
    c.fillRect({0, kBarH, 156.f, h - kBarH}, Color::argb(0xFF0E0C0B));
    c.fillRect({155, kBarH, 1, h - kBarH}, line());
    c.drawText("BROWSER", 14.f, kBarH + 12.f, 13.f, dim(), true);
    const char* cats[] = {"Sounds", "Drums", "Instruments", "Audio Effects", "MIDI Effects", "Samples", "Plugins"};
    for (int i = 0; i < 7; ++i) {
        Rect r{8.f, kBarH + 40.f + i * 32.f, 140.f, 28.f};
        const bool on = i == 0;
        if (on)
            c.fillRoundRect(r, 4.f, Color::argb(0xFF2A2118));
        c.fillEllipse(20.f, r.y + 14.f, 4.f, 4.f, on ? amber() : Color::argb(0xFF3C3732));
        c.drawText(cats[i], 32.f, r.y + 6.f, 14.f, on ? ink() : dim(), on);
    }
}

void arrangement(Canvas& c, const Frame& f) {
    const float w = static_cast<float>(c.width());
    const float h = static_cast<float>(c.height());
    const float headerX = 156.f;
    const float headerW = 188.f;
    const float rightW = 92.f;
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
            c.drawText(m.name, x + 8.f, rulerY + 6.f, 13.f, Color::argb(m.color), true);
        }
    }
    for (int bar = 1; bar <= 40; bar += 2) {
        const float x = xAt((bar - 1) * 4.0);
        if (x > laneR)
            break;
        c.drawText(std::to_string(bar), x + 2.f, rulerY + 20.f, 12.f, dim(), false);
    }
    const double head = f.engine ? f.engine->playhead() : (f.project ? f.project->playhead : 0);
    const float hx = xAt(head);
    const std::string flag = positionText(head);

    for (size_t i = 0; i < rows.size(); ++i) {
        const Track* t = rows[i];
        const float y = rowsTop + static_cast<float>(i) * rowH;
        c.fillRect({headerX, y, headerW, rowH - 1.f}, i % 2 ? Color::argb(0xFF120F0D) : Color::argb(0xFF161311));
        c.fillRect({headerX, y, 5.f, rowH - 1.f}, Color::argb(t->color));
        c.drawText(t->name, headerX + 12.f, y + 6.f, 14.f, ink(), true);
        miniKnob(c, headerX + 28.f, y + rowH * 0.62f, 11.f, dbNorm(t->volumeDb));
        pill(c, {headerX + 48.f, y + rowH * 0.48f, 22.f, 16.f}, "M", t->mute);
        pill(c, {headerX + 74.f, y + rowH * 0.48f, 22.f, 16.f}, "S", t->solo);
        pill(c, {headerX + 100.f, y + rowH * 0.48f, 36.f, 16.f}, "Arm", t->arm);

        c.fillRect({laneX, y, laneR - laneX, rowH - 1.f}, Color::argb(0xFF0C0B0A));
        c.clip({laneX, y, laneR - laneX, rowH});
        for (const auto& clip : t->clips) {
            const float x0 = xAt(clip.startQ);
            const float x1 = xAt(clip.startQ + clip.lengthQ);
            if (x1 < laneX || x0 > laneR)
                continue;
            Rect body{x0, y + 4.f, std::max(8.f, x1 - x0), rowH - 10.f};
            c.fillRoundRect(body, 4.f, Color::argb(0xFF1A1410));
            c.fillRect({body.x, body.y, 3.f, body.h}, Color::argb(clip.color ? clip.color : t->color));
            c.fillRect({body.x, body.y, body.w, 16.f}, Color::argb(0xCC2A2118));
            c.drawText(clip.name.empty() ? t->name : clip.name, body.x + 8.f, body.y + 1.f, 12.f, ink(), true);
            Rect content{body.x + 2.f, body.y + 18.f, body.w - 4.f, body.h - 22.f};
            if (!clip.notes.empty())
                drawNotes(c, clip, content);
            else
                drawWave(c, mediaOf(f.project, clip.mediaId), content, waveTint(clip.color ? clip.color : t->color));
        }
        if (f.project) {
            for (const auto& lane : f.project->automation) {
                const std::string prefix = "track:" + t->id + ":";
                if (lane.id.rfind(prefix, 0) != 0 || lane.points.size() < 2)
                    continue;
                float vmin = lane.points.front().value, vmax = vmin;
                for (const auto& pt : lane.points) {
                    vmin = std::min(vmin, pt.value);
                    vmax = std::max(vmax, pt.value);
                }
                const float spanV = std::max(0.001f, vmax - vmin);
                const std::string label = lane.id.substr(prefix.size());
                c.drawText(label == "cutoff" ? "Filter Cutoff" : label, laneX + 8.f, y + rowH - 18.f, 12.f, rod(), false);
                float px = xAt(lane.points.front().quarter);
                float py = y + 8.f + (1.f - (lane.points.front().value - vmin) / spanV) * (rowH - 20.f);
                for (size_t p = 1; p < lane.points.size(); ++p) {
                    const float nx = xAt(lane.points[p].quarter);
                    const float ny = y + 8.f + (1.f - (lane.points[p].value - vmin) / spanV) * (rowH - 20.f);
                    c.line(px, py, nx, ny, amber(), 1.8f);
                    px = nx;
                    py = ny;
                }
            }
        }
        c.resetClip();

        c.fillRect({laneR, y, rightW, rowH - 1.f}, Color::argb(0xFF120F0D));
        miniKnob(c, laneR + 16.f, y + rowH * 0.38f, 9.f, (t->pan + 1.f) * 0.5f);
        pill(c, {laneR + 30.f, y + 6.f, 18.f, 15.f}, "M", t->mute);
        pill(c, {laneR + 50.f, y + 6.f, 18.f, 15.f}, "S", t->solo);
        pill(c, {laneR + 30.f, y + 24.f, 36.f, 15.f}, "Arm", t->arm);
        drawStereo(c, f, t->id, {laneR + 70.f, y + 10.f, 16.f, rowH - 22.f}, 0.35f);
        c.line(hx, y, hx, y + rowH, rod(), 1.4f);
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
                if (shown >= 4)
                    break;
                const float cxk = r.x + 36.f + static_cast<float>(shown) * 58.f;
                if (cxk > r.x + r.w - 50.f)
                    break;
                const float norm = knobNorm(pr.second);
                miniKnob(c, cxk, r.y + 78.f, 16.f, norm);
                c.drawText(pr.first, cxk - 22.f, r.y + 100.f, 12.f, dim(), false);
                ++shown;
            }
        }
    }
}

void session(Canvas& c, const Frame& f) {
    const float w = static_cast<float>(c.width());
    const float h = static_cast<float>(c.height());
    if (!f.project)
        return;
    const float sceneW = 120.f;
    const float top = kBarH + 16.f;
    const float sceneX = w - sceneW - 10.f;
    std::vector<const Track*> rows;
    for (const auto& t : f.project->tracks)
        if (t.type == TrackType::Audio || t.type == TrackType::Midi)
            rows.push_back(&t);
    const float rowPitch = (h - top - 12.f) / std::max<size_t>(1, rows.size());
    const float rowH = rowPitch - 6.f;
    const float colW = (sceneX - 240.f) / 8.f;
    for (size_t i = 0; i < rows.size(); ++i) {
        const Track* t = rows[i];
        const float y = top + static_cast<float>(i) * rowPitch;
        c.fillRect({164.f, y, 4.f, rowH}, Color::argb(t->color));
        c.drawText(t->name, 176.f, y + 8.f, 14.f, ink(), true);
        for (int s = 0; s < 8; ++s) {
            Rect cell{230.f + s * colW, y, colW - 6.f, rowH};
            c.fillRoundRect(cell, 4.f, Color::argb(0xFF161311));
            if (s < static_cast<int>(t->slots.size()) && t->slots[static_cast<size_t>(s)]) {
                const Clip& clip = *t->slots[static_cast<size_t>(s)];
                c.fillRect({cell.x, cell.y, 3.f, cell.h}, Color::argb(clip.color ? clip.color : t->color));
                c.drawText(clip.name, cell.x + 8.f, cell.y + 4.f, 12.f, ink(), true);
                drawWave(c, mediaOf(f.project, clip.mediaId), {cell.x + 4.f, cell.y + 20.f, cell.w - 8.f, cell.h - 26.f},
                         waveTint(t->color));
                if (!clip.notes.empty())
                    drawNotes(c, clip, {cell.x + 4.f, cell.y + 20.f, cell.w - 8.f, cell.h - 26.f});
            }
        }
    }
    for (int s = 0; s < static_cast<int>(f.project->scenes.size()) && s < 8; ++s) {
        Rect r{sceneX, top + static_cast<float>(s) * ((h - top - 12.f) / 8.f), sceneW, (h - top - 12.f) / 8.f - 6.f};
        c.fillRoundRect(r, 6.f, Color::argb(0xFF1C1814));
        c.strokeRect(r, s == 3 ? gold() : line(), 1.4f);
        c.drawText(f.project->scenes[static_cast<size_t>(s)], r.x + 12.f, r.y + r.h * 0.35f, 15.f, ink(), true);
    }
}

void perform(Canvas& c, const Frame& f) {
    const float w = static_cast<float>(c.width());
    const float h = static_cast<float>(c.height());
    c.drawText("PERFORMANCE", 170.f, kBarH + 12.f, 16.f, dim(), true);
    const char* names[] = {"Intro", "Verse", "Build", "Drop", "Break", "Drop 2", "Outro", "End",
                           "Kick", "Drums", "Snare", "Bass", "Lead", "Pads", "Vox", "FX",
                           "Filter", "Wash", "Chop", "Stutter", "Rise", "Impact", "Tape", "Space"};
    const int cols = 8;
    const float padW = (w - 180.f) / cols;
    const float padH = (h - kBarH - 56.f) / 3.f;
    for (int i = 0; i < 24; ++i) {
        const int col = i % cols;
        const int row = i / cols;
        Rect r{168.f + col * padW, kBarH + 44.f + row * padH, padW - 8.f, padH - 8.f};
        const bool on = i == 3;
        c.fillRoundRect(r, 8.f, on ? Color::argb(0xFF3A2A16) : Color::argb(0xFF161311));
        c.strokeRect(r, on ? gold() : line(), on ? 2.f : 1.f);
        c.drawText(names[i], r.x + 12.f, r.y + r.h * 0.4f, 16.f, on ? rod() : ink(), true);
    }
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
        c.fillRoundRect(r, 6.f, Color::argb(0xFF120F0D));
        c.strokeRect(r, t->id == "lead" ? gold() : line(), t->id == "lead" ? 2.f : 1.f);
        c.fillRect({r.x, r.y, r.w, 4.f}, Color::argb(t->color));
        c.drawText(t->name, r.x + 8.f, r.y + 10.f, 14.f, ink(), true);
        c.drawText("In: All Ins", r.x + 8.f, r.y + 30.f, 12.f, dim(), false);
        c.drawText(std::string("Out: ") + (t->output.empty() ? "Master" : t->output), r.x + 8.f, r.y + 46.f, 12.f, dim(), false);
        for (int s = 0; s < 3; ++s) {
            Rect slot{r.x + 8.f, r.y + 68.f + s * 22.f, r.w - 16.f, 20.f};
            c.fillRoundRect(slot, 3.f, Color::argb(0xFF1C1814));
            const char* label = "—";
            std::string owned;
            if (s < static_cast<int>(t->devices.size())) {
                owned = t->devices[static_cast<size_t>(s)].name;
                label = owned.c_str();
            }
            c.drawText(label, slot.x + 6.f, slot.y + 2.f, 12.f, dim(), false);
        }
        miniKnob(c, r.x + r.w * 0.5f, r.y + 168.f, 16.f, (t->pan + 1.f) * 0.5f);
        char pan[16];
        std::snprintf(pan, sizeof(pan), "%+.1f", t->pan);
        c.drawText(pan, r.x + r.w * 0.5f - 16.f, r.y + 188.f, 12.f, dim(), false);
        float a = -100.f, b = -100.f;
        for (const auto& s : t->sends) {
            if (s.target == "retA")
                a = s.levelDb;
            if (s.target == "retB")
                b = s.levelDb;
        }
        miniKnob(c, r.x + 28.f, r.y + 226.f, 11.f, dbNorm(a));
        miniKnob(c, r.x + 64.f, r.y + 226.f, 11.f, dbNorm(b));
        c.drawText("A", r.x + 22.f, r.y + 240.f, 12.f, dim(), false);
        c.drawText("B", r.x + 58.f, r.y + 240.f, 12.f, dim(), false);

        const float faderTop = r.y + 268.f;
        const float faderBot = r.y + r.h - 46.f;
        const float faderX = r.x + 36.f;
        c.fillRect({faderX, faderTop, 6.f, faderBot - faderTop}, Color::argb(0xFF2A241E));
        const float fn = dbNorm(t->volumeDb);
        const float fy = faderBot - fn * (faderBot - faderTop);
        c.fillRoundRect({faderX - 7.f, fy - 8.f, 20.f, 16.f}, 3.f, amber());
        const char* marks[] = {"+6", "0", "-6", "-12", "-24", "-48"};
        const float markDb[] = {6, 0, -6, -12, -24, -48};
        for (int m = 0; m < 6; ++m) {
            const float my = faderBot - dbNorm(markDb[m]) * (faderBot - faderTop);
            c.drawText(marks[m], r.x + 6.f, my - 6.f, 11.f, dim(), false);
        }
        drawStereo(c, f, t->id, {r.x + r.w - 36.f, faderTop, 22.f, faderBot - faderTop}, 0.4f);
        pill(c, {r.x + 8.f, r.y + r.h - 28.f, 22.f, 18.f}, "M", t->mute);
        pill(c, {r.x + 34.f, r.y + r.h - 28.f, 22.f, 18.f}, "S", t->solo);
        pill(c, {r.x + 60.f, r.y + r.h - 28.f, 36.f, 18.f}, "Arm", t->arm);
    }
}

void deviceChain(Canvas& c, const Frame& f) {
    const float w = static_cast<float>(c.width());
    const float h = static_cast<float>(c.height());
    const Track* chain = f.project ? f.project->findTrack("lead") : nullptr;
    c.drawText("SYNTH LEAD", 24.f, kBarH + 16.f, 20.f, ink(), true);
    if (!chain)
        return;
    const float pw = std::min(420.f, (w - 40.f) / std::max<size_t>(1, chain->devices.size()) - 16.f);
    for (size_t i = 0; i < chain->devices.size(); ++i) {
        const auto& d = chain->devices[i];
        Rect r{20.f + static_cast<float>(i) * (pw + 16.f), kBarH + 60.f, pw, h - kBarH - 90.f};
        panelBox(c, r, 10.f);
        c.fillRect({r.x, r.y, 5.f, r.h}, gold());
        c.drawText(d.name, r.x + 18.f, r.y + 16.f, 22.f, ink(), true);
        drawStereo(c, f, chain->id, {r.x + r.w - 40.f, r.y + 70.f, 24.f, r.h - 110.f}, 0.35f);
        int shown = 0;
        for (const auto& pr : d.params) {
            if (shown >= 6)
                break;
            const float cxk = r.x + 48.f + static_cast<float>(shown % 3) * 90.f;
            const float cyk = r.y + 120.f + static_cast<float>(shown / 3) * 110.f;
            knob(c, cxk, cyk, knobNorm(pr.second), pr.first, "");
            ++shown;
        }
    }
}

void piano(Canvas& c, const Frame& f) {
    const float w = static_cast<float>(c.width());
    const float h = static_cast<float>(c.height());
    const float keyW = 54.f;
    const float top = kBarH + 28.f;
    c.drawText("PIANO ROLL", 16.f, kBarH + 6.f, 14.f, dim(), true);
    const Clip* clip = nullptr;
    if (f.project) {
        for (const auto& t : f.project->tracks) {
            if (t.id == "lead" && !t.clips.empty())
                clip = &t.clips.front();
        }
    }
    for (int p = 0; p < 24; ++p) {
        const int pitch = 72 - p;
        const float y = top + p * ((h - top - 12.f) / 24.f);
        const bool black = pitch % 12 == 1 || pitch % 12 == 3 || pitch % 12 == 6 || pitch % 12 == 8 || pitch % 12 == 10;
        c.fillRect({0, y, keyW, (h - top) / 24.f - 1.f}, black ? Color::argb(0xFF1A1614) : Color::argb(0xFFD1BCAC));
        c.fillRect({keyW, y, w - keyW, (h - top) / 24.f - 1.f}, black ? Color::argb(0xFF100E0C) : Color::argb(0xFF161311));
        if (pitch % 12 == 0) {
            char name[8];
            std::snprintf(name, sizeof(name), "C%d", pitch / 12 - 1);
            c.drawText(name, 8.f, y + 2.f, 12.f, Color::argb(0xFF1A120C), true);
        }
        if (clip) {
            for (const auto& n : clip->notes) {
                if (n.pitch != pitch)
                    continue;
                const float x = keyW + 8.f + static_cast<float>(n.startQ) * 28.f;
                const float nw = std::max(8.f, static_cast<float>(n.lengthQ) * 28.f);
                c.fillRoundRect({x, y + 3.f, nw, (h - top) / 24.f - 7.f}, 3.f, gold());
            }
        }
    }
}

void browser(Canvas& c, const Frame& f) {
    const float w = static_cast<float>(c.width());
    const float h = static_cast<float>(c.height());
    browserColumn(c, f, h);
    c.drawText("Sounds", 180.f, kBarH + 16.f, 20.f, ink(), true);
    const char* items[] = {"Warm Pad", "Analog Bass", "Dusty Kick", "Room Snare", "Tape Lead", "Vinyl Hat"};
    for (int i = 0; i < 6; ++i) {
        Rect r{180.f, kBarH + 56.f + i * 48.f, w - 200.f, 40.f};
        c.fillRoundRect(r, 6.f, Color::argb(0xFF161311));
        c.fillRect({r.x, r.y, 4.f, r.h}, i == 0 ? amber() : gold());
        c.drawText(items[i], r.x + 16.f, r.y + 10.f, 16.f, ink(), false);
    }
}

void pluginWindow(Canvas& c, const Frame& f) {
    const float w = static_cast<float>(c.width());
    const float h = static_cast<float>(c.height());
    panelBox(c, {80.f, 90.f, w - 160.f, h - 160.f}, 10.f);
    c.drawText(f.pluginLoaded ? "GHOVLZ Test Gain" : "Plugin", 108.f, 112.f, 26.f, ink(), true);
    c.drawText(f.pluginLoaded ? "Generic editor" : "No plugin loaded", 108.f, 148.f, 14.f, dim(), false);
    c.fillRect({108.f, 176.f, w - 220.f, 1.f}, gold());
    const float norm = f.pluginGain;
    knob(c, w * 0.5f, h * 0.48f, norm, "Gain", "");
    char v[16];
    std::snprintf(v, sizeof(v), "%.2f", norm);
    c.drawText(v, w * 0.5f - 18.f, h * 0.48f + 78.f, 18.f, rod(), true);
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
