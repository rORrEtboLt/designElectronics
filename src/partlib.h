// designElectronics — part definitions and the built-in parts library.
//
// A PartDef describes how a part looks and connects in each of the three views
// (breadboard, schematic, PCB). Graphics are simple vector primitives so the
// definitions can be embedded verbatim inside .dez project files, which makes a
// shared project self-contained: it opens on any designElectronics install even
// if that install doesn't ship the part.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "geom.h"
#include "json.h"

enum View { BB = 0, SCH = 1, PCB = 2, NUM_VIEWS = 3 };
inline const char* viewKey(int v) { return v == BB ? "breadboard" : v == SCH ? "schematic" : "pcb"; }
inline const char* viewName(int v) { return v == BB ? "Breadboard" : v == SCH ? "Schematic" : "PCB"; }

using Props = std::map<std::string, std::string>;

uint32_t parseColor(const std::string& s, uint32_t def = rgba(0, 0, 0));
std::string colorToHex(uint32_t c);

struct Prim {
    enum Kind { LINE, RECT, CIRCLE, TEXT, POLY, ARC };
    Kind kind = LINE;
    Vec2 a, b;                  // line ends | rect min/max | circle/arc center (a) | text anchor (a)
    float r = 0;                // circle/arc radius, rect corner radius
    float w = 0;                // stroke width (0 = none)
    uint32_t stroke = 0, fill = 0;  // RRGGBBAA, alpha 0 = none
    std::string text;
    float size = 60;            // text height
    int align = 1;              // 0 left, 1 center, 2 right
    std::vector<Vec2> pts;      // polygon (filled/closed) or polyline (stroke only)
    float a0 = 0, a1 = 0;       // arc start/end in degrees (0 = +x, clockwise on screen)
    int tag = 0;                // 1 = LED lens (glows in simulation), 2 = meter display text
};

struct PinDef {
    std::string id, name;
    Vec2 pos[NUM_VIEWS];
    // PCB pad (mils). padH > 0 makes a non-square pad pad × padH (oval when not
    // square). drill 0 is a surface-mount pad: copper, mask and paste on the
    // component side only, always rectangular.
    float pad = 70, drill = 38, padH = 0;
    bool square = false;

    bool smd() const { return drill <= 0; }
    bool rectPad() const { return square || smd(); }
    Vec2 padSize() const { return {pad, padH > 0 ? padH : pad}; }
    // Pad size after a part's placement (quarter turns swap width and height).
    Vec2 padSize(const Xform& xf) const {
        Vec2 s = padSize();
        return (((xf.rot % 4) + 4) % 4) & 1 ? Vec2(s.y, s.x) : s;
    }
    float padRadius() const { Vec2 s = padSize(); return std::max(s.x, s.y) / 2; }
};

// A placed pad of world size `size` centred at `c`, as the segment a–b swept by
// radius r (exact for round and oval pads, close for rectangles): used by DRC
// and the autorouter for clearances.
inline void padStadium(Vec2 c, Vec2 size, Vec2& a, Vec2& b, float& r) {
    r = std::min(size.x, size.y) / 2;
    Vec2 h = size.x > size.y ? Vec2(size.x / 2 - r, 0) : Vec2(0, size.y / 2 - r);
    a = c - h;
    b = c + h;
}

struct PropDef {
    std::string key, label;
    std::vector<std::string> options;  // empty = free text
};

struct PartDef {
    std::string type, title, category, description;
    std::string labelPrefix = "U";
    std::string valueProp;   // property shown next to the label in schematic/PCB
    std::string netProp;     // parts sharing this property's value are connected (GND, net labels)
    std::string sim;         // simulator model name
    std::string simParams;   // model parameters (pin mapping etc.), see docs/FILE_FORMAT.md
    unsigned views = 7;      // bitmask of views the part appears in
    bool isBreadboard = false;
    bool isBoard = false;    // PCB board outline
    bool custom = false;
    std::vector<PropDef> propDefs;
    Props defaults;
    std::vector<PinDef> pins;
    std::vector<Prim> gfx[NUM_VIEWS];
    std::vector<std::pair<Vec2, float>> holes;  // PCB non-plated holes (pos, diameter)
    std::vector<std::vector<int>> buses;        // internally connected pin indices
    Rect bounds[NUM_VIEWS];

    bool inView(int v) const { return (views >> v) & 1u; }
    int pinIndex(const std::string& id) const;
    void computeBounds();
};

using PartDefPtr = std::shared_ptr<const PartDef>;

struct LibEntry {
    std::string type, title, category;
    Props props;  // preset properties (e.g. LED color)
};

const std::vector<LibEntry>& partLibrary();
const std::vector<std::string>& partCategories();
bool isKnownType(const std::string& type);

// Returns the (cached) definition for a type with the given instance properties
// merged over its defaults. Returns nullptr if the type is unknown.
PartDefPtr getPartDef(const std::string& type, const Props& props);
Props mergedProps(const std::string& type, const Props& props);

// Custom/embedded parts (registered from project files or imported .dezpart files).
void registerCustomPart(const PartDef& def);
void clearCustomParts();
bool isCustomType(const std::string& type);

Json partDefToJson(const PartDef& def);
bool partDefFromJson(const Json& j, PartDef& out, std::string* err = nullptr);

// Problems in the parts/*.txt library (bad pin lists, packages, overlapping
// pads); empty when everything checks out. Used by --selftest.
std::vector<std::string> checkPartData();

// Display colour of an LED colour name ("Red", "Blue", …).
uint32_t ledDisplayColor(const std::string& name);

// "4.7kΩ" -> 4700, "100nF" -> 1e-7, "9V" -> 9
double parseSI(const std::string& s, double def = 0);
std::string formatSI(double v, const char* unit);
