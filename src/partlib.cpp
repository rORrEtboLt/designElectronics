#include "partlib.h"
#include "partlib_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>

// ---------------------------------------------------------------------------
// Value helpers
// ---------------------------------------------------------------------------

uint32_t parseColor(const std::string& s, uint32_t def) {
    if (s.size() >= 7 && s[0] == '#') {
        unsigned v = (unsigned)strtoul(s.c_str() + 1, nullptr, 16);
        if (s.size() >= 9) return v;
        return (v << 8) | 0xFF;
    }
    return def;
}

std::string colorToHex(uint32_t c) {
    char buf[16];
    if ((c & 0xFF) == 0xFF) snprintf(buf, sizeof buf, "#%06x", c >> 8);
    else snprintf(buf, sizeof buf, "#%08x", c);
    return buf;
}

double parseSI(const std::string& s, double def) {
    const char* p = s.c_str();
    while (*p == ' ') p++;
    char* end = nullptr;
    double v = strtod(p, &end);
    if (end == p) return def;
    while (*end == ' ') end++;
    switch (*end) {
        case 'p': v *= 1e-12; break;
        case 'n': v *= 1e-9; break;
        case 'u': v *= 1e-6; break;
        case 'm': v *= 1e-3; break;
        case 'k': case 'K': v *= 1e3; break;
        case 'M': v *= 1e6; break;
        case 'G': v *= 1e9; break;
        default:
            if ((unsigned char)end[0] == 0xC2 && (unsigned char)end[1] == 0xB5) v *= 1e-6;  // µ
            break;
    }
    return v;
}

std::string formatSI(double v, const char* unit) {
    struct P { double m; const char* s; } ps[] = {{1e9, "G"}, {1e6, "M"}, {1e3, "k"}, {1, ""}, {1e-3, "m"}, {1e-6, "µ"}, {1e-9, "n"}, {1e-12, "p"}};
    double a = std::fabs(v);
    if (a < 1e-15) return std::string("0") + unit;
    for (auto& p : ps) {
        if (a >= p.m * 0.9995) {
            char buf[48];
            double x = v / p.m;
            if (std::fabs(x - std::round(x)) < 1e-6) snprintf(buf, sizeof buf, "%.0f%s%s", x, p.s, unit);
            else snprintf(buf, sizeof buf, "%.3g%s%s", x, p.s, unit);
            return buf;
        }
    }
    char buf[48];
    snprintf(buf, sizeof buf, "%.3g%s", v, unit);
    return buf;
}

int PartDef::pinIndex(const std::string& id) const {
    for (size_t i = 0; i < pins.size(); i++) if (pins[i].id == id) return (int)i;
    return -1;
}

void PartDef::computeBounds() {
    for (int v = 0; v < NUM_VIEWS; v++) {
        bool any = false;
        Rect r;
        auto add = [&](Vec2 p) {
            if (!any) { r = Rect(p.x, p.y, 0, 0); any = true; }
            else r = r.expandTo(p);
        };
        for (auto& p : gfx[v]) {
            float hw = p.w / 2;
            switch (p.kind) {
                case Prim::LINE: add(p.a - Vec2(hw, hw)); add(p.b + Vec2(hw, hw)); add(p.a + Vec2(hw, hw)); add(p.b - Vec2(hw, hw)); break;
                case Prim::RECT: add(p.a); add(p.b); break;
                case Prim::CIRCLE: case Prim::ARC: add(p.a - Vec2(p.r + hw, p.r + hw)); add(p.a + Vec2(p.r + hw, p.r + hw)); break;
                case Prim::TEXT: {
                    float tw = p.size * 0.6f * (float)p.text.size();
                    float x0 = p.align == 0 ? p.a.x : p.align == 1 ? p.a.x - tw / 2 : p.a.x - tw;
                    add({x0, p.a.y - p.size / 2}); add({x0 + tw, p.a.y + p.size / 2});
                    break;
                }
                case Prim::POLY: for (auto& q : p.pts) add(q); break;
            }
        }
        if (inView(v)) {
            for (auto& pin : pins) {
                float pr = v == PCB ? pin.pad / 2 : 20;
                add(pin.pos[v] - Vec2(pr, pr)); add(pin.pos[v] + Vec2(pr, pr));
            }
        }
        if (!any) r = Rect(-50, -50, 100, 100);
        bounds[v] = r;
    }
}

