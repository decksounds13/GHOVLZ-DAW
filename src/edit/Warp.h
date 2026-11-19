#pragma once

#include "model/Project.h"

#include <algorithm>
#include <cmath>

namespace ghovlz {

// Off keeps one timeline second equal to one media second.
// Repitch locks the clip to the media tempo, so a faster project plays the
// same content in fewer seconds and the pitch moves with it.
// Stretch uses the same tempo lock, then reads each grain at the original rate.
inline double warpedPosition(WarpMode mode, double contentQ, double projectBpm, double mediaBpm, int mediaRate) {
    const double bpm = mode == WarpMode::Off ? projectBpm : mediaBpm;
    return contentQ * (static_cast<double>(std::max(1, mediaRate)) * 60.0 / std::max(1.0, bpm));
}

inline double stretchSource(double warpedPos, double projectBpm, double mediaBpm, int grain = 512) {
    const double rate = std::max(1.0, projectBpm) / std::max(1.0, mediaBpm);
    const int g = std::max(32, grain);
    const double pos = std::max(0.0, warpedPos);
    const int grainIndex = static_cast<int>(pos / g);
    const double within = pos - static_cast<double>(grainIndex) * g;
    return static_cast<double>(grainIndex) * g * rate + within;
}

inline float clipEnvelope(double localQ, double lengthQ, double fadeInQ, double fadeOutQ) {
    float g = 1.f;
    if (fadeInQ > 1e-6 && localQ < fadeInQ)
        g *= static_cast<float>(std::clamp(localQ / fadeInQ, 0.0, 1.0));
    const double tail = lengthQ - localQ;
    if (fadeOutQ > 1e-6 && tail < fadeOutQ)
        g *= static_cast<float>(std::clamp(tail / fadeOutQ, 0.0, 1.0));
    const float x = std::clamp(g, 0.f, 1.f);
    return std::sin(x * 1.5707963f);
}

inline bool inScale(int pitch, int root, bool minor) {
    static const int major[] = {0, 2, 4, 5, 7, 9, 11};
    static const int minor[] = {0, 2, 3, 5, 7, 8, 10};
    const int pc = ((pitch % 12) - (root % 12) + 12) % 12;
    const int* scale = minor ? minor : major;
    for (int i = 0; i < 7; ++i)
        if (scale[i] == pc)
            return true;
    return false;
}

inline double snapQuarter(double q, double grid) {
    if (grid <= 1e-9)
        return q;
    return std::round(q / grid) * grid;
}

} // namespace ghovlz
