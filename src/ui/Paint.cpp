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
namespace {

constexpr float kPi = 3.14159265f;

Color ink() { return Color::argb(0xFFF5F5F5); }
Color dim() { return Color::argb(0xFFA39890); }
Color gold() { return Color::argb(0xFFB49637); }
Color rod() { return Color::argb(0xFFDAA520); }
Color amber() { return Color::argb(0xFFFF821E); }
Color orange() { return Color::argb(0xFFFF6E00); }
Color panel() { return Color::argb(0xFF161311); }
Color panel2() { return Color::argb(0xFF221E1A); }
Color line() { return Color::argb(0xFF3C3732); }
Color bg() { return Color::argb(0xFF0A0A0A); }

std::string positionText(double q) {
    if (q < 0)
        q = 0;
    const int bar = static_cast<int>(q / 4.0) + 1;
    const int beat = static_cast<int>(q) % 4 + 1;
    const int six = static_cast<int>((q - std::floor(q)) * 4.0) + 1;
    return std::to_string(bar) + "." + std::to_string(beat) + "." + std::to_string(six);
}

void panelBox(Canvas& c, Rect r, float radius = 8.f) {
    c.dropShadow(r, radius, 10.f, Color{0, 0, 0, 170});
    c.fillRoundRect(r, radius, panel());
    c.strokeRect(r, line(), 1.f);
}

void meterBar(Canvas& c, Rect r, float peak) {
    c.fillRect(r, Color::argb(0xFF100E0C));
    const float db = peak <= 1.0e-5f ? -60.f : 20.f * std::log10(peak);
    const float t = std::clamp((db + 60.f) / 60.f, 0.f, 1.f);
    const float h = r.h * t;
    const Color col = peak >= 0.98f ? Color::argb(0xFFE23D2C) : rod();
    c.fillRect({r.x + 1, r.y + r.h - h, r.w - 2, h}, col);
}

const Image& knobFilm() {
    static Image image = loadImageRgba(std::string(GHOVLZ_ASSETS_DIR) + "/DarkKnob4_Stitched.png");
    return image;
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
        c.strokeRect({cx - size * 0.5f, cy - size * 0.5f, size, size}, line(), 1.f);
    }
    const float a0 = -kPi * 0.5f - 145.f * kPi / 180.f;
    const float a1 = -kPi * 0.5f + 145.f * kPi / 180.f;
    c.arc(cx, cy, size * 0.5f + 4.f, a0, a0 + (a1 - a0) * norm, orange(), 2.4f);
    const float ang = a0 + (a1 - a0) * norm;
    c.line(cx + std::cos(ang) * 6.f, cy + std::sin(ang) * 6.f, cx + std::cos(ang) * 18.f, cy + std::sin(ang) * 18.f,
           Color::argb(0xFFD1BCAC), 2.2f);
    c.drawText(label, cx - 28.f, cy + 30.f, 11.f, dim(), false);
    c.drawText(value, cx - 22.f, cy + 43.f, 11.f, ink(), false);
}