// ---------------------------------------------------------------------------
// Drawing builder
// ---------------------------------------------------------------------------
namespace plib {

PinDef& addPin(PartDef& d, const std::string& id, const std::string& name, Vec2 bb, Vec2 sch, Vec2 pcb) {
    PinDef p;
    p.id = id; p.name = name;
    p.pos[BB] = bb; p.pos[SCH] = sch; p.pos[PCB] = pcb;
    d.pins.push_back(p);
    return d.pins.back();
}

std::string prop(const Props& p, const char* k, const char* def) {
    auto it = p.find(k);
    return it == p.end() ? def : it->second;
}

// ---- shared view pieces ----------------------------------------------------

// Horizontal breadboard leg drawn as a lead from a pin to the body.
void bbLeg(G& g, Vec2 a, Vec2 b) { g.line(a.x, a.y, b.x, b.y, 18, LEG); }

// PCB: pads come from pins; this draws a silkscreen box.
void silkRect(G& g, float x, float y, float w, float h) { g.rect(x, y, w, h, 0, SILK, SKW); }

// Generic schematic IC box. left/right are pin indices top-to-bottom.
void schBox(PartDef& d, const std::string& title, const std::vector<int>& left, const std::vector<int>& right,
            float boxW, float topPad) {
    G g{d.gfx[SCH]};
    size_t rows = std::max(left.size(), right.size());
    float h = (float)rows * 100 + topPad;
    float x0 = 200, x1 = 200 + boxW;
    g.rect(x0, 0, boxW, h, rgba(255, 255, 255), INK, SW);
    for (size_t i = 0; i < left.size(); i++) {
        float y = topPad + (float)i * 100;
        if (left[i] < 0) continue;
        PinDef& p = d.pins[left[i]];
        p.pos[SCH] = {0, y};
        g.line(0, y, x0, y, SW, INK);
        g.text(x0 + 25, y, p.name, 45, INKTXT, 0);
    }
    for (size_t i = 0; i < right.size(); i++) {
        float y = topPad + (float)i * 100;
        if (right[i] < 0) continue;
        PinDef& p = d.pins[right[i]];
        p.pos[SCH] = {x1 + 200, y};
        g.line(x1, y, x1 + 200, y, SW, INK);
        g.text(x1 - 25, y, p.name, 45, INKTXT, 2);
    }
    g.text(x0 + boxW / 2, -60, title, 60, INKTXT, 1);
}

void zigzag(G& g, float x0, float x1, float y, float amp) {
    std::vector<Vec2> pts;
    pts.push_back({x0, y});
    int n = 6;
    float step = (x1 - x0) / (float)n;
    for (int i = 0; i < n; i++) pts.push_back({x0 + step * ((float)i + 0.5f), y + ((i % 2) ? amp : -amp)});
    pts.push_back({x1, y});
    g.polyline(pts, SW, INK);
}

void arrowHead(G& g, Vec2 tip, Vec2 dir, float size, uint32_t c) {
    dir = normalize(dir);
    Vec2 n = perp(dir);
    g.poly({tip, tip - dir * size + n * (size * 0.45f), tip - dir * size - n * (size * 0.45f)}, c);
}

// ---------------------------------------------------------------------------
// Part generators
// ---------------------------------------------------------------------------

const char* bandName[] = {"black", "brown", "red", "orange", "yellow", "green", "blue", "violet", "grey", "white"};
const uint32_t bandCol[] = {rgba(20, 20, 20), rgba(120, 70, 30), rgba(210, 30, 30), rgba(240, 130, 20), rgba(240, 220, 30),
                            rgba(30, 160, 60), rgba(30, 80, 210), rgba(140, 60, 190), rgba(130, 130, 130), rgba(245, 245, 245)};

void makeResistor(PartDef& d, const Props& p) {
    d.title = "Resistor";
    d.description = "A resistor limits current. Edit the resistance in the Inspector; the color bands update automatically.";
    d.labelPrefix = "R";
    d.valueProp = "resistance";
    d.sim = "resistor";
    d.propDefs = {{"resistance", "Resistance", {"100Ω", "220Ω", "330Ω", "470Ω", "1kΩ", "2.2kΩ", "4.7kΩ", "10kΩ", "47kΩ", "100kΩ", "1MΩ"}},
                  {"power", "Power", {"1/8W", "1/4W", "1/2W", "1W"}},
                  {"tolerance", "Tolerance", {"±1%", "±5%", "±10%"}}};
    addPin(d, "pin0", "Pin 0", {0, 0}, {0, 0}, {0, 0});
    addPin(d, "pin1", "Pin 1", {400, 0}, {400, 0}, {400, 0});

    G bb{d.gfx[BB]};
    bbLeg(bb, {0, 0}, {110, 0});
    bbLeg(bb, {290, 0}, {400, 0});
    uint32_t body = rgba(222, 200, 150), edge = rgba(170, 145, 95);
    bb.circle(115, 0, 46, body, edge, 6);
    bb.circle(285, 0, 46, body, edge, 6);
    bb.rect(115, -38, 170, 76, body);
    bb.line(115, -38, 285, -38, 6, edge);
    bb.line(115, 38, 285, 38, 6, edge);
    // color bands
    double ohms = parseSI(prop(p, "resistance", "220"), 220);
    int d1 = 0, d2 = 0, mult = 0;
    if (ohms > 0) {
        int e = (int)std::floor(std::log10(ohms)) - 1;
        double sig = std::round(ohms / std::pow(10.0, e));
        if (sig >= 100) { sig /= 10; e++; }
        d1 = (int)sig / 10; d2 = (int)sig % 10; mult = e;
    }
    float bx[] = {110, 150, 190, 255};
    uint32_t bc[4] = {bandCol[d1 % 10], bandCol[d2 % 10],
                      mult >= 0 && mult <= 9 ? bandCol[mult] : (mult == -1 ? rgba(200, 160, 40) : rgba(200, 200, 200)),
                      rgba(200, 160, 40)};
    std::string tol = prop(p, "tolerance");
    if (tol.find("1%") != std::string::npos && tol.find("10") == std::string::npos) bc[3] = bandCol[1];
    if (tol.find("10%") != std::string::npos) bc[3] = rgba(200, 200, 200);
    for (int i = 0; i < 4; i++) bb.rect(bx[i], i == 0 ? -46 : -38, 22, i == 0 ? 92 : 76, bc[i]);
    (void)bandName;

    G s{d.gfx[SCH]};
    s.line(0, 0, 100, 0, SW, INK);
    s.line(300, 0, 400, 0, SW, INK);
    zigzag(s, 100, 300, 0, 40);

    G pc{d.gfx[PCB]};
    silkRect(pc, 70, -45, 260, 90);
}

struct LedColor { const char* name; uint32_t c; };
const LedColor ledColors[] = {{"Red", rgba(235, 40, 40)}, {"Green", rgba(40, 200, 70)}, {"Blue", rgba(50, 100, 245)},
                              {"Yellow", rgba(250, 215, 40)}, {"White", rgba(245, 245, 235)}, {"Orange", rgba(255, 140, 30)}};

uint32_t ledColorOf(const std::string& n) {
    for (auto& c : ledColors) if (n == c.name) return c.c;
    return ledColors[0].c;
}

void makeLed(PartDef& d, const Props& p) {
    d.title = prop(p, "color", "Red") + " LED";
    d.description = "Light-emitting diode. The longer leg (anode, +) goes to the positive side; always use a series resistor.";
    d.labelPrefix = "LED";
    d.valueProp = "color";
    d.sim = "led";
    d.propDefs = {{"color", "Color", {"Red", "Green", "Blue", "Yellow", "White", "Orange"}},
                  {"package", "Package", {"5 mm [THT]", "3 mm [THT]"}}};
    addPin(d, "cathode", "cathode (−)", {0, 0}, {300, 0}, {0, 0}).square = true;
    addPin(d, "anode", "anode (+)", {100, 0}, {0, 0}, {100, 0});

    bool small = prop(p, "package").find("3 mm") != std::string::npos;
    float r = small ? 70 : 100;
    uint32_t c = ledColorOf(prop(p, "color", "Red"));
    uint32_t lens = (c & 0xFFFFFF00) | 0xD8;
    uint32_t edge = rgba(((c >> 24) & 0xFF) * 7 / 10, ((c >> 16) & 0xFF) * 7 / 10, ((c >> 8) & 0xFF) * 7 / 10);
    G bb{d.gfx[BB]};
    bb.line(0, -150, 0, 0, 16, LEG);
    bb.polyline({{100, -150}, {100, -90}, {135, -60}, {100, -30}, {100, 0}}, 16, LEG);
    float cx = 50;
    bb.rect(cx - r, -300 - (small ? 0 : 20), 2 * r, 150 + (small ? 0 : 20), lens).tag = 1;
    bb.circle(cx, -300 - (small ? 0 : 20), r, lens).tag = 1;
    bb.rect(cx - r - 18, -175, 2 * r + 36, 26, edge, 0, 0, 6);
    bb.rect(cx - r * 0.55f, -300 - r * 0.6f, r * 0.25f, 120, rgba(255, 255, 255, 90), 0, 0, 10);

    G s{d.gfx[SCH]};
    s.line(0, 0, 100, 0, SW, INK);
    s.line(200, 0, 300, 0, SW, INK);
    s.poly({{100, -60}, {100, 60}, {200, 0}}, rgba(255, 255, 255), INK, SW);
    s.line(200, -60, 200, 60, SW, INK);
    s.line(130, -80, 180, -130, SW * 0.8f, INK);
    s.line(170, -70, 220, -120, SW * 0.8f, INK);
    arrowHead(s, {185, -135}, {1, -1}, 30, INK);
    arrowHead(s, {225, -125}, {1, -1}, 30, INK);

    G pc{d.gfx[PCB]};
    pc.arc(50, 0, small ? 85 : 120, 30, 330, SKW, SILK);
    float rr = small ? 85 : 120;
    pc.line(50 + rr * 0.866f, -rr * 0.5f, 50 + rr * 0.866f, rr * 0.5f, SKW, SILK);
}

void makeCapCeramic(PartDef& d, const Props& p) {
    (void)p;
    d.title = "Ceramic Capacitor";
    d.description = "Non-polarized capacitor, typically 10pF–1µF. Used for decoupling and filtering.";
    d.labelPrefix = "C";
    d.valueProp = "capacitance";
    d.sim = "capacitor";
    d.propDefs = {{"capacitance", "Capacitance", {"22pF", "100pF", "1nF", "10nF", "100nF", "1µF"}}, {"voltage", "Voltage", {"16V", "50V", "100V"}}};
    addPin(d, "pin0", "leg 0", {0, 0}, {0, 0}, {0, 0});
    addPin(d, "pin1", "leg 1", {100, 0}, {300, 0}, {100, 0});
    G bb{d.gfx[BB]};
    bb.line(0, -120, 0, 0, 14, LEG);
    bb.line(100, -120, 100, 0, 14, LEG);
    bb.circle(50, -190, 85, rgba(235, 165, 50), rgba(190, 120, 30), 6);
    bb.text(50, -190, "104", 40, rgba(90, 50, 10));
    G s{d.gfx[SCH]};
    s.line(0, 0, 130, 0, SW, INK);
    s.line(170, 0, 300, 0, SW, INK);
    s.line(130, -70, 130, 70, SW * 1.4f, INK);
    s.line(170, -70, 170, 70, SW * 1.4f, INK);
    G pc{d.gfx[PCB]};
    silkRect(pc, -70, -55, 240, 110);
}

void makeCapElectrolytic(PartDef& d, const Props& p) {
    (void)p;
    d.title = "Electrolytic Capacitor";
    d.description = "Polarized capacitor for larger values. The stripe marks the negative (−) leg.";
    d.labelPrefix = "C";
    d.valueProp = "capacitance";
    d.sim = "capacitor";
    d.propDefs = {{"capacitance", "Capacitance", {"1µF", "10µF", "47µF", "100µF", "470µF", "1000µF"}}, {"voltage", "Voltage", {"6.3V", "16V", "25V", "50V"}}};
    addPin(d, "minus", "− (cathode)", {0, 0}, {300, 0}, {0, 0});
    addPin(d, "plus", "+ (anode)", {100, 0}, {0, 0}, {100, 0}).square = true;
    G bb{d.gfx[BB]};
    bb.line(0, -100, 0, 0, 14, LEG);
    bb.line(100, -100, 100, 0, 14, LEG);
    bb.rect(-60, -420, 220, 330, rgba(40, 70, 160), rgba(25, 45, 110), 6, 30);
    bb.rect(-60, -420, 60, 330, rgba(180, 190, 210), 0, 0, 20);
    bb.text(-30, -330, "−", 50, rgba(40, 70, 160));
    bb.text(-30, -200, "−", 50, rgba(40, 70, 160));
    bb.rect(-60, -420, 220, 30, rgba(160, 165, 175), 0, 0, 14);
    G s{d.gfx[SCH]};
    s.line(0, 0, 130, 0, SW, INK);
    s.line(130, -70, 130, 70, SW * 1.4f, INK);
    s.arc(330, 0, 150, 152, 208, SW * 1.4f, INK);
    s.line(185, 0, 300, 0, SW, INK);
    s.text(80, -70, "+", 60, INK);
    G pc{d.gfx[PCB]};
    pc.circle(50, 0, 130, 0, SILK, SKW);
    pc.text(170, -110, "+", 70, SILK);
}

void makeDiode(PartDef& d, const Props& p) {
    (void)p;
    d.title = "Rectifier Diode";
    d.description = "Lets current flow from anode to cathode only. The band marks the cathode.";
    d.labelPrefix = "D";
    d.valueProp = "part";
    d.sim = "diode";
    d.propDefs = {{"part", "Part #", {"1N4001", "1N4148", "1N5819"}}};
    addPin(d, "anode", "anode", {0, 0}, {0, 0}, {0, 0});
    addPin(d, "cathode", "cathode", {400, 0}, {300, 0}, {400, 0}).square = true;
    G bb{d.gfx[BB]};
    bbLeg(bb, {0, 0}, {110, 0});
    bbLeg(bb, {290, 0}, {400, 0});
    bb.rect(100, -40, 200, 80, rgba(35, 35, 35), 0, 0, 16);
    bb.rect(250, -40, 26, 80, rgba(200, 200, 200));
    G s{d.gfx[SCH]};
    s.line(0, 0, 100, 0, SW, INK);
    s.line(200, 0, 300, 0, SW, INK);
    s.poly({{100, -60}, {100, 60}, {200, 0}}, INK, INK, SW);
    s.line(200, -60, 200, 60, SW, INK);
    G pc{d.gfx[PCB]};
    silkRect(pc, 70, -45, 260, 90);
    pc.line(270, -45, 270, 45, SKW, SILK);
}

void makeInductor(PartDef& d, const Props& p) {
    (void)p;
    d.title = "Inductor";
    d.description = "Coil that stores energy in a magnetic field.";
    d.labelPrefix = "L";
    d.valueProp = "inductance";
    d.sim = "inductor";
    d.propDefs = {{"inductance", "Inductance", {"10µH", "100µH", "1mH", "10mH"}}};
    addPin(d, "pin0", "leg 0", {0, 0}, {0, 0}, {0, 0});
    addPin(d, "pin1", "leg 1", {400, 0}, {400, 0}, {400, 0});
    G bb{d.gfx[BB]};
    bbLeg(bb, {0, 0}, {110, 0});
    bbLeg(bb, {290, 0}, {400, 0});
    bb.rect(100, -55, 200, 110, rgba(60, 130, 80), rgba(30, 90, 50), 6, 40);
    for (int i = 0; i < 3; i++) bb.rect(140 + i * 45, -55, 18, 110, bandCol[i == 0 ? 1 : 0]);
    G s{d.gfx[SCH]};
    s.line(0, 0, 100, 0, SW, INK);
    s.line(300, 0, 400, 0, SW, INK);
    for (int i = 0; i < 4; i++) s.arc(125 + i * 50, 0, 25, 180, 360, SW, INK);
    G pc{d.gfx[PCB]};
    silkRect(pc, 70, -60, 260, 120);
}

void makeCrystal(PartDef& d, const Props& p) {
    (void)p;
    d.title = "Crystal";
    d.description = "Quartz crystal resonator for precise clocks.";
    d.labelPrefix = "Y";
    d.valueProp = "frequency";
    d.propDefs = {{"frequency", "Frequency", {"32.768kHz", "8MHz", "12MHz", "16MHz", "20MHz"}}};
    addPin(d, "pin0", "1", {0, 0}, {0, 0}, {0, 0});
    addPin(d, "pin1", "2", {200, 0}, {300, 0}, {200, 0});
    G bb{d.gfx[BB]};
    bb.line(0, -80, 0, 0, 14, LEG);
    bb.line(200, -80, 200, 0, 14, LEG);
    bb.rect(-80, -230, 360, 160, rgba(200, 200, 205), rgba(140, 140, 150), 8, 80);
    bb.text(100, -150, prop(p, "frequency", "16MHz"), 45, rgba(60, 60, 60));
    G s{d.gfx[SCH]};
    s.line(0, 0, 100, 0, SW, INK);
    s.line(200, 0, 300, 0, SW, INK);
    s.line(100, -60, 100, 60, SW, INK);
    s.line(200, -60, 200, 60, SW, INK);
    s.rect(125, -80, 50, 160, 0, INK, SW);
    G pc{d.gfx[PCB]};
    silkRect(pc, -90, -80, 380, 160);
}

void makePot(PartDef& d, const Props& p) {
    (void)p;
    d.title = "Potentiometer";
    d.description = "Variable resistor with a wiper. Set the wiper position (0–100%) in the Inspector; it's live in simulation.";
    d.labelPrefix = "R";
    d.valueProp = "resistance";
    d.sim = "potentiometer";
    d.propDefs = {{"resistance", "Max resistance", {"1kΩ", "5kΩ", "10kΩ", "50kΩ", "100kΩ"}},
                  {"wiper", "Wiper %", {"0", "25", "50", "75", "100"}}};
    addPin(d, "leg1", "leg 1", {0, 0}, {0, 0}, {0, 0});
    addPin(d, "wiper", "wiper", {100, 0}, {200, -200}, {100, 0});
    addPin(d, "leg2", "leg 2", {200, 0}, {400, 0}, {200, 0});
    G bb{d.gfx[BB]};
    for (int i = 0; i < 3; i++) bb.line(i * 100.f, -90, i * 100.f, 0, 16, LEG);
    bb.rect(-80, -400, 360, 320, rgba(40, 95, 190), rgba(25, 60, 130), 6, 20);
    bb.circle(100, -240, 120, rgba(235, 235, 235), rgba(160, 160, 160), 6);
    float w = (float)std::atof(prop(p, "wiper", "50").c_str()) / 100.f;
    float ang = (float)(M_PI * (0.75 + 1.5 * w));
    bb.line(100, -240, 100 + 100 * std::cos(ang), -240 + 100 * std::sin(ang), 22, rgba(90, 90, 90));
    G s{d.gfx[SCH]};
    s.line(0, 0, 100, 0, SW, INK);
    s.line(300, 0, 400, 0, SW, INK);
    zigzag(s, 100, 300, 0, 40);
    s.line(200, -200, 200, -70, SW, INK);
    arrowHead(s, {200, -45}, {0, 1}, 45, INK);
    G pc{d.gfx[PCB]};
    silkRect(pc, -90, -200, 380, 280);
}

void makePhotocell(PartDef& d, const Props& p) {
    (void)p;
    d.title = "Photocell (LDR)";
    d.description = "Light-dependent resistor: ~1kΩ in bright light, ~1MΩ in the dark. Adjust the light level in the Inspector.";
    d.labelPrefix = "R";
    d.valueProp = "light";
    d.sim = "photocell";
    d.propDefs = {{"light", "Light level %", {"0", "25", "50", "75", "100"}}};
    addPin(d, "pin0", "leg 0", {0, 0}, {0, 0}, {0, 0});
    addPin(d, "pin1", "leg 1", {100, 0}, {400, 0}, {100, 0});
    G bb{d.gfx[BB]};
    bb.line(0, -100, 0, 0, 14, LEG);
    bb.line(100, -100, 100, 0, 14, LEG);
    bb.circle(50, -200, 110, rgba(230, 120, 60), rgba(170, 80, 30), 8);
    bb.polyline({{-10, -260}, {110, -260}, {110, -220}, {-10, -220}, {-10, -180}, {110, -180}, {110, -140}, {-10, -140}}, 10, rgba(120, 60, 20));
    G s{d.gfx[SCH]};
    s.line(0, 0, 100, 0, SW, INK);
    s.line(300, 0, 400, 0, SW, INK);
    s.rect(100, -45, 200, 90, 0, INK, SW);
    s.line(110, -170, 160, -110, SW * 0.8f, INK);
    s.line(170, -170, 220, -110, SW * 0.8f, INK);
    arrowHead(s, {170, -95}, {1, 1.2f}, 30, INK);
    arrowHead(s, {230, -95}, {1, 1.2f}, 30, INK);
    G pc{d.gfx[PCB]};
    pc.circle(50, 0, 120, 0, SILK, SKW);
}

void makePushbutton(PartDef& d, const Props& p) {
    (void)p;
    d.title = "Pushbutton";
    d.description = "Momentary tactile switch. Legs on the same side of the gap are always connected; pressing connects the two sides. In simulation, hold it with the pointer.";
    d.labelPrefix = "S";
    d.sim = "pushbutton";
    d.propDefs = {{"form", "Form", {"[THT] 6mm"}}};
    addPin(d, "pin1", "1", {0, 0}, {0, 0}, {0, 0});
    addPin(d, "pin2", "2", {200, 0}, {400, 0}, {200, 0});
    addPin(d, "pin3", "3", {0, 300}, {0, 200}, {0, 300});
    addPin(d, "pin4", "4", {200, 300}, {400, 200}, {200, 300});
    d.buses = {{0, 2}, {1, 3}};
    G bb{d.gfx[BB]};
    for (auto& pin : d.pins) bb.circle(pin.pos[BB].x, pin.pos[BB].y, 14, LEG);
    bb.rect(-50, 20, 300, 260, rgba(70, 70, 75), rgba(40, 40, 40), 6, 14);
    bb.rect(-20, 50, 240, 200, rgba(150, 150, 155), 0, 0, 10);
    bb.circle(100, 150, 70, rgba(30, 30, 30), rgba(15, 15, 15), 6);
    G s{d.gfx[SCH]};
    s.polyline({{0, 0}, {100, 0}, {100, 200}, {0, 200}}, SW, INK);
    s.polyline({{400, 0}, {300, 0}, {300, 200}, {400, 200}}, SW, INK);
    s.line(100, 100, 150, 100, SW, INK);
    s.line(250, 100, 300, 100, SW, INK);
    s.circle(150, 100, 14, INK);
    s.circle(250, 100, 14, INK);
    s.line(130, 40, 270, 40, SW, INK);
    s.line(200, 40, 200, -40, SW, INK);
    s.line(160, -40, 240, -40, SW, INK);
    G pc{d.gfx[PCB]};
    silkRect(pc, -60, 40, 320, 220);
    pc.circle(100, 150, 70, 0, SILK, SKW);
}

void makeSwitch(PartDef& d, const Props& p) {
    d.title = "Slide Switch (SPDT)";
    d.description = "Single-pole double-throw switch. The middle pin is common. Toggle the position in the Inspector or click it while simulating.";
    d.labelPrefix = "S";
    d.sim = "switch";
    d.propDefs = {{"position", "Position", {"left", "right"}}};
    addPin(d, "a", "left (A)", {0, 0}, {300, -100}, {0, 0});
    addPin(d, "common", "common", {100, 0}, {0, 0}, {100, 0});
    addPin(d, "b", "right (B)", {200, 0}, {300, 100}, {200, 0});
    bool left = prop(p, "position", "left") == "left";
    G bb{d.gfx[BB]};
    for (int i = 0; i < 3; i++) bb.line(i * 100.f, -70, i * 100.f, 0, 14, LEG);
    bb.rect(-100, -240, 400, 170, rgba(110, 110, 120), rgba(70, 70, 80), 6, 12);
    bb.rect(-60, -200, 320, 90, rgba(40, 40, 45), 0, 0, 8);
    bb.rect(left ? -50 : 110, -215, 140, 120, rgba(230, 230, 235), rgba(150, 150, 150), 5, 10);
    G s{d.gfx[SCH]};
    s.line(0, 0, 100, 0, SW, INK);
    s.circle(100, 0, 14, INK);
    s.line(200, -100, 300, -100, SW, INK);
    s.line(200, 100, 300, 100, SW, INK);
    s.circle(200, -100, 14, INK);
    s.circle(200, 100, 14, INK);
    s.line(100, 0, 195, left ? -85 : 85, SW, INK);
    G pc{d.gfx[PCB]};
    silkRect(pc, -110, -90, 420, 180);
}

void makeBattery(PartDef& d, const Props& p) {
    std::string style = prop(p, "style", "9V block");
    d.title = "Battery";
    d.description = "DC voltage source. Choose the style and set any voltage in the Inspector.";
    d.labelPrefix = "VCC";
    d.valueProp = "voltage";
    d.sim = "battery";
    d.propDefs = {{"voltage", "Voltage", {"1.5V", "3V", "3.7V", "4.5V", "5V", "9V", "12V"}}, {"style", "Style", {"9V block", "AA pack", "Coin cell"}}};
    addPin(d, "minus", "−", {0, 0}, {0, 400}, {0, 0});
    addPin(d, "plus", "+", {100, 0}, {0, 0}, {100, 0}).square = true;
    G bb{d.gfx[BB]};
    std::string v = prop(p, "voltage", "9V");
    if (style == "Coin cell") {
        bb.line(0, -200, 0, 0, 14, LEG);
        bb.line(100, -200, 100, 0, 14, LEG);
        bb.circle(50, -420, 230, rgba(190, 190, 195), rgba(120, 120, 125), 10);
        bb.circle(50, -420, 170, rgba(210, 210, 215));
        bb.text(50, -440, "CR2032", 60, rgba(80, 80, 80));
        bb.text(50, -370, v, 60, rgba(80, 80, 80));
    } else if (style == "AA pack") {
        bb.line(0, -320, 0, 0, 22, rgba(30, 30, 30));
        bb.line(100, -320, 100, 0, 22, rgba(210, 40, 40));
        bb.rect(-300, -1100, 700, 780, rgba(30, 30, 30), rgba(10, 10, 10), 8, 30);
        for (int i = 0; i < 2; i++) {
            float x = -250 + i * 330.f;
            bb.rect(x, -1050, 270, 680, rgba(60, 120, 60), 0, 0, 40);
            bb.rect(x, -1050, 270, 120, rgba(200, 200, 200), 0, 0, 30);
            bb.text(x + 135, -700, "AA", 90, rgba(255, 255, 255));
        }
        bb.text(50, -390, v, 60, rgba(255, 255, 255));
    } else {
        bb.line(0, -330, 0, 0, 22, rgba(30, 30, 30));
        bb.line(100, -330, 100, 0, 22, rgba(210, 40, 40));
        bb.rect(-250, -1300, 600, 950, rgba(40, 40, 45), rgba(15, 15, 15), 8, 30);
        bb.rect(-250, -1300, 600, 260, rgba(200, 160, 40), 0, 0, 30);
        bb.rect(-120, -1360, 110, 70, rgba(200, 200, 200), 0, 0, 10);
        bb.circle(160, -1325, 50, rgba(200, 200, 200));
        bb.text(50, -800, v, 150, rgba(255, 255, 255));
        bb.text(-60, -1170, "+", 90, rgba(40, 40, 40));
        bb.text(160, -1170, "−", 90, rgba(40, 40, 40));
    }
    for (int i = 0; i < 2; i++) bb.rect(i * 100.f - 15, -40, 30, 50, LEG);
    G s{d.gfx[SCH]};
    s.line(0, 0, 0, 170, SW, INK);
    s.line(-120, 170, 120, 170, SW * 1.4f, INK);
    s.line(-60, 230, 60, 230, SW * 2.2f, INK);
    s.line(0, 230, 0, 400, SW, INK);
    s.text(-90, 110, "+", 60, INK);
    s.text(170, 200, v, 55, INKTXT, 0);
    G pc{d.gfx[PCB]};
    silkRect(pc, -100, -90, 300, 180);
    pc.text(100, -150, "+", 70, SILK);
}

void makeTransistor(PartDef& d, const Props& p) {
    bool pnp = prop(p, "polarity", "NPN") == "PNP";
    d.title = std::string(pnp ? "PNP" : "NPN") + " Transistor";
    d.description = "Bipolar junction transistor (TO-92, pinout E-B-C). A small base current switches a larger collector current.";
    d.labelPrefix = "Q";
    d.valueProp = "part";
    d.sim = pnp ? "pnp" : "npn";
    d.propDefs = {{"polarity", "Type", {"NPN", "PNP"}}, {"part", "Part #", {"2N2222", "2N3904", "2N3906", "BC547", "BC557"}}};
    addPin(d, "E", "emitter", {0, 0}, {200, 200}, {0, 0});
    addPin(d, "B", "base", {100, 0}, {0, 0}, {100, 0});
    addPin(d, "C", "collector", {200, 0}, {200, -200}, {200, 0});
    d.pins[0].square = true;
    G bb{d.gfx[BB]};
    for (int i = 0; i < 3; i++) bb.line(i * 100.f, -110, i * 100.f, 0, 14, LEG);
    bb.rect(-60, -330, 320, 220, rgba(35, 35, 35), 0, 0, 10);
    bb.circle(100, -330, 160, rgba(35, 35, 35));
    bb.rect(-60, -330, 320, 220, rgba(35, 35, 35));
    bb.text(100, -230, prop(p, "part", "2N2222"), 42, rgba(200, 200, 200));
    G s{d.gfx[SCH]};
    s.circle(150, 0, 125, 0, INK, SW);
    s.line(0, 0, 100, 0, SW, INK);
    s.line(100, -70, 100, 70, SW * 1.4f, INK);
    s.polyline({{100, -35}, {200, -110}, {200, -200}}, SW, INK);
    s.polyline({{100, 35}, {200, 110}, {200, 200}}, SW, INK);
    if (pnp) arrowHead(s, {110, 42}, {-1, -0.75f}, 45, INK);
    else arrowHead(s, {195, 107}, {1, 0.75f}, 45, INK);
    G pc{d.gfx[PCB]};
    pc.arc(100, 30, 170, 180, 360, SKW, SILK);
    pc.line(-70, 30, 270, 30, SKW, SILK);
}

void makeMosfet(PartDef& d, const Props& p) {
    (void)p;
    d.title = "N-Channel MOSFET";
    d.description = "Logic-level power MOSFET (TO-220, G-D-S). Drives motors and LED strips from a microcontroller pin.";
    d.labelPrefix = "Q";
    d.valueProp = "part";
    d.sim = "nmos";
    d.propDefs = {{"part", "Part #", {"IRLZ44N", "IRF520", "2N7000"}}};
    addPin(d, "G", "gate", {0, 0}, {0, 100}, {0, 0}).square = true;
    addPin(d, "D", "drain", {100, 0}, {200, -100}, {100, 0});
    addPin(d, "S", "source", {200, 0}, {200, 300}, {200, 0});
    G bb{d.gfx[BB]};
    for (int i = 0; i < 3; i++) bb.line(i * 100.f, -160, i * 100.f, 0, 18, LEG);
    bb.rect(-100, -800, 400, 330, rgba(190, 190, 195), rgba(130, 130, 135), 6, 10);
    bb.circle(100, -660, 60, rgba(255, 255, 255), rgba(130, 130, 135), 6);
    bb.rect(-100, -480, 400, 330, rgba(35, 35, 35), 0, 0, 6);
    bb.text(100, -330, prop(p, "part", "IRLZ44N"), 50, rgba(200, 200, 200));
    G s{d.gfx[SCH]};
    s.line(0, 100, 80, 100, SW, INK);
    s.line(80, 0, 80, 200, SW, INK);
    for (int i = 0; i < 3; i++) s.line(120, -10 + i * 100.f, 120, 50 + i * 100.f, SW * 1.4f, INK);
    s.polyline({{120, 20}, {200, 20}, {200, -100}}, SW, INK);
    s.polyline({{120, 180}, {200, 180}, {200, 300}}, SW, INK);
    s.line(120, 100, 200, 100, SW, INK);
    s.line(200, 100, 200, 180, SW, INK);
    arrowHead(s, {125, 100}, {-1, 0}, 40, INK);
    G pc{d.gfx[PCB]};
    silkRect(pc, -110, -90, 420, 180);
    pc.line(-110, -50, 310, -50, SKW, SILK);
}

void makeRegulator(PartDef& d, const Props& p) {
    (void)p;
    d.title = "Voltage Regulator";
    d.description = "Linear regulator (TO-220, IN-GND-OUT). Needs ~2V of headroom above the output voltage.";
    d.labelPrefix = "U";
    d.valueProp = "voltage";
    d.sim = "reg";
    d.simParams = "IN=1;GND=2;OUT=3;VPROP=voltage;DROP=2";
    d.propDefs = {{"voltage", "Output", {"3.3V", "5V", "9V", "12V"}}, {"part", "Part #", {"7805", "LM1117", "7809", "7812"}}};
    addPin(d, "in", "IN", {0, 0}, {0, 100}, {0, 0}).square = true;
    addPin(d, "gnd", "GND", {100, 0}, {400, 400}, {100, 0});
    addPin(d, "out", "OUT", {200, 0}, {800, 100}, {200, 0});
    G bb{d.gfx[BB]};
    for (int i = 0; i < 3; i++) bb.line(i * 100.f, -160, i * 100.f, 0, 18, LEG);
    bb.rect(-100, -800, 400, 330, rgba(190, 190, 195), rgba(130, 130, 135), 6, 10);
    bb.circle(100, -660, 60, rgba(255, 255, 255), rgba(130, 130, 135), 6);
    bb.rect(-100, -480, 400, 330, rgba(35, 35, 35), 0, 0, 6);
    bb.text(100, -330, prop(p, "part", "7805"), 55, rgba(200, 200, 200));
    G s{d.gfx[SCH]};
    s.rect(200, 0, 400, 250, rgba(255, 255, 255), INK, SW);
    s.line(0, 100, 200, 100, SW, INK);
    s.line(600, 100, 800, 100, SW, INK);
    s.line(400, 250, 400, 400, SW, INK);
    s.text(230, 100, "IN", 45, INKTXT, 0);
    s.text(570, 100, "OUT", 45, INKTXT, 2);
    s.text(400, 210, "GND", 45, INKTXT);
    s.text(400, -60, prop(p, "part", "7805"), 55, INKTXT);
    G pc{d.gfx[PCB]};
    silkRect(pc, -110, -90, 420, 180);
    pc.line(-110, -50, 310, -50, SKW, SILK);
}

// DIP packages: pin i (0-based) — bottom row left→right, then top row right→left.
void makeDip(PartDef& d, int n, const std::string& chip, const std::vector<std::string>& names) {
    n = std::max(4, std::min(64, n + (n & 1)));
    int half = n / 2;
    d.labelPrefix = "U";
    for (int i = 0; i < n; i++) {
        Vec2 pos = i < half ? Vec2(i * 100.f, 300) : Vec2((n - 1 - i) * 100.f, 0);
        std::string nm = i < (int)names.size() && !names[i].empty() ? names[i] : std::to_string(i + 1);
        PinDef& p = addPin(d, "pin" + std::to_string(i + 1), nm, pos, {0, 0}, pos);
        p.square = i == 0;
    }
    float w = (half - 1) * 100.f;
    G bb{d.gfx[BB]};
    for (int i = 0; i < n; i++) {
        Vec2 p = d.pins[i].pos[BB];
        bb.rect(p.x - 20, i < half ? p.y - 60 : p.y, 40, 60, LEG, 0, 0, 4);
    }
    bb.rect(-70, 40, w + 140, 220, rgba(45, 45, 48), rgba(25, 25, 25), 6, 10);
    bb.arc(-70, 150, 40, 270, 450, 8, rgba(110, 110, 110));
    bb.circle(-20, 215, 14, rgba(110, 110, 110));
    bb.text(w / 2, 150, chip, std::min(70.f, (w + 100) / std::max<size_t>(chip.size(), 1) * 1.4f), rgba(220, 220, 220));
    std::vector<int> left, right;
    for (int i = 0; i < half; i++) left.push_back(i);
    for (int i = n - 1; i >= half; i--) right.push_back(i);
    schBox(d, chip, left, right, 600);
    G pc{d.gfx[PCB]};
    silkRect(pc, -80, 60, w + 160, 180);
    pc.arc(-80, 150, 40, 270, 450, SKW, SILK);
}

std::vector<std::string> splitNames(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') { while (!cur.empty() && cur.front() == ' ') cur.erase(cur.begin()); while (!cur.empty() && cur.back() == ' ') cur.pop_back(); out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    while (!cur.empty() && cur.front() == ' ') cur.erase(cur.begin());
    while (!cur.empty() && cur.back() == ' ') cur.pop_back();
    if (!cur.empty() || !out.empty()) out.push_back(cur);
    return out;
}

void makeIcDip(PartDef& d, const Props& p) {
    d.title = "IC (DIP)";
    d.description = "Generic dual-inline IC. Set the pin count, chip label and comma-separated pin names in the Inspector — a quick way to create any chip.";
    d.valueProp = "chip label";
    d.propDefs = {{"pins", "Pins", {"4", "6", "8", "14", "16", "18", "20", "24", "28", "40"}}, {"chip label", "Chip label", {}}, {"pin names", "Pin names", {}}};
    makeDip(d, std::atoi(prop(p, "pins", "8").c_str()), prop(p, "chip label", "IC"), splitNames(prop(p, "pin names")));
}

void make555(PartDef& d, const Props& p) {
    (void)p;
    d.title = "555 Timer";
    d.description = "The classic NE555 timer for oscillators, delays and PWM.";
    d.valueProp = "part";
    d.propDefs = {{"part", "Part #", {"NE555", "LM555", "TLC555"}}};
    makeDip(d, 8, "555", {"GND", "TRIG", "OUT", "RESET", "CTRL", "THR", "DIS", "VCC"});
    d.sim = "555";
    d.simParams = "U GND=1,TRIG=2,OUT=3,RESET=4,CTRL=5,THR=6,DIS=7,VCC=8";
    d.description += " Fully simulated: build an astable circuit and watch the LED blink.";
    std::vector<int> left = {7, 3, 1, 5, 4}, right = {2, 6, -1, -1, 0};
    d.gfx[SCH].clear();
    schBox(d, "555", left, right, 600);
}

void makeHeader(PartDef& d, const Props& p, bool female) {
    int n = std::max(1, std::min(40, std::atoi(prop(p, "pins", "4").c_str())));
    d.title = female ? "Female Header" : "Pin Header";
    d.description = "Single-row 0.1\" header for connecting modules, sensors and jumper wires.";
    d.labelPrefix = "J";
    d.valueProp = "pins";
    d.propDefs = {{"pins", "Pins", {"1", "2", "3", "4", "5", "6", "8", "10", "12", "16", "20", "40"}}};
    for (int i = 0; i < n; i++) {
        PinDef& pin = addPin(d, "pin" + std::to_string(i + 1), std::to_string(i + 1), {i * 100.f, 0}, {0, i * 100.f}, {i * 100.f, 0});
        pin.square = i == 0;
    }
    G bb{d.gfx[BB]};
    float w = n * 100.f;
    if (female) {
        bb.rect(-50, -50, w, 100, rgba(30, 30, 30), rgba(10, 10, 10), 4, 6);
        for (int i = 0; i < n; i++) bb.rect(i * 100.f - 18, -18, 36, 36, rgba(80, 80, 80));
    } else {
        for (int i = 0; i < n; i++) bb.rect(i * 100.f - 12, -170, 24, 170, rgba(220, 180, 60));
        bb.rect(-50, -110, w, 90, rgba(30, 30, 30), 0, 0, 4);
    }
    G s{d.gfx[SCH]};
    s.rect(200, -60, 200, n * 100.f + 20, rgba(255, 255, 255), INK, SW);
    for (int i = 0; i < n; i++) {
        s.line(0, i * 100.f, 200, i * 100.f, SW, INK);
        s.text(230, i * 100.f, std::to_string(i + 1), 45, INKTXT, 0);
    }
    G pc{d.gfx[PCB]};
    silkRect(pc, -50, -50, w, 100);
}

void makeBuzzer(PartDef& d, const Props& p) {
    (void)p;
    d.title = "Piezo Buzzer";
    d.description = "Beeps when powered. Longer leg is +.";
    d.labelPrefix = "BZ";
    d.sim = "buzzer";
    addPin(d, "minus", "−", {0, 0}, {300, 0}, {0, 0});
    addPin(d, "plus", "+", {300, 0}, {0, 0}, {300, 0}).square = true;
    G bb{d.gfx[BB]};
    bb.circle(150, -40, 230, rgba(30, 30, 30), rgba(10, 10, 10), 6);
    bb.circle(150, -40, 40, rgba(70, 70, 70));
    bb.text(250, -170, "+", 70, rgba(220, 220, 220));
    for (auto& pin : d.pins) bb.circle(pin.pos[BB].x, pin.pos[BB].y, 16, LEG);
    G s{d.gfx[SCH]};
    s.line(0, 0, 100, 0, SW, INK);
    s.line(200, 0, 300, 0, SW, INK);
    s.arc(150, 0, 60, 180, 360, SW, INK);
    s.line(90, 0, 210, 0, SW, INK);
    s.text(60, -60, "+", 55, INK);
    G pc{d.gfx[PCB]};
    pc.circle(150, 0, 240, 0, SILK, SKW);
}

void makeMotor(PartDef& d, const Props& p) {
    (void)p;
    d.title = "DC Motor";
    d.description = "Small brushed DC motor. Drive it through a transistor or MOSFET, never straight from a microcontroller pin.";
    d.labelPrefix = "M";
    d.sim = "motor";
    addPin(d, "pin0", "−", {0, 0}, {0, 300}, {0, 0});
    addPin(d, "pin1", "+", {100, 0}, {0, 0}, {100, 0}).square = true;
    G bb{d.gfx[BB]};
    bb.line(0, -300, 0, 0, 20, rgba(30, 30, 30));
    bb.line(100, -300, 100, 0, 20, rgba(210, 40, 40));
    bb.rect(-250, -950, 600, 650, rgba(185, 185, 190), rgba(120, 120, 125), 8, 80);
    bb.rect(-150, -1020, 400, 80, rgba(130, 130, 135), 0, 0, 20);
    bb.rect(30, -1150, 40, 140, rgba(220, 220, 220));
    bb.circle(50, -620, 100, rgba(160, 160, 165));
    bb.text(50, -620, "M", 120, rgba(80, 80, 80));
    G s{d.gfx[SCH]};
    s.line(0, 0, 0, 50, SW, INK);
    s.line(0, 250, 0, 300, SW, INK);
    s.circle(0, 150, 100, rgba(255, 255, 255), INK, SW);
    s.text(0, 150, "M", 90, INK);
    G pc{d.gfx[PCB]};
    silkRect(pc, -100, -90, 300, 180);
}

void makeMultimeter(PartDef& d, const Props& p) {
    d.title = "Multimeter";
    d.description = "Measures voltage (V+ vs COM) or current through it while the simulator is running.";
    d.labelPrefix = "MM";
    d.sim = "multimeter";
    d.views = (1u << BB) | (1u << SCH);
    d.propDefs = {{"mode", "Mode", {"Voltage (V)", "Current (A)"}}};
    addPin(d, "com", "COM", {0, 0}, {0, 300}, {0, 0});
    addPin(d, "v", "V+", {100, 0}, {0, 0}, {100, 0});
    G bb{d.gfx[BB]};
    bb.line(0, -400, 0, 0, 20, rgba(30, 30, 30));
    bb.line(100, -400, 100, 0, 20, rgba(210, 40, 40));
    bb.rect(-350, -1500, 800, 1100, rgba(250, 190, 30), rgba(190, 140, 10), 10, 60);
    bb.rect(-280, -1430, 660, 300, rgba(170, 190, 160), rgba(60, 60, 60), 10, 10);
    bb.text(50, -1280, "0.00", 160, rgba(30, 40, 30)).tag = 2;
    bb.circle(50, -850, 180, rgba(40, 40, 40));
    bb.line(50, -850, 50, -990, 30, rgba(220, 220, 220));
    bool cur = prop(p, "mode").find("Current") != std::string::npos;
    bb.text(50, -560, cur ? "A" : "V", 80, rgba(40, 40, 40));
    bb.circle(-150, -480, 45, rgba(30, 30, 30));
    bb.circle(250, -480, 45, rgba(210, 40, 40));
    G s{d.gfx[SCH]};
    s.line(0, 0, 0, 50, SW, INK);
    s.line(0, 250, 0, 300, SW, INK);
    s.circle(0, 150, 100, rgba(255, 255, 255), INK, SW);
    s.text(0, 150, cur ? "A" : "V", 90, INK);
}

// Uno-footprint boards (Uno, Leonardo, Uno R4) and Mega-footprint boards (Mega 2560, Due).
void makeArduinoBoard(PartDef& d, const ArduinoSpec& s) {
    d.title = s.title;
    d.description = s.desc + " In simulation it is USB-powered: 5V and 3.3V pins supply the circuit, and the pins listed under \"Pin outputs\" are driven (e.g. D13=HIGH, D9=LOW, D5=BLINK:500, D6=PWM:30). Firmware itself is not executed.";
    d.labelPrefix = "A";
    d.sim = "board";
    d.simParams = "GND=GND;RAIL 5V=5;RAIL 3V3=3.3;IO=" + std::string(s.io3v3 ? "3.3" : "5") + ";ROUT=25";
    d.propDefs = {{"type", "Type", {s.title}},
                  {"outputs", "Pin outputs (simulation)", {"D13=HIGH", "D13=LOW", "D13=BLINK:500", "D9=HIGH, D10=HIGH, D11=HIGH", ""}}};
    const float W = s.mega ? 4000.f : 2700.f;
    // Top header (screen y=100) right→left: D0..D7, then D8..D13, GND, AREF, SDA, SCL.
    std::vector<std::pair<std::string, Vec2>> pins;
    for (int i = 0; i <= 7; i++) pins.push_back({"D" + std::to_string(i), {2600.f - i * 100.f, 100}});
    for (int i = 8; i <= 13; i++) pins.push_back({"D" + std::to_string(i), {1740.f - (i - 8) * 100.f, 100}});
    pins.push_back({"GND", {1140, 100}}); pins.push_back({"AREF", {1040, 100}}); pins.push_back({"SDA", {940, 100}}); pins.push_back({"SCL", {840, 100}});
    const char* power[] = {"NC", "IOREF", "RESET", "3V3", "5V", "GND", "GND", "VIN"};
    for (int i = 0; i < 8; i++) pins.push_back({power[i], {1060.f + i * 100, 2000}});
    int nA = s.mega ? 8 : 6;
    for (int i = 0; i < nA; i++) pins.push_back({"A" + std::to_string(i), {1960.f + i * 100.f, 2000}});
    if (s.mega) {
        const char* comm[] = {"D14/TX3", "D15/RX3", "D16/TX2", "D17/RX2", "D18/TX1", "D19/RX1", "D20/SDA", "D21/SCL"};
        for (int i = 0; i < 8; i++) pins.push_back({comm[i], {2760.f + i * 100.f, 100}});
        for (int i = 0; i < 8; i++) pins.push_back({s.bottom2[(size_t)i], {2760.f + i * 100.f, 2000}});
        pins.push_back({"5V", {3700, 250}}); pins.push_back({"5V", {3800, 250}});
        for (int k = 0; k < 16; k++) {
            pins.push_back({"D" + std::to_string(22 + 2 * k), {3700, 350.f + k * 100.f}});
            pins.push_back({"D" + std::to_string(23 + 2 * k), {3800, 350.f + k * 100.f}});
        }
        pins.push_back({"GND", {3700, 1950}}); pins.push_back({"GND", {3800, 1950}});
    }
    std::map<std::string, int> cnt;
    std::map<std::string, std::vector<int>> same;
    for (auto& pp : pins) {
        const std::string& n = pp.first;
        std::string id;
        for (char c : n) id += c == '/' ? '_' : c;
        if (cnt[n]++) id += "_" + std::to_string(cnt[n]);
        PinDef& pin = addPin(d, id, n, pp.second, {0, 0}, pp.second);
        pin.pad = 66; pin.drill = 40;
        same[n].push_back((int)d.pins.size() - 1);
    }
    for (const char* n : {"GND", "5V"}) if (same[n].size() > 1) d.buses.push_back(same[n]);

    G bb{d.gfx[BB]};
    uint32_t base = s.color, dark = rgba((base >> 24) * 7 / 10, ((base >> 16) & 255) * 7 / 10, ((base >> 8) & 255) * 7 / 10), white = rgba(240, 250, 250);
    std::vector<Vec2> outline = {{0, 40}, {W - 100, 40}, {W, 140}, {W, 1900}, {W - 100, 2060}, {0, 2060}};
    bb.poly(outline, base, dark, 12);
    if (s.usbC) bb.rect(-120, 380, 360, 300, rgba(200, 200, 205), rgba(140, 140, 145), 8, 40);
    else if (s.microUsb) bb.rect(-80, 420, 260, 220, rgba(200, 200, 205), rgba(140, 140, 145), 8, 20);
    else bb.rect(-250, 300, 650, 520, rgba(200, 200, 205), rgba(140, 140, 145), 8, 10);  // USB-B
    bb.rect(-180, 1500, 580, 420, rgba(30, 30, 30), 0, 0, 10);                          // DC jack
    // headers: black strips under each run of pins
    std::vector<Rect> strips = {{785, 45, 1010, 110}, {1845, 45, 810, 110}, {1005, 1945, 810, 110}, {1905, 1945, nA * 100.f + 10, 110}};
    if (s.mega) {
        strips.push_back({2705, 45, 810, 110});
        strips.push_back({2705, 1945, 810, 110});
        strips.push_back({3645, 195, 210, 1810});
    }
    for (auto& r : strips) bb.rect(r.x, r.y, r.w, r.h, rgba(25, 25, 25), 0, 0, 6);
    for (auto& pin : d.pins) bb.rect(pin.pos[BB].x - 20, pin.pos[BB].y - 20, 40, 40, rgba(70, 70, 70));
    for (auto& pin : d.pins) {
        Vec2 p = pin.pos[BB];
        std::string lbl = pin.name.substr(0, pin.name.find('/'));
        if (lbl.size() >= 2 && lbl[0] == 'D' && std::isdigit((unsigned char)lbl[1])) lbl = lbl.substr(1);
        if (s.mega && p.x > 3650 && p.y > 200 && p.y < 2000) {
            bool left = p.x < 3750;
            bb.text(left ? 3620 : 3880, p.y, lbl, 34, white, left ? 2 : 0);
            continue;
        }
        bool isTop = p.y < 1000;
        bb.text(p.x, isTop ? 210 : 1890, lbl, lbl.size() > 3 ? 26 : 38, white);
    }
    bb.text(2200, 290, "DIGITAL (PWM~)", 55, white);
    bb.text(1400, 1800, "POWER", 55, white);
    bb.text(2200, 1800, "ANALOG IN", 55, white);
    if (s.mega) bb.text(3100, 290, "COMMUNICATION", 55, white);
    float cx = s.mega ? 2300.f : 1875.f;
    if (s.mega) bb.rect(cx - 330, 980, 660, 660, rgba(35, 35, 38), 0, 0, 10);
    else bb.rect(1150, 1300, 1450, 340, rgba(35, 35, 38), 0, 0, 10);
    bb.text(cx, s.mega ? 1310 : 1470, s.chip, s.mega ? 60 : std::min(90.f, 1300.f / std::max<size_t>(s.chip.size(), 1) * 1.4f), rgba(170, 170, 170));
    bb.text(1350, 800, s.logo, std::min(260.f, 1500.f / std::max<size_t>(s.logo.size(), 1) * 1.6f), white);
    bb.text(1350, 1050, "ARDUINO", 110, white);
    if (s.ledMatrix) {
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 12; c++) bb.circle(2050 + c * 45.f, 480 + r * 45.f, 13, rgba(240, 70, 60, 120));
    }
    bb.rect(600, 440, 200, 120, rgba(200, 200, 205), 0, 0, 8);  // reset button
    bb.circle(700, 500, 40, rgba(200, 40, 40));
    bb.rect(1000, 450, 50, 30, rgba(240, 200, 60), 0, 0, 4);
    bb.text(1090, 465, "L", 35, white, 0);
    std::vector<Vec2> holes = {{550, 100}, {600, 2000}, {2600, 1400}, {2600, 300}};
    if (s.mega) holes = {{550, 100}, {600, 2000}, {2600, 1400}, {2600, 300}, {3500, 1400}, {3500, 600}};
    std::vector<Vec2> drawn = {{150, 150}, {2550, 800}, {150, 1950}};
    if (s.mega) drawn = {{150, 150}, {2550, 800}, {150, 1950}, {3450, 500}, {3450, 1600}};
    for (auto& h : drawn) bb.circle(h.x, h.y, 55, rgba(255, 255, 255), rgba(180, 180, 180), 6);

