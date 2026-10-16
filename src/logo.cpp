// designElectronics — the logo: a glowing blue 5 mm LED on a dark tile.
// The same geometry (unit square, y down) feeds the on-screen drawing, the
// rasterised app icon and the SVG.
#include "logo.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "gfx.h"

namespace {

struct RGB { float r, g, b; };
constexpr RGB kTile{10, 20, 46}, kGlow{60, 140, 255}, kBody{58, 130, 250}, kBodyHot{170, 215, 255}, kRim{36, 92, 205},
              kLeg{196, 202, 214}, kChipCol{240, 248, 255};
constexpr float kCorner = 0.22f;

// geometry
constexpr float kDomeCx = 0.5f, kDomeCy = 0.36f, kDomeR = 0.165f;    // dome = half circle
constexpr float kBodyX0 = 0.335f, kBodyX1 = 0.665f, kBodyY1 = 0.62f;  // body below the dome
constexpr float kRimX0 = 0.305f, kRimX1 = 0.695f, kRimY0 = 0.60f, kRimY1 = 0.665f;
constexpr float kLegW = 0.036f;
const Vec2 kLegA0{0.43f, 0.66f}, kLegA1{0.43f, 0.89f};  // cathode (short)
const Vec2 kLegB0{0.57f, 0.66f}, kLegB1{0.57f, 0.94f};  // anode (long)
const Vec2 kDie{0.5f, 0.47f};
constexpr float kDieR = 0.04f;
const Vec2 kShine0{0.395f, 0.30f}, kShine1{0.395f, 0.52f};
constexpr float kShineW = 0.032f;

uint32_t col(RGB c, int a = 255) { return rgba((int)c.r, (int)c.g, (int)c.b, a); }
RGB mix(RGB a, RGB b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }

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
float sdBody(Vec2 p) {
    float dome = len(p - Vec2(kDomeCx, kDomeCy)) - kDomeR;
    float box = sdBox(p, kBodyX0, kDomeCy, kBodyX1, kBodyY1);
    return std::min(dome, box);
}

}  // namespace

void drawLogo(Gfx& g, Rect r) {
    float S = std::min(r.w, r.h);
    Vec2 o(r.x + (r.w - S) / 2, r.y + (r.h - S) / 2);
    auto P = [&](Vec2 p) { return o + p * S; };
    g.fillRoundRect(Rect(o.x, o.y, S, S), S * kCorner, col(kTile));
    // halo
    for (int i = 6; i >= 1; i--) g.fillCircle(P({0.5f, 0.44f}), S * (0.18f + 0.045f * (float)i), col(kGlow, 14));
    // legs, rim, body
    g.line(P(kLegA0), P(kLegA1), std::max(1.f, S * kLegW), col(kLeg));
    g.line(P(kLegB0), P(kLegB1), std::max(1.f, S * kLegW), col(kLeg));
    g.fillRoundRect(Rect(o.x + kRimX0 * S, o.y + kRimY0 * S, (kRimX1 - kRimX0) * S, (kRimY1 - kRimY0) * S), S * 0.015f, col(kRim));
    g.fillCircle(P({kDomeCx, kDomeCy}), S * kDomeR, col(kBody));
    g.fillRect(Rect(o.x + kBodyX0 * S, o.y + kDomeCy * S, (kBodyX1 - kBodyX0) * S, (kBodyY1 - kDomeCy) * S), col(kBody));
    // lit chip and shine
    g.fillCircle(P(kDie), S * kDieR * 3.2f, col(kBodyHot, 70));
    g.fillCircle(P(kDie), S * kDieR * 2.2f, col(kBodyHot, 140));
    g.fillCircle(P(kDie), S * kDieR * 1.2f, col(kChipCol, 230));
    g.line(P(kShine0), P(kShine1), std::max(1.f, S * kShineW), rgba(255, 255, 255, 120));
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
            float aTile = cover(sdBox(p, 0, 0, 1, 1, kCorner));
            RGB c = kTile;
            // halo around the lit LED
            float body = sdBody(p);
            c = mix(c, kGlow, 0.75f * std::exp(-std::max(body, 0.f) / 0.07f) * (body > 0 ? 1.f : 0.f));
            // legs and rim
            float leg = std::min(sdSeg(p, kLegA0, kLegA1, kLegW), sdSeg(p, kLegB0, kLegB1, kLegW));
            c = mix(c, kLeg, cover(leg));
            c = mix(c, kRim, cover(sdBox(p, kRimX0, kRimY0, kRimX1, kRimY1, 0.015f)));
            // body: brighter towards the chip
            float heat = std::exp(-len(p - kDie) / 0.13f);
            c = mix(c, mix(kBody, kBodyHot, 0.85f * heat), cover(body));
            c = mix(c, kChipCol, std::clamp(1.4f - len(p - kDie) / kDieR, 0.f, 1.f) * cover(body));
            c = mix(c, RGB{255, 255, 255}, 0.45f * cover(sdSeg(p, kShine0, kShine1, kShineW)));
            row[x * 4 + 0] = (uint8_t)std::clamp(c.r, 0.f, 255.f);
            row[x * 4 + 1] = (uint8_t)std::clamp(c.g, 0.f, 255.f);
            row[x * 4 + 2] = (uint8_t)std::clamp(c.b, 0.f, 255.f);
            row[x * 4 + 3] = (uint8_t)(aTile * 255.f + 0.5f);
        }
    }
    return surf;
}

