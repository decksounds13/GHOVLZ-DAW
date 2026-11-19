#include "dsp/Devices.h"
#include "dsp/Biquad.h"
#include "dsp/Fft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <deque>
#include <mutex>

namespace ghovlz {
namespace {

constexpr float kPi = 3.14159265f;

float coeff(float ms, double sr) {
    const float sec = std::max(0.0001f, ms * 0.001f);
    return 1.f - std::exp(-1.f / (sec * static_cast<float>(sr)));
}

Param P(const char* id, const char* name, float mn, float mx, float v, float skew = 1.f) {
    Param p;
    p.id = id;
    p.name = name;
    p.minV = mn;
    p.maxV = mx;
    p.value = v;
    p.skew = skew;
    return p;
}
Param Choice(const char* id, const char* name, std::vector<std::string> choices, float index) {
    Param p;
    p.id = id;
    p.name = name;
    p.choices = std::move(choices);
    p.minV = 0.f;
    p.maxV = static_cast<float>(p.choices.size() - 1);
    p.value = index;
    return p;
}

class Utility : public Device {
public:
    Utility() {
        type = "utility";
        name = "Utility";
        params = {P("gain", "Gain", -24, 24, 0), P("width", "Width", 0, 2, 1), P("mono", "Mono", 0, 1, 0)};
    }
    void prepare(double, int) override {}
    void process(AudioBuffer& a, MidiBuffer&) override {
        if (bypass.load() || a.channels < 2)
            return;
        const float g = dbToGain(get("gain"));
        const float w = get("width");
        const bool mono = get("mono") >= 0.5f;
        float* l = a.ch(0);
        float* r = a.ch(1);
        for (int i = 0; i < a.frames; ++i) {
            float mid = 0.5f * (l[i] + r[i]);
            float side = 0.5f * (l[i] - r[i]) * w;
            if (mono)
                side = 0.f;
            l[i] = (mid + side) * g;
            r[i] = (mid - side) * g;
        }
    }
};

class FilterDevice : public Device {
public:
    FilterDevice() {
        type = "filter";
        name = "Filter";
        params = {Choice("type", "Type", {"LP", "HP", "BP"}, 0),
                  P("cutoff", "Cutoff", 20, 20000, 800, 0.3f),
                  P("reso", "Reso", 0.2f, 8.f, 0.7f),
                  P("drive", "Drive", 0, 24, 0),
                  P("mix", "Mix", 0, 1, 1)};
    }
    void prepare(double sr, int) override { sampleRate = sr; z.clear(); }
    void process(AudioBuffer& a, MidiBuffer&) override {
        if (bypass.load())
            return;
        const int kind = static_cast<int>(get("type"));
        const float cut = get("cutoff");
        const float q = std::max(0.2f, get("reso"));
        const float drive = dbToGain(get("drive"));
        const float mix = get("mix");
        Biquad proto = kind == 1 ? Biquad::highPass(sampleRate, cut, q)
                     : kind == 2 ? Biquad::bandPass(sampleRate, cut, q)
                                 : Biquad::lowPass(sampleRate, cut, q);
        if (static_cast<int>(z.size()) < a.channels)
            z.resize(a.channels);
        for (int c = 0; c < a.channels; ++c) {
            Biquad b = proto;
            b.z1 = z[c].z1;
            b.z2 = z[c].z2;
            float* x = a.ch(c);
            for (int i = 0; i < a.frames; ++i) {
                const float dry = x[i];
                float y = b.tick(dry);
                y = std::tanh(y * drive);
                x[i] = dry * (1.f - mix) + y * mix;
            }
            z[c] = b;
        }
    }
    double sampleRate = 48000;
    std::vector<Biquad> z;
};

class Dynamics : public Device {
public:
    enum Kind { Comp, Lim, Gate } kind;
    explicit Dynamics(Kind k) : kind(k) {
        if (k == Comp) {
            type = "compressor";
            name = "Glue";
            params = {P("threshold", "Threshold", -60, 0, -18), P("ratio", "Ratio", 1, 20, 4),
                      P("attack", "Attack", 0.1f, 200, 10), P("release", "Release", 5, 2000, 120),
                      P("knee", "Knee", 0, 24, 6), P("makeup", "Makeup", 0, 24, 0), P("mix", "Mix", 0, 1, 1)};
        } else if (k == Lim) {
            type = "limiter";
            name = "Limiter";
            params = {P("ceiling", "Ceiling", -24, 0, 0), P("lookahead", "Lookahead", 0, 20, 0),
                      P("release", "Release", 5, 500, 80)};
        } else {
            type = "gate";
            name = "Gate";
            params = {P("threshold", "Threshold", -80, 0, -45), P("attack", "Attack", 0.1f, 50, 1),
                      P("release", "Release", 5, 1000, 80), P("range", "Range", -80, 0, -80)};
        }
    }
    void prepare(double sr, int block) override {
        sampleRate = sr;
        env = kind == Lim ? 1.f : 0.f;
        lat = 0;
        if (kind == Lim) {
            lat = static_cast<int>(std::round(get("lookahead") * 0.001 * sr));
            const int cap = std::max(8, lat + block + 8);
            for (auto& d : delay) {
                d.assign(cap, 0.f);
            }
            gainDelay.assign(cap, 1.f);
            w = 0;
            capN = cap;
        }
    }
    int latency() const override { return lat; }
    void process(AudioBuffer& a, MidiBuffer&) override {
        if (bypass.load() || a.channels < 1)
            return;
        if (kind == Lim)
            processLim(a);
        else if (kind == Gate)
            processGate(a);
        else
            processComp(a);
    }

