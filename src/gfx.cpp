#include "gfx.h"

#include <cmath>

static const char* kFontFiles[2] = {"Lato-Regular.ttf", "SourceCodePro-Regular.ttf"};

static bool loadFile(const std::string& path, std::vector<unsigned char>& out) {
    size_t size = 0;
    void* d = SDL_LoadFile(path.c_str(), &size);
    if (!d) return false;
    out.assign((unsigned char*)d, (unsigned char*)d + size);
    SDL_free(d);
    return true;
}

bool Gfx::init(SDL_Renderer* r) {
    r_ = r;
    if (!TTF_Init()) {
        SDL_Log("TTF_Init failed: %s — using debug font", SDL_GetError());
        return true;
    }
    const char* base = SDL_GetBasePath();
    std::string b = base ? base : "";
    for (int f = 0; f < 2; f++) {
        // Desktop: next to the executable / in the macOS bundle Resources dir.
        // Android: the APK assets (SDL IO reads them with a relative path).
        const std::string candidates[] = {
            b + "assets/fonts/" + kFontFiles[f],
            b + "../Resources/assets/fonts/" + kFontFiles[f],
            b + "../assets/fonts/" + kFontFiles[f],
            std::string("assets/fonts/") + kFontFiles[f],
            std::string("fonts/") + kFontFiles[f],
        };
        for (auto& c : candidates) if (loadFile(c, fontData_[f])) break;
        if (fontData_[f].empty()) SDL_Log("Font %s not found", kFontFiles[f]);
    }
    // Fallbacks for symbols (Ω, µ, arrows, …) the bundled Latin fonts lack.
    const char* sys[] = {
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf", "/System/Library/Fonts/Helvetica.ttc",
        "/System/Library/Fonts/Apple Symbols.ttf", "/System/Library/Fonts/Menlo.ttc",
        "/system/fonts/Roboto-Regular.ttf", "/system/fonts/NotoSansSymbols-Regular-Subsetted.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/seguisym.ttf"};
    for (const char* p : sys) {
        std::vector<unsigned char> d;
        if (loadFile(p, d)) fallbackData_.push_back(std::move(d));
    }
    if (fontData_[1].empty()) fontData_[1] = fontData_[0];
    if (fontData_[0].empty()) fontData_[0] = fontData_[1];
    return true;
}

void Gfx::shutdown() {
    flush();
    for (auto& kv : textCache_) SDL_DestroyTexture(kv.second.tex);
    textCache_.clear();
    lru_.clear();
    for (auto& kv : fonts_) TTF_CloseFont(kv.second);
    fonts_.clear();
    for (auto* f : fallbackFonts_) TTF_CloseFont(f);
    fallbackFonts_.clear();
    TTF_Quit();
}

TTF_Font* Gfx::font(FontFace face, int px) {
    if (fontData_[face].empty()) return nullptr;
    int key = face * 1000 + px;
    auto it = fonts_.find(key);
    if (it != fonts_.end()) return it->second;
    SDL_IOStream* io = SDL_IOFromConstMem(fontData_[face].data(), fontData_[face].size());
    TTF_Font* f = TTF_OpenFontIO(io, true, (float)px);
    if (f) {
        TTF_SetFontHinting(f, TTF_HINTING_LIGHT);
        // chain: other bundled face, then system fonts
        std::vector<const std::vector<unsigned char>*> chain = {&fontData_[1 - face]};
        for (auto& d : fallbackData_) chain.push_back(&d);
        for (auto* d : chain) {
            if (d->empty() || d == &fontData_[face]) continue;
            TTF_Font* fb = TTF_OpenFontIO(SDL_IOFromConstMem(d->data(), d->size()), true, (float)px);
            if (fb) { TTF_AddFallbackFont(f, fb); fallbackFonts_.push_back(fb); }
        }
    }
    fonts_[key] = f;
    return f;
}

