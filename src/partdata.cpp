// Parts described as data: parts/*.txt, compiled into the app (see
// cmake/embed_parts.cmake and parts/README.md for the format).
//
// Each block is one pinout: a part number, its packages and its pin names in
// pin-number order, written from the manufacturer's datasheet. Other part
// numbers with the same pinout are listed as aliases. A block becomes a part
// generator whose "package" property switches the footprint and whose "part"
// property picks the part number printed on it.
#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>

#include "packages.h"
#include "partlib_internal.h"

extern "C" const unsigned char de_partdata[];
extern "C" const unsigned long de_partdata_size;

namespace plib {
namespace {

struct DataPart {
    std::string type, part, title, category, desc, datasheet, sim, simParams, symbol, prefix, label, sch, style;
    std::string file;
    int line = 0;
    std::vector<std::string> packages, aliases;
    std::string pins, covers, aliasText;
    bool numIds = false;    // ids = numbers: pin ids are "pin<N>" (DIP catalog parts, old files)
    bool replaces = false;  // replaces a catalog part of the same type
    bool builtin = false;   // only records what a built-in part covers (coverage checklist)
    std::vector<std::pair<std::string, std::string>> pinsFor;  // package (or family) → pins
    Props props;                                               // extra default properties
};

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::vector<std::string> splitOn(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { cur = trim(cur); if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    cur = trim(cur);
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == '\n') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string lowerStr(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string typeOf(const std::string& part) {
    std::string t;
    for (char c : lowerStr(part)) t += std::isalnum((unsigned char)c) ? c : '_';
    return t;
}

std::vector<std::string>& errors() {
    static std::vector<std::string> e;
    return e;
}

const std::set<std::string>& knownCategories() {
    static std::set<std::string> s(partCategories().begin(), partCategories().end());
    return s;
}

std::vector<DataPart> parseAll() {
    std::vector<DataPart> out;
    const char* p = (const char*)de_partdata;
    const char* end = p + de_partdata_size;
    std::string file;
    int lineNo = 0;
    DataPart* cur = nullptr;
    std::string* last = nullptr;  // value a continuation line appends to
    auto err = [&](const std::string& m) { errors().push_back(file + ":" + std::to_string(lineNo) + ": " + m); };
    while (p < end) {
        const char* nl = (const char*)std::memchr(p, '\n', (size_t)(end - p));
        if (!nl) nl = end;
        std::string raw(p, nl);
        p = nl + 1;
        lineNo++;
        if (raw.rfind("@file ", 0) == 0) { file = trim(raw.substr(6)); lineNo = 0; cur = nullptr; last = nullptr; continue; }
        std::string line = trim(raw);
        if (line.empty() || line[0] == '#') continue;
        if (!raw.empty() && (raw[0] == ' ' || raw[0] == '\t')) {  // continuation
            if (last) { *last += ' '; *last += line; }
            else err("continuation line outside a value");
            continue;
        }
        if (line[0] == '[') {
            if (line.back() != ']') { err("block header needs a closing ]"); continue; }
            out.emplace_back();
            cur = &out.back();
            cur->part = trim(line.substr(1, line.size() - 2));
            cur->type = typeOf(cur->part);
            cur->file = file;
            cur->line = lineNo;
            last = nullptr;
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) { err("expected key = value"); continue; }
        if (!cur) { err("value outside a [part] block"); continue; }
        std::string key = trim(line.substr(0, eq)), val = trim(line.substr(eq + 1));
        last = nullptr;
        if (key == "title") last = &(cur->title = val);
        else if (key == "category") cur->category = val;
        else if (key == "desc") last = &(cur->desc = val);
        else if (key == "datasheet") cur->datasheet = val;
        else if (key == "package") cur->packages = splitOn(val, ',');
        else if (key == "pins") last = &(cur->pins = val);
        else if (key.rfind("pins.", 0) == 0) { cur->pinsFor.push_back({trim(key.substr(5)), val}); last = &cur->pinsFor.back().second; }
        else if (key == "aliases") last = &(cur->aliasText = val);
        else if (key == "sim") cur->sim = val;
        else if (key == "simParams") last = &(cur->simParams = val);
        else if (key == "symbol") cur->symbol = val;
        else if (key == "prefix") cur->prefix = val;
        else if (key == "label") cur->label = val;
        else if (key == "sch") last = &(cur->sch = val);
        else if (key == "style") cur->style = val;
        else if (key == "type") cur->type = val;
        else if (key == "ids") { if (val == "numbers") cur->numIds = true; else err("ids is \"numbers\" or absent"); }
        else if (key == "replaces") cur->replaces = val == "yes";
        else if (key == "builtin") cur->builtin = val == "yes";
        else if (key == "covers") last = &(cur->covers = val);  // KiCad names this block stands for (coverage tool only)
        else if (key.rfind("prop.", 0) == 0) cur->props[key.substr(5)] = val;
        else err("unknown key \"" + key + "\"");
    }
    for (auto& dp : out) dp.aliases = splitOn(dp.aliasText, ',');
    return out;
}

// ---- pins -----------------------------------------------------------------------

// Pin list for a package: positional names fill pads 1, 2, 3 …; "N:NAME" names
// pad N (any number, or a BGA ball, or EP / TAB).
struct PinMap {
    std::map<std::string, std::string> byNum;
    std::vector<std::string> balls;  // explicit numbers, in order (BGA)
    int positional = 0;
};

PinMap pinMap(const std::string& spec) {
    PinMap m;
    int next = 1;
    for (auto& w : words(spec)) {
        size_t c = w.find(':');
        if (c != std::string::npos && c > 0 && c + 1 < w.size()) {
            std::string num = w.substr(0, c);
            m.byNum[num] = w.substr(c + 1);
            m.balls.push_back(num);
        } else {
            m.byNum[std::to_string(next++)] = w;
            m.positional++;
        }
    }
    return m;
}

const std::string& pinsFor(const DataPart& dp, const Package& pk) {
    std::string fam = words(pk.spec).empty() ? pk.spec : words(pk.spec)[0];
    for (auto& kv : dp.pinsFor) if (kv.first == pk.spec) return kv.second;
    for (auto& kv : dp.pinsFor) if (kv.first == fam || kv.first == pk.family) return kv.second;
    return dp.pins;
}

bool isPowerName(const std::string& n) {
    static const char* pre[] = {"VCC", "VDD", "V+", "VIN", "AVDD", "DVDD", "VBAT", "VS+", "VDDA", "VDDIO", "AVCC", "DVCC", "VPP", "VBUS", "VSUP", "VM", "VCCA", "VCCB", "VCCIO"};
    for (auto* p : pre) if (n.rfind(p, 0) == 0) return true;
    return n == "VS" || n == "V+";
}
bool isGroundName(const std::string& n) {
    static const char* pre[] = {"GND", "VSS", "AGND", "DGND", "PGND", "VEE", "V-", "AVSS", "DVSS", "SGND", "GNDA", "GNDD", "VSSA", "EP"};
    for (auto* p : pre) if (n.rfind(p, 0) == 0) return true;
    return n == "0V";
}

// Pin names starting with ~ are active low: drawn with an overline.
void pinLabel(G& g, float x, float y, const std::string& name, int align, float size = 45) {
    bool bar = name.size() > 1 && name[0] == '~';
    std::string t = bar ? name.substr(1) : name;
    g.text(x, y, t, size, INKTXT, align);
    if (bar) {
        float w = size * 0.6f * (float)t.size();
        float x0 = align == 0 ? x : align == 1 ? x - w / 2 : x - w;
        g.line(x0, y - size * 0.62f, x0 + w, y - size * 0.62f, 4, INKTXT);
    }
}

// Schematic box. Pins go left/right/top/bottom as `sch` says
// ("left: A B C; right: Y; top: VCC; bottom: GND", names or numbers); without
// it, supply pins go on top, ground on the bottom and the rest split in pin
// order down the left and up the right, like the package's top view.
void schematicBox(PartDef& d, const std::string& title, const std::string& sch, const std::vector<int>& shadow) {
    std::vector<int> side[4];  // left, right, top, bottom
    std::vector<int> visible;
    for (size_t i = 0; i < d.pins.size(); i++) if (shadow[i] < 0) visible.push_back((int)i);
    if (!sch.empty()) {
        std::set<int> placed;
        for (auto& part : splitOn(sch, ';')) {
            size_t c = part.find(':');
            if (c == std::string::npos) continue;
            std::string s = lowerStr(trim(part.substr(0, c)));
            int k = s == "left" ? 0 : s == "right" ? 1 : s == "top" ? 2 : s == "bottom" ? 3 : -1;
            if (k < 0) continue;
            for (auto& w : words(part.substr(c + 1))) {
                if (w == "-") { side[k].push_back(-1); continue; }  // gap
                int found = -1;
                for (int i : visible) if (!placed.count(i) && d.pins[(size_t)i].name == w) { found = i; break; }
                if (found < 0 && std::all_of(w.begin(), w.end(), [](char ch) { return std::isdigit((unsigned char)ch) != 0; })) {
                    int n = std::atoi(w.c_str()) - 1;
                    if (n >= 0 && n < (int)d.pins.size() && !placed.count(n)) found = n;
                }
                if (found >= 0) { side[k].push_back(found); placed.insert(found); }
            }
        }
        for (int i : visible) if (!placed.count(i)) side[1].push_back(i);  // anything not listed
    } else {
        std::vector<int> rest;
        bool small = visible.size() <= 3;
        for (int i : visible) {
            const std::string& n = d.pins[(size_t)i].name;
            if (!small && isPowerName(n)) side[2].push_back(i);
            else if (!small && isGroundName(n)) side[3].push_back(i);
            else rest.push_back(i);
        }
        size_t half = (rest.size() + 1) / 2;
        for (size_t i = 0; i < rest.size(); i++) {
            if (i < half) side[0].push_back(rest[i]);
            else side[1].insert(side[1].begin(), rest[i]);
        }
    }
    auto nameLen = [&](int i) { if (i < 0) return 0; const std::string& n = d.pins[(size_t)i].name; return (int)n.size() - (n.size() > 1 && n[0] == '~'); };
    int maxL = 0, maxR = 0, maxTB = 0;
    for (int i : side[0]) maxL = std::max(maxL, nameLen(i));
    for (int i : side[1]) maxR = std::max(maxR, nameLen(i));
    for (int k = 2; k < 4; k++) for (int i : side[k]) maxTB = std::max(maxTB, nameLen(i));
    size_t tb = std::max(side[2].size(), side[3].size());
    float tbStep = maxTB > 3 ? 200.f : 100.f;
    float w = std::max({400.f, (float)(maxL + maxR) * 27 + 160, (float)tb * tbStep + 100});
    w = std::ceil(w / 100) * 100;
    float topPad = side[2].empty() ? 100 : 200, botPad = side[3].empty() ? 100 : 200;
    size_t rows = std::max<size_t>(1, std::max(side[0].size(), side[1].size()));
    float h = topPad + (float)(rows - 1) * 100 + botPad;
    G g{d.gfx[SCH]};
    float x0 = 200, x1 = 200 + w;
    g.rect(x0, 0, w, h, rgba(255, 255, 250), INK, SW);
    for (size_t k = 0; k < side[0].size(); k++) {
        int i = side[0][k];
        if (i < 0) continue;
        float y = topPad + (float)k * 100;
        d.pins[(size_t)i].pos[SCH] = {0, y};
        g.line(0, y, x0, y, SW, INK);
        pinLabel(g, x0 + 25, y, d.pins[(size_t)i].name, 0);
    }
    for (size_t k = 0; k < side[1].size(); k++) {
        int i = side[1][k];
        if (i < 0) continue;
        float y = topPad + (float)k * 100;
        d.pins[(size_t)i].pos[SCH] = {x1 + 200, y};
        g.line(x1, y, x1 + 200, y, SW, INK);
        pinLabel(g, x1 - 25, y, d.pins[(size_t)i].name, 2);
    }
    for (int k = 2; k < 4; k++) {
        auto& v = side[k];
        float span = (float)(v.size() - (v.empty() ? 0 : 1)) * tbStep;
        float start = x0 + std::round((w / 2 - span / 2) / 100) * 100;
        for (size_t j = 0; j < v.size(); j++) {
            int i = v[j];
            if (i < 0) continue;
            float x = start + (float)j * tbStep;
            bool top = k == 2;
            d.pins[(size_t)i].pos[SCH] = {x, top ? -200.f : h + 200};
            g.line(x, top ? -200.f : h, x, top ? 0.f : h + 200, SW, INK);
            pinLabel(g, x, top ? 45.f : h - 45, d.pins[(size_t)i].name, 1, maxTB > 5 ? 34.f : 40.f);
        }
    }
    g.text(x0 + w / 2, side[2].empty() ? -60.f : -260.f, title, 60, INKTXT, 1);
    for (size_t i = 0; i < d.pins.size(); i++) if (shadow[i] >= 0) d.pins[i].pos[SCH] = d.pins[(size_t)shadow[i]].pos[SCH];
}

// ---- discrete symbols -------------------------------------------------------------

int pinNamed(const PartDef& d, std::initializer_list<const char*> names) {
    for (const char* n : names)
        for (size_t i = 0; i < d.pins.size(); i++) if (d.pins[i].name == n) return (int)i;
    return -1;
}

// Draws a two/three-terminal symbol and places its pins. Returns false when the
// pin names don't fit the symbol (the caller falls back to a box).
bool discreteSymbol(PartDef& d, const std::string& sym) {
    G s{d.gfx[SCH]};
    auto at = [&](int i, Vec2 p) { if (i >= 0) d.pins[(size_t)i].pos[SCH] = p; };
    if (sym == "diode" || sym == "zener" || sym == "schottky" || sym == "led" || sym == "tvs") {
        int a = pinNamed(d, {"A", "anode"}), k = pinNamed(d, {"K", "cathode"});
        if (a < 0 || k < 0) return false;
        at(a, {0, 0});
        at(k, {300, 0});
        s.line(0, 0, 100, 0, SW, INK);
        s.line(200, 0, 300, 0, SW, INK);
        bool led = sym == "led";
        s.poly({{100, -60}, {100, 60}, {200, 0}}, led ? rgba(255, 255, 255) : INK, INK, SW);
        if (sym == "tvs") s.poly({{300, -60}, {300, 60}, {200, 0}}, INK, INK, SW);
        s.line(200, -60, 200, 60, SW, INK);
        if (sym == "zener") { s.line(200, -60, 230, -80, SW, INK); s.line(200, 60, 170, 80, SW, INK); }
        if (sym == "schottky") { s.polyline({{230, -80}, {230, -60}, {200, -60}}, SW, INK); s.polyline({{170, 80}, {170, 60}, {200, 60}}, SW, INK); }
        if (led) {
            s.line(130, -80, 180, -130, SW * 0.8f, INK);
            s.line(170, -70, 220, -120, SW * 0.8f, INK);
            arrowHead(s, {185, -135}, {1, -1}, 30, INK);
            arrowHead(s, {225, -125}, {1, -1}, 30, INK);
        }
        return true;
    }
    if (sym == "npn" || sym == "pnp") {
        int b = pinNamed(d, {"B"}), c = pinNamed(d, {"C"}), e = pinNamed(d, {"E"});
        if (b < 0 || c < 0 || e < 0) return false;
        bool pnp = sym == "pnp";
        at(b, {0, 0});
        at(c, {200, pnp ? 200.f : -200.f});
        at(e, {200, pnp ? -200.f : 200.f});
        s.circle(150, 0, 125, 0, INK, SW);
        s.line(0, 0, 100, 0, SW, INK);
        s.line(100, -70, 100, 70, SW * 1.4f, INK);
        s.polyline({{100, -35}, {200, -110}, {200, -200}}, SW, INK);
        s.polyline({{100, 35}, {200, 110}, {200, 200}}, SW, INK);
        if (pnp) arrowHead(s, {110, -42}, {-1, 0.75f}, 45, INK);
        else arrowHead(s, {195, 107}, {1, 0.75f}, 45, INK);
        return true;
    }
    if (sym == "nmos" || sym == "pmos") {
        int g = pinNamed(d, {"G"}), dr = pinNamed(d, {"D"}), so = pinNamed(d, {"S"});
        if (g < 0 || dr < 0 || so < 0) return false;
        bool p = sym == "pmos";
        at(g, {0, 100});
        at(dr, {200, p ? 300.f : -100.f});
        at(so, {200, p ? -100.f : 300.f});
        s.line(0, 100, 80, 100, SW, INK);
        s.line(80, 0, 80, 200, SW, INK);
        for (int i = 0; i < 3; i++) s.line(120, -10 + i * 100.f, 120, 50 + i * 100.f, SW * 1.4f, INK);
        s.polyline({{120, 20}, {200, 20}, {200, -100}}, SW, INK);
        s.polyline({{120, 180}, {200, 180}, {200, 300}}, SW, INK);
        s.line(120, 100, 200, 100, SW, INK);
        s.line(200, 100, 200, p ? 20.f : 180.f, SW, INK);
        if (p) arrowHead(s, {195, 100}, {1, 0}, 40, INK);
        else arrowHead(s, {125, 100}, {-1, 0}, 40, INK);
        return true;
    }
    if (sym == "resistor" || sym == "capacitor" || sym == "inductor" || sym == "fuse" || sym == "ferrite" || sym == "crystal" || sym == "ecap") {
        if (d.pins.size() < 2) return false;
        bool pol = sym == "ecap";
        int a = pol ? pinNamed(d, {"+", "plus"}) : 0, b = pol ? pinNamed(d, {"-", "minus"}) : 1;
        if (a < 0 || b < 0) return false;
        float len = sym == "resistor" || sym == "inductor" || sym == "fuse" || sym == "ferrite" ? 400.f : 300.f;
        at(a, {0, 0});
        at(b, {len, 0});
        if (sym == "resistor") { s.line(0, 0, 100, 0, SW, INK); s.line(300, 0, 400, 0, SW, INK); zigzag(s, 100, 300, 0, 40); }
        else if (sym == "inductor") { s.line(0, 0, 100, 0, SW, INK); s.line(300, 0, 400, 0, SW, INK); for (int i = 0; i < 4; i++) s.arc(125 + i * 50.f, 0, 25, 180, 360, SW, INK); }
        else if (sym == "fuse") { s.line(0, 0, 400, 0, SW, INK); s.rect(100, -40, 200, 80, 0, INK, SW); }
        else if (sym == "ferrite") { s.line(0, 0, 130, 0, SW, INK); s.line(270, 0, 400, 0, SW, INK); s.poly({{130, -30}, {200, -80}, {270, 30}, {200, 80}}, INK, INK, SW); }
        else if (sym == "crystal") { s.line(0, 0, 100, 0, SW, INK); s.line(200, 0, 300, 0, SW, INK); s.line(100, -60, 100, 60, SW, INK); s.line(200, -60, 200, 60, SW, INK); s.rect(125, -80, 50, 160, 0, INK, SW); }
        else {
            s.line(0, 0, 130, 0, SW, INK);
            s.line(170, 0, 300, 0, SW, INK);
            s.line(130, -70, 130, 70, SW * 1.4f, INK);
            if (pol) { s.arc(330, 0, 150, 152, 208, SW * 1.4f, INK); s.text(80, -70, "+", 60, INK); }
            else s.line(170, -70, 170, 70, SW * 1.4f, INK);
        }
        return true;
    }
    return false;
}

// Pin ids: the simulator's names for discrete models, otherwise the pin name
// (so a package change keeps every wire on the same signal).
std::string roleId(const std::string& sym, const std::string& name, size_t index) {
    if (sym == "diode" || sym == "zener" || sym == "schottky" || sym == "led" || sym == "tvs") {
        if (name == "A") return "anode";
        if (name == "K") return "cathode";
    }
    if (sym == "resistor" || sym == "capacitor" || sym == "inductor" || sym == "fuse" || sym == "ferrite" || sym == "crystal")
        return "pin" + std::to_string(index);
    if (sym == "ecap") return name == "+" ? "plus" : name == "-" ? "minus" : name;
    return "";
}

std::string sanitizeId(const std::string& name, std::map<std::string, int>& used) {
    std::string id;
    for (char c : name) id += (std::isalnum((unsigned char)c) || c == '_' || c == '+' || c == '-' || c == '.') ? c : '_';
    if (id.empty()) id = "pin";
    if (used[id]++) id += "_" + std::to_string(used[id]);
    return id;
}

void build(PartDef& d, const DataPart& dp, const Props& p) {
    std::string part = prop(p, "part", dp.part.c_str());
    std::string pkgSpec = prop(p, "package", dp.packages.empty() ? "" : dp.packages[0].c_str());
    Package pk;
    std::string err;
    if (!parsePackage(pkgSpec, pk, &err) && !parsePackage(dp.packages[0], pk, &err)) return;
    PinMap pm = pinMap(pinsFor(dp, pk));
    std::vector<PackagePad> pads = packagePads(pk, pm.balls);
    std::map<std::string, int> used;
    std::map<std::string, int> firstOfNum;
    std::vector<int> shadow;
    for (size_t i = 0; i < pads.size(); i++) {
        const PackagePad& pd = pads[i];
        std::string name;
        auto it = pm.byNum.find(pd.num);
        if (!pd.alt.empty() && pm.byNum.count(pd.alt)) name = pm.byNum[pd.alt];
        else if (it != pm.byNum.end()) name = it->second;
        else name = pd.alt == "EP" ? "EP" : "NC";
        std::string id = dp.numIds ? (pd.alt.empty() ? "pin" + pd.num : lowerStr(pd.alt)) : roleId(dp.symbol, name, i);
        if (id.empty() || used.count(id)) id = sanitizeId(dp.numIds ? id : name, used);
        else used[id]++;
        PinDef& pin = addPin(d, id, name, {0, 0}, {0, 0}, {0, 0});
        (void)pin;
        auto f = firstOfNum.find(pd.num);
        shadow.push_back(f == firstOfNum.end() ? -1 : f->second);
        if (f == firstOfNum.end()) firstOfNum[pd.num] = (int)i;
    }
    // a tab is the same metal as its lead
    for (size_t i = 0; i < shadow.size(); i++)
        if (shadow[i] >= 0) d.buses.push_back({shadow[i], (int)i});
    std::string label = dp.label.empty() ? part : dp.label;
    layoutPackage(d, pk, label, dp.style.empty() ? dp.symbol : dp.style, pm.balls);
    if (dp.symbol.empty() || dp.symbol == "box" || !discreteSymbol(d, dp.symbol)) {
        d.gfx[SCH].clear();
        schematicBox(d, part, dp.sch, shadow);
    }
    for (size_t i = 0; i < shadow.size(); i++)
        if (shadow[i] >= 0) d.pins[i].pos[SCH] = d.pins[(size_t)shadow[i]].pos[SCH];
    d.labelPrefix = dp.prefix.empty() ? "U" : dp.prefix;
    d.valueProp = "part";
    d.title = part + (dp.title.empty() ? "" : " " + dp.title);
    d.description = dp.desc;
    if (!dp.datasheet.empty()) d.description += (d.description.empty() ? "" : " ") + std::string("Datasheet: ") + dp.datasheet;
    d.sim = dp.sim;
    d.simParams = dp.simParams;
    if (d.sim.empty()) d.description += " Pinout only — wire it, route it and export it; the simulator treats it as unconnected.";
    std::vector<std::string> parts = {dp.part};
    for (auto& a : dp.aliases) parts.push_back(a);
    d.propDefs = {{"part", "Part #", parts}, {"package", "Package", dp.packages}};
    for (auto& kv : dp.props) d.propDefs.push_back({kv.first, kv.first, {}});
}

struct Loaded {
    std::vector<DataPart> parts;
    std::vector<std::string> builtins;  // "file:line|type" of builtin = yes blocks
    std::vector<LibEntry> entries;
};

const Loaded& loaded() {
    static Loaded L = [] {
        Loaded l;
        l.parts = parseAll();
        for (auto& dp : l.parts) if (dp.builtin) l.builtins.push_back(dp.file + ":" + std::to_string(dp.line) + "|" + dp.part);
        l.parts.erase(std::remove_if(l.parts.begin(), l.parts.end(), [](const DataPart& dp) { return dp.builtin; }), l.parts.end());
        std::set<std::string> seen;
        for (auto& dp : l.parts) {
            std::string where = dp.file + ":" + std::to_string(dp.line) + ": " + dp.part + ": ";
            if (dp.packages.empty()) { errors().push_back(where + "no package"); continue; }
            if (!knownCategories().count(dp.category)) errors().push_back(where + "unknown category \"" + dp.category + "\"");
            if (!seen.insert(dp.type).second) errors().push_back(where + "duplicate type \"" + dp.type + "\"");
            l.entries.push_back({dp.type, dp.part + (dp.title.empty() ? "" : " " + dp.title), dp.category, {}});
            for (auto& a : dp.aliases) l.entries.push_back({dp.type, a + (dp.title.empty() ? "" : " " + dp.title), dp.category, {{"part", a}}});
        }
        return l;
    }();
    return L;
}

}  // namespace

void addDataGenerators(std::map<std::string, Gen>& g) {
    for (auto& dp : loaded().parts) {
        if (dp.packages.empty()) continue;
        if (g.count(dp.type) && !dp.replaces) { errors().push_back(dp.file + ":" + std::to_string(dp.line) + ": type \"" + dp.type + "\" is already a built-in part (add replaces = yes)"); continue; }
        Props defs = dp.props;
        defs["part"] = dp.part;
        defs["package"] = dp.packages[0];
        const DataPart* ptr = &dp;
        g[dp.type] = Gen{dp.part + (dp.title.empty() ? "" : " " + dp.title), dp.category, defs, [ptr](PartDef& d, const Props& p) { build(d, *ptr, p); }};
    }
}

const std::vector<LibEntry>& dataEntries() { return loaded().entries; }

bool isDataType(const std::string& type) {
    static std::set<std::string> t = [] { std::set<std::string> s; for (auto& dp : loaded().parts) s.insert(dp.type); return s; }();
    return t.count(type) > 0;
}

// Overlapping pads (pins bused together are one conductor) or duplicate pin ids.
std::string padProblem(const PartDef& d) {
    std::set<std::string> ids;
    for (auto& p : d.pins) if (!ids.insert(p.id).second) return "duplicate pin id " + p.id;
    auto joined = [&](size_t i, size_t j) {
        for (auto& b : d.buses)
            if (std::find(b.begin(), b.end(), (int)i) != b.end() && std::find(b.begin(), b.end(), (int)j) != b.end()) return true;
        return false;
    };
    const float minGap = 3.9f;  // mils (0.1 mm)
    for (size_t i = 0; i < d.pins.size(); i++)
        for (size_t j = i + 1; j < d.pins.size(); j++) {
            const PinDef &a = d.pins[i], &b = d.pins[j];
            if (joined(i, j)) continue;
            Vec2 sa = a.padSize(), sb = b.padSize();
            float gx = std::fabs(a.pos[PCB].x - b.pos[PCB].x) - (sa.x + sb.x) / 2;
            float gy = std::fabs(a.pos[PCB].y - b.pos[PCB].y) - (sa.y + sb.y) / 2;
            if (gx < minGap && gy < minGap) return "pads " + a.name + " and " + b.name + " overlap";
        }
    return "";
}

// Validates every data part in every package: the pin list fits the package,
// names are present and pads don't overlap. Used by --selftest.
std::vector<std::string> checkDataParts() {
    std::vector<std::string> out = errors();
    for (auto& b : loaded().builtins) {
        size_t bar = b.find('|');
        if (!generators().count(b.substr(bar + 1))) out.push_back(b.substr(0, bar) + ": no built-in part \"" + b.substr(bar + 1) + "\"");
    }
    for (auto& dp : loaded().parts) {
        std::string where = dp.file + ":" + std::to_string(dp.line) + ": " + dp.part;
        for (auto& spec : dp.packages) {
            Package pk;
            std::string err;
            if (!parsePackage(spec, pk, &err)) { out.push_back(where + ": " + err); continue; }
            PinMap pm = pinMap(pinsFor(dp, pk));
            auto pads = packagePads(pk, pm.balls);
            if (pads.empty()) { out.push_back(where + " [" + spec + "]: no pads"); continue; }
            std::set<std::string> nums;
            for (auto& pd : pads) nums.insert(pd.num);
            int leads = 0;
            for (auto& pd : pads) if (pd.alt.empty()) leads++;
            if (pm.positional && pm.positional != leads)
                out.push_back(where + " [" + spec + "]: " + std::to_string(pm.positional) + " pin names for " + std::to_string(leads) + " pins");
            for (auto& kv : pm.byNum)
                if (!nums.count(kv.first) && kv.first != "EP" && kv.first != "TAB") out.push_back(where + " [" + spec + "]: no pad " + kv.first);
            PartDefPtr d = getPartDef(dp.type, {{"package", spec}});
            if (!d) { out.push_back(where + ": not registered"); continue; }
            std::string bad = padProblem(*d);
            if (!bad.empty()) out.push_back(where + " [" + spec + "]: " + bad);
        }
    }
    // built-in parts in every package / pinout they offer
    for (auto& e : partLibrary()) {
        if (isDataType(e.type)) continue;
        PartDefPtr base = getPartDef(e.type, e.props);
        if (!base) continue;
        for (auto& pd : base->propDefs) {
            if (pd.key != "package" && pd.key != "pinout" && pd.key != "channel" && pd.key != "kind") continue;
            for (auto& opt : pd.options) {
                Props pr = e.props;
                pr[pd.key] = opt;
                PartDefPtr d = getPartDef(e.type, pr);
                std::string bad = d ? padProblem(*d) : "does not build";
                if (!bad.empty()) out.push_back(e.type + " [" + pd.key + "=" + opt + "]: " + bad);
            }
        }
    }
    return out;
}

}  // namespace plib

std::vector<std::string> checkPartData() { return plib::checkDataParts(); }