    // schematic: power + analog on the left, digital on the right, balanced
    std::vector<int> left, right, gnd;
    for (const char* n : {"VIN", "5V", "3V3", "AREF", "IOREF", "RESET", "NC"}) left.push_back(d.pinIndex(n));
    for (size_t i = 0; i < d.pins.size(); i++) {
        const std::string& n = d.pins[i].name;
        bool dig = (n[0] == 'D' && std::isdigit((unsigned char)n[1])) || n == "SDA" || n == "SCL";
        if (dig) right.push_back((int)i);
        else if (n == "GND") gnd.push_back((int)i);
        else if (n == "5V" && d.pins[i].id != "5V") gnd.push_back((int)i);
        else if (std::find(left.begin(), left.end(), (int)i) == left.end()) left.push_back((int)i);
    }
    left.insert(left.end(), gnd.begin(), gnd.end());
    auto num = [&](int i) { const std::string& n = d.pins[(size_t)i].name; return n[0] == 'D' ? std::atoi(n.c_str() + 1) : 1000 + (int)n.size(); };
    std::stable_sort(right.begin(), right.end(), [&](int a, int b) { return num(a) < num(b); });
    while (right.size() > left.size() + 1) { left.push_back(right.front()); right.erase(right.begin()); }
    schBox(d, s.title, left, right, 800);

