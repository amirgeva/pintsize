#pragma once
// Small helpers for the procedural art: colours, noise and signed-distance shapes.
#include "common.h"
#include <algorithm>

namespace pix {

inline uint32_t rgb(int r, int g, int b) {
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

inline uint32_t mixc(uint32_t a, uint32_t b, float t) {
    t = clampf(t, 0, 1);
    int r = (int)lerpf((float)((a >> 16) & 255), (float)((b >> 16) & 255), t);
    int g = (int)lerpf((float)((a >> 8) & 255), (float)((b >> 8) & 255), t);
    int bl = (int)lerpf((float)(a & 255), (float)(b & 255), t);
    return rgb(r, g, bl);
}

inline uint32_t shade(uint32_t c, float f) {
    int r = std::min(255, (int)(((c >> 16) & 255) * f));
    int g = std::min(255, (int)(((c >> 8) & 255) * f));
    int b = std::min(255, (int)((c & 255) * f));
    return rgb(r, g, b);
}

inline float hash2(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xffff) / 65535.0f;
}

inline float vnoise(float x, float y) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    float a = lerpf(hash2(xi, yi), hash2(xi + 1, yi), fx);
    float b = lerpf(hash2(xi, yi + 1), hash2(xi + 1, yi + 1), fx);
    return lerpf(a, b, fy);
}

inline float fbm(float x, float y) {
    return vnoise(x, y) * 0.55f + vnoise(x * 2.1f + 7, y * 2.1f + 3) * 0.3f + vnoise(x * 4.3f + 1, y * 4.3f + 9) * 0.15f;
}

// Signed distance to a rounded box centred on the origin; negative inside.
inline float sdBox(float u, float v, float hu, float hv, float round = 0) {
    float qu = std::fabs(u) - hu + round, qv = std::fabs(v) - hv + round;
    float ou = std::max(qu, 0.0f), ov = std::max(qv, 0.0f);
    return std::sqrt(ou * ou + ov * ov) + std::min(std::max(qu, qv), 0.0f) - round;
}

// Signed distance to a capsule between two points.
inline float sdSeg(float u, float v, float u0, float v0, float u1, float v1, float r) {
    float pu = u - u0, pv = v - v0, du = u1 - u0, dv = v1 - v0;
    float t = clampf((pu * du + pv * dv) / (du * du + dv * dv), 0, 1);
    float eu = pu - du * t, ev = pv - dv * t;
    return std::sqrt(eu * eu + ev * ev) - r;
}

}  // namespace pix
