#pragma once

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <utility>

namespace ghovlz {

// Seeded PCG XSH-RR. The EQ colour randomizer draws from an unseeded system
// generator and is not in this tree. This matches that tool's stop count
// (2 through 10) and an HSV-limit walk, so one seed always reprints the same ramp.
struct Pcg32 {
    uint64_t state = 0;
    uint64_t inc = 1;

    explicit Pcg32(uint64_t seed) : inc((seed << 1u) | 1u) {
        next();
        state += seed;
        next();
    }

    uint32_t next() {
        const uint64_t old = state;
        state = old * 6364136223846793005ull + inc;
        const uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        const uint32_t rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((0u - rot) & 31u));
    }

    // Inclusive lo, exclusive hi.
    int nextInt(int lo, int hi) { return lo + static_cast<int>(next() % static_cast<uint32_t>(hi - lo)); }
    float unit() { return static_cast<float>(next() >> 8) * (1.f / 16777216.f); }
};

struct HsvLimit {
    float hue0 = 0.f;
    float hue1 = 1.f;
    float sat0 = 0.f;
    float sat1 = 1.f;
    float val0 = 0.f;
    float val1 = 1.f;
};

struct ColourStop {
    float pos = 0.f;
    uint32_t argb = 0xFF000000;
};

struct DiceRamp {
    int count = 0;
    bool ordered = false;
    ColourStop stops[10] {};
};

inline uint32_t hsvToArgb(float h, float s, float v) {
    h = h - std::floor(h);
    s = std::clamp(s, 0.f, 1.f);
    v = std::clamp(v, 0.f, 1.f);
    const float c = v * s;
    const float x = c * (1.f - std::fabs(std::fmod(h * 6.f, 2.f) - 1.f));
    const float m = v - c;
    float r = 0.f, g = 0.f, b = 0.f;
    const int sector = static_cast<int>(h * 6.f) % 6;
    if (sector == 0) { r = c; g = x; }
    else if (sector == 1) { r = x; g = c; }
    else if (sector == 2) { g = c; b = x; }
    else if (sector == 3) { g = x; b = c; }
    else if (sector == 4) { r = x; b = c; }
    else { r = c; b = x; }
    const auto byte = [](float ch) { return static_cast<uint32_t>(std::lround(std::clamp(ch, 0.f, 1.f) * 255.f)); };
    return 0xFF000000u | (byte(r + m) << 16) | (byte(g + m) << 8) | byte(b + m);
}

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

inline void randomHsv(Pcg32& rng, const HsvLimit& lim, float& h, float& s, float& v) {
    h = lerp(lim.hue0, lim.hue1, rng.unit());
    s = lerp(lim.sat0, lim.sat1, rng.unit());
    v = lerp(lim.val0, lim.val1, rng.unit());
}

// UI-22. Ordered ramps walk evenly from one in-limit colour to another.
// Unordered ramps pin the ends and scatter the stops between them.
inline DiceRamp rollEqRamp(uint64_t seed, const HsvLimit& limit, bool ordered) {
    Pcg32 rng(seed);
    DiceRamp ramp;
    ramp.ordered = ordered;
    ramp.count = rng.nextInt(2, 11);
    if (ordered) {
        float h0, s0, v0, h1, s1, v1;
        randomHsv(rng, limit, h0, s0, v0);
        randomHsv(rng, limit, h1, s1, v1);
        if (rng.unit() < 0.5f) {
            std::swap(h0, h1);
            std::swap(s0, s1);
            std::swap(v0, v1);
        }
        for (int i = 0; i < ramp.count; ++i) {
            const float t = ramp.count == 1 ? 0.f : static_cast<float>(i) / static_cast<float>(ramp.count - 1);
            ramp.stops[i].pos = t;
            ramp.stops[i].argb = hsvToArgb(lerp(h0, h1, t), lerp(s0, s1, t), lerp(v0, v1, t));
        }
    } else {
        for (int i = 0; i < ramp.count; ++i) {
            float h, s, v;
            randomHsv(rng, limit, h, s, v);
            float pos = rng.unit();
            if (i == 0)
                pos = 0.f;
            else if (i == ramp.count - 1)
                pos = 1.f;
            ramp.stops[i].pos = pos;
            ramp.stops[i].argb = hsvToArgb(h, s, v);
        }
    }
    return ramp;
}

} // namespace ghovlz
