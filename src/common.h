#pragma once
#include <cmath>
#include <cstdint>

constexpr float PI = 3.14159265358979f;
constexpr float TAU = 2.0f * PI;
constexpr float DT = 1.0f / 60.0f;

// The game renders to this fixed low-resolution view, then scales it to the window.
constexpr int VIEW_W = 480;
constexpr int VIEW_H = 270;

// How a patch of ground behaves under a car.
enum class Surface : uint8_t { Road, Rough, Void, Water, Wall, Slick, Ramp, Sticky };

struct Vec2 {
    float x = 0, y = 0;
    Vec2() = default;
    Vec2(float ax, float ay) : x(ax), y(ay) {}
    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
};

inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float len2(Vec2 a) { return dot(a, a); }
inline float len(Vec2 a) { return std::sqrt(len2(a)); }
inline Vec2 norm(Vec2 a) { float l = len(a); return l > 1e-5f ? a * (1.0f / l) : Vec2{1, 0}; }
inline Vec2 fromAngle(float a) { return {std::cos(a), std::sin(a)}; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// Smallest signed difference a - b, in the range (-PI, PI].
inline float angleDiff(float a, float b) {
    float d = std::fmod(a - b, TAU);
    if (d > PI) d -= TAU;
    if (d <= -PI) d += TAU;
    return d;
}