    void processComp(AudioBuffer& a) {
        const float thr = get("threshold");
        const float ratio = std::max(1.f, get("ratio"));
        const float atk = coeff(get("attack"), sampleRate);
        const float rel = coeff(get("release"), sampleRate);
        const float knee = get("knee");
        const float makeup = dbToGain(get("makeup"));
        const float mix = get("mix");
        float* l = a.ch(0);
        float* r = a.channels > 1 ? a.ch(1) : l;
        for (int i = 0; i < a.frames; ++i) {
            const float peak = std::max(std::abs(l[i]), std::abs(r[i]));
            const float c = peak > env ? atk : rel;
            env += c * (peak - env);
            const float db = gainToDb(env);
            float over = db - thr;
            float gr = 0.f;
            if (knee > 0.f && over > -knee * 0.5f && over < knee * 0.5f) {
                const float x = over + knee * 0.5f;
                over = x * x / (2.f * knee);
            }
            if (over > 0.f)
                gr = over * (1.f - 1.f / ratio);
            const float g = dbToGain(-gr) * makeup;
            const float dryL = l[i], dryR = r[i];
            l[i] = dryL * (1.f - mix) + dryL * g * mix;
            if (a.channels > 1)
                r[i] = dryR * (1.f - mix) + dryR * g * mix;
        }
    }
    void processGate(AudioBuffer& a) {
        const float thr = dbToGain(get("threshold"));
        const float atk = coeff(get("attack"), sampleRate);
        const float rel = coeff(get("release"), sampleRate);
        const float floorG = dbToGain(get("range"));
        float* l = a.ch(0);
        float* r = a.channels > 1 ? a.ch(1) : l;
        for (int i = 0; i < a.frames; ++i) {
            const float peak = std::max(std::abs(l[i]), std::abs(r[i]));
            const float target = peak >= thr ? 1.f : 0.f;
            const float c = target > env ? atk : rel;
            env += c * (target - env);
            const float g = floorG + (1.f - floorG) * env;
            l[i] *= g;
            if (a.channels > 1)
                r[i] *= g;
        }
    }
    void processLim(AudioBuffer& a) {
        const int want = static_cast<int>(std::round(get("lookahead") * 0.001 * sampleRate));
        if (want != lat) {
            lat = want;
            capN = std::max(8, lat + a.frames + 8);
            for (auto& d : delay)
                d.assign(capN, 0.f);
            gainDelay.assign(capN, 1.f);
            w = 0;
            env = 1.f;
        }
        const float ceiling = dbToGain(get("ceiling"));
        const float rel = coeff(get("release"), sampleRate);
        float* l = a.ch(0);
        float* r = a.channels > 1 ? a.ch(1) : l;
        for (int i = 0; i < a.frames; ++i) {
            const float peak = std::max(std::abs(l[i]), std::abs(r[i]));
            float target = 1.f;
            if (peak > ceiling && peak > 1.0e-8f)
                target = ceiling / peak;
            if (target < env)
                env = target;
            else
                env += rel * (target - env);
            const int cap = capN;
            const int read = lat <= 0 ? w : (w - lat + cap * 4) % cap;
            const float g = lat <= 0 ? env : gainDelay[read];
            const float yl = lat <= 0 ? l[i] : delay[0][read];
            const float yr = lat <= 0 ? r[i] : delay[1][read];
            delay[0][w] = l[i];
            delay[1][w] = r[i];
            gainDelay[w] = env;
            l[i] = yl * g;
            if (a.channels > 1)
                r[i] = yr * g;
            w = (w + 1) % cap;
        }
    }

