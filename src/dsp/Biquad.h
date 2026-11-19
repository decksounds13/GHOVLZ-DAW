#pragma once

#include <algorithm>
#include <cmath>

namespace ghovlz {

// Transposed direct-form II biquad. Coefficient recipes match the Audio EQ
// Cookbook (RBJ) forms, including the a0 normalisation.
struct Biquad {
    float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f;
    float z1 = 0.f, z2 = 0.f;

    void reset() { z1 = z2 = 0.f; }

    void set(float nb0, float nb1, float nb2, float na0, float na1, float na2) {
        const float inv = na0 == 0.f ? 0.f : 1.f / na0;
        b0 = nb0 * inv;
        b1 = nb1 * inv;
        b2 = nb2 * inv;
        a1 = na1 * inv;
        a2 = na2 * inv;
    }

    inline float tick(float x) {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        if (std::abs(z1) < 1.0e-20f) z1 = 0.f;
        if (std::abs(z2) < 1.0e-20f) z2 = 0.f;
        return y;
    }

    static Biquad lowPass(double sr, float freq, float Q = 0.70710678f) {
        freq = std::clamp(freq, 2.f, static_cast<float>(sr * 0.49));
        const float n = 1.f / std::tan(3.14159265f * freq / static_cast<float>(sr));
        const float n2 = n * n;
        const float invQ = 1.f / Q;
        const float c1 = 1.f / (1.f + invQ * n + n2);
        Biquad b;
        b.set(c1, c1 * 2.f, c1, 1.f, c1 * 2.f * (1.f - n2), c1 * (1.f - invQ * n + n2));
        return b;
    }
    static Biquad highPass(double sr, float freq, float Q = 0.70710678f) {
        freq = std::clamp(freq, 2.f, static_cast<float>(sr * 0.49));
        const float n = std::tan(3.14159265f * freq / static_cast<float>(sr));
        const float n2 = n * n;
        const float invQ = 1.f / Q;
        const float c1 = 1.f / (1.f + invQ * n + n2);
        Biquad b;
        b.set(c1, c1 * -2.f, c1, 1.f, c1 * 2.f * (n2 - 1.f), c1 * (1.f - invQ * n + n2));
        return b;
    }
    static Biquad notch(double sr, float freq, float Q) {
        freq = std::clamp(freq, 2.f, static_cast<float>(sr * 0.49));
        const float n = 1.f / std::tan(3.14159265f * freq / static_cast<float>(sr));
        const float n2 = n * n;
        const float invQ = 1.f / Q;
        const float c1 = 1.f / (1.f + n * invQ + n2);
        const float b0 = c1 * (1.f + n2);
        const float b1 = 2.f * c1 * (1.f - n2);
        Biquad b;
        b.set(b0, b1, b0, 1.f, b1, c1 * (1.f - n * invQ + n2));
        return b;
    }
    static Biquad bandPass(double sr, float freq, float Q) {
        freq = std::clamp(freq, 2.f, static_cast<float>(sr * 0.49));
        const float n = 1.f / std::tan(3.14159265f * freq / static_cast<float>(sr));
        const float n2 = n * n;
        const float invQ = 1.f / Q;
        const float c1 = 1.f / (1.f + invQ * n + n2);
        Biquad b;
        b.set(c1 * n * invQ, 0.f, -c1 * n * invQ, 1.f, c1 * 2.f * (1.f - n2), c1 * (1.f - invQ * n + n2));
        return b;
    }
    static Biquad allPass(double sr, float freq, float Q) {
        freq = std::clamp(freq, 2.f, static_cast<float>(sr * 0.49));
        const float n = 1.f / std::tan(3.14159265f * freq / static_cast<float>(sr));
        const float n2 = n * n;
        const float invQ = 1.f / Q;
        const float c1 = 1.f / (1.f + invQ * n + n2);
        const float b0 = c1 * (1.f - n * invQ + n2);
        const float b1 = c1 * 2.f * (1.f - n2);
        Biquad b;
        b.set(b0, b1, 1.f, 1.f, b1, b0);
        return b;
    }
    static Biquad peak(double sr, float freq, float Q, float gainDb) {
        freq = std::clamp(freq, 2.f, static_cast<float>(sr * 0.49));
        const float A = std::sqrt(std::pow(10.f, gainDb / 20.f));
        const float omega = 2.f * 3.14159265f * freq / static_cast<float>(sr);
        const float alpha = std::sin(omega) / (Q * 2.f);
        const float c2 = -2.f * std::cos(omega);
        Biquad b;
        b.set(1.f + alpha * A, c2, 1.f - alpha * A, 1.f + alpha / A, c2, 1.f - alpha / A);
        return b;
    }
    static Biquad lowShelf(double sr, float freq, float Q, float gainDb) {
        freq = std::clamp(freq, 2.f, static_cast<float>(sr * 0.49));
        const float A = std::sqrt(std::pow(10.f, gainDb / 20.f));
        const float am1 = A - 1.f, ap1 = A + 1.f;
        const float omega = 2.f * 3.14159265f * freq / static_cast<float>(sr);
        const float coso = std::cos(omega);
        const float beta = std::sin(omega) * std::sqrt(A) / Q;
        const float amc = am1 * coso;
        Biquad b;
        b.set(A * (ap1 - amc + beta), A * 2.f * (am1 - ap1 * coso), A * (ap1 - amc - beta),
              ap1 + amc + beta, -2.f * (am1 + ap1 * coso), ap1 + amc - beta);
        return b;
    }
    static Biquad highShelf(double sr, float freq, float Q, float gainDb) {
        freq = std::clamp(freq, 2.f, static_cast<float>(sr * 0.49));
        const float A = std::sqrt(std::pow(10.f, gainDb / 20.f));
        const float am1 = A - 1.f, ap1 = A + 1.f;
        const float omega = 2.f * 3.14159265f * freq / static_cast<float>(sr);
        const float coso = std::cos(omega);
        const float beta = std::sin(omega) * std::sqrt(A) / Q;
        const float amc = am1 * coso;
        Biquad b;
        b.set(A * (ap1 + amc + beta), A * -2.f * (am1 + ap1 * coso), A * (ap1 + amc - beta),
              ap1 - amc + beta, 2.f * (am1 - ap1 * coso), ap1 - amc - beta);
        return b;
    }

    float magnitude(double sr, float freq) const {
        const float w = 2.f * 3.14159265f * freq / static_cast<float>(sr);
        const float c1 = std::cos(w), s1 = std::sin(w);
        const float c2 = std::cos(2.f * w), s2 = std::sin(2.f * w);
        const float nr = b0 + b1 * c1 + b2 * c2;
        const float ni = -(b1 * s1 + b2 * s2);
        const float dr = 1.f + a1 * c1 + a2 * c2;
        const float di = -(a1 * s1 + a2 * s2);
        return std::sqrt((nr * nr + ni * ni) / std::max(1.0e-12f, dr * dr + di * di));
    }
};

} // namespace ghovlz