void topBar(Canvas& c, const Frame& f) {
    const float w = static_cast<float>(c.width());
    c.fillRect({0, 0, w, 52}, Color::argb(0xFF0E0C0A));
    c.fillRect({0, 51, w, 1}, gold());
    c.drawText("GHOVLZ! DAW", 16, 14, 22, ink(), true);
    const char* names[] = {"ARR", "SES", "PERF", "MIX"};
    const View views[] = {View::Arrangement, View::Session, View::Perform, View::Mixer};
    for (int i = 0; i < 4; ++i) {
        Rect r{250.f + i * 74.f, 10, 66, 32};
        const bool on = f.view == views[i];
        c.fillRoundRect(r, 4, on ? Color::argb(0xFF3C3223) : panel2());
        c.strokeRect(r, on ? gold() : line(), 1);
        c.drawText(names[i], r.x + 16, r.y + 8, 13, on ? rod() : dim(), true);
    }
    c.fillRoundRect({560, 10, 36, 32}, 4, f.playing ? orange() : panel2());
    c.drawText(f.playing ? "||" : ">", 572, 16, 16, ink(), true);
    c.fillRoundRect({602, 10, 36, 32}, 4, panel2());
    c.drawText("[]", 612, 16, 14, dim(), true);
    c.fillRoundRect({644, 10, 36, 32}, 4, f.recording ? Color::argb(0xFF8A2A22) : panel2());
    c.fillEllipse(662, 26, 7, 7, f.recording ? Color::argb(0xFFE23D2C) : Color::argb(0xFF5A4038));
    char bpm[32];
    std::snprintf(bpm, sizeof(bpm), "%.2f", f.project ? f.project->bpm : 120.0);
    c.drawText(bpm, 700, 10, 18, rod(), true);
    c.drawText("BPM", 778, 16, 11, dim(), false);
    c.drawText("4/4", 820, 14, 16, ink(), false);
    c.drawText(positionText(f.playhead), 880, 12, 18, ink(), true);
    char cpu[32];
    std::snprintf(cpu, sizeof(cpu), "CPU %d%%", std::clamp(static_cast<int>(f.cpu * 100.f), 0, 999));
    c.drawText(cpu, static_cast<float>(c.width()) - 280, 16, 13, dim(), false);
    Rect badge{static_cast<float>(c.width()) - 150, 12, 130, 28};
    c.fillRoundRect(badge, 4, Color::argb(0xFF3C3223));
    c.strokeRect(badge, gold(), 1);
    c.drawText(f.backend, badge.x + 10, badge.y + 6, 13, rod(), true);
}

void browserColumn(Canvas& c, Rect r, int selected) {
    panelBox(c, r, 0);
    c.drawText("BROWSER", r.x + 14, r.y + 12, 12, gold(), true);
    const char* cats[] = {"Sounds", "Drums", "Instruments", "Audio Effects", "MIDI Effects", "Samples", "Plugins"};
    for (int i = 0; i < 7; ++i) {
        Rect row{r.x + 8, r.y + 40 + i * 28.f, r.w - 16, 24};
        if (i == selected)
            c.fillRoundRect(row, 3, Color::argb(0xFF3C3223));
        c.drawText(cats[i], row.x + 8, row.y + 4, 13, i == selected ? ink() : dim(), false);
    }
}

void drawClip(Canvas& c, Rect r, const Clip& clip) {
    c.dropShadow(r, 4, 6, Color{0, 0, 0, 120});
    c.fillRoundRect(r, 4, Color::argb(clip.color ? clip.color : 0xFFB49637));
    c.drawText(clip.name.empty() ? "Clip" : clip.name, r.x + 6, r.y + 6, 12, Color::argb(0xFF1A140E), true);
}