Gfx::TextEntry* Gfx::textEntry(const std::string& s, int px, FontFace face) {
    std::string key;
    key.reserve(s.size() + 8);
    key += (char)('0' + face);
    key += std::to_string(px);
    key += '|';
    key += s;
    auto it = textCache_.find(key);
    if (it != textCache_.end()) {
        lru_.splice(lru_.begin(), lru_, it->second.lru);
        return &it->second;
    }
    TTF_Font* f = font(face, px);
    if (!f) return nullptr;
    SDL_Surface* surf = TTF_RenderText_Blended(f, s.c_str(), s.size(), SDL_Color{255, 255, 255, 255});
    if (!surf) return nullptr;
    TextEntry e;
    e.tex = SDL_CreateTextureFromSurface(r_, surf);
    e.w = (float)surf->w;
    e.h = (float)surf->h;
    SDL_DestroySurface(surf);
    if (!e.tex) return nullptr;
    SDL_SetTextureScaleMode(e.tex, SDL_SCALEMODE_LINEAR);
    lru_.push_front(key);
    e.lru = lru_.begin();
    auto& ref = textCache_[key] = e;
    while (textCache_.size() > 1500) {
        auto& old = lru_.back();
        auto oit = textCache_.find(old);
        if (oit != textCache_.end()) { SDL_DestroyTexture(oit->second.tex); textCache_.erase(oit); }
        lru_.pop_back();
    }
    return &ref;
}

float Gfx::textWidth(const std::string& s, float px, FontFace face) {
    if (s.empty()) return 0;
    int ipx = std::max(4, (int)std::lround(px));
    TTF_Font* f = font(face, ipx);
    if (!f) return (float)s.size() * px * 0.6f;
    int w = 0, h = 0;
    TTF_GetStringSize(f, s.c_str(), s.size(), &w, &h);
    return (float)w;
}

float Gfx::text(Vec2 pos, const std::string& s, float px, uint32_t col, int align, FontFace face, float angleDeg) {
    if (s.empty() || px < 3.5f || (col & 0xFF) == 0) return 0;
    if (px > 600) px = 600;
    TextEntry* e = hasFonts() ? textEntry(s, (int)std::lround(px), face) : nullptr;
    if (!e) {
        // Fallback: SDL's built-in 8x8 debug font, scaled.
        flush();
        float scale = std::max(1.0f, px / 10.0f);
        float w = (float)s.size() * 8 * scale;
        float x = align == 1 ? pos.x - w / 2 : align == 2 ? pos.x - w : pos.x;
        SDL_SetRenderDrawColor(r_, (Uint8)(col >> 24), (Uint8)(col >> 16), (Uint8)(col >> 8), (Uint8)col);
        SDL_SetRenderScale(r_, scale, scale);
        SDL_RenderDebugText(r_, x / scale, (pos.y - 4 * scale) / scale, s.c_str());
        SDL_SetRenderScale(r_, 1, 1);
        return w;
    }
    flush();
    SDL_SetTextureColorMod(e->tex, (Uint8)(col >> 24), (Uint8)(col >> 16), (Uint8)(col >> 8));
    SDL_SetTextureAlphaMod(e->tex, (Uint8)col);
    float ox = align == 1 ? -e->w / 2 : align == 2 ? -e->w : 0;
    if (angleDeg == 0) {
        SDL_FRect dst{std::round(pos.x + ox), std::round(pos.y - e->h / 2), e->w, e->h};
        SDL_RenderTexture(r_, e->tex, nullptr, &dst);
    } else {
        SDL_FRect dst{pos.x + ox, pos.y - e->h / 2, e->w, e->h};
        SDL_FPoint center{-ox, e->h / 2};
        SDL_RenderTextureRotated(r_, e->tex, nullptr, &dst, angleDeg, &center, SDL_FLIP_NONE);
    }
    return e->w;
}

// ---------------------------------------------------------------------------
// Geometry batching
// ---------------------------------------------------------------------------

void Gfx::flush() {
    if (v_.empty()) return;
    SDL_RenderGeometry(r_, nullptr, v_.data(), (int)v_.size(), nullptr, 0);
    v_.clear();
}