    G pc{d.gfx[PCB]};
    std::vector<Vec2> ol = outline;
    ol.push_back(outline[0]);
    pc.polyline(ol, SKW, SILK);
    for (auto& pin : d.pins) {
        Vec2 p = pin.pos[PCB];
        if (s.mega && p.x > 3650 && p.y > 200 && p.y < 2000) continue;
        bool isTop = p.y < 1000;
        std::string lbl = pin.name.substr(0, std::min<size_t>(3, pin.name.find('/')));
        pc.text(p.x, isTop ? 200 : 1900, lbl, 30, SILK);
    }
    d.holes.clear();
    for (auto& h : holes) d.holes.push_back({h, 125});
    pc.text(1350, 1050, s.title + " shield", 70, SILK);
}

void makeArduinoUno(PartDef& d, const Props& p) {
    (void)p;
    ArduinoSpec s;
    s.title = "Arduino Uno (Rev3)";
    s.chip = "ATMEGA328P";
    s.logo = "UNO";
    s.desc = "ATmega328P microcontroller board, 5 V logic.";
    makeArduinoBoard(d, s);
}

void makeBreadboard(PartDef& d, const Props& p) {
    std::string size = prop(p, "size", "half+");
    int cols = size == "full+" ? 63 : size == "mini" ? 17 : 30;
    bool rails = size != "mini";
    d.title = size == "full+" ? "Full+ Breadboard" : size == "mini" ? "Mini Breadboard" : "Half+ Breadboard";
    d.description = "Solderless breadboard. Each column of 5 holes (a–e, f–j) is connected; the long rails carry power. Parts snap into the holes.";
    d.labelPrefix = "BB";
    d.isBreadboard = true;
    d.views = 1u << BB;
    d.propDefs = {{"size", "Size", {"half+", "full+", "mini"}}};
    const char* rowsTop = "jihgf";
    const char* rowsBot = "edcba";
    float yTop0 = rails ? 300 : 0;
    float yBot0 = yTop0 + 700;
    float yEnd = yBot0 + 400;
    float x1 = (cols - 1) * 100.f;
    G bb{d.gfx[BB]};
    float top = rails ? -150 : -150, bottom = rails ? yEnd + 450 : yEnd + 150;
    bb.rect(-200, top, x1 + 400, bottom - top, rgba(236, 236, 234), rgba(205, 205, 200), 8, 20);
    bb.rect(-200, yTop0 + 470, x1 + 400, 60, rgba(205, 205, 200));
    uint32_t hole = rgba(70, 70, 70);
    auto addHole = [&](const std::string& id, const std::string& name, Vec2 pos) {
        addPin(d, id, name, pos, pos, pos);
        bb.rect(pos.x - 17, pos.y - 17, 34, 34, hole, 0, 0, 3);
        return (int)d.pins.size() - 1;
    };
    for (int c = 0; c < cols; c++) {
        std::vector<int> busT, busB;
        for (int r = 0; r < 5; r++) {
            std::string a(1, rowsTop[r]), b(1, rowsBot[r]);
            busT.push_back(addHole(a + std::to_string(c + 1), a + std::to_string(c + 1), {c * 100.f, yTop0 + r * 100.f}));
            busB.push_back(addHole(b + std::to_string(c + 1), b + std::to_string(c + 1), {c * 100.f, yBot0 + r * 100.f}));
        }
        d.buses.push_back(busT);
        d.buses.push_back(busB);
        if ((c + 1) % 5 == 0 || c == 0) {
            bb.text(c * 100.f, yTop0 - 90, std::to_string(c + 1), 45, rgba(120, 120, 120));
            bb.text(c * 100.f, yEnd + 90, std::to_string(c + 1), 45, rgba(120, 120, 120));
        }
    }
    for (int r = 0; r < 5; r++) {
        bb.text(-110, yTop0 + r * 100.f, std::string(1, rowsTop[r]), 45, rgba(120, 120, 120));
        bb.text(x1 + 110, yTop0 + r * 100.f, std::string(1, rowsTop[r]), 45, rgba(120, 120, 120));
        bb.text(-110, yBot0 + r * 100.f, std::string(1, rowsBot[r]), 45, rgba(120, 120, 120));
        bb.text(x1 + 110, yBot0 + r * 100.f, std::string(1, rowsBot[r]), 45, rgba(120, 120, 120));
    }
    if (rails) {
        struct Rail { const char* id; const char* name; float y; };
        Rail rs[] = {{"tp", "top +", 0}, {"tn", "top −", 100}, {"bp", "bottom +", yEnd + 200}, {"bn", "bottom −", yEnd + 300}};
        for (auto& r : rs) {
            std::vector<int> bus;
            int k = 0;
            for (int c = 1; c < cols - 1; c++) {
                if (c % 6 == 0) continue;
                k++;
                bus.push_back(addHole(std::string(r.id) + std::to_string(k), std::string(r.name) + " " + std::to_string(k), {c * 100.f, r.y}));
            }
            d.buses.push_back(bus);
        }
        uint32_t red = rgba(220, 60, 60), blue = rgba(60, 90, 210);
        bb.line(0, -80, x1, -80, 8, red);
        bb.line(0, 180, x1, 180, 8, blue);
        bb.line(0, yEnd + 120, x1, yEnd + 120, 8, red);
        bb.line(0, yEnd + 380, x1, yEnd + 380, 8, blue);
        bb.text(-110, 0, "+", 60, red);
        bb.text(-110, 100, "−", 60, blue);
        bb.text(-110, yEnd + 200, "+", 60, red);
        bb.text(-110, yEnd + 300, "−", 60, blue);
    }
}