void arrangement(Canvas& c, const Frame& f) {
    const float w = static_cast<float>(c.width());
    const float h = static_cast<float>(c.height());
    browserColumn(c, {0, 52, 210, h - 52 - 210}, 0);
    Rect ruler{210, 52, w - 210, 36};
    c.fillRect(ruler, Color::argb(0xFF120F0D));
    const double loop0 = f.project ? f.project->loopStart : 0;
    const double loop1 = f.project ? f.project->loopEnd : 128;
    const double span = std::max(1.0, loop1 - loop0);
    auto xAt = [&](double q) { return ruler.x + static_cast<float>((q - loop0) / span) * ruler.w; };
    if (f.project && f.project->loop) {
        c.fillRect({xAt(loop0), ruler.y, std::max(2.f, xAt(loop1) - xAt(loop0)), 4}, gold());
    }
    for (int bar = 1; bar <= 32; bar += 2) {
        float x = xAt((bar - 1) * 4.0);
        c.line(x, ruler.y + 8, x, ruler.y + ruler.h, line(), 1);
        c.drawText(std::to_string(bar), x + 4, ruler.y + 14, 11, dim(), false);
    }
    if (f.project) {
        for (const auto& m : f.project->markers) {
            float x = xAt(m.quarter);
            c.fillRect({x, ruler.y + 4, 2, ruler.h - 4}, Color::argb(m.color));
            c.drawText(m.name, x + 4, ruler.y + 4, 11, Color::argb(m.color), true);
        }
    }
    float y = 88;
    const float chainTop = h - 206;
    const float rowH = f.project && !f.project->tracks.empty()
                           ? std::min(72.f, (chainTop - 90.f) / static_cast<float>(f.project->tracks.size()))
                           : 48.f;
    if (f.project) {
        for (const auto& t : f.project->tracks) {
            if (y + rowH > h - 210)
                break;
            c.fillRect({210, y, 160, rowH - 2}, panel2());
            c.fillRect({210, y, 4, rowH - 2}, Color::argb(t.color));
            c.drawText(t.name, 222, y + 8, 13, ink(), true);
            char vol[16];
            std::snprintf(vol, sizeof(vol), "%+.0f", t.volumeDb);
            c.drawText(vol, 222, y + 26, 11, dim(), false);
            c.fillRect({370, y, w - 370, rowH - 2}, Color::argb(0xFF100E0C));
            for (const auto& clip : t.clips) {
                float x0 = xAt(clip.startQ);
                float x1 = xAt(clip.startQ + clip.lengthQ);
                drawClip(c, {x0 + 2, y + 6, std::max(8.f, x1 - x0 - 4), rowH - 16}, clip);
            }
            y += rowH;
        }
    }
    const float px = xAt(f.playhead);
    c.line(px, 52, px, h - 210, amber(), 1.5f);
    Rect chain{210, h - 206, w - 220, 196};
    panelBox(c, chain);
    c.drawText("SYNTH LEAD", chain.x + 16, chain.y + 10, 13, gold(), true);
    c.drawText("Wavetable   Filter   Saturator", chain.x + 150, chain.y + 12, 12, dim(), false);
    knob(c, chain.x + 70, chain.y + 90, 0.62f, "Position", "62");
    knob(c, chain.x + 160, chain.y + 90, 0.40f, "Wave", "40");
    knob(c, chain.x + 250, chain.y + 90, 0.25f, "Unison", "2");
    knob(c, chain.x + 340, chain.y + 90, 0.70f, "Level", "-6");
    knob(c, chain.x + 470, chain.y + 90, 0.55f, "Cutoff", "2.4k");
    knob(c, chain.x + 560, chain.y + 90, 0.22f, "Reso", "18");
    knob(c, chain.x + 650, chain.y + 90, 0.15f, "Drive", "3");
    knob(c, chain.x + 740, chain.y + 90, 1.f, "Mix", "100");
    knob(c, chain.x + 870, chain.y + 90, 0.48f, "Drive", "8");
    knob(c, chain.x + 960, chain.y + 90, 0.35f, "Tone", "35");
    knob(c, chain.x + 1050, chain.y + 90, 0.80f, "Mix", "80");
    knob(c, chain.x + 1140, chain.y + 90, 0.66f, "Output", "-1");
}

void session(Canvas& c, const Frame& f) {
    const float w = static_cast<float>(c.width());
    const float h = static_cast<float>(c.height());
    browserColumn(c, {0, 52, 200, h - 52}, 0);
    if (!f.project)
        return;
    const int n = static_cast<int>(std::min<size_t>(f.project->tracks.size(), 8));
    const float sceneW = 104.f;
    const float colW = (w - 220 - sceneW - 16) / std::max(1, n);
    for (int i = 0; i < n; ++i) {
        const Track& t = f.project->tracks[static_cast<size_t>(i)];
        float x = 210 + i * colW;
        c.drawText(t.name, x + 8, 60, 12, ink(), true);
        for (int s = 0; s < 8 && s < static_cast<int>(t.slots.size()); ++s) {
            Rect r{x + 6, 86 + s * 72.f, colW - 14, 64};
            if (t.slots[static_cast<size_t>(s)])
                drawClip(c, r, *t.slots[static_cast<size_t>(s)]);
            else
                c.strokeRect(r, line(), 1);
        }
    }
    for (int s = 0; s < 8 && f.project && s < static_cast<int>(f.project->scenes.size()); ++s) {
        Rect r{w - sceneW - 12, 86 + s * 72.f, sceneW, 64};
        c.fillRoundRect(r, 4, Color::argb(0xFF3C3223));
        c.strokeRect(r, gold(), 1);
        const std::string& scene = f.project->scenes[static_cast<size_t>(s)];
        c.drawText(scene, r.x + 10, r.y + 24, 13, rod(), true);
    }
}