void Gfx::setClip(const Rect* r) {
    flush();
    if (!r) { SDL_SetRenderClipRect(r_, nullptr); return; }
    SDL_Rect c{(int)std::floor(r->x), (int)std::floor(r->y), (int)std::ceil(r->w), (int)std::ceil(r->h)};
    SDL_SetRenderClipRect(r_, &c);
}

void Gfx::vtx(Vec2 p, uint32_t c) {
    SDL_Vertex v;
    v.position = {p.x, p.y};
    v.color = {((c >> 24) & 0xFF) / 255.f, ((c >> 16) & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, (c & 0xFF) / 255.f};
    v.tex_coord = {0, 0};
    v_.push_back(v);
}

void Gfx::tri(Vec2 a, Vec2 b, Vec2 c, uint32_t ca, uint32_t cb, uint32_t cc) {
    vtx(a, ca); vtx(b, cb); vtx(c, cc);
    if (v_.size() > 60000) flush();
}

void Gfx::quad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, uint32_t ca, uint32_t cb, uint32_t cc, uint32_t cd) {
    tri(a, b, c, ca, cb, cc);
    tri(a, c, d, ca, cc, cd);
}

void Gfx::fillRect(Rect r, uint32_t c) {
    if (r.w <= 0 || r.h <= 0 || (c & 0xFF) == 0) return;
    Vec2 a{r.x, r.y}, b{r.r(), r.y}, cc{r.r(), r.b()}, d{r.x, r.b()};
    quad(a, b, cc, d, c, c, c, c);
}

void Gfx::strokeRect(Rect r, float w, uint32_t c) {
    fillRect({r.x, r.y, r.w, w}, c);
    fillRect({r.x, r.b() - w, r.w, w}, c);
    fillRect({r.x, r.y + w, w, r.h - 2 * w}, c);
    fillRect({r.r() - w, r.y + w, w, r.h - 2 * w}, c);
}

void Gfx::fillRoundRect(Rect r, float rad, uint32_t c) {
    rad = std::min(rad, std::min(r.w, r.h) / 2);
    if (rad < 0.75f) { fillRect(r, c); return; }
    fillRect({r.x + rad, r.y, r.w - 2 * rad, r.h}, c);
    fillRect({r.x, r.y + rad, rad, r.h - 2 * rad}, c);
    fillRect({r.r() - rad, r.y + rad, rad, r.h - 2 * rad}, c);
    // quarter circles with AA fringe
    Vec2 cs[4] = {{r.x + rad, r.y + rad}, {r.r() - rad, r.y + rad}, {r.r() - rad, r.b() - rad}, {r.x + rad, r.b() - rad}};
    float start[4] = {180, 270, 0, 90};
    uint32_t t = withAlpha(c, 0);
    int n = std::max(4, std::min(24, (int)(rad * 0.5f)));
    for (int k = 0; k < 4; k++) {
        for (int i = 0; i < n; i++) {
            float a0 = (start[k] + 90.f * i / n) * (float)M_PI / 180, a1 = (start[k] + 90.f * (i + 1) / n) * (float)M_PI / 180;
            Vec2 d0{std::cos(a0), std::sin(a0)}, d1{std::cos(a1), std::sin(a1)};
            Vec2 i0 = cs[k] + d0 * (rad - 0.5f), i1 = cs[k] + d1 * (rad - 0.5f);
            tri(cs[k], i0, i1, c, c, c);
            quad(i0, cs[k] + d0 * (rad + 0.5f), cs[k] + d1 * (rad + 0.5f), i1, c, t, t, c);
        }
    }
}

