// designElectronics — geometry helpers.
// World units are mils (1/1000 inch). Breadboard pitch = 100 mil.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

constexpr uint32_t rgba(int r, int g, int b, int a = 255) {
    return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | (uint32_t)a;
}

struct Vec2 {
    float x = 0, y = 0;
    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}
    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
    bool operator==(Vec2 o) const { return x == o.x && y == o.y; }
    bool operator!=(Vec2 o) const { return !(*this == o); }
};

inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
inline float len(Vec2 a) { return std::sqrt(dot(a, a)); }
inline float dist(Vec2 a, Vec2 b) { return len(a - b); }
inline Vec2 normalize(Vec2 a) { float l = len(a); return l > 1e-9f ? a / l : Vec2(0, 0); }
inline Vec2 perp(Vec2 a) { return {-a.y, a.x}; }
inline Vec2 lerp(Vec2 a, Vec2 b, float t) { return a + (b - a) * t; }
inline float snapf(float v, float g) { return g > 0 ? std::round(v / g) * g : v; }
inline Vec2 snapv(Vec2 v, float g) { return {snapf(v.x, g), snapf(v.y, g)}; }

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    Rect() = default;
    Rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}
    bool contains(Vec2 p) const { return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h; }
    Vec2 center() const { return {x + w / 2, y + h / 2}; }
    float r() const { return x + w; }
    float b() const { return y + h; }
    Rect inset(float d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    bool overlaps(const Rect& o) const { return x < o.r() && o.x < r() && y < o.b() && o.y < b(); }
    static Rect fromPoints(Vec2 a, Vec2 b) {
        return {std::min(a.x, b.x), std::min(a.y, b.y), std::fabs(a.x - b.x), std::fabs(a.y - b.y)};
    }
    Rect united(const Rect& o) const {
        if (w <= 0 && h <= 0) return o;
        float nx = std::min(x, o.x), ny = std::min(y, o.y);
        return {nx, ny, std::max(r(), o.r()) - nx, std::max(b(), o.b()) - ny};
    }
    Rect expandTo(Vec2 p) const {
        float nx = std::min(x, p.x), ny = std::min(y, p.y);
        return {nx, ny, std::max(r(), p.x) - nx, std::max(b(), p.y) - ny};
    }
};

inline float distPointSeg(Vec2 p, Vec2 a, Vec2 b) {
    Vec2 ab = b - a;
    float l2 = dot(ab, ab);
    if (l2 < 1e-12f) return dist(p, a);
    float t = std::clamp(dot(p - a, ab) / l2, 0.0f, 1.0f);
    return dist(p, a + ab * t);
}

inline Vec2 closestOnSeg(Vec2 p, Vec2 a, Vec2 b) {
    Vec2 ab = b - a;
    float l2 = dot(ab, ab);
    if (l2 < 1e-12f) return a;
    float t = std::clamp(dot(p - a, ab) / l2, 0.0f, 1.0f);
    return a + ab * t;
}

inline bool segsIntersect(Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    float d1 = cross(b - a, c - a), d2 = cross(b - a, d - a);
    float d3 = cross(d - c, a - c), d4 = cross(d - c, b - c);
    return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
}

inline float distSegSeg(Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    if (segsIntersect(a, b, c, d)) return 0;
    return std::min(std::min(distPointSeg(a, c, d), distPointSeg(b, c, d)),
                    std::min(distPointSeg(c, a, b), distPointSeg(d, a, b)));
}

// Placement of a part in one view: translate, then rotate in 90° steps, optional mirror.
struct Xform {
    Vec2 pos;
    int rot = 0;      // quarter turns clockwise (screen coords, y down)
    bool flip = false; // mirror horizontally (applied before rotation)

    Vec2 dir(Vec2 p) const {
        if (flip) p.x = -p.x;
        switch (((rot % 4) + 4) % 4) {
            case 1: return {-p.y, p.x};
            case 2: return {-p.x, -p.y};
            case 3: return {p.y, -p.x};
            default: return p;
        }
    }
    Vec2 apply(Vec2 p) const { return pos + dir(p); }
    Vec2 inverse(Vec2 w) const {
        Vec2 p = w - pos;
        switch (((rot % 4) + 4) % 4) {
            case 1: p = {p.y, -p.x}; break;
            case 2: p = {-p.x, -p.y}; break;
            case 3: p = {-p.y, p.x}; break;
            default: break;
        }
        if (flip) p.x = -p.x;
        return p;
    }
    Rect applyRect(const Rect& r) const {
        Vec2 a = apply({r.x, r.y}), b = apply({r.r(), r.b()});
        return Rect::fromPoints(a, b);
    }
};