void perform(Canvas& c, const Frame&) {
    const char* names[6][8] = {
        {"Kick Loop", "Bassline", "Lead Synth", "Vocal Chop", "Pad Chords", "FX Rise", "Sub Hit", "Perc Fill"},
        {"Main Kick", "808 Bass", "Saw Lead", "Vox Ahh", "Atmosphere", "Impact", "Sub Bass", "Clap"},
        {"Hat Tight", "Hat Open", "Rim", "Tom Low", "Shaker", "Ride", "Crash", "Cowbell"},
        {"Chord A", "Chord B", "Chord C", "Stab", "Pluck", "Bell", "Noise", "Sweep"},
        {"Scene 1", "Scene 2", "Scene 3", "Scene 4", "Fill", "Drop", "Break", "End"},
        {"Macro 1", "Macro 2", "Macro 3", "Macro 4", "Rev Send", "Dly Send", "Filter", "Tape"},
    };
    const uint32_t colors[6] = {0xFF8A5A28, 0xFF1F6B62, 0xFF6A5438, 0xFFB49637, 0xFF5AA05A, 0xFFAA6A22};
    c.drawText("PERFORMANCE", 24, 64, 14, gold(), true);
    const float x0 = 24, y0 = 100, pw = 190, ph = 130, gap = 12;
    for (int row = 0; row < 6; ++row) {
        for (int col = 0; col < 8; ++col) {
            Rect r{x0 + col * (pw + gap), y0 + row * (ph + gap), pw, ph};
            if (r.y + r.h > c.height() - 8)
                continue;
            c.dropShadow(r, 8, 8, Color{0, 0, 0, 140});
            c.fillRoundRect(r, 8, Color::argb(colors[row]));
            c.drawText(names[row][col], r.x + 12, r.y + r.h * 0.4f, 16, Color::argb(0xFF140F0C), true);
        }
    }
}

void mixer(Canvas& c, const Frame& f) {
    c.drawText("MIXER", 24, 64, 14, gold(), true);
    if (!f.project)
        return;
    std::vector<const Track*> tracks;
    for (const auto& t : f.project->tracks)
        tracks.push_back(&t);
    tracks.push_back(&f.project->master);
    const float stripW = std::min(118.f, (c.width() - 40.f) / std::max<float>(1, static_cast<float>(tracks.size())));
    for (size_t i = 0; i < tracks.size(); ++i) {
        const Track& t = *tracks[i];
        Rect r{16 + i * stripW, 96, stripW - 8, static_cast<float>(c.height()) - 116};
        panelBox(c, r, 6);
        c.fillRect({r.x, r.y, r.w, 4}, Color::argb(t.color ? t.color : 0xFFB49637));
        c.drawText(t.name, r.x + 8, r.y + 14, 12, ink(), true);
        const float peak = f.engine ? f.engine->meter(t.id) : 0.f;
        meterBar(c, {r.x + r.w - 28, r.y + 44, 16, r.h - 120}, peak);
        const float travel = r.h - 150;
        const float gain = std::clamp((t.volumeDb + 48.f) / 54.f, 0.f, 1.f);
        const float fy = r.y + 50 + (1.f - gain) * travel;
        c.fillRect({r.x + 28, r.y + 50, 4, travel}, Color::argb(0xFF2A241E));
        c.fillRoundRect({r.x + 18, fy, 24, 36}, 3, Color::argb(0xFF3C3732));
        c.fillRect({r.x + 22, fy + 16, 16, 3}, Color::argb(0xFFD1BCAC));
        char vol[16];
        std::snprintf(vol, sizeof(vol), "%+.1f", t.volumeDb);
        c.drawText(vol, r.x + 8, r.y + r.h - 28, 12, rod(), false);
    }
}