std::string logoSvg() {
    auto hex = [](RGB c) { char b[8]; snprintf(b, sizeof b, "#%02x%02x%02x", (int)c.r, (int)c.g, (int)c.b); return std::string(b); };
    auto n = [](float v) { char b[16]; snprintf(b, sizeof b, "%.1f", v * 256); return std::string(b); };
    std::string s;
    s += "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 256 256\" width=\"256\" height=\"256\">\n";
    s += "  <title>designElectronics</title>\n  <defs>\n";
    s += "    <radialGradient id=\"halo\" cx=\"0.5\" cy=\"0.44\" r=\"0.42\"><stop offset=\"0.35\" stop-color=\"" + hex(kGlow) +
         "\" stop-opacity=\"0.75\"/><stop offset=\"1\" stop-color=\"" + hex(kGlow) + "\" stop-opacity=\"0\"/></radialGradient>\n";
    s += "    <radialGradient id=\"lit\" cx=\"" + n(kDie.x) + "\" cy=\"" + n(kDie.y) + "\" r=\"" + n(0.24f) +
         "\" gradientUnits=\"userSpaceOnUse\"><stop offset=\"0\" stop-color=\"" + hex(kBodyHot) + "\"/><stop offset=\"1\" stop-color=\"" +
         hex(kBody) + "\"/></radialGradient>\n";
    s += "    <radialGradient id=\"chip\"><stop offset=\"0.3\" stop-color=\"" + hex(kChipCol) + "\"/><stop offset=\"1\" stop-color=\"" +
         hex(kBodyHot) + "\" stop-opacity=\"0\"/></radialGradient>\n  </defs>\n";
    s += "  <rect width=\"256\" height=\"256\" rx=\"" + n(kCorner) + "\" fill=\"" + hex(kTile) + "\"/>\n";
    s += "  <rect width=\"256\" height=\"256\" rx=\"" + n(kCorner) + "\" fill=\"url(#halo)\"/>\n";
    s += "  <g stroke=\"" + hex(kLeg) + "\" stroke-width=\"" + n(kLegW) + "\" stroke-linecap=\"round\">\n";
    s += "    <line x1=\"" + n(kLegA0.x) + "\" y1=\"" + n(kLegA0.y) + "\" x2=\"" + n(kLegA1.x) + "\" y2=\"" + n(kLegA1.y) + "\"/>\n";
    s += "    <line x1=\"" + n(kLegB0.x) + "\" y1=\"" + n(kLegB0.y) + "\" x2=\"" + n(kLegB1.x) + "\" y2=\"" + n(kLegB1.y) + "\"/>\n  </g>\n";
    s += "  <rect x=\"" + n(kRimX0) + "\" y=\"" + n(kRimY0) + "\" width=\"" + n(kRimX1 - kRimX0) + "\" height=\"" + n(kRimY1 - kRimY0) +
         "\" rx=\"" + n(0.015f) + "\" fill=\"" + hex(kRim) + "\"/>\n";
    s += "  <path d=\"M" + n(kBodyX0) + " " + n(kBodyY1) + " V" + n(kDomeCy) + " A" + n(kDomeR) + " " + n(kDomeR) + " 0 0 1 " + n(kBodyX1) +
         " " + n(kDomeCy) + " V" + n(kBodyY1) + " Z\" fill=\"url(#lit)\"/>\n";
    s += "  <circle cx=\"" + n(kDie.x) + "\" cy=\"" + n(kDie.y) + "\" r=\"" + n(kDieR * 2.4f) + "\" fill=\"url(#chip)\"/>\n";
    s += "  <line x1=\"" + n(kShine0.x) + "\" y1=\"" + n(kShine0.y) + "\" x2=\"" + n(kShine1.x) + "\" y2=\"" + n(kShine1.y) +
         "\" stroke=\"#ffffff\" stroke-opacity=\"0.45\" stroke-width=\"" + n(kShineW) + "\" stroke-linecap=\"round\"/>\n";
    s += "</svg>\n";
    return s;
}
