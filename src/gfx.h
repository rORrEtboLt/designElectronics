// designElectronics — 2D drawing on top of SDL3's renderer.
// Anti-aliased lines/circles/polygons via SDL_RenderGeometry with feathered edges,
// and cached TTF text (SDL3_ttf) with a debug-font fallback.
#pragma once
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

#include "geom.h"

enum FontFace { FONT_UI = 0, FONT_MONO = 1 };

class Gfx {
public:
    bool init(SDL_Renderer* r);
    void shutdown();
    SDL_Renderer* renderer() const { return r_; }

    void flush();
    void setClip(const Rect* r);

    // primitives (pixel coordinates)
    void fillRect(Rect r, uint32_t c);
    void strokeRect(Rect r, float w, uint32_t c);
    void fillRoundRect(Rect r, float rad, uint32_t c);
    void strokeRoundRect(Rect r, float rad, float w, uint32_t c);
    void line(Vec2 a, Vec2 b, float w, uint32_t c, bool roundCaps = true);
    void polyline(const std::vector<Vec2>& pts, float w, uint32_t c, bool closed = false);
    void dashedLine(Vec2 a, Vec2 b, float w, float dash, float gap, uint32_t c);
    void fillCircle(Vec2 c, float r, uint32_t col);
    void strokeCircle(Vec2 c, float r, float w, uint32_t col);
    void arc(Vec2 c, float r, float a0deg, float a1deg, float w, uint32_t col);
    void fillConvex(const std::vector<Vec2>& pts, uint32_t col);
    void fillPoly(const std::vector<Vec2>& pts, uint32_t col);  // ear clipping, any simple polygon
    void radialGlow(Vec2 c, float r, uint32_t col);

    // text: pos.y is the vertical centre; align 0 left, 1 centre, 2 right
    float text(Vec2 pos, const std::string& s, float px, uint32_t col, int align = 0, FontFace face = FONT_UI, float angleDeg = 0);
    float textWidth(const std::string& s, float px, FontFace face = FONT_UI);
    bool hasFonts() const { return !fontData_[0].empty(); }

private:
    SDL_Renderer* r_ = nullptr;
    std::vector<SDL_Vertex> v_;
    std::vector<unsigned char> fontData_[2];
    std::vector<std::vector<unsigned char>> fallbackData_;  // system fonts for missing glyphs
    std::vector<TTF_Font*> fallbackFonts_;
    std::unordered_map<int, TTF_Font*> fonts_;   // key = face*1000 + px

    struct TextEntry {
        SDL_Texture* tex = nullptr;
        float w = 0, h = 0;
        std::list<std::string>::iterator lru;
    };
    std::unordered_map<std::string, TextEntry> textCache_;
    std::list<std::string> lru_;

    TTF_Font* font(FontFace face, int px);
    TextEntry* textEntry(const std::string& s, int px, FontFace face);

    void vtx(Vec2 p, uint32_t c);
    void tri(Vec2 a, Vec2 b, Vec2 c, uint32_t ca, uint32_t cb, uint32_t cc);
    void quad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, uint32_t ca, uint32_t cb, uint32_t cc, uint32_t cd);
    void segment(Vec2 a, Vec2 b, float w, uint32_t c);
};

inline uint32_t withAlpha(uint32_t c, int a) { return (c & 0xFFFFFF00u) | (uint32_t)std::clamp(a, 0, 255); }
inline uint32_t mixColor(uint32_t a, uint32_t b, float t) {
    auto ch = [&](int s) {
        float x = (float)((a >> s) & 0xFF), y = (float)((b >> s) & 0xFF);
        return (uint32_t)std::clamp((int)std::lround(x + (y - x) * t), 0, 255) << s;
    };
    return ch(24) | ch(16) | ch(8) | ch(0);
}