void makeGround(PartDef& d, const Props& p) {
    (void)p;
    d.title = "Ground";
    d.description = "Schematic ground symbol. All ground symbols are connected to each other (net \"GND\").";
    d.labelPrefix = "GND";
    d.netProp = "net";
    d.views = 1u << SCH;
    d.defaults["net"] = "GND";
    addPin(d, "gnd", "GND", {0, 0}, {0, 0}, {0, 0});
    G s{d.gfx[SCH]};
    s.line(0, 0, 0, 100, SW, INK);
    s.line(-90, 100, 90, 100, SW, INK);
    s.line(-55, 140, 55, 140, SW, INK);
    s.line(-20, 180, 20, 180, SW, INK);
}

void makePowerLabel(PartDef& d, const Props& p) {
    d.title = "Power Label";
    d.description = "Schematic power symbol. Symbols with the same net name (e.g. 5V) are connected.";
    d.labelPrefix = "V";
    d.netProp = "net";
    d.valueProp = "";
    d.views = 1u << SCH;
    d.propDefs = {{"net", "Net name", {"VCC", "5V", "3V3", "12V", "VIN"}}};
    addPin(d, "pin", "power", {0, 0}, {0, 0}, {0, 0});
    G s{d.gfx[SCH]};
    s.line(0, 0, 0, -100, SW, INK);
    s.line(-80, -100, 80, -100, SW * 1.4f, INK);
    s.text(0, -160, prop(p, "net", "VCC"), 55, INK);
}