void deviceChain(Canvas& c, const Frame& f) {
    c.drawText("DEVICE - SYNTH LEAD", 24, 64, 14, gold(), true);
    Rect panels[] = {{24, 100, 480, 520}, {520, 100, 480, 520}, {1016, 100, 480, static_cast<float>(c.height()) - 140}};
    const char* titles[] = {"WAVETABLE", "FILTER", "SATURATOR"};
    panelBox(c, panels[0]);
    panelBox(c, panels[1]);
    panelBox(c, {panels[2].x, panels[2].y, panels[2].w, 520}, 8);
    for (int i = 0; i < 3; ++i)
        c.drawText(titles[i], panels[i].x + 16, panels[i].y + 12, 13, gold(), true);
    knob(c, panels[0].x + 90, panels[0].y + 150, 0.62f, "Position", "62");
    knob(c, panels[0].x + 200, panels[0].y + 150, 0.40f, "Wave", "Saw");
    knob(c, panels[0].x + 310, panels[0].y + 150, 0.30f, "Unison", "4");
    knob(c, panels[0].x + 90, panels[0].y + 300, 0.10f, "Attack", "8 ms");
    knob(c, panels[0].x + 200, panels[0].y + 300, 0.35f, "Decay", "240");
    knob(c, panels[0].x + 310, panels[0].y + 300, 0.72f, "Sustain", "72");
    knob(c, panels[0].x + 200, panels[0].y + 440, 0.70f, "Level", "-6 dB");
    knob(c, panels[1].x + 90, panels[1].y + 180, 0.55f, "Cutoff", "2.4 kHz");
    knob(c, panels[1].x + 210, panels[1].y + 180, 0.22f, "Reso", "18%");
    knob(c, panels[1].x + 330, panels[1].y + 180, 0.18f, "Drive", "3 dB");
    knob(c, panels[1].x + 210, panels[1].y + 340, 1.f, "Mix", "100%");
    knob(c, panels[2].x + 90, panels[2].y + 180, 0.48f, "Drive", "8 dB");
    knob(c, panels[2].x + 210, panels[2].y + 180, 0.35f, "Tone", "35");
    knob(c, panels[2].x + 330, panels[2].y + 180, 0.80f, "Mix", "80%");
    knob(c, panels[2].x + 210, panels[2].y + 340, 0.62f, "Output", "-1 dB");
    if (f.project) {
        const Track* lead = nullptr;
        for (const auto& t : f.project->tracks)
            if (t.id == "lead")
                lead = &t;
        if (lead) {
            for (const auto& d : lead->devices)
                (void)d;
        }
    }
    auto eq = createDevice("eq");
    if (eq) {
        Rect plot{24, 640, static_cast<float>(c.width()) - 48, static_cast<float>(c.height()) - 660};
        if (plot.h > 40) {
            panelBox(c, plot, 6);
            c.drawText("EQ EIGHT", plot.x + 12, plot.y + 8, 12, gold(), true);
            std::vector<float> freqs, mags;
            for (int i = 0; i < 80; ++i) {
                float fhz = 20.f * std::pow(1000.f, i / 79.f);
                freqs.push_back(fhz);
            }
            mags = eqMagnitudeDb(*eq, 48000, freqs);
            float prevX = plot.x + 80, prevY = plot.y + plot.h * 0.5f;
            for (size_t i = 0; i < mags.size(); ++i) {
                float x = plot.x + 80 + (plot.w - 100) * (static_cast<float>(i) / std::max<size_t>(1, mags.size() - 1));
                float y = plot.y + plot.h * 0.55f - mags[i] * 4.f;
                if (i)
                    c.line(prevX, prevY, x, y, rod(), 1.6f);
                prevX = x;
                prevY = y;
            }
        }
    }
}

void piano(Canvas& c, const Frame& f) {
    c.drawText("PIANO ROLL - SYNTH LEAD", 24, 64, 14, gold(), true);
    const float x0 = 70, y0 = 100;
    const float keyH = 18;
    const int keys = 36;
    const int low = 48;
    for (int i = 0; i < keys; ++i) {
        const int pitch = low + keys - 1 - i;
        const int pc = pitch % 12;
        const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        Rect r{8, y0 + i * keyH, 56, keyH - 1};
        c.fillRect(r, black ? Color::argb(0xFF1A1612) : Color::argb(0xFFE6D8C4));
        if (pc == 0)
            c.drawText("C" + std::to_string(pitch / 12 - 1), 14, r.y + 2, 10, Color::argb(0xFF3C3223), false);
        c.fillRect({x0, r.y, static_cast<float>(c.width()) - x0 - 12, keyH - 1},
                   black ? Color::argb(0xFF120F0C) : Color::argb(0xFF181410));
    }
    const Clip* clip = nullptr;
    if (f.project) {
        for (const auto& t : f.project->tracks)
            if (t.id == "lead" && !t.clips.empty())
                clip = &t.clips.front();
    }
    if (!clip)
        return;
    const float pxPerQ = 28.f;
    for (const auto& n : clip->notes) {
        int row = (low + keys - 1) - n.pitch;
        if (row < 0 || row >= keys)
            continue;
        Rect r{x0 + static_cast<float>(n.startQ) * pxPerQ, y0 + row * keyH + 1, static_cast<float>(n.lengthQ) * pxPerQ - 2, keyH - 3};
        c.fillRoundRect(r, 2, rod());
    }
}