void Gfx::strokeRoundRect(Rect r, float rad, float w, uint32_t c) {
    rad = std::min(rad, std::min(r.w, r.h) / 2);
    if (rad < 0.75f) { strokeRect(r, w, c); return; }
    float h = w / 2;
    line({r.x + rad, r.y + h}, {r.r() - rad, r.y + h}, w, c, false);
    line({r.x + rad, r.b() - h}, {r.r() - rad, r.b() - h}, w, c, false);
    line({r.x + h, r.y + rad}, {r.x + h, r.b() - rad}, w, c, false);
    line({r.r() - h, r.y + rad}, {r.r() - h, r.b() - rad}, w, c, false);
    arc({r.x + rad, r.y + rad}, rad - h, 180, 270, w, c);
    arc({r.r() - rad, r.y + rad}, rad - h, 270, 360, w, c);
    arc({r.r() - rad, r.b() - rad}, rad - h, 0, 90, w, c);
    arc({r.x + rad, r.b() - rad}, rad - h, 90, 180, w, c);
}

void Gfx::segment(Vec2 a, Vec2 b, float w, uint32_t c) {
    Vec2 d = b - a;
    float l = len(d);
    if (l < 1e-4f) return;
    Vec2 n = perp(d / l);
    float hw = w / 2;
    uint32_t core = c;
    if (hw < 0.5f) { core = withAlpha(c, (int)((c & 0xFF) * hw * 2)); hw = 0.5f; }
    uint32_t t = withAlpha(c, 0);
    float in = hw - 0.5f, out = hw + 0.5f;
    if (in > 0) quad(a + n * in, b + n * in, b - n * in, a - n * in, core, core, core, core);
    quad(a + n * in, a + n * out, b + n * out, b + n * in, core, t, t, core);
    quad(a - n * in, a - n * out, b - n * out, b - n * in, core, t, t, core);
}

void Gfx::line(Vec2 a, Vec2 b, float w, uint32_t c, bool roundCaps) {
    if ((c & 0xFF) == 0) return;
    segment(a, b, w, c);
    if (roundCaps && w > 2.5f) { fillCircle(a, w / 2, c); fillCircle(b, w / 2, c); }
}

void Gfx::polyline(const std::vector<Vec2>& pts, float w, uint32_t c, bool closed) {
    if (pts.size() < 2) return;
    for (size_t i = 0; i + 1 < pts.size(); i++) segment(pts[i], pts[i + 1], w, c);
    if (closed) segment(pts.back(), pts.front(), w, c);
    if (w > 2.5f) for (auto& p : pts) fillCircle(p, w / 2, c);
}

void Gfx::dashedLine(Vec2 a, Vec2 b, float w, float dash, float gap, uint32_t c) {
    float l = dist(a, b);
    if (l < 1e-3f) return;
    Vec2 d = (b - a) / l;
    for (float t = 0; t < l; t += dash + gap) segment(a + d * t, a + d * std::min(l, t + dash), w, c);
}

void Gfx::fillCircle(Vec2 c, float r, uint32_t col) {
    if (r <= 0 || (col & 0xFF) == 0) return;
    int n = std::max(8, std::min(128, (int)(r * 1.1f) + 6));
    uint32_t t = withAlpha(col, 0);
    float ri = std::max(0.f, r - 0.5f), ro = r + 0.5f;
    Vec2 prevI, prevO;
    for (int i = 0; i <= n; i++) {
        float a = (float)i / n * 2 * (float)M_PI;
        Vec2 d{std::cos(a), std::sin(a)};
        Vec2 pi = c + d * ri, po = c + d * ro;
        if (i > 0) {
            tri(c, prevI, pi, col, col, col);
            quad(prevI, prevO, po, pi, col, t, t, col);
        }
        prevI = pi; prevO = po;
    }
}

void Gfx::strokeCircle(Vec2 c, float r, float w, uint32_t col) { arc(c, r, 0, 360, w, col); }