void makeNetLabel(PartDef& d, const Props& p) {
    d.title = "Net Label";
    d.description = "Connects schematic wires by name instead of drawing a wire.";
    d.labelPrefix = "N";
    d.netProp = "net";
    d.views = 1u << SCH;
    d.propDefs = {{"net", "Net name", {}}};
    addPin(d, "pin", "net", {0, 0}, {0, 0}, {0, 0});
    std::string n = prop(p, "net", "NET1");
    float w = 60 + 36 * (float)n.size();
    G s{d.gfx[SCH]};
    s.poly({{0, 0}, {50, -50}, {50 + w, -50}, {50 + w, 50}, {50, 50}}, rgba(255, 255, 255), INK, SW);
    s.text(50 + w / 2, 0, n, 50, INK);
}

void makeVia(PartDef& d, const Props& p) {
    (void)p;
    d.title = "Via";
    d.description = "Plated hole connecting top and bottom copper layers.";
    d.labelPrefix = "V";
    d.views = 1u << PCB;
    PinDef& pin = addPin(d, "via", "via", {0, 0}, {0, 0}, {0, 0});
    pin.pad = 40; pin.drill = 20;
}

void makeHole(PartDef& d, const Props& p) {
    d.title = "Mounting Hole";
    d.description = "Non-plated hole for screws and standoffs.";
    d.labelPrefix = "H";
    d.views = 1u << PCB;
    d.valueProp = "diameter";
    d.propDefs = {{"diameter", "Diameter", {"2.2mm", "3.2mm", "4.3mm"}}};
    float dia = (float)(parseSI(prop(p, "diameter", "3.2mm"), 3.2) / 25.4e-3) / 1000.f;
    if (prop(p, "diameter").find("mm") != std::string::npos) dia = (float)(std::atof(prop(p, "diameter").c_str()) / 0.0254);
    d.holes = {{{0, 0}, dia}};
    G pc{d.gfx[PCB]};
    pc.circle(0, 0, dia / 2 + 20, 0, SILK, SKW);
}

