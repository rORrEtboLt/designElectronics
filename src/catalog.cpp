// Catalog of common ICs, development boards, modules, sensors and displays.
//
// Each entry is data: a package/drawing style, pin names and (when possible) a
// simulation model with its pin mapping in `simParams` (see docs/FILE_FORMAT.md
// "Simulation models"). Pin tokens in simParams are 1-based pin numbers or pin
// names. Parts without a model are "pinout only": they can be wired, routed and
// exported, but the simulator treats them as open circuits.
#include <cmath>

#include "partlib_internal.h"

namespace plib {
namespace {

std::vector<std::string> names(const char* s) { return splitNames(s); }

std::string pinId(const std::string& name, std::map<std::string, int>& used) {
    std::string id;
    for (char c : name) id += (std::isalnum((unsigned char)c) || c == '_' || c == '+' || c == '-' || c == '.') ? c : '_';
    if (id.empty()) id = "pin";
    if (used[id]++) id += "_" + std::to_string(used[id]);
    return id;
}

std::string shortLabel(const std::string& n) {
    if (n.rfind("GPIO", 0) == 0) return n.substr(4);
    return n;
}

void finish(PartDef& d, const std::string& title, const std::string& desc, const std::string& sim, const std::string& params) {
    d.title = title;
    d.sim = sim;
    d.simParams = params;
    d.description = desc;
    if (sim.empty()) d.description += " Pinout only — wire it, route it and export it; the simulator treats it as unconnected.";
}

// Real outline of a dev board, in mm: length along the pin rows, width across
// them, and the distance from the USB end of the board to the first pin.
struct BoardSize {
    float len = 0, wid = 0, first = 0;
    bool usb = true;
};

// ---- two-row development boards (horizontal, straddling the breadboard gap) ----
void makeModule(PartDef& d, const std::string& label, const std::vector<std::string>& top, const std::vector<std::string>& bottom,
                float spacing, uint32_t color, const std::string& chip, uint32_t chipColor, const BoardSize* size = nullptr) {
    d.labelPrefix = "U";
    std::map<std::string, int> used;
    std::vector<int> topIdx, botIdx;
    for (size_t i = 0; i < top.size(); i++) {
        Vec2 p(i * 100.f, 0);
        addPin(d, pinId(top[i], used), top[i], p, {0, 0}, p).square = i == 0;
        topIdx.push_back((int)d.pins.size() - 1);
    }
    for (size_t i = 0; i < bottom.size(); i++) {
        Vec2 p(i * 100.f, spacing);
        addPin(d, pinId(bottom[i], used), bottom[i], p, {0, 0}, p);
        botIdx.push_back((int)d.pins.size() - 1);
    }
    std::map<std::string, std::vector<int>> same;
    for (size_t i = 0; i < d.pins.size(); i++) same[d.pins[i].name].push_back((int)i);
    for (const char* pw : {"GND", "3V3", "3.3V", "5V", "VIN", "VCC"})
        if (same[pw].size() > 1) d.buses.push_back(same[pw]);
    size_t n = std::max(top.size(), bottom.size());
    float w = (n - 1) * 100.f;
    // board outline: true to size when known, otherwise a margin around the pins
    const float mm = 1000.f / 25.4f;
    float x0 = -160, x1 = w + 160, y0 = -150, y1 = spacing + 150;
    bool usb = true;
    if (size) {
        x0 = -size->first * mm;
        x1 = x0 + size->len * mm;
        y0 = spacing / 2 - size->wid * mm / 2;
        y1 = spacing / 2 + size->wid * mm / 2;
        usb = size->usb;
    }
    G bb{d.gfx[BB]};
    bb.rect(x0, y0, x1 - x0, y1 - y0, color, rgba(0, 0, 0, 60), 6, std::min(40.f, (y1 - y0) * 0.06f));
    if (usb) {
        float uw = std::min(320.f, (y1 - y0) * 0.36f);
        bb.rect(x0 - (size ? 50.f : 140.f), spacing / 2 - uw / 2, 250, uw, rgba(205, 205, 210), rgba(140, 140, 145), 6, 12);  // USB
    }
    for (float y : {0.f, spacing}) {
        bb.rect(-50, y - 50, w + 100, 100, rgba(25, 25, 25), 0, 0, 6);
    }
    for (auto& pin : d.pins) bb.rect(pin.pos[BB].x - 18, pin.pos[BB].y - 18, 36, 36, rgba(220, 180, 60));
    float maxLen = 1;
    for (auto& pin : d.pins) maxLen = std::max(maxLen, (float)shortLabel(pin.name).size());
    float ts = std::min(34.f, 95.f / (maxLen * 0.55f));
    for (auto& pin : d.pins) {
        bool isTop = pin.pos[BB].y < spacing / 2;
        bb.text(pin.pos[BB].x, isTop ? 95 : spacing - 95, shortLabel(pin.name), ts, rgba(255, 255, 255, 220));
    }
    float cw = std::min(w * 0.55f, 1400.f), ch = std::max(160.f, spacing - 360);
    bb.rect(w / 2 - cw / 2, spacing / 2 - ch / 2, cw, ch, chipColor, rgba(0, 0, 0, 80), 6, 14);
    bb.text(w / 2, spacing / 2, chip, std::min(90.f, std::min(ch * 0.4f, cw / std::max<size_t>(chip.size(), 1) * 1.5f)), rgba(230, 230, 230));
    schBox(d, label, topIdx, botIdx, 900);
    G pc{d.gfx[PCB]};
    pc.rect(x0, y0, x1 - x0, y1 - y0, 0, SILK, SKW, 40);
    pc.text(w / 2, spacing / 2, label, std::min(70.f, (x1 - x0) / std::max<size_t>(label.size(), 1) * 1.6f), SILK);
}

// ---- single-row breakout modules (sensors, displays, small boards) ----
void makeBreakout(PartDef& d, const std::string& label, const std::vector<std::string>& pinNames, float bw, float bh, uint32_t color,
                  const std::string& chip, uint32_t chipColor, bool screen = false) {
    d.labelPrefix = "M";
    std::map<std::string, int> used;
    std::vector<int> idx;
    for (size_t i = 0; i < pinNames.size(); i++) {
        Vec2 p(i * 100.f, 0);
        addPin(d, pinId(pinNames[i], used), pinNames[i], p, {0, 0}, p).square = i == 0;
        idx.push_back((int)i);
    }
    float n = (float)pinNames.size();
    float width = std::max(bw, n * 100 + 100);
    float x0 = (n - 1) * 50 - width / 2;
    G bb{d.gfx[BB]};
    bb.rect(x0, -bh, width, bh + 70, color, rgba(0, 0, 0, 60), 6, 30);
    bb.rect(-50, -50, n * 100, 100, rgba(25, 25, 25), 0, 0, 6);
    for (auto& pin : d.pins) bb.rect(pin.pos[BB].x - 16, -16, 32, 32, rgba(220, 180, 60));
    float maxLen = 1;
    for (auto& pin : d.pins) maxLen = std::max(maxLen, (float)pin.name.size());
    float ts = std::min(34.f, 95.f / (maxLen * 0.55f));
    for (auto& pin : d.pins) bb.text(pin.pos[BB].x, -95, pin.name, ts, rgba(255, 255, 255, 220));
    float cx = x0 + width / 2, cy = -bh / 2 - 60;
    if (screen) {
        float sw = width * 0.82f, sh = bh * 0.62f;
        bb.rect(cx - sw / 2, cy - sh / 2 - 20, sw, sh, chipColor, rgba(60, 60, 60), 8, 6).tag = 3;  // a sketch can draw here
        bb.text(cx, cy - 20, chip, std::min(70.f, sh * 0.3f), rgba(120, 200, 255)).tag = 4;
    } else if (!chip.empty()) {
        float cs = std::min(width * 0.5f, bh * 0.55f);
        bb.rect(cx - cs / 2, cy - cs / 2, cs, cs, chipColor, rgba(0, 0, 0, 80), 5, 8);
        bb.text(cx, cy, chip, std::min(56.f, cs / std::max<size_t>(chip.size(), 1) * 1.6f), rgba(220, 220, 220));
    }
    bb.text(cx, -bh + 50, label, std::min(48.f, width / std::max<size_t>(label.size(), 1) * 1.5f), rgba(255, 255, 255, 230));
    schBox(d, label, idx, {}, 700);
    G pc{d.gfx[PCB]};
    pc.rect(x0, -bh, width, bh + 70, 0, SILK, SKW, 30);
    pc.text(cx, -bh / 2, label, 50, SILK);
}

// ---- Raspberry Pi single-board computers (40-pin GPIO header) ----
const char* const kPiHeader[40] = {
    "3V3", "5V", "GPIO2/SDA", "5V", "GPIO3/SCL", "GND", "GPIO4", "GPIO14/TXD", "GND", "GPIO15/RXD",
    "GPIO17", "GPIO18/PCM_CLK", "GPIO27", "GND", "GPIO22", "GPIO23", "3V3", "GPIO24", "GPIO10/MOSI", "GND",
    "GPIO9/MISO", "GPIO25", "GPIO11/SCLK", "GPIO8/CE0", "GND", "GPIO7/CE1", "GPIO0/ID_SD", "GPIO1/ID_SC", "GPIO5", "GND",
    "GPIO6", "GPIO12/PWM0", "GPIO13/PWM1", "GND", "GPIO19/MISO1", "GPIO16", "GPIO26", "GPIO20/MOSI1", "GND", "GPIO21/SCLK1"};

enum class PiKind { Pi5, Pi4, Pi3, Zero };

void makeRaspberryPi(PartDef& d, const std::string& title, PiKind kind) {
    d.labelPrefix = "RPI";
    const bool zero = kind == PiKind::Zero;
    const float W = zero ? 2560.f : 3350.f, H = zero ? 1180.f : 2205.f;
    std::map<std::string, int> used;
    std::map<std::string, std::vector<int>> same;
    // physical pin n: odd pins on the inner row, even pins on the board edge; pin 1 at the left
    for (int n = 1; n <= 40; n++) {
        int col = (n - 1) / 2;
        Vec2 p(330.f + col * 100.f, n % 2 ? 190.f : 90.f);
        std::string name = kPiHeader[n - 1];
        PinDef& pin = addPin(d, pinId(name, used), name, p, {0, 0}, p);
        pin.square = n == 1;
        same[name].push_back(n - 1);
    }
    for (const char* n : {"GND", "3V3", "5V"}) d.buses.push_back(same[n]);

    G bb{d.gfx[BB]};
    uint32_t green = rgba(40, 140, 70), port = rgba(205, 205, 210), portEdge = rgba(140, 140, 145), white = rgba(240, 250, 240);
    bb.rect(0, 0, W, H, green, rgba(25, 95, 45), 10, 110);
    for (Vec2 h : {Vec2(138, 138), Vec2(2421, 138), Vec2(138, H - 138), Vec2(2421, H - 138)}) bb.circle(h.x, h.y, 70, rgba(230, 210, 130), rgba(180, 160, 90), 8);
    bb.rect(270, 30, 2060, 220, rgba(25, 25, 25), 0, 0, 6);
    for (auto& pin : d.pins) bb.rect(pin.pos[BB].x - 20, pin.pos[BB].y - 20, 40, 40, rgba(220, 180, 60));
    for (auto& pin : d.pins) {
        std::string l = pin.name.substr(0, pin.name.find('/'));
        if (l.rfind("GPIO", 0) == 0) l = l.substr(4);
        bool inner = pin.pos[BB].y > 150;
        bb.text(pin.pos[BB].x, inner ? 290 : -45, l, l.size() > 2 ? 28 : 34, inner ? white : rgba(60, 60, 60));
    }
    auto box = [&](float x, float y, float w, float h, uint32_t c, const std::string& t = "", float ts = 40, uint32_t tc = rgba(60, 60, 60)) {
        bb.rect(x, y, w, h, c, c == port ? portEdge : 0, c == port ? 8 : 0, 10);
        if (!t.empty()) bb.text(x + w / 2, y + h / 2, t, ts, tc);
    };
    if (zero) {
        box(900, 380, 480, 480, rgba(50, 50, 55), "RP3A0", 60, rgba(200, 200, 200));
        box(-60, 330, 260, 480, rgba(160, 160, 165), "SD", 40);
        box(300, H - 160, 450, 220, port, "mini HDMI", 34);
        box(1520, H - 130, 300, 180, port, "USB", 34);
        box(2020, H - 130, 300, 180, port, "PWR", 34);
        bb.rect(2400, 330, 130, 480, rgba(230, 230, 230), rgba(120, 120, 120), 4, 6);  // camera connector
        bb.text(1880, 700, "Raspberry Pi Zero", 60, white);
    } else {
        const char* soc = kind == PiKind::Pi5 ? "BCM2712" : kind == PiKind::Pi4 ? "BCM2711" : "BCM2837B0";
        box(900, 700, 560, 560, rgba(180, 180, 185), soc, 60);
        box(1600, 700, 420, 560, rgba(40, 40, 44), "RAM", 50, rgba(200, 200, 200));
        box(-70, 900, 300, 420, rgba(160, 160, 165), "SD", 40);
        // right-edge ports: Ethernet on top for the Pi 4, at the bottom for the Pi 3 and Pi 5
        bool ethTop = kind == PiKind::Pi4;
        float usbY = ethTop ? 900 : 120, ethY = ethTop ? 120 : 1550;
        box(2700, usbY, 800, 560, port, "USB", 50);
        box(2700, usbY + 640, 800, 560, kind == PiKind::Pi3 ? port : rgba(50, 100, 200), kind == PiKind::Pi3 ? "USB" : "USB 3", 50,
            kind == PiKind::Pi3 ? rgba(60, 60, 60) : white);
        box(2620, ethY, 880, 620, port, "Ethernet", 50);
        if (kind == PiKind::Pi3) {
            box(250, H - 200, 330, 260, port, "PWR", 36);
            box(1000, H - 260, 600, 320, port, "HDMI", 44);
            box(1950, H - 280, 300, 340, rgba(30, 30, 30), "AV", 36, white);
        } else {
            box(250, H - 200, 360, 260, port, "USB-C", 36);
            box(830, H - 180, 300, 240, port, "HDMI0", 32);
            box(1360, H - 180, 300, 240, port, "HDMI1", 32);
            if (kind == PiKind::Pi4) box(1950, H - 280, 300, 340, rgba(30, 30, 30), "AV", 36, white);
        }
        std::string name = kind == PiKind::Pi5 ? "Raspberry Pi 5" : kind == PiKind::Pi4 ? "Raspberry Pi 4 Model B" : "Raspberry Pi 3 Model B+";
        bb.text(1400, 1500, name, 70, white);
    }

    // schematic: power pins and GPIO 0–13 on the left, the rest on the right
    std::vector<int> pwr, gpio;
    for (size_t i = 0; i < d.pins.size(); i++) (d.pins[i].name.rfind("GPIO", 0) == 0 ? gpio : pwr).push_back((int)i);
    std::sort(pwr.begin(), pwr.end(), [&](int a, int b) { return d.pins[(size_t)a].name < d.pins[(size_t)b].name; });
    std::sort(gpio.begin(), gpio.end(), [&](int a, int b) { return std::atoi(d.pins[(size_t)a].name.c_str() + 4) < std::atoi(d.pins[(size_t)b].name.c_str() + 4); });
    std::vector<int> left = pwr, right;
    for (int i : gpio) (left.size() < 20 ? left : right).push_back(i);
    schBox(d, title, left, right, 900);

    G pc{d.gfx[PCB]};
    pc.rect(0, 0, W, H, 0, SILK, SKW, 110);
    pc.rect(270, 30, 2060, 220, 0, SILK, SKW);
    pc.text(330, 330, "1", 50, SILK);
    pc.text(W / 2, H / 2, title + " HAT", 70, SILK);
    for (Vec2 h : {Vec2(138, 138), Vec2(2421, 138), Vec2(138, H - 138), Vec2(2421, H - 138)}) d.holes.push_back({h, 110});
}

void makeTo92(PartDef& d, const std::string& label, const std::vector<std::string>& pinNames) {
    std::map<std::string, int> used;
    std::vector<int> idx;
    for (int i = 0; i < 3; i++) {
        addPin(d, pinId(pinNames[(size_t)i], used), pinNames[(size_t)i], {i * 100.f, 0}, {0, 0}, {i * 100.f, 0}).square = i == 0;
        idx.push_back(i);
    }
    G bb{d.gfx[BB]};
    drawTo92(bb, label);
    schBox(d, label, idx, {}, 600);
    G pc{d.gfx[PCB]};
    pc.arc(100, 30, 170, 180, 360, SKW, SILK);
    pc.line(-70, 30, 270, 30, SKW, SILK);
}

void makeTo220(PartDef& d, const std::string& label, const std::vector<std::string>& pinNames) {
    std::map<std::string, int> used;
    std::vector<int> idx;
    for (int i = 0; i < 3; i++) {
        addPin(d, pinId(pinNames[(size_t)i], used), pinNames[(size_t)i], {i * 100.f, 0}, {0, 0}, {i * 100.f, 0}).square = i == 0;
        idx.push_back(i);
    }
    G bb{d.gfx[BB]};
    for (int i = 0; i < 3; i++) bb.line(i * 100.f, -160, i * 100.f, 0, 18, LEG);
    bb.rect(-100, -800, 400, 330, rgba(190, 190, 195), rgba(130, 130, 135), 6, 10);
    bb.circle(100, -660, 60, rgba(255, 255, 255), rgba(130, 130, 135), 6);
    bb.rect(-100, -480, 400, 330, rgba(35, 35, 35), 0, 0, 6);
    bb.text(100, -330, label, std::min(55.f, 560.f / std::max<size_t>(label.size(), 1)), rgba(200, 200, 200));
    schBox(d, label, idx, {}, 600);
    G pc{d.gfx[PCB]};
    pc.rect(-110, -90, 420, 180, 0, SILK, SKW);
    pc.line(-110, -50, 310, -50, SKW, SILK);
}

void makeDipChip(PartDef& d, int n, const std::string& chip, const char* pinNames) {
    makeDip(d, n, chip, names(pinNames));
}

// 7-segment display, 10-pin, 0.56"
void makeSeg7(PartDef& d, bool anode, uint32_t col) {
    // pins: 1 e, 2 d, 3 COM, 4 c, 5 dp (bottom row, left→right), 10 g, 9 f, 8 COM, 7 a, 6 b (top row, left→right)
    const char* nm[] = {"e", "d", "COM", "c", "DP", "b", "a", "COM", "f", "g"};
    std::map<std::string, int> used;
    for (int i = 0; i < 10; i++) {
        Vec2 pos = i < 5 ? Vec2(i * 100.f, 600) : Vec2((9 - i) * 100.f, 0);
        addPin(d, "pin" + std::to_string(i + 1), nm[i], pos, {0, 0}, pos).square = i == 0;
    }
    G bb{d.gfx[BB]};
    for (auto& pin : d.pins) bb.rect(pin.pos[BB].x - 14, pin.pos[BB].y - (pin.pos[BB].y > 300 ? 60 : 0), 28, 60, LEG);
    bb.rect(-70, 40, 540, 520, rgba(40, 40, 42), rgba(20, 20, 20), 6, 10);
    uint32_t off = (col & 0xFFFFFF00) | 0x30;
    auto seg = [&](int idx, std::vector<Vec2> pts) {
        Prim& p = bb.poly(pts, off);
        p.stroke = col;  // lit colour (tag >= 10: segment index)
        p.tag = 10 + idx;
    };
    float x0 = 110, x1 = 330, y0 = 110, y1 = 300, y2 = 490, t = 34;
    seg(0, {{x0 + 20, y0 - t / 2}, {x1 - 20, y0 - t / 2}, {x1 - 40, y0 + t / 2}, {x0 + 40, y0 + t / 2}});            // a
    seg(1, {{x1 + t / 2 - 4, y0 + 20}, {x1 + t / 2 - 10, y1 - 20}, {x1 - t / 2 - 10, y1 - 30}, {x1 - t / 2, y0 + 40}});  // b
    seg(2, {{x1 + t / 2 - 10, y1 + 20}, {x1 + t / 2 - 16, y2 - 20}, {x1 - t / 2 - 16, y2 - 40}, {x1 - t / 2 - 10, y1 + 30}});  // c
    seg(3, {{x0 + 20, y2 + t / 2}, {x1 - 30, y2 + t / 2}, {x1 - 50, y2 - t / 2}, {x0 + 40, y2 - t / 2}});            // d
    seg(4, {{x0 - t / 2 - 10, y1 + 20}, {x0 - t / 2 - 16, y2 - 20}, {x0 + t / 2 - 16, y2 - 40}, {x0 + t / 2 - 10, y1 + 30}});  // e
    seg(5, {{x0 - t / 2 - 4, y0 + 20}, {x0 - t / 2 - 10, y1 - 20}, {x0 + t / 2 - 10, y1 - 30}, {x0 + t / 2, y0 + 40}});  // f
    seg(6, {{x0 + 10, y1}, {x0 + 30, y1 - t / 2}, {x1 - 30, y1 - t / 2}, {x1 - 10, y1}, {x1 - 30, y1 + t / 2}, {x0 + 30, y1 + t / 2}});  // g
    Prim& dp = bb.circle(x1 + 70, y2, 26, off);
    dp.stroke = col;
    dp.tag = 17;
    // pin numbers: segment a..g,dp map
    d.buses = {{2, 7}};  // both COM pins are joined inside the display
    d.sim = "seg7";
    d.simParams = std::string(anode ? "CA;" : "") + "COM=3,8;SEG 7,6,4,2,1,9,10,5";
    std::vector<int> left = {6, 5, 3, 1, 0, 8, 9, 4}, right = {2, 7};
    schBox(d, anode ? "7-seg CA" : "7-seg CC", left, right, 600);
    G pc{d.gfx[PCB]};
    pc.rect(-80, 40, 560, 520, 0, SILK, SKW);
}

void makeRgbLed(PartDef& d, const Props& p) {
    bool anode = prop(p, "common") == "anode";
    d.title = std::string("RGB LED (common ") + (anode ? "anode)" : "cathode)");
    addPin(d, "R", "red", {0, 0}, {0, 0}, {0, 0}).square = true;
    addPin(d, "COM", anode ? "common anode (+)" : "common cathode (−)", {100, 0}, {0, 0}, {100, 0});
    addPin(d, "G", "green", {200, 0}, {0, 0}, {200, 0});
    addPin(d, "B", "blue", {300, 0}, {0, 0}, {300, 0});
    G bb{d.gfx[BB]};
    for (int i = 0; i < 4; i++) bb.line(i * 100.f, i == 1 ? -200 : -150, i * 100.f, 0, 16, LEG);
    uint32_t lens = rgba(245, 245, 250, 215);
    bb.rect(30, -360, 240, 190, lens).tag = 1;
    bb.circle(150, -360, 120, lens).tag = 1;
    bb.rect(10, -190, 280, 26, rgba(200, 200, 205), 0, 0, 6);
    schBox(d, "RGB", {0, 2, 3}, {1}, 500);
    G pc{d.gfx[PCB]};
    pc.circle(150, 0, 220, 0, SILK, SKW);
    d.sim = "rgbled";
    d.simParams = std::string(anode ? "CA;" : "") + "COM=2;R=1;G=3;B=4";
    d.labelPrefix = "LED";
    d.propDefs = {{"common", "Common pin", {"cathode", "anode"}}};
    d.description = "Three LEDs (red, green, blue) in one package. Mix colours by driving each leg through its own resistor.";
}

// ---------------------------------------------------------------------------

struct Item {
    std::string type, title, category;
    Props defaults;
    std::function<void(PartDef&, const Props&)> make;
};

std::vector<Item>& items() {
    static std::vector<Item> v;
    if (!v.empty()) return v;
    auto add = [&](const std::string& type, const std::string& title, const std::string& cat, Props defs, std::function<void(PartDef&, const Props&)> mk) {
        v.push_back({type, title, cat, std::move(defs), std::move(mk)});
    };
    // ---- DIP chip helper ----
    auto chip = [&](const std::string& type, const std::string& title, const std::string& cat, int n, const std::string& label,
                    const char* pins, const std::string& desc, const std::string& sim, const std::string& params) {
        std::string pinStr = pins;
        add(type, title, cat, {}, [=](PartDef& d, const Props&) {
            makeDipChip(d, n, label, pinStr.c_str());
            finish(d, title, desc, sim, params);
        });
    };

    // ===================== Microcontroller boards =====================
    auto board = [&](const std::string& type, const std::string& title, const char* top, const char* bot, float spacing, uint32_t color,
                     const std::string& chipText, uint32_t chipColor, const std::string& params, const std::string& outputs, const std::string& desc) {
        std::string t = top, b = bot;
        // real board outlines (length × width mm, USB end → first pin mm)
        static const std::map<std::string, BoardSize> sizes = {
            {"esp32_devkit", {54.4f, 27.9f, 2.4f}},     {"esp32_devkit30", {51.5f, 28.3f, 2.6f}},
            {"esp32c3_supermini", {22.5f, 18.0f, 2.4f}}, {"nodemcu_esp8266", {48.3f, 25.8f, 3.0f}},
            {"rpi_pico", {51.0f, 21.0f, 1.37f}},       {"rpi_pico2", {51.0f, 21.0f, 1.37f}},
            {"arduino_nano", {45.0f, 18.0f, 4.7f}},    {"arduino_nano_every", {45.0f, 18.0f, 4.7f}},
            {"nano_33_iot", {45.0f, 18.0f, 4.7f}},     {"nano_33_ble", {45.0f, 18.0f, 4.7f}},
            {"nano_esp32", {45.0f, 18.0f, 4.7f}},      {"nano_rp2040", {45.0f, 18.0f, 4.7f}},
            {"arduino_pro_micro", {33.0f, 18.0f, 2.5f}}, {"arduino_pro_mini", {33.0f, 18.0f, 2.5f, false}},
            {"arduino_micro", {48.0f, 18.0f, 3.7f}},   {"stm32_bluepill", {53.0f, 22.6f, 2.4f}},
            {"stm32_blackpill", {53.0f, 21.0f, 2.4f}}, {"teensy40", {35.6f, 17.8f, 1.3f}},
            {"xiao", {21.0f, 17.8f, 3.0f}},            {"wemos_d1_mini", {34.2f, 25.6f, 3.0f}},
            {"esp32_cam", {40.5f, 27.0f, 2.5f, false}}, {"feather", {50.8f, 22.8f, 6.3f}},
            {"mkr_wifi_1010", {61.5f, 25.0f, 14.0f}},
        };
        add(type, title, "Microcontroller", {{"outputs", outputs}}, [=](PartDef& d, const Props&) {
            auto it = sizes.find(type);
            makeModule(d, title, names(t.c_str()), names(b.c_str()), spacing, color, chipText, chipColor, it == sizes.end() ? nullptr : &it->second);
            finish(d, title, desc, "board", params);
            d.labelPrefix = "U";
            d.propDefs = {{"outputs", "Pin outputs (simulation)", {outputs, "", "GPIO2=HIGH", "GPIO2=BLINK:250"}}};
        });
    };
    const std::string mcuNote = " In simulation the board is USB-powered: its power pins supply voltage, and the pins listed under \"Pin outputs\" are driven — e.g. GPIO2=HIGH, GPIO4=LOW, GPIO5=BLINK:500 (toggle every 500 ms) or GPIO18=PWM:30 (30% duty). Firmware itself is not executed.";
#define NANO_TOP "D13,3V3,AREF,A0,A1,A2,A3,A4,A5,A6,A7,5V,RST,GND,VIN"
#define NANO_BOT "D12,D11,D10,D9,D8,D7,D6,D5,D4,D3,D2,GND,RST,D0/RX,D1/TX"
    board("esp32_devkit", "ESP32 DevKitC (38-pin)",
          "5V,GPIO11,GPIO10,GPIO9,GPIO13,GND,GPIO12,GPIO14,GPIO27,GPIO26,GPIO25,GPIO33,GPIO32,GPIO35,GPIO34,GPIO39,GPIO36,EN,3V3",
          "CLK,SD0,SD1,GPIO15,GPIO2,GPIO0,GPIO4,GPIO16,GPIO17,GPIO5,GPIO18,GPIO19,GND,GPIO21,GPIO3,GPIO1,GPIO22,GPIO23,GND",
          1000, rgba(25, 25, 28), "ESP32-WROOM-32", rgba(175, 178, 185), "GND=GND;RAIL 3V3=3.3;RAIL 5V=5;IO=3.3;ROUT=40", "GPIO2=BLINK:500",
          "Espressif ESP32 dual-core Wi-Fi + Bluetooth microcontroller board. 3.3 V logic." + mcuNote);
    board("esp32_devkit30", "ESP32 DevKit V1 (30-pin)",
          "VIN,GND,GPIO13,GPIO12,GPIO14,GPIO27,GPIO26,GPIO25,GPIO33,GPIO32,GPIO35,GPIO34,GPIO39,GPIO36,EN",
          "3V3,GND,GPIO15,GPIO2,GPIO4,GPIO16,GPIO17,GPIO5,GPIO18,GPIO19,GPIO21,GPIO3,GPIO1,GPIO22,GPIO23",
          1000, rgba(20, 60, 120), "ESP32-WROOM-32", rgba(175, 178, 185), "GND=GND;RAIL 3V3=3.3;RAIL VIN=5;IO=3.3;ROUT=40", "GPIO2=BLINK:500",
          "Popular 30-pin ESP32 board (DOIT DevKit V1). Wi-Fi + Bluetooth, 3.3 V logic. On-board LED on GPIO2." + mcuNote);
    board("esp32c3_supermini", "ESP32-C3 SuperMini",
          "GPIO5,GPIO6,GPIO7,GPIO8,GPIO9,GPIO10,GPIO20,GPIO21", "5V,GND,3V3,GPIO4,GPIO3,GPIO2,GPIO1,GPIO0",
          600, rgba(30, 30, 34), "ESP32-C3", rgba(60, 60, 65), "GND=GND;RAIL 3V3=3.3;RAIL 5V=5;IO=3.3;ROUT=40", "GPIO8=BLINK:500",
          "Tiny RISC-V ESP32-C3 board with Wi-Fi and BLE." + mcuNote);
    board("esp32s3_devkit", "ESP32-S3 DevKitC-1",
          "GND,5V,GPIO14,GPIO13,GPIO12,GPIO11,GPIO10,GPIO9,GPIO46,GPIO3,GPIO8,GPIO18,GPIO17,GPIO16,GPIO15,GPIO7,GPIO6,GPIO5,GPIO4,RST,3V3,3V3",
          "GND,GND,GPIO19,GPIO20,GPIO21,GPIO47,GPIO48,GPIO45,GPIO0,GPIO35,GPIO36,GPIO37,GPIO38,GPIO39,GPIO40,GPIO41,GPIO42,GPIO2,GPIO1,GPIO44,GPIO43,GND",
          1000, rgba(25, 25, 28), "ESP32-S3-WROOM-1", rgba(175, 178, 185), "GND=GND;RAIL 3V3=3.3;RAIL 5V=5;IO=3.3;ROUT=40", "GPIO2=BLINK:500",
          "ESP32-S3 with Wi-Fi, BLE, USB-OTG and AI vector instructions." + mcuNote);
    board("nodemcu_esp8266", "NodeMCU ESP8266 (v2)",
          "VIN,GND,RST,EN,3V3,GND,CLK,SD0,CMD,SD1,SD2,SD3,RSV,RSV,A0", "3V3,GND,TX,RX,D8,D7,D6,D5,GND,3V3,D4,D3,D2,D1,D0",
          900, rgba(25, 25, 28), "ESP-12E", rgba(175, 178, 185), "GND=GND;RAIL 3V3=3.3;RAIL VIN=5;IO=3.3;ROUT=40", "D4=BLINK:500",
          "ESP8266 Wi-Fi board. Pins are labelled D0–D8; the on-board LED is on D4 (active low)." + mcuNote);
    board("rpi_pico", "Raspberry Pi Pico / Pico W",
          "VBUS,VSYS,GND,3V3_EN,3V3,ADC_VREF,GP28,AGND,GP27,GP26,RUN,GP22,GND,GP21,GP20,GP19,GP18,GND,GP17,GP16",
          "GP0,GP1,GND,GP2,GP3,GP4,GP5,GND,GP6,GP7,GP8,GP9,GND,GP10,GP11,GP12,GP13,GND,GP14,GP15",
          700, rgba(30, 120, 60), "RP2040", rgba(40, 40, 44), "GND=GND;RAIL 3V3=3.3;RAIL VBUS=5;RAIL VSYS=4.8;IO=3.3;ROUT=40", "GP15=BLINK:500",
          "Raspberry Pi RP2040 dual-core Cortex-M0+ board (Pico W adds Wi-Fi). 3.3 V logic." + mcuNote);
    board("rpi_pico2", "Raspberry Pi Pico 2 / Pico 2 W",
          "VBUS,VSYS,GND,3V3_EN,3V3,ADC_VREF,GP28,AGND,GP27,GP26,RUN,GP22,GND,GP21,GP20,GP19,GP18,GND,GP17,GP16",
          "GP0,GP1,GND,GP2,GP3,GP4,GP5,GND,GP6,GP7,GP8,GP9,GND,GP10,GP11,GP12,GP13,GND,GP14,GP15",
          700, rgba(30, 120, 60), "RP2350", rgba(40, 40, 44), "GND=GND;RAIL 3V3=3.3;RAIL VBUS=5;RAIL VSYS=4.8;IO=3.3;ROUT=40", "GP15=BLINK:500",
          "Raspberry Pi RP2350 board (Cortex-M33 / RISC-V)." + mcuNote);
    board("arduino_nano", "Arduino Nano",
          NANO_TOP, NANO_BOT,
          600, rgba(0, 110, 160), "ATMEGA328P", rgba(35, 35, 38), "GND=GND;RAIL 5V=5;RAIL 3V3=3.3;IO=5;ROUT=25", "D13=BLINK:500",
          "Breadboard-friendly ATmega328P Arduino. 5 V logic, on-board LED on D13." + mcuNote);
    board("arduino_nano_every", "Arduino Nano Every",
          NANO_TOP, NANO_BOT,
          600, rgba(0, 110, 160), "ATMEGA4809", rgba(35, 35, 38), "GND=GND;RAIL 5V=5;RAIL 3V3=3.3;IO=5;ROUT=25", "D13=BLINK:500",
          "Nano-footprint board with the ATmega4809." + mcuNote);
    board("arduino_pro_micro", "Arduino Pro Micro (ATmega32U4)",
          "RAW,GND,RST,VCC,A3,A2,A1,A0,15,14,16,10", "TX0,RX1,GND,GND,2,3,4,5,6,7,8,9",
          600, rgba(170, 30, 40), "32U4", rgba(35, 35, 38), "GND=GND;RAIL VCC=5;RAIL RAW=5;IO=5;ROUT=25", "9=HIGH",
          "Small ATmega32U4 board with native USB (keyboard/mouse emulation)." + mcuNote);
    board("stm32_bluepill", "STM32 Blue Pill (STM32F103C8)",
          "B12,B13,B14,B15,A8,A9,A10,A11,A12,A15,B3,B4,B5,B6,B7,B8,B9,5V,GND,3V3",
          "VB,C13,C14,C15,A0,A1,A2,A3,A4,A5,A6,A7,B0,B1,B10,B11,RST,3V3,GND,GND",
          600, rgba(20, 70, 160), "STM32F103", rgba(35, 35, 38), "GND=GND;RAIL 3V3=3.3;RAIL 5V=5;IO=3.3;ROUT=40", "C13=BLINK:500",
          "72 MHz ARM Cortex-M3 board. On-board LED on C13." + mcuNote);
    board("teensy40", "Teensy 4.0",
          "VIN,GND,3.3V,23,22,21,20,19,18,17,16,15,14,13", "GND,0,1,2,3,4,5,6,7,8,9,10,11,12",
          600, rgba(30, 110, 50), "iMXRT1062", rgba(35, 35, 38), "GND=GND;RAIL 3.3V=3.3;RAIL VIN=5;IO=3.3;ROUT=40", "13=BLINK:500",
          "600 MHz Cortex-M7 board from PJRC." + mcuNote);
    board("xiao", "Seeed XIAO (ESP32-C3 / RP2040 / SAMD21)",
          "5V,GND,3V3,D10,D9,D8,D7", "D0,D1,D2,D3,D4,D5,D6",
          600, rgba(40, 40, 44), "XIAO", rgba(175, 178, 185), "GND=GND;RAIL 3V3=3.3;RAIL 5V=5;IO=3.3;ROUT=40", "D0=BLINK:500",
          "Thumb-sized XIAO boards share this pinout." + mcuNote);
    board("nano_33_iot", "Arduino Nano 33 IoT",
          "D13,3V3,AREF,A0,A1,A2,A3,A4,A5,A6,A7,VUSB,RST,GND,VIN", NANO_BOT,
          600, rgba(0, 110, 160), "SAMD21", rgba(35, 35, 38), "GND=GND;RAIL 3V3=3.3;RAIL VUSB=5;IO=3.3;ROUT=40", "D13=BLINK:500",
          "Nano-footprint SAMD21 board with Wi-Fi/BLE (NINA-W102) and an IMU. 3.3 V logic — not 5 V tolerant." + mcuNote);
    board("nano_33_ble", "Arduino Nano 33 BLE / BLE Sense",
          "D13,3V3,AREF,A0,A1,A2,A3,A4,A5,A6,A7,VUSB,RST,GND,VIN", NANO_BOT,
          600, rgba(0, 110, 160), "nRF52840", rgba(35, 35, 38), "GND=GND;RAIL 3V3=3.3;RAIL VUSB=5;IO=3.3;ROUT=40", "D13=BLINK:500",
          "Nano-footprint nRF52840 board with Bluetooth LE; the Sense adds microphone, gesture, humidity and pressure sensors. 3.3 V logic." + mcuNote);
    board("nano_esp32", "Arduino Nano ESP32",
          "D13,3V3,B0,A0,A1,A2,A3,A4,A5,A6,A7,VBUS,B1,GND,VIN", NANO_BOT,
          600, rgba(0, 110, 160), "ESP32-S3", rgba(35, 35, 38), "GND=GND;RAIL 3V3=3.3;RAIL VBUS=5;IO=3.3;ROUT=40", "D13=BLINK:500",
          "Nano-footprint ESP32-S3 board with Wi-Fi and Bluetooth. 3.3 V logic." + mcuNote);
    board("nano_rp2040", "Arduino Nano RP2040 Connect",
          "D13,3V3,AREF,A0,A1,A2,A3,A4,A5,A6,A7,VUSB,RST,GND,VIN", NANO_BOT,
          600, rgba(0, 110, 160), "RP2040", rgba(35, 35, 38), "GND=GND;RAIL 3V3=3.3;RAIL VUSB=5;IO=3.3;ROUT=40", "D13=BLINK:500",
          "Nano-footprint RP2040 board with Wi-Fi/BLE, IMU and microphone. 3.3 V logic." + mcuNote);
    board("arduino_micro", "Arduino Micro",
          "D13,3V3,AREF,A0,A1,A2,A3,A4,A5,NC,NC,5V,RST,GND,VIN,MISO,SCK",
          "D12,D11,D10,D9,D8,D7,D6,D5,D4,D3,D2,GND,RST,D0/RX,D1/TX,SS,MOSI",
          600, rgba(0, 110, 160), "ATMEGA32U4", rgba(35, 35, 38), "GND=GND;RAIL 5V=5;RAIL 3V3=3.3;IO=5;ROUT=25", "D13=BLINK:500",
          "ATmega32U4 board with native USB, in a breadboard-friendly DIP shape. 5 V logic." + mcuNote);
    board("arduino_pro_mini", "Arduino Pro Mini (5 V / 16 MHz)",
          "RAW,GND,RST,VCC,A3,A2,A1,A0,D13,D12,D11,D10", "D1/TX,D0/RX,RST,GND,D2,D3,D4,D5,D6,D7,D8,D9",
          600, rgba(170, 30, 40), "328P", rgba(35, 35, 38), "GND=GND;RAIL VCC=5;RAIL RAW=7;IO=5;ROUT=25", "D13=BLINK:500",
          "Minimal ATmega328P board, programmed through a USB-serial adapter on its FTDI header (drawn at the left end)." + mcuNote);
    board("mkr_wifi_1010", "Arduino MKR WiFi 1010",
          "5V,VIN,VCC,GND,RESET,D14/TX,D13/RX,D12/SCL,D11/SDA,D10/MISO,D9/SCK,D8/MOSI,D7,D6",
          "AREF,A0/DAC0,A1,A2,A3,A4,A5,A6,D0,D1,D2,D3,D4,D5",
          800, rgba(0, 135, 145), "SAMD21", rgba(35, 35, 38), "GND=GND;RAIL 5V=5;RAIL VCC=3.3;IO=3.3;ROUT=40", "D6=BLINK:500",
          "MKR-family SAMD21 board with Wi-Fi/BLE and a LiPo charger. On-board LED on D6. 3.3 V logic." + mcuNote);
    board("wemos_d1_mini", "Wemos / LOLIN D1 Mini (ESP8266)",
          "3V3,D8,D7,D6,D5,D0,A0,RST", "5V,GND,D4,D3,D2,D1,RX,TX",
          900, rgba(30, 90, 160), "ESP-12F", rgba(175, 178, 185), "GND=GND;RAIL 3V3=3.3;RAIL 5V=5;IO=3.3;ROUT=40", "D4=BLINK:500",
          "Small ESP8266 Wi-Fi board with a huge range of stackable shields. On-board LED on D4 (active low)." + mcuNote);
    board("esp32_cam", "ESP32-CAM (AI-Thinker)",
          "3V3,GPIO16,GPIO0,GND,VCC,GPIO3/U0RXD,GPIO1/U0TXD,GND", "5V,GND,GPIO12,GPIO13,GPIO15,GPIO14,GPIO2,GPIO4",
          900, rgba(30, 30, 34), "OV2640", rgba(70, 70, 80), "GND=GND;RAIL 3V3=3.3;RAIL 5V=5;IO=3.3;ROUT=40", "GPIO4=BLINK:500",
          "ESP32 with an OV2640 camera and microSD slot. The bright flash LED is on GPIO4. Program it through a USB-serial adapter (GPIO0 to GND to flash)." + mcuNote);
    board("stm32_blackpill", "STM32 Black Pill (STM32F411CE)",
          "B12,B13,B14,B15,A8,A9,A10,A11,A12,A15,B3,B4,B5,B6,B7,B8,B9,5V,GND,3V3",
          "VB,C13,C14,C15,R,A0,A1,A2,A3,A4,A5,A6,A7,B0,B1,B2,B10,3V3,GND,5V",
          600, rgba(30, 30, 34), "STM32F411", rgba(60, 60, 65), "GND=GND;RAIL 3V3=3.3;RAIL 5V=5;IO=3.3;ROUT=40", "C13=BLINK:500",
          "WeAct 100 MHz Cortex-M4F board with USB-C. On-board LED on C13 (active low)." + mcuNote);
    board("feather", "Adafruit Feather (M0 / M4 / RP2040 / ESP32-S3)",
          "RST,3V3,AREF,GND,A0,A1,A2,A3,A4,A5,SCK,MOSI,MISO,RX,TX,NC", "BAT,EN,USB,D13,D12,D11,D10,D9,D6,D5,SCL,SDA",
          800, rgba(25, 25, 28), "FEATHER", rgba(70, 70, 80), "GND=GND;RAIL 3V3=3.3;RAIL USB=5;RAIL BAT=3.7;IO=3.3;ROUT=40", "D13=BLINK:500",
          "Adafruit's Feather form factor (shared pinout across many chips) with LiPo charging and FeatherWing add-ons." + mcuNote);

    // ===================== Arduino Uno / Mega footprints =====================
    auto ardBoard = [&](const std::string& type, std::function<void(ArduinoSpec&)> cfg) {
        ArduinoSpec probe;
        cfg(probe);
        add(type, probe.title, "Microcontroller", {{"type", probe.title}, {"outputs", "D13=BLINK:500"}}, [=](PartDef& d, const Props&) {
            ArduinoSpec s;
            cfg(s);
            makeArduinoBoard(d, s);
        });
    };
    ardBoard("arduino_leonardo", [](ArduinoSpec& s) {
        s.title = "Arduino Leonardo"; s.chip = "ATMEGA32U4"; s.logo = "LEONARDO"; s.microUsb = true;
        s.desc = "Uno-footprint ATmega32U4 board with native USB (can act as a keyboard or mouse), 5 V logic.";
    });
    ardBoard("arduino_uno_r4_minima", [](ArduinoSpec& s) {
        s.title = "Arduino Uno R4 Minima"; s.chip = "RA4M1"; s.logo = "UNO R4"; s.usbC = true;
        s.desc = "Uno-footprint 48 MHz Renesas RA4M1 (Cortex-M4) board with USB-C, 5 V logic.";
    });
    ardBoard("arduino_uno_r4_wifi", [](ArduinoSpec& s) {
        s.title = "Arduino Uno R4 WiFi"; s.chip = "RA4M1"; s.logo = "UNO R4"; s.usbC = true; s.ledMatrix = true;
        s.desc = "Uno R4 with an ESP32-S3 for Wi-Fi/Bluetooth and a 12×8 LED matrix, 5 V logic.";
    });
    ardBoard("arduino_mega2560", [](ArduinoSpec& s) {
        s.title = "Arduino Mega 2560"; s.chip = "ATMEGA2560"; s.logo = "MEGA 2560"; s.mega = true;
        s.desc = "ATmega2560 board with 54 digital pins (15 PWM), 16 analog inputs and 4 serial ports, 5 V logic.";
    });
    ardBoard("arduino_due", [](ArduinoSpec& s) {
        s.title = "Arduino Due"; s.chip = "SAM3X8E"; s.logo = "DUE"; s.mega = true; s.io3v3 = true; s.microUsb = true;
        s.color = rgba(0, 110, 160);
        s.bottom2 = {"A8", "A9", "A10", "A11", "DAC0", "DAC1", "CANRX", "CANTX"};
        s.desc = "84 MHz ARM Cortex-M3 board in the Mega footprint, with two DACs and CAN. 3.3 V logic — not 5 V tolerant.";
    });

    // ===================== Raspberry Pi computers =====================
    const std::string piNote = " In simulation the Pi is powered: its 5V and 3V3 pins supply the circuit and the GPIO pins listed under \"Pin outputs\" are driven at 3.3 V (e.g. GPIO17=HIGH, GPIO18=PWM:30, GPIO27=BLINK:500). Your Python/C program itself is not run. GPIO pins are 3.3 V only — never connect 5 V to them.";
    auto pi = [&](const std::string& type, const std::string& title, PiKind kind, const std::string& desc) {
        add(type, title, "Microcontroller", {{"outputs", "GPIO17=BLINK:500"}}, [=](PartDef& d, const Props&) {
            makeRaspberryPi(d, title, kind);
            finish(d, title, desc + piNote, "board", "GND=GND;RAIL 5V=5;RAIL 3V3=3.3;IO=3.3;ROUT=40");
            d.propDefs = {{"outputs", "Pin outputs (simulation)", {"GPIO17=BLINK:500", "", "GPIO17=HIGH", "GPIO18=PWM:50"}}};
        });
    };
    pi("rpi5", "Raspberry Pi 5", PiKind::Pi5, "Quad-core Cortex-A76 single-board computer (BCM2712) with the standard 40-pin GPIO header, PCIe and dual micro-HDMI.");
    pi("rpi4", "Raspberry Pi 4 Model B", PiKind::Pi4, "Quad-core Cortex-A72 single-board computer (BCM2711) with the standard 40-pin GPIO header, USB 3 and dual micro-HDMI.");
    pi("rpi3", "Raspberry Pi 3 Model B / B+", PiKind::Pi3, "Quad-core Cortex-A53 single-board computer with the standard 40-pin GPIO header, Wi-Fi and Bluetooth.");
    pi("rpi_zero", "Raspberry Pi Zero / Zero W / Zero 2 W", PiKind::Zero, "Tiny single-board computer with the standard 40-pin GPIO header (Zero 2 W: quad-core RP3A0, Wi-Fi + BLE).");

    // ===================== Microcontroller chips =====================
    auto mcu = [&](const std::string& type, const std::string& title, int n, const std::string& label, const char* pins, const std::string& params,
                   const std::string& desc) {
        std::string pinStr = pins;
        add(type, title, "Microcontroller", {{"outputs", ""}}, [=](PartDef& d, const Props&) {
            makeDipChip(d, n, label, pinStr.c_str());
            finish(d, title, desc + " In simulation, power it on VCC/GND and list pins to drive under \"Pin outputs\" (e.g. PB0=BLINK:500).", "mcu", params);
            d.propDefs = {{"outputs", "Pin outputs (simulation)", {"", "PB0=HIGH", "PB0=BLINK:500"}}};
        });
    };
    mcu("atmega328p", "ATmega328P (DIP-28)", 28, "ATMEGA328P",
        "PC6/RESET,PD0/RXD,PD1/TXD,PD2,PD3,PD4,VCC,GND,PB6/XTAL1,PB7/XTAL2,PD5,PD6,PD7,PB0,PB1,PB2,PB3/MOSI,PB4/MISO,PB5/SCK,AVCC,AREF,GND,PC0/ADC0,PC1/ADC1,PC2/ADC2,PC3/ADC3,PC4/SDA,PC5/SCL",
        "VCC=7,20;GND=8,22;ROUT=25", "The chip at the heart of the Arduino Uno.");
    mcu("attiny85", "ATtiny85 (DIP-8)", 8, "ATTINY85", "PB5/RESET,PB3,PB4,GND,PB0,PB1,PB2,VCC", "VCC=8;GND=4;ROUT=25",
        "Tiny 8-pin AVR microcontroller.");
    mcu("attiny84", "ATtiny84 (DIP-14)", 14, "ATTINY84", "VCC,PB0,PB1,PB3/RESET,PB2,PA7,PA6,PA5,PA4,PA3,PA2,PA1,PA0,GND", "VCC=1;GND=14;ROUT=25",
        "14-pin AVR microcontroller.");
    mcu("pic12f675", "PIC12F675 (DIP-8)", 8, "PIC12F675", "VDD,GP5,GP4,GP3/MCLR,GP2,GP1,GP0,VSS", "VCC=1;GND=8;ROUT=25",
        "Microchip 8-pin PIC microcontroller.");
    mcu("pic16f628a", "PIC16F628A (DIP-18)", 18, "PIC16F628A",
        "RA2,RA3,RA4,RA5/MCLR,VSS,RB0,RB1,RB2,RB3,RB4,RB5,RB6,RB7,VDD,RA6,RA7,RA0,RA1", "VCC=14;GND=5;ROUT=25",
        "Microchip 18-pin PIC microcontroller.");

    // ===================== Logic =====================
    auto quad = [&](const char* gate, const char* layout) {
        std::string s = layout, r;
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '%' && i + 1 < s.size() && s[i + 1] == 's') { r += gate; i++; }
            else r += s[i];
        }
        return r;
    };
    const char* std74 = "VCC=14;GND=7;%s 1,2>3;%s 4,5>6;%s 9,10>8;%s 12,13>11";
    const char* pins74quad = "1A,1B,1Y,2A,2B,2Y,GND,3Y,3A,3B,4Y,4A,4B,VCC";
    const char* std4000 = "VCC=14;GND=7;%s 1,2>3;%s 5,6>4;%s 8,9>10;%s 12,13>11;ROUT=300";
    const char* pins4000quad = "A,B,J,K,C,D,VSS,E,F,L,M,G,H,VDD";
    const char* hex = "VCC=14;GND=7;%s 1>2;%s 3>4;%s 5>6;%s 9>8;%s 11>10;%s 13>12";
    const char* pinsHex = "1A,1Y,2A,2Y,3A,3Y,GND,4Y,4A,5Y,5A,6Y,6A,VCC";
    auto hexp = [&](const char* gate, int rout) {
        std::string r;
        std::string s = hex;
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '%' && i + 1 < s.size() && s[i + 1] == 's') { r += gate; i++; }
            else r += s[i];
        }
        return r + ";ROUT=" + std::to_string(rout);
    };
    const std::string lg = " Fully simulated as digital logic (inputs above half of VCC read as HIGH).";
    chip("74hc00", "74HC00 Quad NAND", "Logic", 14, "74HC00", pins74quad, "Four 2-input NAND gates." + lg, "logic", quad("NAND", std74));
    chip("74hc02", "74HC02 Quad NOR", "Logic", 14, "74HC02", "1Y,1A,1B,2Y,2A,2B,GND,3A,3B,3Y,4A,4B,4Y,VCC", "Four 2-input NOR gates." + lg, "logic",
         "VCC=14;GND=7;NOR 2,3>1;NOR 5,6>4;NOR 8,9>10;NOR 11,12>13");
    chip("74hc04", "74HC04 Hex Inverter", "Logic", 14, "74HC04", pinsHex, "Six inverters (NOT gates)." + lg, "logic", hexp("NOT", 50));
    chip("74hc08", "74HC08 Quad AND", "Logic", 14, "74HC08", pins74quad, "Four 2-input AND gates." + lg, "logic", quad("AND", std74));
    chip("74hc10", "74HC10 Triple 3-input NAND", "Logic", 14, "74HC10", "1A,1B,2A,2B,2C,2Y,GND,3Y,3A,3B,3C,1Y,1C,VCC", "Three 3-input NAND gates." + lg, "logic",
         "VCC=14;GND=7;NAND 1,2,13>12;NAND 3,4,5>6;NAND 9,10,11>8");
    chip("74hc11", "74HC11 Triple 3-input AND", "Logic", 14, "74HC11", "1A,1B,2A,2B,2C,2Y,GND,3Y,3A,3B,3C,1Y,1C,VCC", "Three 3-input AND gates." + lg, "logic",
         "VCC=14;GND=7;AND 1,2,13>12;AND 3,4,5>6;AND 9,10,11>8");
    chip("74hc14", "74HC14 Hex Schmitt Inverter", "Logic", 14, "74HC14", pinsHex, "Six Schmitt-trigger inverters — great for RC oscillators and debouncing." + lg, "logic", hexp("SNOT", 50));
    chip("74hc20", "74HC20 Dual 4-input NAND", "Logic", 14, "74HC20", "1A,1B,NC,1C,1D,1Y,GND,2Y,2A,2B,NC,2C,2D,VCC", "Two 4-input NAND gates." + lg, "logic",
         "VCC=14;GND=7;NAND 1,2,4,5>6;NAND 9,10,12,13>8");
    chip("74hc32", "74HC32 Quad OR", "Logic", 14, "74HC32", pins74quad, "Four 2-input OR gates." + lg, "logic", quad("OR", std74));
    chip("74hc86", "74HC86 Quad XOR", "Logic", 14, "74HC86", pins74quad, "Four 2-input XOR gates." + lg, "logic", quad("XOR", std74));
    chip("74hc125", "74HC125 Quad Tri-state Buffer", "Logic", 14, "74HC125", "1OE,1A,1Y,2OE,2A,2Y,GND,3Y,3A,3OE,4Y,4A,4OE,VCC",
         "Four buffers with active-low output enable." + lg, "logic", "VCC=14;GND=7;TRI 2,1>3;TRI 5,4>6;TRI 9,10>8;TRI 12,13>11");
    chip("74hc74", "74HC74 Dual D Flip-Flop", "Logic", 14, "74HC74", "1CLR,1D,1CLK,1PRE,1Q,1Q',GND,2Q',2Q,2PRE,2CLK,2D,2CLR,VCC",
         "Two positive-edge D flip-flops with active-low preset and clear." + lg, "logic", "VCC=14;GND=7;DFF 2,3,4,1>5,6;DFF 12,11,10,13>9,8");
    chip("74hc138", "74HC138 3-to-8 Decoder", "Logic", 16, "74HC138", "A,B,C,G2A,G2B,G1,Y7,GND,Y6,Y5,Y4,Y3,Y2,Y1,Y0,VCC",
         "Selects one of eight active-low outputs." + lg, "logic", "VCC=16;GND=8;DEC138 1,2,3,6,4,5>15,14,13,12,11,10,9,7");
    chip("74hc165", "74HC165 8-bit PISO Shift Register", "Logic", 16, "74HC165", "SH/LD,CLK,E,F,G,H,QH',GND,QH,SER,A,B,C,D,CLKINH,VCC",
         "Reads 8 inputs into a serial stream (button matrices).", "", "");
    chip("74hc245", "74HC245 Octal Bus Transceiver", "Logic", 20, "74HC245", "DIR,A1,A2,A3,A4,A5,A6,A7,A8,GND,B8,B7,B6,B5,B4,B3,B2,B1,OE,VCC",
         "Bidirectional 8-bit buffer.", "", "");
    chip("74hc393", "74HC393 Dual 4-bit Counter", "Logic", 14, "74HC393", "1CLK,1CLR,1QA,1QB,1QC,1QD,GND,2QD,2QC,2QB,2QA,2CLR,2CLK,VCC",
         "Two ripple counters, counting on the falling clock edge." + lg, "logic", "VCC=14;GND=7;CNT 1,2>3,4,5,6;CNT 13,12>11,10,9,8");
    chip("74hc595", "74HC595 8-bit Shift Register", "Logic", 16, "74HC595", "QB,QC,QD,QE,QF,QG,QH,GND,QH',SRCLR,SRCLK,RCLK,OE,SER,QA,VCC",
         "Serial-in, parallel-out shift register with output latch — drive 8 LEDs from 3 pins." + lg, "logic",
         "VCC=16;GND=8;SR595 14,11,12,10,13>15,1,2,3,4,5,6,7,9");
    chip("74hc4051", "74HC4051 8-channel Analog Mux", "Logic", 16, "74HC4051", "Y4,Y6,Z,Y7,Y5,E,VEE,GND,S2,S1,S0,Y3,Y0,Y1,Y2,VCC",
         "Connects the common Z pin to one of eight channels (80 Ω on-resistance). Simulated.", "logic", "VCC=16;GND=8;MUX 11,10,9,6,3>13,14,15,12,1,5,2,4");
    chip("cd4001", "CD4001 Quad NOR", "Logic", 14, "CD4001", pins4000quad, "CMOS 3–15 V quad NOR." + lg, "logic", quad("NOR", std4000));
    chip("cd4011", "CD4011 Quad NAND", "Logic", 14, "CD4011", pins4000quad, "CMOS 3–15 V quad NAND." + lg, "logic", quad("NAND", std4000));
    chip("cd4013", "CD4013 Dual D Flip-Flop", "Logic", 14, "CD4013", "Q1,Q1',CLK1,RST1,D1,SET1,VSS,SET2,D2,RST2,CLK2,Q2',Q2,VDD",
         "CMOS dual D flip-flop with active-high set/reset." + lg, "logic", "VCC=14;GND=7;ROUT=300;DFFH 5,3,6,4>1,2;DFFH 9,11,8,10>13,12");
    chip("cd4017", "CD4017 Decade Counter", "Logic", 16, "CD4017", "Q5,Q1,Q0,Q2,Q6,Q7,Q3,VSS,Q8,Q4,Q9,CO,INH,CLK,RST,VDD",
         "Lights 10 outputs one after another — the classic LED chaser with a 555." + lg, "logic",
         "VCC=16;GND=8;ROUT=300;J4017 14,13,15>3,2,4,7,10,1,5,6,9,11,12");
    chip("cd4066", "CD4066 Quad Bilateral Switch", "Logic", 14, "CD4066", "1A,1B,2B,2A,2C,3C,VSS,3A,3B,4B,4A,4C,1C,VDD",
         "Four analog switches controlled by logic inputs. Simulated.", "logic", "VCC=14;GND=7;SW 13>1,2;SW 5>4,3;SW 6>8,9;SW 12>11,10");
    chip("cd4069", "CD4069 Hex Inverter", "Logic", 14, "CD4069", pinsHex, "CMOS hex inverter." + lg, "logic", hexp("NOT", 300));
    chip("cd4070", "CD4070 Quad XOR", "Logic", 14, "CD4070", pins4000quad, "CMOS quad XOR." + lg, "logic", quad("XOR", std4000));
    chip("cd4071", "CD4071 Quad OR", "Logic", 14, "CD4071", pins4000quad, "CMOS quad OR." + lg, "logic", quad("OR", std4000));
    chip("cd4081", "CD4081 Quad AND", "Logic", 14, "CD4081", pins4000quad, "CMOS quad AND." + lg, "logic", quad("AND", std4000));
    chip("cd4093", "CD4093 Quad Schmitt NAND", "Logic", 14, "CD4093", pins4000quad, "CMOS Schmitt-trigger NAND — oscillators and debouncing." + lg, "logic", quad("SNAND", std4000));
    chip("cd4511", "CD4511 BCD to 7-Segment Driver", "Logic", 16, "CD4511", "B,C,LT,BL,LE,D,A,VSS,e,d,c,b,a,g,f,VDD",
         "Turns a 4-bit number into 7-segment outputs for a common-cathode display." + lg, "logic",
         "VCC=16;GND=8;ROUT=200;BCD7 7,1,2,6,3,4,5>13,12,11,10,9,15,14");

    // ===================== Analog ICs =====================
    const std::string an = " Simulated (high-gain amplifier with output limits).";
    chip("lm358", "LM358 Dual Op-Amp", "Analog ICs", 8, "LM358", "1OUT,1IN-,1IN+,GND,2IN+,2IN-,2OUT,VCC",
         "Single-supply dual op-amp; output reaches GND but not VCC." + an, "opamp", "VCC=8;GND=4;HI=1.5;LO=0.02;OA 3,2>1;OA 5,6>7");
    chip("lm324", "LM324 Quad Op-Amp", "Analog ICs", 14, "LM324", "1OUT,1IN-,1IN+,VCC,2IN+,2IN-,2OUT,3OUT,3IN-,3IN+,GND,4IN+,4IN-,4OUT",
         "Four single-supply op-amps." + an, "opamp", "VCC=4;GND=11;HI=1.5;LO=0.02;OA 3,2>1;OA 5,6>7;OA 10,9>8;OA 12,13>14");
    chip("lm741", "LM741 Op-Amp", "Analog ICs", 8, "LM741", "OFFSET N1,IN-,IN+,V-,OFFSET N2,OUT,V+,NC", "The classic general-purpose op-amp." + an,
         "opamp", "VCC=7;GND=4;HI=2;LO=2;OA 3,2>6");
    chip("tl072", "TL072 Dual JFET Op-Amp", "Analog ICs", 8, "TL072", "1OUT,1IN-,1IN+,V-,2IN+,2IN-,2OUT,V+", "Low-noise audio op-amp." + an, "opamp",
         "VCC=8;GND=4;HI=1.5;LO=1.5;OA 3,2>1;OA 5,6>7");
    chip("ne5532", "NE5532 Dual Low-Noise Op-Amp", "Analog ICs", 8, "NE5532", "1OUT,1IN-,1IN+,V-,2IN+,2IN-,2OUT,V+", "Audio op-amp." + an, "opamp",
         "VCC=8;GND=4;HI=1.5;LO=1.5;OA 3,2>1;OA 5,6>7");
    chip("mcp6002", "MCP6002 Rail-to-Rail Op-Amp", "Analog ICs", 8, "MCP6002", "1OUT,1IN-,1IN+,VSS,2IN+,2IN-,2OUT,VDD",
         "1.8–6 V rail-to-rail dual op-amp." + an, "opamp", "VCC=8;GND=4;HI=0.02;LO=0.02;OA 3,2>1;OA 5,6>7");
    chip("lm393", "LM393 Dual Comparator", "Analog ICs", 8, "LM393", "1OUT,1IN-,1IN+,GND,2IN+,2IN-,2OUT,VCC",
         "Open-collector comparator: the output pulls LOW when IN+ < IN−, so add a pull-up resistor. Simulated.", "comparator",
         "VCC=8;GND=4;CMP 3,2>1;CMP 5,6>7");
    chip("lm339", "LM339 Quad Comparator", "Analog ICs", 14, "LM339", "2OUT,1OUT,VCC,1IN-,1IN+,2IN-,2IN+,3IN-,3IN+,4IN-,4IN+,GND,4OUT,3OUT",
         "Four open-collector comparators (pull-ups required). Simulated.", "comparator", "VCC=3;GND=12;CMP 5,4>2;CMP 7,6>1;CMP 9,8>14;CMP 11,10>13");
    chip("ne556", "NE556 Dual Timer", "Analog ICs", 14, "NE556", "1DIS,1THR,1CTRL,1RESET,1OUT,1TRIG,GND,2TRIG,2OUT,2RESET,2CTRL,2THR,2DIS,VCC",
         "Two 555 timers in one package. Fully simulated.", "555",
         "U GND=7,TRIG=6,OUT=5,RESET=4,CTRL=3,THR=2,DIS=1,VCC=14;U GND=7,TRIG=8,OUT=9,RESET=10,CTRL=11,THR=12,DIS=13,VCC=14");
    chip("lm386", "LM386 Audio Amplifier", "Analog ICs", 8, "LM386", "GAIN,IN-,IN+,GND,OUT,VS,BYPASS,GAIN",
         "Low-voltage audio power amplifier (gain 20). Simulated as a DC amplifier biased at VS/2.", "amp386", "VS=6;GND=4;IN+=3;IN-=2;OUT=5;GAIN=20");
    chip("pc817", "PC817 Optocoupler", "Analog ICs", 4, "PC817", "A,K,E,C", "Isolates two circuits with an internal LED and phototransistor. Simulated.",
         "opto", "LED 1>2;SW 4>3");
    chip("4n35", "4N35 Optocoupler", "Analog ICs", 6, "4N35", "A,K,NC,E,C,B", "Phototransistor optocoupler. Simulated.", "opto", "LED 1>2;SW 5>4");
    chip("max232", "MAX232 RS-232 Driver", "Analog ICs", 16, "MAX232", "C1+,V+,C1-,C2+,C2-,V-,T2OUT,R2IN,R2OUT,T2IN,T1IN,R1OUT,R1IN,T1OUT,GND,VCC",
         "TTL ↔ RS-232 level converter.", "", "");
    add("tl431", "TL431 Shunt Reference", "Analog ICs", {}, [](PartDef& d, const Props&) {
        makeTo92(d, "TL431", names("REF,A,K"));
        finish(d, "TL431 Adjustable Shunt Reference", "Programmable 2.5–36 V reference / precision zener. Simulated.", "tl431", "REF=1;A=2;K=3");
    });

    // ===================== Drivers =====================
    chip("uln2003", "ULN2003A Darlington Array", "Drivers", 16, "ULN2003A", "IN1,IN2,IN3,IN4,IN5,IN6,IN7,GND,COM,OUT7,OUT6,OUT5,OUT4,OUT3,OUT2,OUT1",
         "Seven open-collector drivers (500 mA) for relays, steppers and LED strips. Simulated.", "uln",
         "GND=8;IN 1>16;IN 2>15;IN 3>14;IN 4>13;IN 5>12;IN 6>11;IN 7>10");
    chip("uln2803", "ULN2803A Darlington Array", "Drivers", 18, "ULN2803A", "IN1,IN2,IN3,IN4,IN5,IN6,IN7,IN8,GND,COM,OUT8,OUT7,OUT6,OUT5,OUT4,OUT3,OUT2,OUT1",
         "Eight open-collector drivers. Simulated.", "uln", "GND=9;IN 1>18;IN 2>17;IN 3>16;IN 4>15;IN 5>14;IN 6>13;IN 7>12;IN 8>11");
    chip("l293d", "L293D Dual H-Bridge", "Drivers", 16, "L293D", "EN12,1A,1Y,GND,GND,2Y,2A,VCC2,EN34,3A,3Y,GND,GND,4Y,4A,VCC1",
         "Drives two DC motors forwards/backwards (600 mA). Motor supply on VCC2. Simulated.", "l293", "VS=8;GND=4;CH 1:2>3;CH 1:7>6;CH 9:10>11;CH 9:15>14");
    add("l298n_module", "L298N Motor Driver Module", "Drivers", {}, [](PartDef& d, const Props&) {
        makeBreakout(d, "L298N", names("OUT1,OUT2,12V,GND,5V,ENA,IN1,IN2,IN3,IN4,ENB,OUT3,OUT4"), 1500, 1400, rgba(170, 30, 40), "L298N", rgba(30, 30, 30));
        finish(d, "L298N Motor Driver Module", "Dual H-bridge module for two DC motors or one stepper (2 A). Simulated.", "l293",
               "VS=3;GND=4;CH 6:7>1;CH 6:8>2;CH 11:9>12;CH 11:10>13");
    });
    add("a4988", "A4988 Stepper Driver", "Drivers", {}, [](PartDef& d, const Props&) {
        makeModule(d, "A4988", names("EN,MS1,MS2,MS3,RST,SLP,STEP,DIR"), names("VMOT,GND,2B,2A,1A,1B,VDD,GND"), 500, rgba(30, 110, 50), "A4988", rgba(35, 35, 38));
        finish(d, "A4988 Stepper Driver", "Microstepping bipolar stepper driver (Pololu footprint).", "", "");
    });
    add("drv8825", "DRV8825 Stepper Driver", "Drivers", {}, [](PartDef& d, const Props&) {
        makeModule(d, "DRV8825", names("EN,M0,M1,M2,RST,SLP,STEP,DIR"), names("VMOT,GND,B2,B1,A1,A2,FLT,GND"), 500, rgba(120, 40, 140), "DRV8825", rgba(35, 35, 38));
        finish(d, "DRV8825 Stepper Driver", "High-current microstepping driver (Pololu footprint).", "", "");
    });
    add("relay_module", "Relay Module (5V, SPDT)", "Drivers", {}, [](PartDef& d, const Props&) {
        makeBreakout(d, "Relay 5V", names("COIL+,COIL-,COM,NO,NC"), 1000, 900, rgba(40, 90, 200), "SRD-05VDC", rgba(30, 60, 160));
        finish(d, "Relay (5V, SPDT)", "Electromechanical switch: current through the 70 Ω coil (≥40 mA) moves COM from NC to NO. Simulated, with a click!",
               "relay", "COIL 1,2;COM=3;NO=4;NC=5;R=70;I=0.04");
        d.labelPrefix = "K";
    });
    add("tip120", "TIP120 Darlington", "Drivers", {}, [](PartDef& d, const Props&) {
        makeTo220(d, "TIP120", names("B,C,E"));
        finish(d, "TIP120 Darlington Transistor", "High-gain (β≈1000) NPN power Darlington, 5 A. Simulated.", "npn", "BETA=1000;VF=1.3");
        d.labelPrefix = "Q";
    });
    add("irf540n", "IRF540N MOSFET", "Drivers", {}, [](PartDef& d, const Props&) {
        makeTo220(d, "IRF540N", names("G,D,S"));
        finish(d, "IRF540N N-MOSFET", "100 V / 33 A MOSFET. Needs ~10 V on the gate for full turn-on (use a logic-level part with 3.3/5 V). Simulated.",
               "nmos", "VTH=4");
        d.labelPrefix = "Q";
    });

    // ===================== Power =====================
    add("lm317", "LM317 Adjustable Regulator", "Power", {}, [](PartDef& d, const Props&) {
        makeTo220(d, "LM317", names("ADJ,OUT,IN"));
        finish(d, "LM317 Adjustable Regulator", "1.25–37 V linear regulator: Vout = 1.25 V × (1 + R2/R1). Simulated.", "reg", "IN=3;OUT=2;ADJ=1;VREF=1.25;DROP=2");
    });
    add("ams1117_33", "AMS1117-3.3 LDO", "Power", {}, [](PartDef& d, const Props&) {
        makeTo220(d, "AMS1117", names("GND,OUT,IN"));
        finish(d, "AMS1117-3.3 LDO Regulator", "3.3 V low-dropout regulator (shown in a through-hole outline). Simulated.", "reg", "IN=3;OUT=2;GND=1;V=3.3;DROP=1.1");
    });
    add("l7805_to92", "78L05 Regulator (TO-92)", "Power", {}, [](PartDef& d, const Props&) {
        makeTo92(d, "78L05", names("OUT,GND,IN"));
        finish(d, "78L05 Regulator (100 mA)", "Small 5 V regulator. Simulated.", "reg", "IN=3;GND=2;OUT=1;V=5;DROP=1.7");
    });
    add("lm2596_module", "LM2596 Buck Converter", "Power", {{"voltage", "5V"}}, [](PartDef& d, const Props&) {
        makeBreakout(d, "LM2596", names("IN+,IN-,OUT-,OUT+"), 1700, 900, rgba(30, 90, 190), "LM2596", rgba(30, 30, 30));
        finish(d, "LM2596 Buck Converter Module", "Adjustable step-down converter (set the output voltage in the Inspector). Simulated.", "reg",
               "IN=1;GND=2;OUT=4;VPROP=voltage;DROP=1.5");
        d.propDefs = {{"voltage", "Output", {"3.3V", "5V", "9V", "12V"}}};
        d.valueProp = "voltage";
    });
    add("mt3608_module", "MT3608 Boost Converter", "Power", {{"voltage", "5V"}}, [](PartDef& d, const Props&) {
        makeBreakout(d, "MT3608", names("VIN+,VIN-,VOUT-,VOUT+"), 1000, 700, rgba(30, 90, 190), "MT3608", rgba(30, 30, 30));
        finish(d, "MT3608 Boost Converter Module", "Step-up converter: 2–24 V in, adjustable output. Simulated.", "reg", "IN=1;GND=2;OUT=4;VPROP=voltage;BOOST");
        d.propDefs = {{"voltage", "Output", {"5V", "9V", "12V"}}};
        d.valueProp = "voltage";
    });
    add("tp4056_module", "TP4056 Li-ion Charger", "Power", {}, [](PartDef& d, const Props&) {
        makeBreakout(d, "TP4056", names("IN+,IN-,B+,B-,OUT+,OUT-"), 900, 700, rgba(30, 90, 190), "TP4056", rgba(30, 30, 30));
        finish(d, "TP4056 Li-ion Charger Module", "1 A single-cell lithium battery charger with protection.", "", "");
    });

    // ===================== Sensors =====================
    auto sensor = [&](const std::string& type, const std::string& title, const char* pins, float w, float h, uint32_t col, const std::string& chipT,
                      const std::string& desc, const std::string& sim, const std::string& params, Props defs = {}, std::vector<PropDef> pdefs = {}) {
        std::string ps = pins;
        add(type, title, "Sensors", defs, [=](PartDef& d, const Props&) {
            makeBreakout(d, title.substr(0, title.find(' ')), names(ps.c_str()), w, h, col, chipT, rgba(30, 30, 32));
            finish(d, title, desc, sim, params);
            d.labelPrefix = "S";
            d.propDefs = pdefs;
        });
    };
    add("lm35", "LM35 Temperature Sensor", "Sensors", {{"temperature", "25"}}, [](PartDef& d, const Props&) {
        makeTo92(d, "LM35", names("+VS,VOUT,GND"));
        finish(d, "LM35 Temperature Sensor", "Outputs 10 mV per °C. Set the temperature in the Inspector. Simulated.", "sensor", "VCC=1;GND=3;LIN 2:temperature:0:0.01");
        d.propDefs = {{"temperature", "Temperature °C", {"0", "25", "37", "60", "100"}}};
        d.valueProp = "temperature";
        d.labelPrefix = "S";
    });
    add("tmp36", "TMP36 Temperature Sensor", "Sensors", {{"temperature", "25"}}, [](PartDef& d, const Props&) {
        makeTo92(d, "TMP36", names("+VS,VOUT,GND"));
        finish(d, "TMP36 Temperature Sensor", "0.5 V at 0 °C, +10 mV per °C. Simulated.", "sensor", "VCC=1;GND=3;LIN 2:temperature:0.5:0.01");
        d.propDefs = {{"temperature", "Temperature °C", {"-10", "0", "25", "37", "60"}}};
        d.valueProp = "temperature";
        d.labelPrefix = "S";
    });
    add("ds18b20", "DS18B20 Temperature Sensor", "Sensors", {}, [](PartDef& d, const Props&) {
        makeTo92(d, "DS18B20", names("GND,DQ,VDD"));
        finish(d, "DS18B20 1-Wire Temperature Sensor", "Digital temperature sensor (needs a 4.7 kΩ pull-up on DQ).", "", "");
        d.labelPrefix = "S";
    });
    add("a3144", "A3144 Hall Sensor", "Sensors", {{"magnet", "no"}}, [](PartDef& d, const Props&) {
        makeTo92(d, "A3144", names("VCC,GND,OUT"));
        finish(d, "A3144 Hall-Effect Switch", "Open-collector output pulls LOW near a magnet (add a pull-up). Toggle \"magnet\" in the Inspector. Simulated.",
               "sensor", "VCC=1;GND=2;OC 3:magnet");
        d.propDefs = {{"magnet", "Magnet near", {"no", "yes"}}};
        d.labelPrefix = "S";
    });
    sensor("dht11", "DHT11 Temperature & Humidity", "VCC,DATA,GND", 700, 900, rgba(40, 110, 200), "DHT11", "Single-wire digital temperature/humidity sensor module.", "", "");
    sensor("dht22", "DHT22 (AM2302) Temperature & Humidity", "VCC,DATA,GND", 700, 1000, rgba(240, 240, 240), "DHT22", "More accurate digital temperature/humidity sensor.", "", "");
    sensor("hcsr04", "HC-SR04 Ultrasonic Distance", "VCC,TRIG,ECHO,GND", 1800, 800, rgba(30, 90, 190), "HC-SR04", "Measures distance (2–400 cm) with ultrasound pulses.", "", "");
    sensor("hcsr501", "HC-SR501 PIR Motion", "VCC,OUT,GND", 1300, 1200, rgba(30, 110, 60), "PIR", "Outputs 3.3 V when motion is detected. Toggle \"motion\" in the Inspector. Simulated.",
           "sensor", "VCC=1;GND=3;DIG 2:motion:3.3", {{"motion", "no"}}, {{"motion", "Motion detected", {"no", "yes"}}});
    sensor("soil_moisture", "Soil Moisture Sensor", "VCC,GND,DO,AO", 900, 700, rgba(30, 90, 190), "LM393", "Analog output rises with moisture (set % in the Inspector). Simulated.",
           "sensor", "VCC=1;GND=2;RATIO 4:moisture", {{"moisture", "40"}}, {{"moisture", "Moisture %", {"0", "20", "40", "60", "80", "100"}}});
    sensor("mq2", "MQ-2 Gas Sensor", "VCC,GND,DO,AO", 1000, 1000, rgba(30, 90, 190), "MQ-2", "Smoke/LPG/CO sensor module; AO rises with gas concentration. Simulated.",
           "sensor", "VCC=1;GND=2;RATIO 4:gas", {{"gas", "10"}}, {{"gas", "Gas level %", {"0", "10", "50", "90"}}});
    sensor("joystick", "Analog Joystick (KY-023)", "GND,+5V,VRX,VRY,SW", 1100, 1300, rgba(30, 30, 32), "JOY", "Two potentiometers and a push switch. Set X/Y in the Inspector. Simulated.",
           "sensor", "VCC=2;GND=1;RATIO 3:x;RATIO 4:y", {{"x", "50"}, {"y", "50"}}, {{"x", "X position %", {"0", "50", "100"}}, {"y", "Y position %", {"0", "50", "100"}}});
    sensor("max6675", "MAX6675 Thermocouple Module (K-type)", "GND,VCC,SCK,CS,SO", 1000, 1100, rgba(170, 30, 40), "MAX6675",
           "Reads a K-type thermocouple (0–1024 °C, 0.25 °C steps) over a 3-wire SPI-style interface: pull CS low, then read 16 bits on SO, "
           "one per SCK pulse (temperature = bits 14..3 × 0.25 °C; bit 2 = thermocouple open). Set the temperature in the Inspector. Simulated.",
           "logic", "VCC=2;GND=1;TC6675 3,4>5", {{"temperature", "25"}, {"thermocouple", "connected"}},
           {{"temperature", "Temperature °C", {"0", "25", "100", "250", "500"}}, {"thermocouple", "Thermocouple", {"connected", "open"}}});
    sensor("mpu6050", "MPU-6050 Accelerometer/Gyro", "VCC,GND,SCL,SDA,XDA,XCL,AD0,INT", 900, 700, rgba(30, 90, 190), "MPU6050", "6-axis motion sensor (I²C).", "", "");
    sensor("bmp280", "BMP280 Pressure Sensor", "VCC,GND,SCL,SDA,CSB,SDO", 700, 600, rgba(120, 40, 140), "BMP280", "Barometric pressure and temperature (I²C/SPI).", "", "");
    sensor("bme280", "BME280 Environment Sensor", "VIN,GND,SCL,SDA", 700, 600, rgba(120, 40, 140), "BME280", "Temperature, humidity and pressure (I²C).", "", "");
    sensor("vl53l0x", "VL53L0X Time-of-Flight", "VIN,GND,SCL,SDA,GPIO1,XSHUT", 700, 600, rgba(120, 40, 140), "VL53L0X", "Laser distance sensor up to 2 m (I²C).", "", "");
    sensor("ir_obstacle", "IR Obstacle Sensor", "VCC,GND,OUT", 600, 1200, rgba(30, 90, 190), "IR", "Infrared reflective obstacle detector with digital output.", "", "");
    sensor("vs1838b", "VS1838B IR Receiver", "OUT,GND,VCC", 400, 500, rgba(30, 30, 32), "IR", "38 kHz infrared remote-control receiver.", "", "");
    sensor("sound_sensor", "Sound Sensor (KY-038)", "AO,GND,VCC,DO", 900, 800, rgba(30, 90, 190), "MIC", "Microphone with analog and digital outputs.", "", "");
    sensor("hc05", "HC-05 Bluetooth Module", "STATE,RXD,TXD,GND,VCC,EN", 700, 1500, rgba(30, 90, 190), "HC-05", "Classic Bluetooth serial (UART) module.", "", "");
    sensor("rc522", "RC522 RFID Reader", "SDA,SCK,MOSI,MISO,IRQ,GND,RST,3.3V", 1600, 2000, rgba(30, 90, 190), "MFRC522", "13.56 MHz RFID/NFC reader (SPI).", "", "");
    sensor("neo6m", "NEO-6M GPS Module", "VCC,RX,TX,GND", 1000, 1000, rgba(30, 90, 190), "NEO-6M", "GPS receiver with UART output.", "", "");
    sensor("nrf24l01", "nRF24L01 2.4 GHz Radio", "GND,VCC,CE,CSN,SCK,MOSI,MISO,IRQ", 1200, 600, rgba(30, 30, 32), "nRF24", "Low-power 2.4 GHz transceiver (SPI).", "", "");
    sensor("ads1115", "ADS1115 16-bit ADC", "VDD,GND,SCL,SDA,ADDR,ALRT,A0,A1,A2,A3", 1100, 600, rgba(120, 40, 140), "ADS1115", "Four-channel precision ADC (I²C).", "", "");
    sensor("ds3231", "DS3231 Real-Time Clock", "32K,SQW,SCL,SDA,VCC,GND", 1500, 900, rgba(30, 90, 190), "DS3231", "Accurate I²C real-time clock with battery backup.", "", "");

    // ===================== Displays =====================
    add("seg7_cc", "7-Segment Display (CC)", "Displays", {{"color", "Red"}}, [](PartDef& d, const Props& p) {
        makeSeg7(d, false, ledColorOf(prop(p, "color", "Red")));
        d.title = "7-Segment Display (common cathode)";
        d.description = "Single digit, 0.56\". Connect COM to GND and each segment through a resistor. Segments light in simulation.";
        d.propDefs = {{"color", "Color", {"Red", "Green", "Blue", "Yellow", "White", "Orange"}}};
        d.labelPrefix = "DS";
    });
    add("seg7_ca", "7-Segment Display (CA)", "Displays", {{"color", "Red"}}, [](PartDef& d, const Props& p) {
        makeSeg7(d, true, ledColorOf(prop(p, "color", "Red")));
        d.title = "7-Segment Display (common anode)";
        d.description = "Single digit, 0.56\". Connect COM to + and pull segments LOW through resistors. Segments light in simulation.";
        d.propDefs = {{"color", "Color", {"Red", "Green", "Blue", "Yellow", "White", "Orange"}}};
        d.labelPrefix = "DS";
    });
    add("rgb_led", "RGB LED", "Displays", {{"common", "cathode"}}, makeRgbLed);
    auto display = [&](const std::string& type, const std::string& title, const char* pins, float w, float h, uint32_t col, const std::string& text,
                       uint32_t screen, const std::string& desc, const std::string& sim = "", const std::string& params = "") {
        std::string ps = pins;
        add(type, title, "Displays", {}, [=](PartDef& d, const Props&) {
            makeBreakout(d, title.substr(0, title.find(' ')), names(ps.c_str()), w, h, col, text, screen, true);
            finish(d, title, desc, sim, params);
            d.labelPrefix = "DS";
        });
    };
    display("ssd1306", "SSD1306 OLED 0.96\" (I²C)", "GND,VCC,SCL,SDA", 1100, 1100, rgba(30, 60, 140), "128×64 OLED", rgba(10, 10, 14), "Tiny monochrome OLED display.");
    display("lcd1602", "LCD 16×2 (HD44780)", "VSS,VDD,V0,RS,RW,E,D0,D1,D2,D3,D4,D5,D6,D7,A,K", 3200, 1400, rgba(30, 110, 50), "Hello, world!", rgba(150, 200, 60),
            "Character LCD. The backlight (A/K) is simulated as an LED.", "leds", "LED 15>16:3.0:20");
    display("lcd1602_i2c", "LCD 16×2 with I²C Backpack", "GND,VCC,SDA,SCL", 3200, 1400, rgba(30, 110, 50), "Hello, world!", rgba(150, 200, 60), "Character LCD with PCF8574 I²C adapter.");
    display("tm1637", "TM1637 4-Digit Display", "CLK,DIO,VCC,GND", 1700, 800, rgba(30, 90, 190), "8.8.:8.8.", rgba(30, 10, 10), "Four-digit 7-segment module with 2-wire interface.");
    display("max7219_matrix", "MAX7219 8×8 LED Matrix", "VCC,GND,DIN,CS,CLK", 1300, 1400, rgba(30, 90, 190), "8×8 LED", rgba(30, 10, 10), "Dot-matrix module, chainable (SPI).");
    display("ws2812_strip", "WS2812B LED Strip (NeoPixel)", "5V,DIN,GND", 2000, 400, rgba(240, 240, 240), "● ● ● ●", rgba(40, 40, 40), "Addressable RGB LEDs, one data wire.");
    display("st7735", "ST7735 1.8\" TFT", "GND,VCC,SCL,SDA,RES,DC,CS,BL", 1400, 1800, rgba(170, 30, 40), "160×128 TFT", rgba(10, 10, 14), "Colour TFT display (SPI).");

    // ===================== Memory / interface ICs =====================
    chip("24lc256", "24LC256 EEPROM (I²C)", "ICs", 8, "24LC256", "A0,A1,A2,VSS,SDA,SCL,WP,VCC", "32 KB serial EEPROM.", "", "");
    chip("ds1307", "DS1307 Real-Time Clock", "ICs", 8, "DS1307", "X1,X2,VBAT,GND,SDA,SCL,SQW,VCC", "I²C real-time clock (needs a 32.768 kHz crystal).", "", "");
    chip("mcp3008", "MCP3008 8-ch 10-bit ADC", "ICs", 16, "MCP3008", "CH0,CH1,CH2,CH3,CH4,CH5,CH6,CH7,DGND,CS,DIN,DOUT,CLK,AGND,VREF,VDD",
         "SPI ADC — adds analog inputs to a Raspberry Pi.", "", "");
    chip("mcp23017", "MCP23017 16-bit I/O Expander", "ICs", 28, "MCP23017",
         "GPB0,GPB1,GPB2,GPB3,GPB4,GPB5,GPB6,GPB7,VDD,VSS,NC,SCK,SDA,NC,A0,A1,A2,RESET,INTB,INTA,GPA0,GPA1,GPA2,GPA3,GPA4,GPA5,GPA6,GPA7",
         "Adds 16 I/O pins over I²C.", "", "");
    chip("pcf8574", "PCF8574 8-bit I/O Expander", "ICs", 16, "PCF8574", "A0,A1,A2,P0,P1,P2,P3,VSS,P4,P5,P6,P7,INT,SCL,SDA,VDD", "Adds 8 I/O pins over I²C.", "", "");
    chip("max7219", "MAX7219 LED Driver", "ICs", 24, "MAX7219",
         "DIN,DIG0,DIG4,GND,DIG6,DIG2,DIG3,DIG7,GND,DIG5,DIG1,LOAD,CLK,SEGA,SEGF,SEGB,SEGG,ISET,V+,SEGC,SEGE,SEGDP,SEGD,DOUT",
         "Drives 64 LEDs or eight 7-segment digits over SPI.", "", "");
    chip("ch340g", "CH340G USB-Serial", "ICs", 16, "CH340G", "GND,TXD,RXD,V3,UD+,UD-,XI,XO,CTS,DSR,RI,DCD,DTR,RTS,R232,VCC", "USB to UART bridge.", "", "");
    return v;
}

}  // namespace

void addCatalogGenerators(std::map<std::string, Gen>& g) {
    for (auto& it : items()) g[it.type] = Gen{it.title, it.category, it.defaults, it.make};
}

const std::vector<LibEntry>& catalogEntries() {
    static std::vector<LibEntry> e = [] {
        std::vector<LibEntry> v;
        for (auto& it : items()) v.push_back({it.type, it.title, it.category, {}});
        return v;
    }();
    return e;
}

}  // namespace plib