void browser(Canvas& c, const Frame&) {
    browserColumn(c, {0, 52, 260, static_cast<float>(c.height()) - 52}, 3);
    c.drawText("AUDIO EFFECTS", 290, 70, 16, gold(), true);
    const char* items[] = {"EQ Eight", "DYN", "Glue Compressor", "Limiter", "Gate", "Saturator", "Filter",
                           "Delay", "Reverb", "Chorus", "Flanger", "Phaser", "Utility", "Analyzer"};
    for (int i = 0; i < 14; ++i) {
        Rect r{290.f + (i % 2) * 420.f, 110.f + (i / 2) * 64.f, 400, 52};
        panelBox(c, r, 6);
        c.drawText(items[i], r.x + 16, r.y + 16, 15, ink(), false);
    }
}

void plugin(Canvas& c, const Frame& f) {
    c.fillRect({0, 52, static_cast<float>(c.width()), static_cast<float>(c.height()) - 52}, Color::argb(0xFF070605));
    Rect win{360, 140, 960, 640};
    c.dropShadow(win, 10, 18, Color{0, 0, 0, 200});
    c.fillRoundRect(win, 8, Color::argb(0xFF14110E));
    c.fillRect({win.x, win.y, win.w, 42}, Color::argb(0xFF1C1814));
    c.drawText(f.pluginLoaded ? "GHOVLZ Test Gain" : "Plugin", win.x + 16, win.y + 12, 16, ink(), true);
    c.drawText("VST3", win.x + win.w - 70, win.y + 14, 13, gold(), true);
    c.drawText("Generic editor - no vendor view on this plugin", win.x + 24, win.y + 70, 13, dim(), false);
    knob(c, win.x + win.w * 0.5f, win.y + 280, f.pluginGain, "Gain", f.pluginLoaded ? "0.50" : "--");
    c.drawText(f.pluginLoaded ? "Hosted in-process from the test bundle" : "Bundle not loaded", win.x + 24, win.y + 520, 13, dim(), false);
    c.drawText(std::string("Audio  ") + f.audioBackend + "    MIDI  " + (f.midiPort.empty() ? "none" : f.midiPort),
               win.x + 24, win.y + 560, 13, dim(), false);
}

} // namespace

void paintFrame(Canvas& canvas, const Frame& frame) {
    canvas.begin();
    canvas.clear(bg());
    switch (frame.view) {
        case View::Arrangement: arrangement(canvas, frame); break;
        case View::Session: session(canvas, frame); break;
        case View::Perform: perform(canvas, frame); break;
        case View::Mixer: mixer(canvas, frame); break;
        case View::Device: deviceChain(canvas, frame); break;
        case View::Piano: piano(canvas, frame); break;
        case View::Browser: browser(canvas, frame); break;
        case View::Plugin: plugin(canvas, frame); break;
    }
    topBar(canvas, frame);
    Rect wash{0, 52, 210, 80};
    if (frame.view == View::Arrangement || frame.view == View::Session || frame.view == View::Browser)
        canvas.blurRect(frame.view == View::Browser ? "browser-wash" : "side-wash", wash, 2.f);
    canvas.end();
}

Hit hitTest(float x, float y, int, int) {
    if (y < 10 || y > 42)
        return Hit::None;
    for (int i = 0; i < 4; ++i)
        if (Rect{250.f + i * 74.f, 10, 66, 32}.contains(x, y))
            return static_cast<Hit>(static_cast<int>(Hit::Arr) + i);
    if (Rect{560, 10, 36, 32}.contains(x, y))
        return Hit::Play;
    if (Rect{602, 10, 36, 32}.contains(x, y))
        return Hit::Stop;
    if (Rect{644, 10, 36, 32}.contains(x, y))
        return Hit::Rec;
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