void makeBoard(PartDef& d, const Props& p) {
    d.title = "PCB";
    d.description = "Printed circuit board outline. Set the size in millimetres in the Inspector.";
    d.labelPrefix = "PCB";
    d.views = 1u << PCB;
    d.isBoard = true;
    d.propDefs = {{"width", "Width (mm)", {"50", "80", "100"}}, {"height", "Height (mm)", {"30", "50", "80"}},
                  {"layers", "Layers", {"two layers (double-sided)", "one layer (single-sided)"}}};
    float w = (float)std::max(5.0, std::atof(prop(p, "width", "80").c_str())) / 0.0254f;
    float h = (float)std::max(5.0, std::atof(prop(p, "height", "50").c_str())) / 0.0254f;
    G pc{d.gfx[PCB]};
    pc.rect(0, 0, w, h, rgba(52, 110, 60), rgba(180, 180, 160), 12, 0);
}

const std::map<std::string, Gen>& generators() {
    static std::map<std::string, Gen> g = {
        {"resistor", {"Resistor", "Core", {{"resistance", "220Ω"}, {"power", "1/4W"}, {"tolerance", "±5%"}}, makeResistor}},
        {"led", {"LED", "Core", {{"color", "Red"}, {"package", "5 mm [THT]"}}, makeLed}},
        {"capacitor_ceramic", {"Ceramic Capacitor", "Core", {{"capacitance", "100nF"}, {"voltage", "50V"}}, makeCapCeramic}},
        {"capacitor_electrolytic", {"Electrolytic Capacitor", "Core", {{"capacitance", "100µF"}, {"voltage", "16V"}}, makeCapElectrolytic}},
        {"diode", {"Diode", "Core", {{"part", "1N4001"}}, makeDiode}},
        {"inductor", {"Inductor", "Core", {{"inductance", "100µH"}}, makeInductor}},
        {"crystal", {"Crystal", "Core", {{"frequency", "16MHz"}}, makeCrystal}},
        {"potentiometer", {"Potentiometer", "Input", {{"resistance", "10kΩ"}, {"wiper", "50"}}, makePot}},
        {"photocell", {"Photocell (LDR)", "Input", {{"light", "50"}}, makePhotocell}},
        {"pushbutton", {"Pushbutton", "Input", {{"form", "[THT] 6mm"}, {"pressed", "no"}}, makePushbutton}},
        {"switch", {"Slide Switch", "Input", {{"position", "left"}}, makeSwitch}},
        {"battery", {"Battery", "Power", {{"voltage", "9V"}, {"style", "9V block"}}, makeBattery}},
        {"regulator", {"Voltage Regulator", "Power", {{"voltage", "5V"}, {"part", "7805"}}, makeRegulator}},
        {"transistor", {"Transistor", "ICs", {{"polarity", "NPN"}, {"part", "2N2222"}}, makeTransistor}},
        {"mosfet", {"MOSFET", "ICs", {{"part", "IRLZ44N"}}, makeMosfet}},
        {"timer555", {"555 Timer", "ICs", {{"part", "NE555"}}, make555}},
        {"ic_dip", {"IC (DIP)", "ICs", {{"pins", "8"}, {"chip label", "IC"}, {"pin names", ""}}, makeIcDip}},
        {"buzzer", {"Piezo Buzzer", "Output", {}, makeBuzzer}},
        {"dc_motor", {"DC Motor", "Output", {}, makeMotor}},
        {"arduino_uno", {"Arduino Uno", "Microcontroller", {{"type", "Arduino Uno (Rev3)"}, {"outputs", "D13=HIGH"}}, makeArduinoUno}},
        {"breadboard", {"Breadboard", "Breadboard", {{"size", "half+"}}, makeBreadboard}},
        {"header_male", {"Pin Header", "Connection", {{"pins", "4"}}, [](PartDef& d, const Props& p) { makeHeader(d, p, false); }}},
        {"header_female", {"Female Header", "Connection", {{"pins", "4"}}, [](PartDef& d, const Props& p) { makeHeader(d, p, true); }}},
        {"ground", {"Ground", "Schematic", {{"net", "GND"}}, makeGround}},
        {"power_label", {"Power Label", "Schematic", {{"net", "5V"}}, makePowerLabel}},
        {"net_label", {"Net Label", "Schematic", {{"net", "NET1"}}, makeNetLabel}},
        {"pcb_board", {"PCB", "PCB", {{"width", "80"}, {"height", "50"}, {"layers", "two layers (double-sided)"}}, makeBoard}},
        {"via", {"Via", "PCB", {}, makeVia}},
        {"hole", {"Mounting Hole", "PCB", {{"diameter", "3.2mm"}}, makeHole}},
        {"multimeter", {"Multimeter", "Tools", {{"mode", "Voltage (V)"}}, makeMultimeter}},
    };
    static bool catalogAdded = [] { addCatalogGenerators(g); return true; }();
    (void)catalogAdded;
    return g;
}

std::mutex g_mutex;
std::map<std::string, PartDefPtr> g_cache;
std::map<std::string, PartDef> g_custom;

}  // namespace plib

using namespace plib;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

const std::vector<LibEntry>& partLibrary() {
    static std::vector<LibEntry> lib = [] {
        std::vector<LibEntry> v;
        auto add = [&](const char* type, const char* title, Props pr = {}) {
            v.push_back({type, title, generators().at(type).category, pr});
        };
        add("resistor", "Resistor");
        add("led", "Red LED", {{"color", "Red"}});
        add("led", "Green LED", {{"color", "Green"}});
        add("led", "Blue LED", {{"color", "Blue"}});
        add("led", "Yellow LED", {{"color", "Yellow"}});
        add("led", "White LED", {{"color", "White"}});
        add("capacitor_ceramic", "Ceramic Capacitor");
        add("capacitor_electrolytic", "Electrolytic Capacitor");
        add("diode", "Rectifier Diode");
        add("inductor", "Inductor");
        add("crystal", "Crystal");
        add("potentiometer", "Potentiometer");
        add("photocell", "Photocell (LDR)");
        add("pushbutton", "Pushbutton");
        add("switch", "Slide Switch");
        add("battery", "9V Battery", {{"style", "9V block"}, {"voltage", "9V"}});
        add("battery", "2×AA Battery Pack", {{"style", "AA pack"}, {"voltage", "3V"}});
        add("battery", "Coin Cell", {{"style", "Coin cell"}, {"voltage", "3V"}});
        add("regulator", "Voltage Regulator");
        add("transistor", "NPN Transistor", {{"polarity", "NPN"}, {"part", "2N2222"}});
        add("transistor", "PNP Transistor", {{"polarity", "PNP"}, {"part", "2N3906"}});
        add("mosfet", "N-Channel MOSFET");
        add("timer555", "555 Timer");
        add("ic_dip", "IC (DIP-8)", {{"pins", "8"}});
        add("ic_dip", "IC (DIP-16)", {{"pins", "16"}, {"chip label", "74HC595"},
                                       {"pin names", "QB,QC,QD,QE,QF,QG,QH,GND,QH',SRCLR,SRCLK,RCLK,OE,SER,QA,VCC"}});
        add("buzzer", "Piezo Buzzer");
        add("dc_motor", "DC Motor");
        add("arduino_uno", "Arduino Uno");
        add("breadboard", "Half+ Breadboard", {{"size", "half+"}});
        add("breadboard", "Full+ Breadboard", {{"size", "full+"}});
        add("breadboard", "Mini Breadboard", {{"size", "mini"}});
        add("header_male", "Pin Header");
        add("header_female", "Female Header");
        add("ground", "Ground");
        add("power_label", "Power Label");
        add("net_label", "Net Label");
        add("pcb_board", "PCB (Rectangle)");
        add("via", "Via");
        add("hole", "Mounting Hole");
        add("multimeter", "Multimeter");
        for (auto& e : catalogEntries()) v.push_back(e);
        return v;
    }();
    return lib;
}