    double sampleRate = 48000;
    float env = 0.f;
    int lat = 0;
    int capN = 8;
    int w = 0;
    std::vector<float> delay[2];
    std::vector<float> gainDelay;
};

class Saturator : public Device {
public:
    Saturator() {
        type = "saturator";
        name = "Saturator";
        params = {P("drive", "Drive", 0, 36, 6), P("tone", "Tone", -1, 1, 0),
                  P("mix", "Mix", 0, 1, 1), P("output", "Output", -24, 12, 0)};
    }
    void prepare(double sr, int) override { sampleRate = sr; }
    void process(AudioBuffer& a, MidiBuffer&) override {
        if (bypass.load())
            return;
        const float drive = dbToGain(get("drive"));
        const float norm = std::tanh(drive);
        const float tone = get("tone");
        const float mix = get("mix");
        const float outG = dbToGain(get("output"));
        const float pole = std::exp(-2.f * kPi * 1200.f / static_cast<float>(sampleRate));
        for (int c = 0; c < a.channels; ++c) {
            float* x = a.ch(c);
            float lp = low[c];
            for (int i = 0; i < a.frames; ++i) {
                const float dry = x[i];
                float y = std::tanh(dry * drive) / std::max(0.05f, norm);
                lp = lp + (1.f - pole) * (y - lp);
                const float tilted = y + tone * (lp - y);
                x[i] = (dry * (1.f - mix) + tilted * mix) * outG;
            }
            low[c] = lp;
        }
    }
    double sampleRate = 48000;
    float low[2] {};
};

class DelayDevice : public Device {
public:
    DelayDevice() {
        type = "delay";
        name = "Delay";
        params = {P("timeMs", "Time", 1, 2000, 375), P("feedback", "Feedback", 0, 0.95f, 0.35f),
                  P("mix", "Mix", 0, 1, 0.25f), P("sync", "Sync", 0, 1, 0)};
    }
    void prepare(double sr, int) override {
        sampleRate = sr;
        const int n = static_cast<int>(sr * 4);
        for (auto& d : buf)
            d.assign(n, 0.f);
        w = 0;
        cap = n;
    }
    void process(AudioBuffer& a, MidiBuffer&) override {
        if (bypass.load() || cap <= 0)
            return;
        int delaySamp = static_cast<int>(get("timeMs") * 0.001 * sampleRate);
        delaySamp = std::clamp(delaySamp, 1, cap - 1);
        const float fb = get("feedback");
        const float mix = get("mix");
        for (int i = 0; i < a.frames; ++i) {
            const int r = (w - delaySamp + cap) % cap;
            for (int c = 0; c < std::min(2, a.channels); ++c) {
                float* x = a.ch(c);
                const float wet = buf[c][r];
                buf[c][w] = x[i] + wet * fb;
                x[i] = x[i] * (1.f - mix) + wet * mix;
            }
            w = (w + 1) % cap;
        }
    }
    double sampleRate = 48000;
    std::vector<float> buf[2];
    int w = 0, cap = 0;
};

class Reverb : public Device {
public:
    Reverb() {
        type = "reverb";
        name = "Reverb";
        params = {P("size", "Size", 0.2f, 1.f, 0.7f), P("damp", "Damp", 0, 1, 0.4f),
                  P("mix", "Mix", 0, 1, 0.25f)};
    }
    void prepare(double sr, int) override {
        sampleRate = sr;
        const int lens[4] = {1557, 1617, 1491, 1422};
        for (int i = 0; i < 4; ++i) {
            comb[i].assign(lens[i], 0.f);
            cw[i] = 0;
        }
        ap[0].assign(225, 0.f);
        ap[1].assign(556, 0.f);
        aw[0] = aw[1] = 0;
        dampState = 0.f;
    }
    void process(AudioBuffer& a, MidiBuffer&) override {
        if (bypass.load() || a.channels < 1)
            return;
        const float mix = get("mix");
        const float feedback = 0.55f + 0.4f * get("size");
        const float damp = get("damp");
        float* l = a.ch(0);
        float* r = a.channels > 1 ? a.ch(1) : l;
        for (int i = 0; i < a.frames; ++i) {
            const float in = 0.5f * (l[i] + r[i]);
            float s = 0.f;
            for (int k = 0; k < 4; ++k) {
                const int n = static_cast<int>(comb[k].size());
                const float y = comb[k][cw[k]];
                dampState = y * (1.f - damp) + dampState * damp;
                comb[k][cw[k]] = in + dampState * feedback;
                cw[k] = (cw[k] + 1) % n;
                s += y;
            }
            s *= 0.25f;
            for (int k = 0; k < 2; ++k) {
                const int n = static_cast<int>(ap[k].size());
                const float buf = ap[k][aw[k]];
                ap[k][aw[k]] = s + buf * 0.5f;
                s = buf - s * 0.5f;
                aw[k] = (aw[k] + 1) % n;
            }
            l[i] = l[i] * (1.f - mix) + s * mix;
            if (a.channels > 1)
                r[i] = r[i] * (1.f - mix) + s * 0.97f * mix;
        }
    }
    double sampleRate = 48000;
    std::vector<float> comb[4], ap[2];
    int cw[4] {}, aw[2] {};
    float dampState = 0.f;
};

class ModDelay : public Device {
public:
    enum Kind { Chorus, Flanger, Phaser } kind;
    explicit ModDelay(Kind k) : kind(k) {
        if (k == Chorus) {
            type = "chorus";
            name = "Chorus";
            params = {P("rate", "Rate", 0.05f, 5, 0.4f), P("depth", "Depth", 0, 1, 0.4f), P("mix", "Mix", 0, 1, 0.4f)};
        } else if (k == Flanger) {
            type = "flanger";
            name = "Flanger";
            params = {P("rate", "Rate", 0.05f, 8, 0.25f), P("depth", "Depth", 0, 1, 0.6f),
                      P("feedback", "Feedback", 0, 0.9f, 0.4f), P("mix", "Mix", 0, 1, 0.45f)};
        } else {
            type = "phaser";
            name = "Phaser";
            params = {P("rate", "Rate", 0.05f, 8, 0.3f), P("depth", "Depth", 0, 1, 0.7f), P("mix", "Mix", 0, 1, 0.5f)};
        }
    }
    void prepare(double sr, int) override {
        sampleRate = sr;
        phase = 0.f;
        buf.assign(static_cast<int>(sr * 0.1), 0.f);
        w = 0;
        for (auto& s : ap)
            s.reset();
    }
    void process(AudioBuffer& a, MidiBuffer&) override {
        if (bypass.load())
            return;
        const float rate = get("rate");
        const float depth = get("depth");
        const float mix = get("mix");
        const float fb = kind == Flanger ? get("feedback") : 0.f;
        for (int i = 0; i < a.frames; ++i) {
            phase += 2.f * kPi * rate / static_cast<float>(sampleRate);
            if (phase > 2.f * kPi)
                phase -= 2.f * kPi;
            const float lfo = 0.5f + 0.5f * std::sin(phase);
            for (int c = 0; c < std::min(2, a.channels); ++c) {
                float* x = a.ch(c);
                float wet = x[i];
                if (kind == Phaser) {
                    const float f = 200.f + lfo * depth * 2000.f;
                    ap[c] = Biquad::allPass(sampleRate, f, 0.7f);
                    // keep state by ticking a persistent filter: rebuild loses state.
                    // Use a one-pole allpass instead.
                    const float g = std::tan(kPi * f / static_cast<float>(sampleRate));
                    const float a = (g - 1.f) / (g + 1.f);
                    const float y = a * x[i] + apMem[c];
                    apMem[c] = x[i] - a * y;
                    wet = y;
                } else {
                    const float ms = kind == Chorus ? (7.f + lfo * depth * 12.f) : (0.5f + lfo * depth * 6.f);
                    int d = std::clamp(static_cast<int>(ms * 0.001f * sampleRate), 1, static_cast<int>(buf.size()) - 1);
                    const int r = (w - d + static_cast<int>(buf.size())) % static_cast<int>(buf.size());
                    wet = buf[r];
                    if (c == 0)
                        buf[w] = x[i] + wet * fb;
                }
                x[i] = x[i] * (1.f - mix) + wet * mix;
            }
            if (kind != Phaser)
                w = (w + 1) % static_cast<int>(buf.size());
        }
    }
    double sampleRate = 48000;
    float phase = 0.f;
    std::vector<float> buf;
    int w = 0;
    Biquad ap[2];
    float apMem[2] {};
};

class MidiFx : public Device {
public:
    enum Kind { Arp, Chord, Vel } kind;
    explicit MidiFx(Kind k) : kind(k) {
        if (k == Arp) {
            type = "arpeggiator";
            name = "Arpeggiator";
            params = {P("rate", "Rate", 1, 16, 8), P("octaves", "Octaves", 1, 4, 1)};
        } else if (k == Chord) {
            type = "chord";
            name = "Chord";
            params = {Choice("mode", "Mode", {"Major", "Minor", "Fifth"}, 0)};
        } else {
            type = "velocity";
            name = "Velocity";
            params = {P("scale", "Scale", 0, 2, 1), P("offset", "Offset", -64, 64, 0)};
        }
    }
    void prepare(double sr, int) override { sampleRate = sr; held.clear(); step = 0; countdown = 0; }
    void process(AudioBuffer& audio, MidiBuffer& midi) override {
        if (bypass.load())
            return;
        if (kind == Vel) {
            const float scale = get("scale");
            const int off = static_cast<int>(get("offset"));
            for (auto& e : midi.events) {
                if (e.isNoteOn() || e.isNoteOff()) {
                    int v = std::clamp(static_cast<int>(e.data2 * scale + off), 1, 127);
                    if (e.isNoteOff())
                        v = 0;
                    e.data2 = static_cast<uint8_t>(v);
                }
            }
            return;
        }
        if (kind == Chord) {
            const int mode = static_cast<int>(get("mode"));
            const int iv[3][2] = {{4, 7}, {3, 7}, {7, 12}};
            std::vector<MidiEvent> extra;
            for (const auto& e : midi.events) {
                if (e.isNoteOn() || e.isNoteOff()) {
                    for (int k = 0; k < 2; ++k) {
                        MidiEvent n = e;
                        n.data1 = static_cast<uint8_t>(std::clamp(e.data1 + iv[mode][k], 0, 127));
                        extra.push_back(n);
                    }
                }
            }
            midi.events.insert(midi.events.end(), extra.begin(), extra.end());
            return;
        }
        const double quartersPerStep = 4.0 / std::max(1.f, get("rate"));
        const int stepSamples = std::max(1, static_cast<int>(quartersPerStep * sampleRate * 60.0 / 120.0));
        for (const auto& e : midi.events) {
            if (e.isNoteOn())
                held.push_back(e.data1);
            else if (e.isNoteOff())
                held.erase(std::remove(held.begin(), held.end(), e.data1), held.end());
        }
        midi.clear();
        if (held.empty())
            return;
        countdown -= std::max(1, audio.frames);
        if (countdown <= 0) {
            countdown = stepSamples;
            const int oct = static_cast<int>(get("octaves"));
            const int idx = step % static_cast<int>(held.size() * std::max(1, oct));
            const int note = held[idx % held.size()] + 12 * (idx / static_cast<int>(held.size()));
            midi.add(0, 0x90, static_cast<uint8_t>(std::clamp(note, 0, 127)), 100);
            midi.add(std::max(1, stepSamples / 2), 0x80, static_cast<uint8_t>(std::clamp(note, 0, 127)), 0);
            ++step;
        }
    }
    double sampleRate = 48000;
    std::vector<int> held;
    int step = 0;
    int countdown = 0;
};

struct Voice {
    bool on = false;
    int note = 60;
    float vel = 0.f;
    float phase = 0.f;
    float phase2 = 0.f;
    float env = 0.f;
    int stage = 0; // 0 atk 1 dec 2 sus 3 rel
    float f1 = 0, f2 = 0;
};

class Wavetable : public Device {
public:
    Wavetable() {
        type = "wavetable";
        name = "Wavetable";
        params = {P("position", "Position", 0, 1, 0.25f), P("wave", "Wave", 0, 1, 0.2f),
                  P("unison", "Unison", 1, 8, 2), P("cutoff", "Cutoff", 40, 18000, 4000, 0.3f),
                  P("resonance", "Reso", 0.2f, 6, 0.7f), P("attack", "Attack", 0.001f, 2, 0.01f),
                  P("decay", "Decay", 0.01f, 2, 0.2f), P("sustain", "Sustain", 0, 1, 0.7f),
                  P("release", "Release", 0.01f, 3, 0.3f), P("level", "Level", 0, 1, 0.8f)};
    }
    void prepare(double sr, int) override { sampleRate = sr; voices.clear(); voices.resize(8); }
    void process(AudioBuffer& a, MidiBuffer& midi) override {
        if (a.channels < 1)
            return;
        for (const auto& e : midi.events) {
            if (e.isNoteOn())
                noteOn(e.data1, e.data2 / 127.f);
            else if (e.isNoteOff())
                noteOff(e.data1);
        }
        if (bypass.load()) {
            midi.clear();
            return;
        }
        a.clear();
        const float level = get("level");
        const float cut = get("cutoff");
        const float q = get("resonance");
        Biquad filt = Biquad::lowPass(sampleRate, cut, q);
        const float atk = std::max(0.001f, get("attack"));
        const float dec = std::max(0.01f, get("decay"));
        const float sus = get("sustain");
        const float rel = std::max(0.01f, get("release"));
        const int uni = std::clamp(static_cast<int>(std::round(get("unison"))), 1, 7);
        const float pos = get("position");
        const float wave = get("wave");
        float* l = a.ch(0);
        float* r = a.channels > 1 ? a.ch(1) : nullptr;
        for (int i = 0; i < a.frames; ++i) {
            float s = 0.f;
            for (auto& v : voices) {
                if (!v.on && v.env < 1.0e-4f)
                    continue;
                if (v.stage == 0) {
                    v.env += 1.f / (atk * static_cast<float>(sampleRate));
                    if (v.env >= 1.f) { v.env = 1.f; v.stage = 1; }
                } else if (v.stage == 1) {
                    v.env += (sus - v.env) * (1.f / (dec * static_cast<float>(sampleRate)));
                    if (std::abs(v.env - sus) < 0.001f) v.stage = 2;
                } else if (v.stage == 3) {
                    v.env -= 1.f / (rel * static_cast<float>(sampleRate));
                    if (v.env <= 0.f) { v.env = 0.f; v.on = false; }
                }
                const float freq = 440.f * std::pow(2.f, (v.note - 69) / 12.f);
                float acc = 0.f;
                for (int u = 0; u < uni; ++u) {
                    const float det = 1.f + (u - (uni - 1) * 0.5f) * 0.006f * (0.3f + pos);
                    float& ph = (u == 0 ? v.phase : v.phase2);
                    if (u > 1)
                        ph = v.phase * (1.f + 0.01f * u);
                    ph += freq * det / static_cast<float>(sampleRate);
                    ph -= std::floor(ph);
                    const float sine = std::sin(2.f * kPi * ph);
                    const float saw = 2.f * ph - 1.f;
                    const float sq = ph < 0.5f ? 1.f : -1.f;
                    const float tri = ph < 0.5f ? (4.f * ph - 1.f) : (3.f - 4.f * ph);
                    float w0 = sine * (1.f - wave) + saw * wave;
                    if (wave > 0.66f)
                        w0 = sq * (wave - 0.66f) * 3.f + tri * (1.f - (wave - 0.66f) * 3.f);
                    acc += w0;
                }
                s += (acc / static_cast<float>(uni)) * v.env * v.vel;
            }
            s *= level * 0.4f;
            filt.z1 = fState.z1;
            filt.z2 = fState.z2;
            s = filt.tick(s);
            fState = filt;
            l[i] = s;
            if (r)
                r[i] = s;
        }
        midi.clear();
    }
    void noteOn(int note, float vel) {
        Voice* slot = &voices[0];
        for (auto& v : voices)
            if (!v.on) { slot = &v; break; }
        slot->on = true;
        slot->note = note;
        slot->vel = std::max(0.05f, vel);
        slot->stage = 0;
        slot->env = 0.f;
    }
    void noteOff(int note) {
        for (auto& v : voices)
            if (v.on && v.note == note && v.stage != 3)
                v.stage = 3;
    }
    double sampleRate = 48000;
    std::vector<Voice> voices;
    Biquad fState;
};

class Sampler : public Device {
public:
    Sampler() {
        type = "sampler";
        name = "Sampler";
        params = {P("root", "Root", 0, 127, 60), P("tune", "Tune", -12, 12, 0),
                  P("attack", "Attack", 0.001f, 1, 0.005f), P("release", "Release", 0.01f, 2, 0.2f),
                  P("level", "Level", 0, 1, 0.9f)};
        sample.assign(48000, 0.f);
        for (int i = 0; i < 48000; ++i)
            sample[i] = 0.4f * std::sin(2.f * kPi * 220.f * i / 48000.f) * std::exp(-3.f * i / 48000.f);
    }
    void loadSample(const float* interleaved, int frames, int channels) override {
        sample.resize(frames);
        for (int i = 0; i < frames; ++i) {
            float s = 0.f;
            for (int c = 0; c < channels; ++c)
                s += interleaved[i * channels + c];
            sample[i] = s / std::max(1, channels);
        }
        loadedRate = 48000;
    }
    void prepare(double sr, int) override { sampleRate = sr; on = false; pos = 0.f; env = 0.f; }
    void process(AudioBuffer& a, MidiBuffer& midi) override {
        if (a.channels < 1)
            return;
        for (const auto& e : midi.events) {
            if (e.isNoteOn()) { on = true; releasing = false; pos = 0.f; env = 0.f; note = e.data1; vel = e.data2 / 127.f; }
            else if (e.isNoteOff() && e.data1 == note) releasing = true;
        }
        if (bypass.load() || sample.empty()) {
            midi.clear();
            return;
        }
        const float root = get("root") + get("tune");
        const float ratio = std::pow(2.f, (note - root) / 12.f) * (48000.f / static_cast<float>(sampleRate));
        const float atk = std::max(1.f, get("attack") * static_cast<float>(sampleRate));
        const float rel = std::max(1.f, get("release") * static_cast<float>(sampleRate));
        const float level = get("level");
        float* l = a.ch(0);
        float* r = a.channels > 1 ? a.ch(1) : nullptr;
        for (int i = 0; i < a.frames; ++i) {
            float s = 0.f;
            if (on) {
                if (!releasing)
                    env = std::min(1.f, env + 1.f / atk);
                else {
                    env -= 1.f / rel;
                    if (env <= 0.f) { env = 0.f; on = false; }
                }
                const int i0 = static_cast<int>(pos);
                if (i0 >= static_cast<int>(sample.size()) - 1)
                    on = false;
                else {
                    const float f = pos - i0;
                    s = sample[i0] * (1.f - f) + sample[i0 + 1] * f;
                    pos += ratio;
                }
            }
            s *= env * vel * level;
            l[i] = s;
            if (r) r[i] = s;
        }
        midi.clear();
    }
    double sampleRate = 48000;
    int loadedRate = 48000;
    std::vector<float> sample;
    bool on = false, releasing = false;
    float pos = 0, env = 0, vel = 1;
    int note = 60;
};

class DrumRack : public Device {
public:
    DrumRack() {
        type = "drumrack";
        name = "Drum Rack";
        params = {P("level", "Level", 0, 1, 0.9f), P("tone", "Tone", 0, 1, 0.5f)};
    }
    void prepare(double sr, int) override { sampleRate = sr; for (auto& p : pads) p = {}; }
    void process(AudioBuffer& a, MidiBuffer& midi) override {
        if (a.channels < 1)
            return;
        for (const auto& e : midi.events) {
            if (e.isNoteOn() && e.data1 >= 36 && e.data1 < 44) {
                auto& p = pads[e.data1 - 36];
                p.on = true;
                p.pos = 0;
                p.vel = e.data2 / 127.f;
            }
        }
        if (bypass.load())
            return;
        a.clear();
        const float level = get("level");
        const float tone = 80.f + get("tone") * 200.f;
        float* l = a.ch(0);
        float* r = a.channels > 1 ? a.ch(1) : nullptr;
        for (int i = 0; i < a.frames; ++i) {
            float s = 0.f;
            for (int k = 0; k < 8; ++k) {
                auto& p = pads[k];
                if (!p.on)
                    continue;
                const float t = p.pos / static_cast<float>(sampleRate);
                float y = 0.f;
                if (k == 0 || k == 4)
                    y = std::sin(2.f * kPi * (tone * 1.5f) * t) * std::exp(-18.f * t);
                else if (k == 1 || k == 7)
                    y = (static_cast<float>(std::rand()) / RAND_MAX * 2.f - 1.f) * std::exp(-12.f * t);
                else
                    y = std::sin(2.f * kPi * (180.f + k * 40.f) * t) * std::exp(-8.f * t);
                s += y * p.vel;
                p.pos++;
                if (t > 0.5f)
                    p.on = false;
            }
            s *= level * 0.6f;
            l[i] = s;
            if (r) r[i] = s;
        }
        midi.clear();
    }
    struct Pad { bool on = false; int pos = 0; float vel = 0; };
    double sampleRate = 48000;
    Pad pads[8];
};

class EqDevice : public Device {
public:
    static constexpr int kBands = 8;
    EqDevice() {
        type = "eq";
        name = "EQ Eight";
        const char* names[kBands] = {"HP", "Low Shelf", "Bell", "Bell", "Bell", "Bell", "High Shelf", "LP"};
        const float freqs[kBands] = {40, 120, 400, 1000, 2500, 5000, 8000, 14000};
        const int types[kBands] = {5, 1, 0, 0, 0, 0, 2, 6};
        for (int i = 0; i < kBands; ++i) {
            const std::string n = std::to_string(i + 1);
            params.push_back(P(("on" + n).c_str(), names[i], 0, 1, (i == 2 || i == 6) ? 1.f : 0.f));
            params.push_back(Choice(("type" + n).c_str(), "Type",
                                    {"Bell", "Low Shelf", "High Shelf", "Notch", "Band Pass", "HP", "LP"},
                                    static_cast<float>(types[i])));
            params.push_back(P(("freq" + n).c_str(), "Freq", 20, 20000, freqs[i], 0.3f));
            params.push_back(P(("gain" + n).c_str(), "Gain", -18, 18, i == 2 ? 3.f : (i == 6 ? 1.5f : 0.f)));
            params.push_back(P(("q" + n).c_str(), "Q", 0.1f, 10.f, 0.7f));
        }
        params.push_back(Choice("phase", "Phase", {"Min", "Linear"}, 0));
        params.push_back(P("side", "SideCheck", 0, 1, 0));
        params.push_back(P("sideAmt", "Amount", 0, 1, 0.5f));
        params.push_back(P("sideHp", "Side HP", 20, 2000, 80, 0.4f));
        params.push_back(P("sideLp", "Side LP", 200, 18000, 8000, 0.4f));
        params.push_back(P("lfoRate", "LFO Rate", 0.01f, 8, 0.2f));
        params.push_back(P("lfoDepth", "LFO Depth", 0, 1, 0));
        designed = false;
    }
    void prepare(double sr, int block) override {
        sampleRate = sr;
        for (auto& ch : filters)
            for (auto& b : ch)
                b.reset();
        lat = get("phase") >= 0.5f ? 512 : 0;
        hist[0].assign(1024, 0.f);
        hist[1].assign(1024, 0.f);
        histPos[0] = histPos[1] = 0;
        lfoPhase = 0.f;
        (void)block;
        designed = false;
    }
    int latency() const override { return lat; }
    void process(AudioBuffer& a, MidiBuffer&) override {
        if (bypass.load() || a.channels < 1)
            return;
        design();
        const bool linear = get("phase") >= 0.5f;
        lat = linear ? 512 : 0;
        const bool side = get("side") >= 0.5f;
        const float amt = get("sideAmt");
        if (side && a.channels > 1)
            encodeSide(a, amt, true);
        if (linear)
            processLinear(a);
        else
            processMin(a);
        if (side && a.channels > 1)
            encodeSide(a, amt, false);
    }
    void design() {
        const float lfo = get("lfoDepth") > 0.001f
                              ? std::sin(lfoPhase) * get("lfoDepth")
                              : 0.f;
        lfoPhase += 0.01f;
        for (int i = 0; i < kBands; ++i) {
            const std::string n = std::to_string(i + 1);
            const bool on = get("on" + n) >= 0.5f;
            const int ty = static_cast<int>(get("type" + n));
            float freq = get("freq" + n);
            if (i == 2)
                freq *= std::pow(2.f, lfo);
            const float g = get("gain" + n);
            const float q = std::max(0.1f, get("q" + n));
            Biquad b;
            if (!on)
                b = Biquad{};
            else if (ty == 1)
                b = Biquad::lowShelf(sampleRate, freq, q, g);
            else if (ty == 2)
                b = Biquad::highShelf(sampleRate, freq, q, g);
            else if (ty == 3)
                b = Biquad::notch(sampleRate, freq, q);
            else if (ty == 4)
                b = Biquad::bandPass(sampleRate, freq, q);
            else if (ty == 5)
                b = Biquad::highPass(sampleRate, freq, q);
            else if (ty == 6)
                b = Biquad::lowPass(sampleRate, freq, q);
            else
                b = Biquad::peak(sampleRate, freq, q, g);
            proto[i] = b;
        }
        if (get("phase") >= 0.5f)
            buildIr();
        designed = true;
    }
    void processMin(AudioBuffer& a) {
        for (int c = 0; c < std::min(2, a.channels); ++c) {
            float* x = a.ch(c);
            for (int i = 0; i < a.frames; ++i) {
                float y = x[i];
                for (int b = 0; b < kBands; ++b) {
                    filters[c][b].b0 = proto[b].b0;
                    filters[c][b].b1 = proto[b].b1;
                    filters[c][b].b2 = proto[b].b2;
                    filters[c][b].a1 = proto[b].a1;
                    filters[c][b].a2 = proto[b].a2;
                    y = filters[c][b].tick(y);
                }
                x[i] = y;
            }
        }
    }
    void buildIr() {
        constexpr int N = 1024;
        std::vector<Biquad> chain(kBands);
        for (int i = 0; i < kBands; ++i)
            chain[i] = proto[i];
        std::vector<float> ir(N, 0.f);
        ir[0] = 1.f;
        for (int n = 0; n < N; ++n) {
            float y = ir[n];
            for (auto& b : chain)
                y = b.tick(y);
            ir[n] = y;
        }
        std::vector<std::complex<float>> spec(N);
        for (int n = 0; n < N; ++n)
            spec[n] = ir[n];
        fftRadix2(spec, false);
        constexpr int delay = 512;
        for (int k = 0; k < N; ++k) {
            const float mag = std::abs(spec[k]);
            const float ang = -2.f * kPi * k * delay / static_cast<float>(N);
            spec[k] = std::polar(mag, ang);
        }
        fftRadix2(spec, true);
        fir.resize(N);
        for (int n = 0; n < N; ++n)
            fir[n] = spec[n].real();
    }
    void processLinear(AudioBuffer& a) {
        if (fir.empty())
            buildIr();
        const int N = static_cast<int>(fir.size());
        for (int c = 0; c < std::min(2, a.channels); ++c) {
            float* x = a.ch(c);
            auto& h = hist[c];
            if (static_cast<int>(h.size()) != N)
                h.assign(N, 0.f);
            int pos = histPos[c];
            for (int i = 0; i < a.frames; ++i) {
                h[pos] = x[i];
                float y = 0.f;
                int idx = pos;
                for (int k = 0; k < N; ++k) {
                    y += fir[k] * h[idx];
                    if (--idx < 0)
                        idx = N - 1;
                }
                x[i] = y;
                pos = (pos + 1) % N;
            }
            histPos[c] = pos;
        }
    }
    void encodeSide(AudioBuffer& a, float amount, bool into) {
        float* l = a.ch(0);
        float* r = a.ch(1);
        Biquad hpF = Biquad::highPass(sampleRate, get("sideHp"), 0.7f);
        Biquad lpF = Biquad::lowPass(sampleRate, get("sideLp"), 0.7f);
        for (int i = 0; i < a.frames; ++i) {
            if (into) {
                const float mid = 0.5f * (l[i] + r[i]);
                float side = 0.5f * (l[i] - r[i]);
                side = lpF.tick(hpF.tick(side));
                side *= amount;
                l[i] = mid + side;
                r[i] = mid - side;
            }
        }
    }