void Gfx::arc(Vec2 c, float r, float a0, float a1, float w, uint32_t col) {
    if ((col & 0xFF) == 0 || r <= 0) return;
    float span = a1 - a0;
    int n = std::max(6, std::min(160, (int)(std::fabs(span) / 360.f * (r * 1.1f + 8))));
    float hw = w / 2;
    uint32_t core = col;
    if (hw < 0.5f) { core = withAlpha(col, (int)((col & 0xFF) * hw * 2)); hw = 0.5f; }
    uint32_t t = withAlpha(col, 0);
    float rs[4] = {r - hw - 0.5f, r - hw + 0.5f, r + hw - 0.5f, r + hw + 0.5f};
    if (rs[1] > rs[2]) rs[1] = rs[2] = r;
    rs[0] = std::max(0.f, rs[0]);
    Vec2 prev[4];
    for (int i = 0; i <= n; i++) {
        float a = (a0 + span * i / n) * (float)M_PI / 180;
        Vec2 d{std::cos(a), std::sin(a)};
        Vec2 p[4];
        for (int k = 0; k < 4; k++) p[k] = c + d * rs[k];
        if (i > 0) {
            quad(prev[0], prev[1], p[1], p[0], t, core, core, t);
            quad(prev[1], prev[2], p[2], p[1], core, core, core, core);
            quad(prev[2], prev[3], p[3], p[2], core, t, t, core);
        }
        for (int k = 0; k < 4; k++) prev[k] = p[k];
    }
}

void Gfx::fillConvex(const std::vector<Vec2>& pts, uint32_t col) {
    if (pts.size() < 3 || (col & 0xFF) == 0) return;
    for (size_t i = 1; i + 1 < pts.size(); i++) tri(pts[0], pts[i], pts[i + 1], col, col, col);
    // AA fringe outward
    float area = 0;
    for (size_t i = 0; i < pts.size(); i++) area += cross(pts[i], pts[(i + 1) % pts.size()]);
    float s = area > 0 ? -1.f : 1.f;
    uint32_t t = withAlpha(col, 0);
    for (size_t i = 0; i < pts.size(); i++) {
        Vec2 a = pts[i], b = pts[(i + 1) % pts.size()];
        Vec2 n = perp(normalize(b - a)) * s;
        quad(a, a + n * 0.9f, b + n * 0.9f, b, col, t, t, col);
    }
}

void Gfx::fillPoly(const std::vector<Vec2>& in, uint32_t col) {
    if (in.size() < 3) return;
    // ear clipping
    std::vector<Vec2> p = in;
    float area = 0;
    for (size_t i = 0; i < p.size(); i++) area += cross(p[i], p[(i + 1) % p.size()]);
    if (area < 0) std::reverse(p.begin(), p.end());
    std::vector<int> idx;
    for (size_t i = 0; i < p.size(); i++) idx.push_back((int)i);
    int guard = 0;
    while (idx.size() > 3 && guard++ < 10000) {
        bool clipped = false;
        for (size_t i = 0; i < idx.size(); i++) {
            Vec2 a = p[idx[(i + idx.size() - 1) % idx.size()]], b = p[idx[i]], c = p[idx[(i + 1) % idx.size()]];
            if (cross(b - a, c - b) <= 0) continue;
            bool inside = false;
            for (int k : idx) {
                Vec2 q = p[k];
                if (q == a || q == b || q == c) continue;
                if (cross(b - a, q - a) > 0 && cross(c - b, q - b) > 0 && cross(a - c, q - c) > 0) { inside = true; break; }
            }
            if (inside) continue;
            tri(a, b, c, col, col, col);
            idx.erase(idx.begin() + (long)i);
            clipped = true;
            break;
        }
        if (!clipped) break;
    }
    if (idx.size() == 3) tri(p[idx[0]], p[idx[1]], p[idx[2]], col, col, col);
    // light AA on the outline
    polyline(in, 1.0f, col, true);
}

void Gfx::radialGlow(Vec2 c, float r, uint32_t col) {
    int n = 48;
    uint32_t t = withAlpha(col, 0);
    for (int i = 0; i < n; i++) {
        float a0 = (float)i / n * 2 * (float)M_PI, a1 = (float)(i + 1) / n * 2 * (float)M_PI;
        tri(c, c + Vec2(std::cos(a0), std::sin(a0)) * r, c + Vec2(std::cos(a1), std::sin(a1)) * r, col, t, t);
    }
}