const std::vector<std::string>& partCategories() {
    static std::vector<std::string> c = {"Core", "Input", "Output", "Power", "ICs", "Logic", "Analog ICs", "Drivers", "Microcontroller", "Sensors", "Displays", "Modules", "Breadboard", "Connection", "Schematic", "PCB", "Tools"};
    return c;
}

bool isKnownType(const std::string& type) {
    std::lock_guard<std::mutex> lk(g_mutex);
    return generators().count(type) || g_custom.count(type);
}

bool isCustomType(const std::string& type) {
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_custom.count(type) > 0;
}

Props mergedProps(const std::string& type, const Props& props) {
    Props out;
    auto it = generators().find(type);
    if (it != generators().end()) out = it->second.defaults;
    else {
        std::lock_guard<std::mutex> lk(g_mutex);
        auto c = g_custom.find(type);
        if (c != g_custom.end()) out = c->second.defaults;
    }
    for (auto& kv : props) out[kv.first] = kv.second;
    return out;
}

PartDefPtr getPartDef(const std::string& type, const Props& props) {
    Props merged = mergedProps(type, props);
    std::string key = type;
    for (auto& kv : merged) { key += '\x1f'; key += kv.first; key += '='; key += kv.second; }
    std::lock_guard<std::mutex> lk(g_mutex);
    auto hit = g_cache.find(key);
    if (hit != g_cache.end()) return hit->second;
    auto def = std::make_shared<PartDef>();
    auto c = g_custom.find(type);
    auto gi = generators().find(type);
    if (c != g_custom.end()) {
        *def = c->second;
    } else if (gi != generators().end()) {
        def->type = type;
        def->category = gi->second.category;
        def->defaults = gi->second.defaults;
        gi->second.make(*def, merged);
        if (def->title.empty()) def->title = gi->second.title;
        for (auto& kv : def->defaults) if (!merged.count(kv.first)) merged[kv.first] = kv.second;
    } else {
        return nullptr;
    }
    def->computeBounds();
    if (g_cache.size() > 4000) g_cache.clear();
    g_cache[key] = def;
    return def;
}

void registerCustomPart(const PartDef& def) {
    std::lock_guard<std::mutex> lk(g_mutex);
    PartDef d = def;
    d.custom = true;
    d.computeBounds();
    g_custom[d.type] = d;
    // invalidate cached variants of this type
    for (auto it = g_cache.begin(); it != g_cache.end();) {
        if (it->first.compare(0, d.type.size(), d.type) == 0 && (it->first.size() == d.type.size() || it->first[d.type.size()] == '\x1f'))
            it = g_cache.erase(it);
        else ++it;
    }
}

void clearCustomParts() {
    std::lock_guard<std::mutex> lk(g_mutex);
    for (auto& kv : g_custom) {
        const std::string& t = kv.first;
        for (auto it = g_cache.begin(); it != g_cache.end();) {
            if (it->first.compare(0, t.size(), t) == 0) it = g_cache.erase(it); else ++it;
        }
    }
    g_custom.clear();
}

// ---------------------------------------------------------------------------
// JSON (de)serialization of part definitions
// ---------------------------------------------------------------------------

static Json vec(Vec2 v) { Json a = Json::array(); a.push(Json(v.x)); a.push(Json(v.y)); return a; }
static Vec2 vecOf(const Json& j) { return {j[(size_t)0].f(), j[(size_t)1].f()}; }

static const char* kindName(Prim::Kind k) {
    switch (k) {
        case Prim::LINE: return "line";
        case Prim::RECT: return "rect";
        case Prim::CIRCLE: return "circle";
        case Prim::TEXT: return "text";
        case Prim::POLY: return "poly";
        case Prim::ARC: return "arc";
    }
    return "line";
}

Json partDefToJson(const PartDef& d) {
    Json j = Json::object();
    j.set("type", d.type);
    j.set("title", d.title);
    j.set("category", d.category);
    j.set("description", d.description);
    j.set("labelPrefix", d.labelPrefix);
    if (!d.valueProp.empty()) j.set("valueProp", d.valueProp);
    if (!d.netProp.empty()) j.set("netProp", d.netProp);
    if (!d.sim.empty()) j.set("sim", d.sim);
    if (!d.simParams.empty()) j.set("simParams", d.simParams);
    Json views = Json::array();
    for (int v = 0; v < NUM_VIEWS; v++) if (d.inView(v)) views.push(viewKey(v));
    j.set("views", views);
    if (d.isBreadboard) j.set("isBreadboard", true);
    if (d.isBoard) j.set("isBoard", true);
    Json props = Json::object();
    for (auto& kv : d.defaults) props.set(kv.first, kv.second);
    j.set("properties", props);
    Json pdefs = Json::array();
    for (auto& pd : d.propDefs) {
        Json o = Json::object();
        o.set("key", pd.key);
        o.set("label", pd.label);
        Json opts = Json::array();
        for (auto& s : pd.options) opts.push(s);
        o.set("options", opts);
        pdefs.push(o);
    }
    j.set("propertyDefs", pdefs);
    Json pins = Json::array();
    for (auto& p : d.pins) {
        Json o = Json::object();
        o.set("id", p.id);
        o.set("name", p.name);
        for (int v = 0; v < NUM_VIEWS; v++) o.set(viewKey(v), vec(p.pos[v]));
        o.set("pad", p.pad);
        o.set("drill", p.drill);
        if (p.square) o.set("square", true);
        pins.push(o);
    }
    j.set("pins", pins);
    Json buses = Json::array();
    for (auto& b : d.buses) {
        Json a = Json::array();
        for (int i : b) a.push(d.pins[i].id);
        buses.push(a);
    }
    j.set("buses", buses);
    Json holes = Json::array();
    for (auto& h : d.holes) { Json o = Json::object(); o.set("at", vec(h.first)); o.set("diameter", h.second); holes.push(o); }
    j.set("holes", holes);
    Json gfx = Json::object();
    for (int v = 0; v < NUM_VIEWS; v++) {
        Json arr = Json::array();
        for (auto& p : d.gfx[v]) {
            Json o = Json::object();
            o.set("k", kindName(p.kind));
            switch (p.kind) {
                case Prim::LINE: o.set("a", vec(p.a)); o.set("b", vec(p.b)); break;
                case Prim::RECT: o.set("a", vec(p.a)); o.set("b", vec(p.b)); if (p.r > 0) o.set("r", p.r); break;
                case Prim::CIRCLE: o.set("c", vec(p.a)); o.set("r", p.r); break;
                case Prim::ARC: o.set("c", vec(p.a)); o.set("r", p.r); o.set("a0", p.a0); o.set("a1", p.a1); break;
                case Prim::TEXT: o.set("at", vec(p.a)); o.set("text", p.text); o.set("size", p.size); o.set("align", p.align); break;
                case Prim::POLY: { Json pts = Json::array(); for (auto& q : p.pts) pts.push(vec(q)); o.set("pts", pts); break; }
            }
            if (p.w > 0) o.set("w", p.w);
            if (p.stroke & 0xFF) o.set("stroke", colorToHex(p.stroke));
            if (p.fill & 0xFF) o.set("fill", colorToHex(p.fill));
            if (p.tag) o.set("tag", p.tag);
            arr.push(o);
        }
        gfx.set(viewKey(v), arr);
    }
    j.set("graphics", gfx);
    return j;
}

bool partDefFromJson(const Json& j, PartDef& d, std::string* err) {
    if (!j.isObject() || j["type"].str().empty()) { if (err) *err = "part definition needs a \"type\""; return false; }
    d = PartDef();
    d.type = j["type"].str();
    d.title = j["title"].str(d.type);
    d.category = j["category"].str("Custom");
    d.description = j["description"].str();
    d.labelPrefix = j["labelPrefix"].str("U");
    d.valueProp = j["valueProp"].str();
    d.netProp = j["netProp"].str();
    d.sim = j["sim"].str();
    d.simParams = j["simParams"].str();
    d.isBreadboard = j["isBreadboard"].boolean();
    d.isBoard = j["isBoard"].boolean();
    if (j["views"].isArray()) {
        d.views = 0;
        for (auto& v : j["views"].items())
            for (int k = 0; k < NUM_VIEWS; k++) if (v.str() == viewKey(k)) d.views |= 1u << k;
    }
    for (auto& kv : j["properties"].members()) d.defaults[kv.first] = kv.second.isString() ? kv.second.str() : kv.second.dump();
    for (auto& o : j["propertyDefs"].items()) {
        PropDef pd;
        pd.key = o["key"].str();
        pd.label = o["label"].str(pd.key);
        for (auto& s : o["options"].items()) pd.options.push_back(s.str());
        d.propDefs.push_back(pd);
    }
    for (auto& o : j["pins"].items()) {
        PinDef p;
        p.id = o["id"].str();
        p.name = o["name"].str(p.id);
        for (int v = 0; v < NUM_VIEWS; v++) p.pos[v] = vecOf(o[viewKey(v)]);
        p.pad = o["pad"].f(70);
        p.drill = o["drill"].f(38);
        p.square = o["square"].boolean();
        if (p.id.empty()) { if (err) *err = "pin without id"; return false; }
        d.pins.push_back(p);
    }
    for (auto& b : j["buses"].items()) {
        std::vector<int> bus;
        for (auto& id : b.items()) { int i = d.pinIndex(id.str()); if (i >= 0) bus.push_back(i); }
        if (bus.size() > 1) d.buses.push_back(bus);
    }
    for (auto& h : j["holes"].items()) d.holes.push_back({vecOf(h["at"]), h["diameter"].f(125)});
    const Json& gfx = j["graphics"];
    for (int v = 0; v < NUM_VIEWS; v++) {
        for (auto& o : gfx[viewKey(v)].items()) {
            Prim p;
            std::string k = o["k"].str();
            if (k == "line") { p.kind = Prim::LINE; p.a = vecOf(o["a"]); p.b = vecOf(o["b"]); }
            else if (k == "rect") { p.kind = Prim::RECT; p.a = vecOf(o["a"]); p.b = vecOf(o["b"]); p.r = o["r"].f(); }
            else if (k == "circle") { p.kind = Prim::CIRCLE; p.a = vecOf(o["c"]); p.r = o["r"].f(); }
            else if (k == "arc") { p.kind = Prim::ARC; p.a = vecOf(o["c"]); p.r = o["r"].f(); p.a0 = o["a0"].f(); p.a1 = o["a1"].f(); }
            else if (k == "text") { p.kind = Prim::TEXT; p.a = vecOf(o["at"]); p.text = o["text"].str(); p.size = o["size"].f(60); p.align = o["align"].i(1); }
            else if (k == "poly") { p.kind = Prim::POLY; for (auto& q : o["pts"].items()) p.pts.push_back(vecOf(q)); }
            else continue;
            p.w = o["w"].f();
            p.stroke = o.has("stroke") ? parseColor(o["stroke"].str()) : 0;
            p.fill = o.has("fill") ? parseColor(o["fill"].str()) : 0;
            p.tag = o["tag"].i();
            d.gfx[v].push_back(p);
        }
    }
    d.computeBounds();
    return true;
}
