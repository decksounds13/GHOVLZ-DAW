#pragma once

#include <cmath>
#include <complex>
#include <vector>

namespace ghovlz {

inline void fftRadix2(std::vector<std::complex<float>>& a, bool inverse) {
    const int n = static_cast<int>(a.size());
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        const float ang = (inverse ? 2.f : -2.f) * 3.14159265f / static_cast<float>(len);
        const std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1.f, 0.f);
            for (int k = 0; k < len / 2; ++k) {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
    if (inverse) {
        const float inv = 1.f / static_cast<float>(n);
        for (auto& s : a)
            s *= inv;
    }
}

} // namespace ghovlz