    double sampleRate = 48000;
    int lat = 0;
    bool designed = false;
    float lfoPhase = 0.f;
    int histPos[2] {};
    Biquad proto[kBands];
    Biquad filters[2][kBands];
    std::vector<float> fir;
    std::vector<float> hist[2];
};

class DynDevice : public Device {
public:
    DynDevice() {
        type = "dyn";
        name = "DYN";
        params = {P("bands", "Bands", 1, 6, 3),
                  P("f1", "Split 1", 40, 2000, 120, 0.4f),
                  P("f2", "Split 2", 80, 8000, 2000, 0.4f),
                  P("threshold", "Threshold", -60, 0, -18),
                  P("ratio", "Ratio", 1, 20, 4),
                  P("attack", "Attack", 0.1f, 200, 12),
                  P("release", "Release", 5, 2000, 180),
                  P("knee", "Knee", 0, 24, 8),
                  P("makeup", "Makeup", -12, 24, 0),
                  P("mix", "Mix", 0, 1, 1)};
        for (int i = 0; i < 3; ++i)
            env[i] = 0.f;
    }
    void prepare(double sr, int) override {
        sampleRate = sr;
        for (auto& e : env) e = 0.f;
        for (auto& f : split) f.reset();
    }
    void process(AudioBuffer& a, MidiBuffer&) override {
        if (bypass.load() || a.channels < 1)
            return;
        const float f1 = get("f1");
        const float f2 = std::max(get("f2"), f1 * 1.2f);
        const float thr = get("threshold");
        const float ratio = std::max(1.f, get("ratio"));
        const float atk = coeff(get("attack"), sampleRate);
        const float rel = coeff(get("release"), sampleRate);
        const float makeup = dbToGain(get("makeup"));
        const float mix = get("mix");
        Biquad lp1 = Biquad::lowPass(sampleRate, f1, 0.707f);
        Biquad lp1b = lp1;
        Biquad hp1 = Biquad::highPass(sampleRate, f1, 0.707f);
        Biquad hp1b = hp1;
        Biquad lp2 = Biquad::lowPass(sampleRate, f2, 0.707f);
        Biquad lp2b = lp2;
        Biquad hp2 = Biquad::highPass(sampleRate, f2, 0.707f);
        Biquad hp2b = hp2;
        for (int c = 0; c < std::min(2, a.channels); ++c) {
            float* x = a.ch(c);
            for (int i = 0; i < a.frames; ++i) {
                const float dry = x[i];
                float low = lp1b.tick(lp1.tick(dry));
                float mid = lp2b.tick(lp2.tick(hp1b.tick(hp1.tick(dry))));
                float high = hp2b.tick(hp2.tick(dry));
                float bands[3] = {low, mid, high};
                float sum = 0.f;
                for (int b = 0; b < 3; ++b) {
                    const float peak = std::abs(bands[b]);
                    const float cf = peak > env[b] ? atk : rel;
                    env[b] += cf * (peak - env[b]);
                    float over = gainToDb(env[b]) - thr;
                    float gr = over > 0.f ? over * (1.f - 1.f / ratio) : 0.f;
                    sum += bands[b] * dbToGain(-gr);
                }
                sum *= makeup;
                x[i] = dry * (1.f - mix) + sum * mix;
            }
        }
    }
    double sampleRate = 48000;
    float env[3] {};
    Biquad split[8];
};

class Analyzer : public Device {
public:
    Analyzer() {
        type = "analyzer";
        name = "Analyzer";
        params = {P("gain", "Gain", -24, 24, 0)};
    }
    void prepare(double sr, int) override {
        sampleRate = sr;
        acc.clear();
        scopePos = 0;
        k1 = Biquad::highShelf(sr, 1682.f, 0.7f, 4.f);
        k2 = Biquad::highPass(sr, 38.f, 0.5f);
        moment.clear();
        snap = {};
    }
    void process(AudioBuffer& a, MidiBuffer&) override {
        if (a.channels < 1)
            return;
        const float g = bypass.load() ? 1.f : dbToGain(get("gain"));
        float* l = a.ch(0);
        float* r = a.channels > 1 ? a.ch(1) : l;
        float peak = 0.f;
        double sumSq = 0.0;
        double corrN = 0, corrL = 0, corrR = 0;
        for (int i = 0; i < a.frames; ++i) {
            l[i] *= g;
            if (a.channels > 1)
                r[i] *= g;
            const float mid = 0.5f * (l[i] + (a.channels > 1 ? r[i] : l[i]));
            acc.push_back(mid);
            scope[scopePos] = mid;
            scopePos = (scopePos + 1) % 512;
            peak = std::max(peak, std::max(std::abs(l[i]), std::abs(a.channels > 1 ? r[i] : l[i])));
            const float kw = k2.tick(k1.tick(mid));
            moment.push_back(kw * kw);
            if (a.channels > 1) {
                corrN += l[i] * r[i];
                corrL += l[i] * l[i];
                corrR += r[i] * r[i];
            }
            sumSq += kw * kw;
        }
        while (static_cast<int>(moment.size()) > static_cast<int>(sampleRate * 0.4))
            moment.pop_front();
        if (static_cast<int>(acc.size()) >= 2048) {
            std::vector<std::complex<float>> spec(2048);
            const int start = static_cast<int>(acc.size()) - 2048;
            for (int i = 0; i < 2048; ++i) {
                const float w = 0.5f * (1.f - std::cos(2.f * kPi * i / 2047.f));
                spec[i] = acc[start + i] * w;
            }
            fftRadix2(spec, false);
            AnalyzerSnapshot next = snap;
            for (int b = 0; b < 256; ++b) {
                const float f0 = 20.f * std::pow(1000.f, b / 256.f);
                const float f1 = 20.f * std::pow(1000.f, (b + 1) / 256.f);
                int i0 = std::clamp(static_cast<int>(f0 * 2048 / sampleRate), 1, 1023);
                int i1 = std::clamp(static_cast<int>(f1 * 2048 / sampleRate), i0 + 1, 1024);
                float m = 0.f;
                for (int k = i0; k < i1; ++k)
                    m = std::max(m, std::abs(spec[k]));
                const float db = gainToDb(m / 1024.f);
                next.spectrum[b] = std::clamp((db + 72.f) / 72.f, 0.f, 1.f);
            }
            for (int i = 0; i < 512; ++i)
                next.scope[i] = scope[(scopePos + i) % 512];
            const double den = std::sqrt(std::max(1e-12, corrL * corrR));
            next.correlation = a.channels > 1 ? static_cast<float>(corrN / den) : 1.f;
            double ms = 0;
            for (float v : moment)
                ms += v;
            ms /= std::max<size_t>(1, moment.size());
            next.momentaryLufs = -0.691f + 10.f * std::log10(static_cast<float>(std::max(1e-12, ms)));
            next.peakDb = gainToDb(peak);
            std::lock_guard<std::mutex> lock(mu);
            snap = next;
            acc.clear();
        }
    }
    AnalyzerSnapshot copy() const {
        std::lock_guard<std::mutex> lock(mu);
        return snap;
    }
    double sampleRate = 48000;
    std::vector<float> acc;
    std::deque<float> moment;
    float scope[512] {};
    int scopePos = 0;
    Biquad k1, k2;
    mutable std::mutex mu;
    AnalyzerSnapshot snap;
};

class Missing : public Device {
public:
    Missing() { type = "missing"; name = "Missing"; }
    void prepare(double, int) override {}
    void process(AudioBuffer&, MidiBuffer&) override {}
};

} // namespace

std::unique_ptr<Device> createDevice(const std::string& type) {
    if (type == "utility") return std::make_unique<Utility>();
    if (type == "filter") return std::make_unique<FilterDevice>();
    if (type == "compressor") return std::make_unique<Dynamics>(Dynamics::Comp);
    if (type == "limiter") return std::make_unique<Dynamics>(Dynamics::Lim);
    if (type == "gate") return std::make_unique<Dynamics>(Dynamics::Gate);
    if (type == "saturator") return std::make_unique<Saturator>();
    if (type == "delay") return std::make_unique<DelayDevice>();
    if (type == "reverb") return std::make_unique<Reverb>();
    if (type == "chorus") return std::make_unique<ModDelay>(ModDelay::Chorus);
    if (type == "flanger") return std::make_unique<ModDelay>(ModDelay::Flanger);
    if (type == "phaser") return std::make_unique<ModDelay>(ModDelay::Phaser);
    if (type == "arpeggiator") return std::make_unique<MidiFx>(MidiFx::Arp);
    if (type == "chord") return std::make_unique<MidiFx>(MidiFx::Chord);
    if (type == "velocity") return std::make_unique<MidiFx>(MidiFx::Vel);
    if (type == "wavetable") return std::make_unique<Wavetable>();
    if (type == "sampler") return std::make_unique<Sampler>();
    if (type == "drumrack") return std::make_unique<DrumRack>();
    if (type == "eq") return std::make_unique<EqDevice>();
    if (type == "dyn") return std::make_unique<DynDevice>();
    if (type == "analyzer") return std::make_unique<Analyzer>();
    return std::make_unique<Missing>();
}

std::vector<std::string> nativeDeviceTypes() {
    return {"utility", "eq", "dyn", "compressor", "limiter", "gate", "saturator", "filter",
            "delay", "reverb", "chorus", "flanger", "phaser", "analyzer",
            "wavetable", "sampler", "drumrack", "arpeggiator", "chord", "velocity"};
}

std::string deviceDisplayName(const std::string& type) {
    if (type == "eq") return "EQ Eight";
    if (type == "compressor") return "Glue";
    if (type == "dyn") return "DYN";
    if (type == "drumrack") return "Drum Rack";
    if (type == "arpeggiator") return "Arpeggiator";
    auto d = createDevice(type);
    return d ? d->name : type;
}

std::vector<float> eqMagnitudeDb(const Device& eq, double sampleRate, const std::vector<float>& freqs) {
    std::vector<float> out(freqs.size(), 0.f);
    if (eq.type != "eq")
        return out;
    for (size_t i = 0; i < freqs.size(); ++i) {
        float mag = 1.f;
        for (int b = 0; b < 8; ++b) {
            const std::string n = std::to_string(b + 1);
            if (eq.get("on" + n) < 0.5f)
                continue;
            const int ty = static_cast<int>(eq.get("type" + n));
            const float f = eq.get("freq" + n);
            const float g = eq.get("gain" + n);
            const float q = std::max(0.1f, eq.get("q" + n));
            Biquad bi;
            if (ty == 1) bi = Biquad::lowShelf(sampleRate, f, q, g);
            else if (ty == 2) bi = Biquad::highShelf(sampleRate, f, q, g);
            else if (ty == 3) bi = Biquad::notch(sampleRate, f, q);
            else if (ty == 4) bi = Biquad::bandPass(sampleRate, f, q);
            else if (ty == 5) bi = Biquad::highPass(sampleRate, f, q);
            else if (ty == 6) bi = Biquad::lowPass(sampleRate, f, q);
            else bi = Biquad::peak(sampleRate, f, q, g);
            mag *= bi.magnitude(sampleRate, freqs[i]);
        }
        out[i] = 20.f * std::log10(std::max(mag, 1.0e-6f));
    }
    return out;
}

AnalyzerSnapshot readAnalyzer(const Device& device) {
    if (const auto* a = dynamic_cast<const Analyzer*>(&device))
        return a->copy();
    return {};
}

} // namespace ghovlz
