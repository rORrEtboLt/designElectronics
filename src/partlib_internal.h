// Internal helpers shared by the built-in part generators (partlib.cpp, catalog.cpp).
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "partlib.h"

namespace plib {

inline constexpr uint32_t LEG = rgba(160, 160, 165);
inline constexpr uint32_t INK = rgba(30, 30, 30);
inline constexpr uint32_t INKTXT = rgba(50, 50, 50);
inline constexpr uint32_t SILK = rgba(255, 255, 255);
inline constexpr float SW = 10;   // schematic stroke
inline constexpr float SKW = 10;  // silkscreen stroke

struct G {
    std::vector<Prim>& v;
    void line(float x1, float y1, float x2, float y2, float w, uint32_t c) {
        Prim p; p.kind = Prim::LINE; p.a = {x1, y1}; p.b = {x2, y2}; p.w = w; p.stroke = c; v.push_back(p);
    }
    void polyline(std::vector<Vec2> pts, float w, uint32_t c) {
        Prim p; p.kind = Prim::POLY; p.pts = std::move(pts); p.w = w; p.stroke = c; v.push_back(p);
    }
    Prim& poly(std::vector<Vec2> pts, uint32_t fill, uint32_t stroke = 0, float sw = 0) {
        Prim p; p.kind = Prim::POLY; p.pts = std::move(pts); p.fill = fill; p.stroke = stroke; p.w = sw; v.push_back(p);
        return v.back();
    }
    Prim& rect(float x, float y, float w, float h, uint32_t fill, uint32_t stroke = 0, float sw = 0, float rad = 0) {
        Prim p; p.kind = Prim::RECT; p.a = {x, y}; p.b = {x + w, y + h}; p.fill = fill; p.stroke = stroke; p.w = sw; p.r = rad;
        v.push_back(p); return v.back();
    }
    Prim& circle(float x, float y, float r, uint32_t fill, uint32_t stroke = 0, float sw = 0) {
        Prim p; p.kind = Prim::CIRCLE; p.a = {x, y}; p.r = r; p.fill = fill; p.stroke = stroke; p.w = sw;
        v.push_back(p); return v.back();
    }
    void arc(float x, float y, float r, float a0, float a1, float w, uint32_t c) {
        Prim p; p.kind = Prim::ARC; p.a = {x, y}; p.r = r; p.a0 = a0; p.a1 = a1; p.w = w; p.stroke = c; v.push_back(p);
    }
    Prim& text(float x, float y, const std::string& s, float size, uint32_t c, int align = 1) {
        Prim p; p.kind = Prim::TEXT; p.a = {x, y}; p.text = s; p.size = size; p.fill = c; p.align = align;
        v.push_back(p); return v.back();
    }
};

struct Gen {
    std::string title, category;
    Props defaults;
    std::function<void(PartDef&, const Props&)> make;
};

PinDef& addPin(PartDef& d, const std::string& id, const std::string& name, Vec2 bb, Vec2 sch, Vec2 pcb);
std::string prop(const Props& p, const char* k, const char* def = "");
void schBox(PartDef& d, const std::string& title, const std::vector<int>& left, const std::vector<int>& right,
            float boxW = 600, float topPad = 100);
void zigzag(G& g, float x0, float x1, float y, float amp);
void arrowHead(G& g, Vec2 tip, Vec2 dir, float size, uint32_t c);
void drawTo92(G& bb, const std::string& label);
void makeDip(PartDef& d, int n, const std::string& chip, const std::vector<std::string>& names);
std::vector<std::string> splitNames(const std::string& s);
uint32_t ledColorOf(const std::string& n);

struct ArduinoSpec {
    std::string title, chip, logo, desc;
    uint32_t color = rgba(0, 135, 145);
    bool mega = false, io3v3 = false, usbC = false, microUsb = false, ledMatrix = false;
    std::vector<std::string> bottom2 = {"A8", "A9", "A10", "A11", "A12", "A13", "A14", "A15"};
};
void makeArduinoBoard(PartDef& d, const ArduinoSpec& s);

const std::map<std::string, Gen>& generators();

// catalog.cpp
void addCatalogGenerators(std::map<std::string, Gen>& g);
const std::vector<LibEntry>& catalogEntries();

// partdata.cpp (parts/*.txt)
void addDataGenerators(std::map<std::string, Gen>& g);
const std::vector<LibEntry>& dataEntries();
bool isDataType(const std::string& type);
std::vector<std::string> checkDataParts();

}  // namespace plib
