// designElectronics — the logo: a lit LED drawn as flat blue line art on a
// navy disc (pads on the legs, three rays of light). No gradients or glow.
// The same geometry (unit square, y down) feeds the on-screen drawing, the
// rasterised app icon and the SVG.
#include "logo.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "gfx.h"

namespace {

struct RGB { float r, g, b; };
constexpr RGB kDisc{10, 20, 46}, kInk{60, 140, 255};

// geometry
constexpr float kStroke = 0.05f;                                     // line width
constexpr float kDomeCx = 0.5f, kDomeCy = 0.42f, kDomeR = 0.14f;      // dome = half circle
constexpr float kBodyY1 = 0.58f;                                      // body sides run down to here
constexpr float kRimX0 = 0.32f, kRimX1 = 0.68f, kRimY0 = 0.58f, kRimY1 = 0.635f;
const Vec2 kLegA0{0.44f, 0.635f}, kLegA1{0.44f, 0.73f};  // cathode (short)
const Vec2 kLegB0{0.56f, 0.635f}, kLegB1{0.56f, 0.78f};  // anode (long)
constexpr float kPadR = 0.035f;                          // pad ring at each leg's end
constexpr float kRayR0 = 0.205f, kRayR1 = 0.275f;        // rays, measured from the dome centre
const float kRayDeg[] = {-90.f, -135.f, -45.f};

uint32_t col(RGB c, int a = 255) { return rgba((int)c.r, (int)c.g, (int)c.b, a); }
RGB mix(RGB a, RGB b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
Vec2 pad(Vec2 legEnd) { return {legEnd.x, legEnd.y + kPadR}; }
Vec2 ray(float deg, float r) {
    float a = deg * 3.14159265f / 180.f;
    return {kDomeCx + std::cos(a) * r, kDomeCy + std::sin(a) * r};
}

// signed distances (negative inside)
float sdBox(Vec2 p, float x0, float y0, float x1, float y1, float rad = 0) {
    Vec2 c((x0 + x1) / 2, (y0 + y1) / 2), h((x1 - x0) / 2 - rad, (y1 - y0) / 2 - rad);
    Vec2 q(std::fabs(p.x - c.x) - h.x, std::fabs(p.y - c.y) - h.y);
    return len(Vec2(std::max(q.x, 0.f), std::max(q.y, 0.f))) + std::min(std::max(q.x, q.y), 0.f) - rad;
}
float sdSeg(Vec2 p, Vec2 a, Vec2 b, float w) {
    Vec2 ab = b - a;
    float t = std::clamp(dot(p - a, ab) / std::max(1e-9f, dot(ab, ab)), 0.f, 1.f);
    return len(p - (a + ab * t)) - w / 2;
}
// everything drawn in white
float sdInk(Vec2 p) {
    float x0 = kDomeCx - kDomeR, x1 = kDomeCx + kDomeR;
    float dome = p.y <= kDomeCy ? std::fabs(len(p - Vec2(kDomeCx, kDomeCy)) - kDomeR) - kStroke / 2 : 1e9f;
    float d = std::min({dome, sdSeg(p, {x0, kDomeCy}, {x0, kBodyY1}, kStroke), sdSeg(p, {x1, kDomeCy}, {x1, kBodyY1}, kStroke)});
    d = std::min(d, sdBox(p, kRimX0, kRimY0, kRimX1, kRimY1, 0.012f));
    d = std::min({d, sdSeg(p, kLegA0, kLegA1, kStroke), sdSeg(p, kLegB0, kLegB1, kStroke)});
    for (Vec2 e : {kLegA1, kLegB1}) d = std::min(d, std::fabs(len(p - pad(e)) - kPadR) - kStroke / 2);
    for (float a : kRayDeg) d = std::min(d, sdSeg(p, ray(a, kRayR0), ray(a, kRayR1), kStroke));
    return d;
}

}  // namespace

void drawLogo(Gfx& g, Rect r, float opacity) {
    float S = std::min(r.w, r.h);
    Vec2 o(r.x + (r.w - S) / 2, r.y + (r.h - S) / 2);
    auto P = [&](Vec2 p) { return o + p * S; };
    int a = (int)std::lround(255 * opacity);
    uint32_t ink = col(kInk, a);
    float w = std::max(1.5f, S * kStroke);  // stays legible in the menu bar
    g.fillCircle(P({0.5f, 0.5f}), S * 0.5f, col(kDisc, a));
    // dome and body outline, open at the bottom where the rim sits
    std::vector<Vec2> outline;
    outline.push_back(P({kDomeCx - kDomeR, kBodyY1}));
    for (int i = 0; i <= 24; i++) {
        float t = 3.14159265f * (1.f + (float)i / 24.f);
        outline.push_back(P({kDomeCx + std::cos(t) * kDomeR, kDomeCy + std::sin(t) * kDomeR}));
    }
    outline.push_back(P({kDomeCx + kDomeR, kBodyY1}));
    g.polyline(outline, w, ink);
    g.fillRoundRect(Rect(o.x + kRimX0 * S, o.y + kRimY0 * S, (kRimX1 - kRimX0) * S, (kRimY1 - kRimY0) * S), S * 0.012f, ink);
    g.line(P(kLegA0), P(kLegA1), w, ink, false);
    g.line(P(kLegB0), P(kLegB1), w, ink, false);
    g.strokeCircle(P(pad(kLegA1)), S * kPadR, w, ink);
    g.strokeCircle(P(pad(kLegB1)), S * kPadR, w, ink);
    for (float d : kRayDeg) g.line(P(ray(d, kRayR0)), P(ray(d, kRayR1)), w, ink);
}

SDL_Surface* makeLogoSurface(int size) {
    SDL_Surface* surf = SDL_CreateSurface(size, size, SDL_PIXELFORMAT_RGBA32);
    if (!surf) return nullptr;
    const float px = 1.f / (float)size;
    auto cover = [&](float sd) { return std::clamp(0.5f - sd / px, 0.f, 1.f); };
    for (int y = 0; y < size; y++) {
        auto* row = (uint8_t*)surf->pixels + (size_t)y * (size_t)surf->pitch;
        for (int x = 0; x < size; x++) {
            Vec2 p(((float)x + 0.5f) * px, ((float)y + 0.5f) * px);
            float aDisc = cover(len(p - Vec2(0.5f, 0.5f)) - 0.5f);
            RGB c = mix(kDisc, kInk, cover(sdInk(p)));
            row[x * 4 + 0] = (uint8_t)std::clamp(c.r, 0.f, 255.f);
            row[x * 4 + 1] = (uint8_t)std::clamp(c.g, 0.f, 255.f);
            row[x * 4 + 2] = (uint8_t)std::clamp(c.b, 0.f, 255.f);
            row[x * 4 + 3] = (uint8_t)(aDisc * 255.f + 0.5f);
        }
    }
    return surf;
}

std::string logoSvg() {
    auto hex = [](RGB c) { char b[8]; snprintf(b, sizeof b, "#%02x%02x%02x", (int)c.r, (int)c.g, (int)c.b); return std::string(b); };
    auto n = [](float v) { char b[16]; snprintf(b, sizeof b, "%.1f", v * 256); return std::string(b); };
    auto ln = [&](Vec2 a, Vec2 b) {
        return "    <line x1=\"" + n(a.x) + "\" y1=\"" + n(a.y) + "\" x2=\"" + n(b.x) + "\" y2=\"" + n(b.y) + "\"/>\n";
    };
    std::string s;
    s += "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 256 256\" width=\"256\" height=\"256\">\n";
    s += "  <title>designElectronics</title>\n";
    s += "  <circle cx=\"128\" cy=\"128\" r=\"128\" fill=\"" + hex(kDisc) + "\"/>\n";
    s += "  <g fill=\"none\" stroke=\"" + hex(kInk) + "\" stroke-width=\"" + n(kStroke) + "\" stroke-linejoin=\"round\">\n";
    s += "    <path d=\"M" + n(kDomeCx - kDomeR) + " " + n(kBodyY1) + " V" + n(kDomeCy) + " A" + n(kDomeR) + " " + n(kDomeR) + " 0 0 1 " +
         n(kDomeCx + kDomeR) + " " + n(kDomeCy) + " V" + n(kBodyY1) + "\"/>\n";
    s += ln(kLegA0, kLegA1) + ln(kLegB0, kLegB1);
    for (Vec2 e : {kLegA1, kLegB1}) s += "    <circle cx=\"" + n(pad(e).x) + "\" cy=\"" + n(pad(e).y) + "\" r=\"" + n(kPadR) + "\"/>\n";
    s += "  </g>\n  <g stroke=\"" + hex(kInk) + "\" stroke-width=\"" + n(kStroke) + "\" stroke-linecap=\"round\">\n";
    for (float a : kRayDeg) s += ln(ray(a, kRayR0), ray(a, kRayR1));
    s += "  </g>\n  <g fill=\"" + hex(kInk) + "\">\n";
    s += "    <rect x=\"" + n(kRimX0) + "\" y=\"" + n(kRimY0) + "\" width=\"" + n(kRimX1 - kRimX0) + "\" height=\"" + n(kRimY1 - kRimY0) +
         "\" rx=\"" + n(0.012f) + "\"/>\n  </g>\n";
    s += "</svg>\n";
    return s;
}
