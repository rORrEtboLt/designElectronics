// IC and discrete packages: PCB land patterns, breadboard bodies and pin order.
//
// Dimensions are millimetres from the JEDEC outlines (MS-001 DIP, MS-012/013
// SOIC, MO-150 SSOP, MO-153 TSSOP, MO-187 MSOP, MO-220/229 QFN/DFN, MS-026
// LQFP/TQFP, TO-236 / MO-178 / MO-203 SOT-23 / SC-70, TO-261 SOT-223, TO-252
// DPAK, TO-263 D2PAK, DO-214 SMA/SMB/SMC, EIA chip sizes) and the land
// patterns follow IPC-7351 nominal (density level B) fillets, rounded to the
// values manufacturers print as recommended layouts. Converted to mils at the end.
//
// Orientation: pin 1 at the lower left. Dual-row packages run pin 1..n/2 along
// the bottom row left to right and the rest back along the top (the top view of
// a DIP lying with its notch to the left); quad packages run counter-clockwise
// from the top of the left side.
#include "packages.h"

#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <map>

#include "partlib_internal.h"

namespace plib {
namespace {

constexpr float MIL = 1000.f / 25.4f;  // mils per mm

struct Pad {
    std::string num, alt;  // number as printed in datasheets; alt "EP" / "TAB"
    Vec2 pos, size;        // mm
    bool tht = false;
    float drill = 0;
};

std::string up(std::string s) {
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

// ---- package family table ----------------------------------------------------

enum Kind { DUAL, QUAD, SOT, TAB, TWO, DIP, INLINE, HDR, AXIAL, RADIAL, BGA };

struct Family {
    const char* name;
    Kind kind;
    int defN;       // default pin count when the name carries none (SOT-23 = 3)
    float pitch;    // default pitch (mm)
    float bodyY;    // default body across the rows (mm)
    float rowSpan;  // pad centre to pad centre across the rows (mm), 0 = from body
    float padL, padW;
    bool noLead;    // DFN/QFN: pads tuck under the body edge
};

// Dual-row gull-wing pads: lead span E (tip to tip), foot length L and lead
// width b from the JEDEC drawing give Z = E + 2·Jt and G = E − 2·L − 2·Jh with
// Jt = 0.35, Jh = 0.35, Js = 0.05 (IPC-7351 level B).
const Family kFamilies[] = {
    // name        kind    n  pitch  bodyY  span   padL  padW  noLead
    {"SOIC",       DUAL,   8, 1.27f, 3.9f,  5.4f,  1.55f, 0.6f, false},  // MS-012
    {"SO",         DUAL,   8, 1.27f, 3.9f,  5.4f,  1.55f, 0.6f, false},
    {"SOP",        DUAL,   8, 1.27f, 5.3f,  7.0f,  1.8f,  0.6f, false},  // EIAJ 5.3 mm
    {"SSOP",       DUAL,  20, 0.65f, 5.3f,  7.0f,  1.85f, 0.45f, false}, // MO-150
    {"QSOP",       DUAL,  16, 0.635f, 3.9f, 5.4f,  1.5f,  0.4f, false},  // MO-137
    {"TSSOP",      DUAL,  14, 0.65f, 4.4f,  5.8f,  1.5f,  0.45f, false}, // MO-153
    {"HTSSOP",     DUAL,  20, 0.65f, 4.4f,  5.8f,  1.5f,  0.45f, false},
    {"MSOP",       DUAL,   8, 0.65f, 3.0f,  4.4f,  1.45f, 0.45f, false}, // MO-187
    {"VSSOP",      DUAL,   8, 0.65f, 3.0f,  4.4f,  1.45f, 0.45f, false},
    {"HVSSOP",     DUAL,   8, 0.65f, 3.0f,  4.4f,  1.45f, 0.45f, false},
    {"TSOP-II",    DUAL,  44, 0.8f, 10.16f, 11.0f, 1.6f,  0.5f, false},  // memory, 400 mil
    {"TSOP-I",     DUAL,  48, 0.5f, 18.4f, 19.3f,  1.4f,  0.3f, false},  // leads on the short sides
    {"SOJ",        DUAL,  28, 1.27f, 7.6f,  8.6f,  2.2f,  0.65f, false},
    {"DFN",        DUAL,   8, 0.5f,  3.0f,  0,     0.85f, 0,    true},   // MO-229
    {"SON",        DUAL,   8, 0.65f, 3.0f,  0,     0.85f, 0,    true},
    {"WSON",       DUAL,   8, 0.8f,  6.0f,  0,     0.85f, 0,    true},
    {"VSON",       DUAL,   8, 0.65f, 3.0f,  0,     0.85f, 0,    true},
    {"USON",       DUAL,   8, 0.5f,  2.0f,  0,     0.6f,  0,    true},
    {"UDFN",       DUAL,   8, 0.5f,  2.0f,  0,     0.6f,  0,    true},
    {"WDFN",       DUAL,   8, 0.5f,  2.0f,  0,     0.7f,  0,    true},
    {"QFN",        QUAD,  32, 0.5f,  5.0f,  0,     0.85f, 0,    true},   // MO-220
    {"VQFN",       QUAD,  32, 0.5f,  5.0f,  0,     0.85f, 0,    true},
    {"WQFN",       QUAD,  32, 0.5f,  5.0f,  0,     0.85f, 0,    true},
    {"UQFN",       QUAD,  20, 0.4f,  3.0f,  0,     0.7f,  0,    true},
    {"TQFN",       QUAD,  32, 0.5f,  5.0f,  0,     0.85f, 0,    true},
    {"LFCSP",      QUAD,  32, 0.5f,  5.0f,  0,     0.85f, 0,    true},
    {"LQFP",       QUAD,  64, 0.5f, 10.0f,  0,     1.5f,  0,    false},  // MS-026
    {"TQFP",       QUAD,  44, 0.8f, 10.0f,  0,     1.5f,  0,    false},
    {"QFP",        QUAD,  44, 0.8f, 10.0f,  0,     1.5f,  0,    false},
    {"PQFP",       QUAD, 100, 0.65f, 14.0f, 0,     2.0f,  0,    false},  // 3.2 mm lead span
    {"MQFP",       QUAD,  44, 0.8f, 10.0f,  0,     2.0f,  0,    false},
    {"SOT-23",     SOT,    3, 0.95f, 1.3f,  2.3f,  1.3f,  0.6f, false},  // TO-236 / MO-178
    {"TSOT-23",    SOT,    5, 0.95f, 1.6f,  2.3f,  1.3f,  0.6f, false},
    {"SC-70",      SOT,    5, 0.65f, 1.25f, 1.9f,  1.0f,  0.45f, false}, // MO-203
    {"SOT-323",    SOT,    3, 0.65f, 1.25f, 1.9f,  1.0f,  0.45f, false},
    {"SOT-353",    SOT,    5, 0.65f, 1.25f, 1.9f,  1.0f,  0.45f, false},
    {"SOT-363",    SOT,    6, 0.65f, 1.25f, 1.9f,  1.0f,  0.45f, false},
    {"SOT-563",    SOT,    6, 0.5f,  1.2f,  1.5f,  0.6f,  0.3f, false},
    {"SOT-666",    SOT,    6, 0.5f,  1.2f,  1.5f,  0.6f,  0.3f, false},
    {"SOT-143",    SOT,    4, 1.9f,  1.3f,  2.3f,  1.3f,  0.6f, false},
    {"SOT-89",     TAB,    3, 1.5f,  2.5f,  0,     1.2f,  0.9f, false},
    {"SOT-223",    TAB,    3, 2.3f,  3.5f,  0,     2.0f,  1.0f, false},  // TO-261
    {"TO-252",     TAB,    3, 2.28f, 6.1f,  0,     3.0f,  1.6f, false},  // DPAK
    {"TO-263",     TAB,    3, 2.54f, 9.15f, 0,     3.6f,  1.1f, false},  // D2PAK
    {"CHIP",       TWO,    2, 0,     0,     0,     0,     0,    false},
    {"SOD-123",    TWO,    2, 0,     1.6f,  3.3f,  0.9f,  1.2f, false},
    {"SOD-123F",   TWO,    2, 0,     1.6f,  3.1f,  1.0f,  1.2f, false},
    {"SOD-323",    TWO,    2, 0,     1.25f, 2.2f,  0.6f,  0.5f, false},
    {"SOD-523",    TWO,    2, 0,     0.8f,  1.4f,  0.5f,  0.4f, false},
    {"SOD-128",    TWO,    2, 0,     2.5f,  4.4f,  1.4f,  2.5f, false},
    {"SMA",        TWO,    2, 0,     2.6f,  4.0f,  2.0f,  1.7f, false},  // DO-214AC
    {"SMB",        TWO,    2, 0,     3.6f,  4.3f,  2.5f,  2.3f, false},  // DO-214AA
    {"SMC",        TWO,    2, 0,     5.9f,  6.8f,  3.3f,  3.3f, false},  // DO-214AB
    {"SMAF",       TWO,    2, 0,     2.6f,  3.6f,  1.6f,  1.5f, false},
    {"MINIMELF",   TWO,    2, 0,     1.5f,  3.3f,  1.3f,  1.7f, false},  // SOD-80
    {"MELF",       TWO,    2, 0,     2.5f,  4.8f,  1.6f,  2.6f, false},  // DO-213AB
    {"POWERDI-123", TWO,   2, 0,     1.8f,  3.0f,  1.4f,  1.3f, false},
    {"DIP",        DIP,    8, 2.54f, 6.35f, 7.62f, 0,     0,    false},  // MS-001
    {"SIP",        INLINE, 4, 2.54f, 0,     0,     0,     0,    false},
    {"TO-92",      INLINE, 3, 2.54f, 0,     0,     0,     0,    false},
    {"TO-220",     INLINE, 3, 2.54f, 0,     0,     0,     0,    false},
    {"TO-247",     INLINE, 3, 5.45f, 0,     0,     0,     0,    false},
    {"TO-126",     INLINE, 3, 2.28f, 0,     0,     0,     0,    false},
    {"HDR",        HDR,    4, 2.54f, 0,     0,     0,     0,    false},
    {"DO-35",      AXIAL,  2, 7.62f, 0,     0,     0,     0,    false},
    {"DO-41",      AXIAL,  2, 10.16f, 0,    0,     0,     0,    false},
    {"DO-15",      AXIAL,  2, 12.7f, 0,     0,     0,     0,    false},
    {"DO-201",     AXIAL,  2, 15.24f, 0,    0,     0,     0,    false},
    {"AXIAL",      AXIAL,  2, 10.16f, 0,    0,     0,     0,    false},
    {"RADIAL",     RADIAL, 2, 2.54f, 5.0f,  0,     0,     0,    false},
    {"LED-3MM",    RADIAL, 2, 2.54f, 3.0f,  0,     0,     0,    false},
    {"LED-5MM",    RADIAL, 2, 2.54f, 5.0f,  0,     0,     0,    false},
    {"HC-49",      RADIAL, 2, 4.88f, 4.65f, 0,     0,     0,    false},
    {"BGA",        BGA,    0, 0.8f,  0,     0,     0,     0,    false},
    {"LFBGA",      BGA,    0, 0.8f,  0,     0,     0,     0,    false},
    {"TFBGA",      BGA,    0, 0.8f,  0,     0,     0,     0,    false},
    {"UFBGA",      BGA,    0, 0.5f,  0,     0,     0,     0,    false},
    {"WLCSP",      BGA,    0, 0.4f,  0,     0,     0,     0,    false},
};

// EIA chip sizes (inch code): body length × width and the land pattern
// (pad centre to centre, pad length along the part, pad width).
struct Chip { const char* code; float len, wid, span, padL, padW; };
const Chip kChips[] = {
    {"0201", 0.6f, 0.3f, 0.6f, 0.3f, 0.35f},  {"0402", 1.0f, 0.5f, 0.95f, 0.55f, 0.6f},
    {"0603", 1.6f, 0.8f, 1.55f, 0.8f, 0.95f},  {"0805", 2.0f, 1.25f, 1.85f, 1.0f, 1.45f},
    {"1206", 3.2f, 1.6f, 2.95f, 1.15f, 1.8f},  {"1210", 3.2f, 2.5f, 2.95f, 1.15f, 2.7f},
    {"1812", 4.5f, 3.2f, 4.2f, 1.3f, 3.4f},    {"2010", 5.0f, 2.5f, 4.6f, 1.2f, 2.7f},
    {"2512", 6.3f, 3.2f, 5.9f, 1.3f, 3.4f},    {"2220", 5.7f, 5.0f, 5.3f, 1.4f, 5.3f},
};

const Family* findFamily(const std::string& name) {
    for (auto& f : kFamilies) if (name == f.name) return &f;
    return nullptr;
}
const Chip* findChip(const std::string& code) {
    for (auto& c : kChips) if (code == c.code) return &c;
    return nullptr;
}

// Body length along the rows for dual-row packages.
float dualBodyX(const Package& p) {
    if (p.bodyX > 0) return p.bodyX;
    int half = p.n / 2;
    if (p.family == "SOIC" || p.family == "SO") return p.wide ? half * 1.27f + 0.15f : half * 1.27f - 0.2f;
    if (p.family == "TSOP-I") return 12.0f;
    return half * p.pitch + (p.pitch > 1 ? 0.3f : 0.6f);
}

// LQFP/TQFP body from the pin count and pitch (MS-026 variants).
float qfpBody(int n, float pitch) {
    int side = n / 4;
    float b = side * pitch + 2.0f;
    static const float sizes[] = {5, 7, 10, 12, 14, 20, 24, 28, 32, 40};
    for (float s : sizes) if (s >= b - 1.3f) return s;
    return b;
}

// QFN/DFN body when the spec gives none: the smallest common size the pins fit.
float qfnBody(int side, float pitch) {
    float need = side * pitch + 0.6f;
    static const float sizes[] = {1.5f, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12};
    for (float s : sizes) if (s >= need) return s;
    return need;
}

}  // namespace

const std::vector<std::string>& packageFamilies() {
    static std::vector<std::string> v = [] {
        std::vector<std::string> out;
        for (auto& f : kFamilies) if (std::string(f.name) != "CHIP") out.push_back(f.name);
        for (auto& c : kChips) out.push_back(c.code);
        return out;
    }();
    return v;
}

// ---- parsing ------------------------------------------------------------------

bool parsePackage(const std::string& spec, Package& p, std::string* err) {
    auto fail = [&](const std::string& m) { if (err) *err = "package \"" + spec + "\": " + m; return false; };
    p = Package();
    p.spec = spec;
    std::vector<std::string> toks;
    {
        std::string cur;
        for (char c : spec) {
            if (c == ' ' || c == '\t') { if (!cur.empty()) toks.push_back(up(cur)); cur.clear(); }
            else cur += c;
        }
        if (!cur.empty()) toks.push_back(up(cur));
    }
    if (toks.empty()) return fail("empty");
    std::string head = toks[0];
    const Family* fam = nullptr;
    if (const Chip* c = findChip(head)) {
        fam = findFamily("CHIP");
        p.family = c->code;
        p.n = 2;
    } else if ((fam = findFamily(head))) {
        p.family = head;
        p.n = fam->defN;
    } else {
        // FAMILY-N[W] or HDR-RxN
        size_t dash = head.rfind('-');
        if (dash == std::string::npos) return fail("unknown family");
        std::string fname = head.substr(0, dash), cnt = head.substr(dash + 1);
        fam = findFamily(fname);
        if (!fam) return fail("unknown family \"" + fname + "\"");
        p.family = fname;
        if (fam->kind == HDR) {
            size_t x = cnt.find('X');
            if (x == std::string::npos) return fail("headers are HDR-1xN or HDR-2xN");
            p.rows = std::atoi(cnt.substr(0, x).c_str());
            p.n = p.rows * std::atoi(cnt.substr(x + 1).c_str());
            if (p.rows < 1 || p.rows > 2 || p.n < 1) return fail("headers are HDR-1xN or HDR-2xN");
        } else {
            if (!cnt.empty() && cnt.back() == 'W') { p.wide = true; cnt.pop_back(); }
            if (cnt.empty() || cnt.find_first_not_of("0123456789") != std::string::npos) return fail("bad pin count");
            p.n = std::atoi(cnt.c_str());
        }
    }
    Kind k = fam->kind;
    p.tht = k == DIP || k == INLINE || k == HDR || k == AXIAL || k == RADIAL;
    p.pitch = fam->pitch;
    p.bodyY = fam->bodyY;
    bool ep = false;
    for (size_t i = 1; i < toks.size(); i++) {
        const std::string& t = toks[i];
        if (t == "W") p.wide = true;
        else if (t[0] == 'P' && t.size() > 1 && (std::isdigit((unsigned char)t[1]) || t[1] == '.')) p.pitch = (float)std::atof(t.c_str() + 1);
        else if (t[0] == 'D' && t.size() > 1 && std::isdigit((unsigned char)t[1])) p.bodyY = (float)std::atof(t.c_str() + 1);
        else if (startsWith(t, "EP")) {
            ep = true;
            std::string s = t.substr(2);
            size_t x = s.find('X');
            if (!s.empty()) { p.epX = (float)std::atof(s.c_str()); p.epY = x == std::string::npos ? p.epX : (float)std::atof(s.c_str() + x + 1); }
        } else if (std::isdigit((unsigned char)t[0])) {
            size_t x = t.find('X');
            if (x == std::string::npos) return fail("body size is WxL in mm, e.g. 5x5");
            p.bodyX = (float)std::atof(t.c_str());
            p.bodyY = (float)std::atof(t.c_str() + x + 1);
        } else if (t == "STAGGERED" || t == "TAB") {
            // accepted, handled by the layout
        } else {
            return fail("unknown option \"" + t + "\"");
        }
    }
    if (k == DUAL || k == DIP) {
        if (p.n < 2 || p.n % 2) return fail("dual-row packages need an even pin count");
        if (p.wide && k == DUAL) {
            if (p.family == "SOIC" || p.family == "SO") p.bodyY = 7.5f;
            else return fail("W is only for SOIC and DIP");
        }
    }
    if (k == QUAD) {
        if (p.n < 8 || p.n % 4) return fail("quad packages need a multiple of 4 pins");
        int side = p.n / 4;
        if (!fam->noLead) {
            if (p.bodyX <= 0) {
                if (fam->pitch == p.pitch && (p.family == "LQFP" || p.family == "QFP" || p.family == "TQFP" || p.family == "PQFP" || p.family == "MQFP")) {
                    // conventional pitch for the count when the spec gives none
                    bool hasP = false;
                    for (size_t i = 1; i < toks.size(); i++) if (toks[i][0] == 'P') hasP = true;
                    if (!hasP) p.pitch = (p.n == 32 || p.n == 44) ? 0.8f : (p.family == "PQFP" ? 0.65f : 0.5f);
                }
                p.bodyX = p.bodyY = qfpBody(p.n, p.pitch);
            }
        } else if (p.bodyX <= 0) {
            p.bodyX = p.bodyY = qfnBody(side, p.pitch);
        }
        if (p.bodyY <= 0) p.bodyY = p.bodyX;
    }
    if (k == DUAL && fam->noLead) {
        if (p.bodyX <= 0) { p.bodyX = std::max(p.bodyY, qfnBody(p.n / 2, p.pitch)); }
    }
    if (k == SOT) {
        static const std::map<std::string, std::vector<int>> ok = {
            {"SOT-23", {3, 5, 6, 8}}, {"TSOT-23", {5, 6, 8}}, {"SC-70", {3, 5, 6}}, {"SOT-323", {3}}, {"SOT-353", {5}},
            {"SOT-363", {6}}, {"SOT-563", {6}}, {"SOT-666", {6}}, {"SOT-143", {4}}};
        auto& v = ok.at(p.family);
        if (std::find(v.begin(), v.end(), p.n) == v.end()) return fail("unsupported pin count");
        if (p.n == 8) p.pitch = 0.65f;
    }
    if (k == SOT && p.bodyX <= 0) p.bodyX = p.family == "SOT-563" || p.family == "SOT-666" ? 1.6f : p.pitch < 0.9f ? 2.0f : 2.9f;
    if (k == TAB && p.bodyX <= 0) p.bodyX = p.family == "SOT-89" ? 4.5f : p.family == "SOT-223" ? 6.5f : p.family == "TO-252" ? 6.6f : 10.2f;
    if (k == TAB) {
        if (p.family == "SOT-89" && p.n != 3) return fail("SOT-89 has 3 leads");
        if (p.family == "SOT-223" && p.n != 3 && p.n != 5) return fail("SOT-223 has 3 or 5 leads");
        if (p.family == "SOT-223" && p.n == 5) p.pitch = 1.5f;
        if (p.family == "TO-252" && (p.n < 2 || p.n > 5)) return fail("TO-252 has 2 to 5 leads");
        if (p.family == "TO-252" && p.n > 3) p.pitch = p.n == 4 ? 1.5f : 1.27f;
        if (p.family == "TO-263" && (p.n < 2 || p.n > 9)) return fail("TO-263 has 2 to 9 leads");
        if (p.family == "TO-263" && p.n > 3) p.pitch = p.n <= 5 ? 1.7f : p.n <= 7 ? 1.27f : 1.0f;
        p.extra = 1;
    }
    if (k == INLINE) {
        if (p.family == "TO-220" && p.n > 3 && p.pitch == fam->pitch) p.pitch = p.n <= 5 ? 1.7f : 1.27f;
        if (p.family == "TO-92" && p.n != 2 && p.n != 3) return fail("TO-92 has 2 or 3 leads");
    }
    if (k == DIP) {
        if (p.wide || (p.n >= 24 && p.n != 28)) p.bodyY = 13.97f;
        if (p.n == 28 && p.wide) p.bodyY = 13.97f;
    }
    if (k == BGA) {
        if (p.bodyX <= 0) return fail("BGA needs a body size, e.g. BGA-100 10x10 P0.8");
        if (p.n <= 0) return fail("BGA needs a ball count");
    }
    if (ep) {
        if (k != DUAL && k != QUAD) return fail("EP is only for dual and quad packages");
        p.extra = 1;
        float bx = k == QUAD ? p.bodyX : dualBodyX(p), by = p.bodyY;
        if (p.epX <= 0) {
            if (fam->noLead) { p.epX = std::max(0.4f, (k == QUAD ? bx - 1.6f : bx - 0.6f)); p.epY = std::max(0.4f, by - 1.6f); }
            else { p.epX = bx * 0.6f; p.epY = by * 0.6f; }
        }
    }
    return true;
}

// ---- pad geometry -------------------------------------------------------------

namespace {

void dualRow(const Package& p, const Family& f, std::vector<Pad>& out) {
    int half = p.n / 2;
    float span = f.rowSpan, L = f.padL, W = f.padW;
    if (p.family == "SOIC" || p.family == "SO") { if (p.wide) { span = 9.3f; L = 2.0f; } }
    if ((p.family == "MSOP" || p.family == "VSSOP" || p.family == "HVSSOP") && p.pitch < 0.6f) W = 0.3f;
    if (p.family == "TSSOP" && p.pitch < 0.6f) W = 0.3f;
    // a body narrower or wider than the family's usual one moves the pad rows with it
    if (!f.noLead && span > 0 && !p.wide && std::fabs(p.bodyY - f.bodyY) > 0.05f) span += p.bodyY - f.bodyY;
    if (f.noLead) {
        // pads run from 0.375 mm outside the body edge inwards, clear of the EP
        W = std::min(0.5f * p.pitch + 0.05f, 0.6f);
        float outer = p.bodyY / 2 + 0.375f, inner = outer - L;
        if (p.extra && p.epY > 0) inner = std::max(inner, p.epY / 2 + 0.2f);
        span = outer + inner;
        L = outer - inner;
    } else if (span <= 0) {
        span = p.bodyY + 1.5f;
    }
    if (W <= 0) W = std::min(0.6f * p.pitch, 0.6f);
    for (int i = 0; i < p.n; i++) {
        bool bottom = i < half;
        int k = bottom ? i : p.n - 1 - i;
        float x = ((float)k - (half - 1) / 2.f) * p.pitch;
        float y = bottom ? span / 2 : -span / 2;
        out.push_back({std::to_string(i + 1), "", {x, y}, {W, L}});
    }
}

void quad(const Package& p, const Family& f, std::vector<Pad>& out) {
    int side = p.n / 4;
    float W, L = f.padL, c;
    if (f.noLead) {
        W = std::min(0.5f * p.pitch + 0.05f, 0.6f);
        float outer = p.bodyX / 2 + 0.375f, inner = outer - L;
        if (p.extra && p.epX > 0) inner = std::max(inner, p.epX / 2 + 0.2f);
        L = outer - inner;
        c = (outer + inner) / 2;
    } else {
        W = p.pitch >= 0.8f ? 0.55f : p.pitch >= 0.65f ? 0.4f : p.pitch >= 0.5f ? 0.3f : 0.25f;
        c = p.bodyX / 2 + (f.padL > 1.8f ? 1.4f : 0.75f);
    }
    float cy = f.noLead ? (p.bodyY / 2 + 0.375f) - L / 2 : p.bodyY / 2 + (f.padL > 1.8f ? 1.4f : 0.75f);
    for (int i = 0; i < p.n; i++) {
        int s = i / side, k = i % side;
        float o = ((float)k - (side - 1) / 2.f) * p.pitch;
        Pad pd{std::to_string(i + 1), "", {}, {}};
        switch (s) {
            case 0: pd.pos = {-c, o}; pd.size = {L, W}; break;    // left, top → bottom
            case 1: pd.pos = {o, cy}; pd.size = {W, L}; break;    // bottom, left → right
            case 2: pd.pos = {c, -o}; pd.size = {L, W}; break;    // right, bottom → top
            default: pd.pos = {-o, -cy}; pd.size = {W, L}; break; // top, right → left
        }
        out.push_back(pd);
    }
}

void sot(const Package& p, const Family& f, std::vector<Pad>& out) {
    float y = f.rowSpan / 2, e = p.pitch;
    Vec2 sz{f.padW, f.padL};
    if (p.n == 8) sz.x = 0.4f;
    auto add = [&](int num, float x, float yy, Vec2 s) { out.push_back({std::to_string(num), "", {x, yy}, s}); };
    if (p.family == "SOT-143") {  // pin 1 is the wide collector/source lead
        add(1, -0.95f, y, {1.0f, f.padL}); add(2, 0.95f, y, sz); add(3, 0.95f, -y, sz); add(4, -0.95f, -y, sz);
        return;
    }
    if (p.n == 3) {
        float d = p.family == "SOT-23" ? 0.95f : 0.65f;
        add(1, -d, y, sz); add(2, d, y, sz); add(3, 0, -y, sz);
    } else if (p.n == 5) {
        add(1, -e, y, sz); add(2, 0, y, sz); add(3, e, y, sz); add(4, e, -y, sz); add(5, -e, -y, sz);
    } else if (p.n == 6) {
        add(1, -e, y, sz); add(2, 0, y, sz); add(3, e, y, sz); add(4, e, -y, sz); add(5, 0, -y, sz); add(6, -e, -y, sz);
    } else {
        for (int i = 0; i < 4; i++) add(i + 1, (i - 1.5f) * e, y, sz);
        for (int i = 0; i < 4; i++) add(i + 5, (1.5f - i) * e, -y, sz);
    }
}

// Leads along the bottom, a big tab across the top. The tab pad takes the middle
// lead's number (it is the same metal); for even lead counts the middle lead is
// cut, the leads are numbered around it and the tab gets the missing number.
void tabbed(const Package& p, const Family& f, std::vector<Pad>& out) {
    struct Geo { float leadY, tabY, tabW, tabL; };
    Geo g{};
    if (p.family == "SOT-89") g = {1.75f, -0.45f, 1.9f, 2.6f};
    else if (p.family == "SOT-223") g = {3.15f, -3.15f, 3.4f, 2.0f};
    else if (p.family == "TO-252") g = {3.8f, -2.5f, 5.8f, 6.2f};
    else g = {5.6f, -2.4f, 10.8f, 9.0f};  // TO-263
    int n = p.n;
    bool cut = (n % 2 == 0) && (p.family == "TO-252" || p.family == "TO-263") && n <= 3;
    std::vector<std::pair<float, int>> leads;  // x, number
    if (cut) {  // 2 leads at the outer positions of a 3-lead frame
        leads = {{-p.pitch, 1}, {p.pitch, 3}};
    } else {
        for (int i = 0; i < n; i++) leads.push_back({((float)i - (n - 1) / 2.f) * p.pitch, i + 1});
    }
    Vec2 lead{f.padW, f.padL};
    if (p.family == "SOT-223" && n == 5) lead.x = 0.7f;
    if (p.family == "TO-263" && n > 3) lead.x = std::min(1.1f, p.pitch * 0.6f);
    if (p.family == "TO-252" && n > 3) lead.x = std::min(1.6f, p.pitch * 0.6f);
    // pads stay in number order so pin index i is pin number i + 1; a tab that
    // shares a lead's number goes last
    Pad tab{cut ? "2" : std::to_string((n + 1) / 2), "TAB", {0, g.tabY}, {g.tabW, g.tabL}};
    if (p.family == "SOT-223" && n == 5) tab.num = "6";
    for (auto& l : leads) {
        if (cut && l.second == 3) out.push_back(tab);
        out.push_back({std::to_string(l.second), "", {l.first, g.leadY}, lead});
    }
    if (!cut) out.push_back(tab);
}

void two(const Package& p, const Family& f, std::vector<Pad>& out) {
    float span, L, W;
    if (const Chip* c = findChip(p.family)) { span = c->span; L = c->padL; W = c->padW; }
    else { span = f.rowSpan; L = f.padL; W = f.padW; }
    out.push_back({"1", "", {-span / 2, 0}, {L, W}});
    out.push_back({"2", "", {span / 2, 0}, {L, W}});
}

float drillFor(const Package& p) {
    if (p.family == "TO-247") return 1.4f;
    if (p.family == "TO-220" && p.n <= 3) return 1.1f;
    if (p.family == "DO-201" || p.family == "DO-15") return 1.3f;
    if (p.family == "DO-41") return 1.0f;
    if (p.family == "TO-92" || p.family == "LED-3MM" || p.family == "LED-5MM" || p.family == "HC-49") return 0.8f;
    if (p.pitch < 2.0f) return 0.7f;
    return 1.0f;
}

void tht(const Package& p, const Family& f, std::vector<Pad>& out) {
    float drill = drillFor(p);
    float pad = std::max(drill + 0.6f, std::min(p.pitch - 0.5f, drill * 1.8f));
    if (p.family == "DIP") pad = 1.6f;
    auto add = [&](int num, Vec2 pos, Vec2 size) { Pad pd{std::to_string(num), "", pos, size, true, drill}; out.push_back(pd); };
    switch (f.kind) {
        case DIP: {
            int half = p.n / 2;
            float row = p.bodyY > 10 ? 15.24f : 7.62f;
            for (int i = 0; i < p.n; i++) {
                bool bottom = i < half;
                int k = bottom ? i : p.n - 1 - i;
                add(i + 1, {k * 2.54f, bottom ? row : 0}, {pad, pad});
            }
            break;
        }
        case INLINE: {
            bool stagger = (p.family == "TO-220" && p.n > 3) || p.spec.find("STAGGERED") != std::string::npos;
            Vec2 sz = p.family == "TO-220" || p.family == "TO-247" || p.family == "TO-126" ? Vec2(std::min(pad, p.pitch * (stagger ? 1.6f : 0.9f)), 2.4f) : Vec2(pad, pad);
            if (p.family == "TO-92") sz = {1.3f, 1.7f};
            for (int i = 0; i < p.n; i++) add(i + 1, {i * p.pitch, stagger && (i % 2) ? 3.7f : 0}, sz);
            break;
        }
        case HDR: {
            int cols = p.n / p.rows;
            float pd2 = p.pitch >= 2.5f ? 1.7f : p.pitch >= 2.0f ? 1.35f : 1.0f;
            float dr = p.pitch >= 2.5f ? 1.0f : p.pitch >= 2.0f ? 0.8f : 0.65f;
            for (int i = 0; i < p.n; i++) {
                int col = p.rows == 2 ? i / 2 : i, row = p.rows == 2 ? i % 2 : 0;
                Pad pd{std::to_string(i + 1), "", {col * p.pitch, row * p.pitch}, {pd2, pd2}, true, dr};
                out.push_back(pd);
            }
            (void)cols;
            break;
        }
        case AXIAL:
            add(1, {0, 0}, {pad + 0.4f, pad + 0.4f});
            add(2, {p.pitch, 0}, {pad + 0.4f, pad + 0.4f});
            break;
        case RADIAL: {
            float pd2 = std::min(p.pitch - 0.4f, 1.8f);
            add(1, {0, 0}, {pd2, pd2});
            add(2, {p.pitch, 0}, {pd2, pd2});
            break;
        }
        default: break;
    }
}

const char* kBgaRows = "ABCDEFGHJKLMNPRTUVWY";  // JEDEC skips I, O, Q, S, X, Z

// Ball "C7" → row 2, column 6; two-letter rows (AA, AB …) continue after Y.
bool ballPos(const std::string& ball, int& row, int& col) {
    size_t i = 0;
    while (i < ball.size() && std::isalpha((unsigned char)ball[i])) i++;
    if (i == 0 || i > 2 || i == ball.size()) return false;
    auto idx = [](char c) { const char* p = std::strchr(kBgaRows, (char)std::toupper((unsigned char)c)); return p ? (int)(p - kBgaRows) : -1; };
    if (i == 1) row = idx(ball[0]);
    else { int a = idx(ball[0]), b = idx(ball[1]); row = a < 0 || b < 0 ? -1 : 20 + a * 20 + b; }
    col = std::atoi(ball.c_str() + i) - 1;
    return row >= 0 && col >= 0;
}

}  // namespace

// ---- breadboard bodies --------------------------------------------------------

namespace {

const uint32_t kBreakout = rgba(28, 95, 70), kBreakoutEdge = rgba(15, 60, 45), kChip = rgba(38, 38, 42), kLead = rgba(185, 185, 190);

// SMD parts on the breadboard sit on a breakout board (the adapter you'd solder
// them to): header pins on the 0.1" grid in two rows — pins 1..n/2 along the
// bottom, the rest back along the top, like a DIP — and the chip drawn to scale.
void breakout(PartDef& d, const Package& p, const std::string& label, const std::vector<int>& vis) {
    int n = (int)vis.size();
    auto P = [&](int i) -> PinDef& { return d.pins[(size_t)vis[(size_t)i]]; };
    G bb{d.gfx[BB]};
    float chipW = (p.bodyX > 0 ? p.bodyX : 2) * MIL, chipH = (p.bodyY > 0 ? p.bodyY : 1.2f) * MIL;
    if (findChip(p.family)) { auto* c = findChip(p.family); chipW = c->len * MIL; chipH = c->wid * MIL; }
    if (n <= 3) {  // single row: SOT-23, SMD diodes and passives
        for (int i = 0; i < n; i++) P(i).pos[BB] = {i * 100.f, 0};
        float w = std::max(n * 100.f + 40, chipW + 120);
        float x0 = (n - 1) * 50.f - w / 2;
        bb.rect(x0, -330, w, 400, kBreakout, kBreakoutEdge, 6, 20);
        bb.rect(-50, -50, n * 100.f, 100, rgba(25, 25, 25), 0, 0, 6);
        for (int i = 0; i < n; i++) bb.rect(P(i).pos[BB].x - 16, -16, 32, 32, rgba(220, 180, 60));
        float cx = (n - 1) * 50.f, cy = -190;
        bb.rect(cx - chipW / 2, cy - chipH / 2, chipW, chipH, kChip, 0, 0, 4);
        bb.text(cx, -300, label, std::min(34.f, w / std::max<size_t>(label.size(), 1) * 1.5f), rgba(255, 255, 255, 230));
        return;
    }
    int half = (n + 1) / 2;
    float rowGap = std::max(300.f, std::ceil((chipH + 260) / 100.f) * 100.f);
    for (int i = 0; i < n; i++) {
        bool bottom = i < half;
        int k = bottom ? i : n - 1 - i;
        P(i).pos[BB] = {k * 100.f, bottom ? rowGap : 0};
    }
    float w = (half - 1) * 100.f;
    float bw = std::max(w + 120, chipW + 160);
    float x0 = w / 2 - bw / 2;
    bb.rect(x0, -70, bw, rowGap + 140, kBreakout, kBreakoutEdge, 6, 16);
    for (int i = 0; i < n; i++) {
        Vec2 q = P(i).pos[BB];
        bb.circle(q.x, q.y, 30, rgba(220, 180, 60));
        bb.circle(q.x, q.y, 14, rgba(60, 50, 30));
    }
    float cx = w / 2, cy = rowGap / 2;
    bool quadPk = p.family.find("QF") != std::string::npos || p.family == "LFCSP" || p.family.find("BGA") != std::string::npos || p.family == "WLCSP";
    // leads
    if (!quadPk && p.family.find("DFN") == std::string::npos && p.family.find("SON") == std::string::npos) {
        int per = p.n / 2;
        for (int i = 0; i < per; i++) {
            float x = cx + ((float)i - (per - 1) / 2.f) * p.pitch * MIL;
            float lw = std::max(6.f, p.pitch * MIL * 0.45f), ll = 0.5f * MIL;
            bb.rect(x - lw / 2, cy + chipH / 2, lw, ll, kLead);
            bb.rect(x - lw / 2, cy - chipH / 2 - ll, lw, ll, kLead);
        }
    } else if (quadPk && p.family.find("BGA") == std::string::npos && p.family != "WLCSP") {
        int side = p.n / 4;
        float ll = (p.family.find("QFN") != std::string::npos || p.family == "LFCSP") ? 0 : 0.5f * MIL;
        float lw = std::max(5.f, p.pitch * MIL * 0.45f);
        for (int i = 0; i < side && ll > 0; i++) {
            float o = ((float)i - (side - 1) / 2.f) * p.pitch * MIL;
            bb.rect(cx + o - lw / 2, cy + chipH / 2, lw, ll, kLead);
            bb.rect(cx + o - lw / 2, cy - chipH / 2 - ll, lw, ll, kLead);
            bb.rect(cx - chipW / 2 - ll, cy + o - lw / 2, ll, lw, kLead);
            bb.rect(cx + chipW / 2, cy + o - lw / 2, ll, lw, kLead);
        }
    }
    bb.rect(cx - chipW / 2, cy - chipH / 2, chipW, chipH, kChip, rgba(0, 0, 0, 90), 4, 4);
    float dot = std::max(8.f, std::min(chipW, chipH) * 0.08f);
    bb.circle(cx - chipW / 2 + dot * 2, cy + chipH / 2 - dot * 2, dot, rgba(110, 110, 115));
    float ts = std::min({chipH * 0.35f, chipW / std::max<size_t>(label.size(), 1) * 1.5f, 60.f});
    if (ts >= 14) bb.text(cx, cy, label, ts, rgba(215, 215, 215));
    else bb.text(cx, -110, label, std::min(40.f, bw / std::max<size_t>(label.size(), 1) * 1.5f), rgba(255, 255, 255, 220));
}

void dipBody(PartDef& d, const Package& p, const std::string& label) {
    int n = p.n, half = n / 2;
    float row = p.bodyY > 10 ? 600.f : 300.f;
    for (int i = 0; i < n; i++) {
        bool bottom = i < half;
        int k = bottom ? i : n - 1 - i;
        d.pins[(size_t)i].pos[BB] = {k * 100.f, bottom ? row : 0};
    }
    float w = (half - 1) * 100.f;
    G bb{d.gfx[BB]};
    for (int i = 0; i < n; i++) {
        Vec2 q = d.pins[(size_t)i].pos[BB];
        bb.rect(q.x - 20, i < half ? q.y - 60 : q.y, 40, 60, LEG, 0, 0, 4);
    }
    bb.rect(-70, 40, w + 140, row - 80, rgba(45, 45, 48), rgba(25, 25, 25), 6, 10);
    bb.arc(-70, row / 2, 40, 270, 450, 8, rgba(110, 110, 110));
    bb.circle(-20, row - 85, 14, rgba(110, 110, 110));
    bb.text(w / 2, row / 2, label, std::min(70.f, (w + 100) / std::max<size_t>(label.size(), 1) * 1.4f), rgba(220, 220, 220));
}

void inlineBody(PartDef& d, const Package& p, const std::string& label) {
    int n = (int)d.pins.size();
    for (int i = 0; i < n; i++) d.pins[(size_t)i].pos[BB] = {i * 100.f, 0};
    G bb{d.gfx[BB]};
    float w = (n - 1) * 100.f;
    if (p.family == "TO-92" && n == 3) { drawTo92(bb, label); return; }
    if (p.family == "TO-92") {
        for (int i = 0; i < n; i++) bb.line(i * 100.f, -110, i * 100.f, 0, 14, LEG);
        bb.rect(-45, -265, 190, 160, rgba(35, 35, 35), 0, 0, 8);
        bb.circle(50, -265, 95, rgba(35, 35, 35));
        bb.text(50, -195, label, std::min(30.f, 180.f / std::max<size_t>(label.size(), 1) * 1.6f), rgba(200, 200, 200));
        return;
    }
    if (p.family == "TO-220" || p.family == "TO-247" || p.family == "TO-126") {
        bool tab = p.family != "TO-126";
        float bw = std::max(w + 200, p.family == "TO-247" ? 620.f : 400.f), x0 = w / 2 - bw / 2;
        for (int i = 0; i < n; i++) bb.line(i * 100.f, -160, i * 100.f, 0, 18, LEG);
        if (tab) {
            bb.rect(x0, -800, bw, 330, rgba(190, 190, 195), rgba(130, 130, 135), 6, 10);
            bb.circle(w / 2, -660, 60, rgba(255, 255, 255), rgba(130, 130, 135), 6);
        }
        bb.rect(x0, -480, bw, 330, rgba(35, 35, 35), 0, 0, 6);
        bb.text(w / 2, -330, label, std::min(55.f, (bw - 40) / std::max<size_t>(label.size(), 1) * 1.4f), rgba(200, 200, 200));
        return;
    }
    // SIP: a flat black body over the leads
    for (int i = 0; i < n; i++) bb.line(i * 100.f, -110, i * 100.f, 0, 14, LEG);
    bb.rect(-60, -330, w + 120, 230, rgba(40, 40, 44), 0, 0, 8);
    bb.circle(-20, -140, 14, rgba(110, 110, 110));
    bb.text(w / 2, -215, label, std::min(50.f, (w + 100) / std::max<size_t>(label.size(), 1) * 1.4f), rgba(220, 220, 220));
}

void headerBody(PartDef& d, const Package& p) {
    int n = (int)d.pins.size();
    for (int i = 0; i < n; i++) {
        int col = p.rows == 2 ? i / 2 : i, row = p.rows == 2 ? i % 2 : 0;
        d.pins[(size_t)i].pos[BB] = {col * 100.f, -row * 100.f};
    }
    int cols = n / std::max(1, p.rows);
    G bb{d.gfx[BB]};
    bb.rect(-50, -50 - (p.rows - 1) * 100.f, cols * 100.f, p.rows * 100.f, rgba(30, 30, 30), 0, 0, 4);
    for (auto& pin : d.pins) bb.rect(pin.pos[BB].x - 14, pin.pos[BB].y - 14, 28, 28, rgba(220, 180, 60));
}

void twoBody(PartDef& d, const Package& p, const std::string& label, const std::string& style, const std::vector<int>& vis) {
    bool axial = !p.tht || findFamily(p.family) == nullptr || findFamily(p.family)->kind == AXIAL;
    if (findFamily(p.family) && findFamily(p.family)->kind == RADIAL) axial = false;
    G bb{d.gfx[BB]};
    if (!p.tht) { breakout(d, p, label, vis); return; }
    if (axial) {
        d.pins[0].pos[BB] = {0, 0};
        d.pins[1].pos[BB] = {400, 0};
        bb.line(0, 0, 110, 0, 18, LEG);
        bb.line(290, 0, 400, 0, 18, LEG);
        if (style == "resistor") {
            uint32_t body = rgba(222, 200, 150), edge = rgba(170, 145, 95);
            bb.circle(115, 0, 46, body, edge, 6); bb.circle(285, 0, 46, body, edge, 6);
            bb.rect(115, -38, 170, 76, body);
        } else if (style == "inductor") {
            bb.rect(100, -55, 200, 110, rgba(60, 130, 80), rgba(30, 90, 50), 6, 40);
        } else {
            bool glass = p.family == "DO-35";
            bb.rect(100, -40, 200, 80, glass ? rgba(200, 120, 90, 200) : rgba(35, 35, 35), 0, 0, 16);
            bb.rect(250, -40, 26, 80, glass ? rgba(30, 30, 30) : rgba(200, 200, 200));
        }
        return;
    }
    d.pins[0].pos[BB] = {0, 0};
    d.pins[1].pos[BB] = {100, 0};
    bb.line(0, -120, 0, 0, 14, LEG);
    bb.line(100, -120, 100, 0, 14, LEG);
    if (style == "led" || p.family == "LED-3MM" || p.family == "LED-5MM") {
        float r = p.family == "LED-3MM" ? 70 : 100;
        bb.rect(50 - r, -300, 2 * r, 150, rgba(235, 40, 40, 216)).tag = 1;
        bb.circle(50, -300, r, rgba(235, 40, 40, 216)).tag = 1;
        bb.rect(50 - r - 18, -175, 2 * r + 36, 26, rgba(160, 30, 30), 0, 0, 6);
    } else if (style == "crystal" || p.family == "HC-49") {
        bb.rect(-80, -260, 260, 150, rgba(200, 200, 205), rgba(140, 140, 150), 8, 70);
        bb.text(50, -185, label, std::min(40.f, 240.f / std::max<size_t>(label.size(), 1) * 1.5f), rgba(60, 60, 60));
    } else if (style == "ecap") {
        float r = std::max(2.5f, p.bodyY / 2) * MIL;
        bb.rect(50 - r, -120 - 2.2f * r, 2 * r, 2.2f * r, rgba(40, 70, 160), rgba(25, 45, 110), 6, 30);
        bb.rect(50 - r, -120 - 2.2f * r, r * 0.5f, 2.2f * r, rgba(180, 190, 210), 0, 0, 20);
    } else {
        bb.circle(50, -190, 85, rgba(235, 165, 50), rgba(190, 120, 30), 6);
        bb.text(50, -190, label.substr(0, 4), 36, rgba(90, 50, 10));
    }
}

// ---- PCB silkscreen ---------------------------------------------------------------

void silk(PartDef& d, const Package& p, Kind k, const std::vector<Pad>& pads) {
    G pc{d.gfx[PCB]};
    Rect pr;  // pad extent (mils)
    bool any = false;
    for (auto& pd : pads) {
        Rect r((pd.pos.x - pd.size.x / 2) * MIL, (pd.pos.y - pd.size.y / 2) * MIL, pd.size.x * MIL, pd.size.y * MIL);
        pr = any ? pr.united(r) : r;
        any = true;
    }
    const float gap = 12;  // keep ink off the copper
    switch (k) {
        case DUAL: case SOT: {
            float bx = (k == DUAL ? dualBodyX(p) : std::max(p.pitch * 2 + 0.6f, 2.9f)) * MIL;
            float by = std::max(p.bodyY * MIL, 40.f);
            float x0 = -bx / 2, x1 = bx / 2;
            // short edges of the body, clear of the pad rows
            float yIn = 1e9f;
            for (auto& pd : pads) yIn = std::min(yIn, std::fabs(pd.pos.y) * MIL - pd.size.y * MIL / 2);
            float yy = std::max(10.f, std::min(by / 2, yIn - gap));
            pc.line(x0, -yy, x0, yy, SKW, SILK);
            pc.line(x1, -yy, x1, yy, SKW, SILK);
            Vec2 p1 = pads.front().pos * MIL;
            pc.circle(p1.x - pads.front().size.x * MIL / 2 - 25, p1.y + pads.front().size.y * MIL / 2 - 10, 10, SILK);
            break;
        }
        case QUAD: case BGA: {
            float bx = p.bodyX * MIL / 2, by = p.bodyY * MIL / 2, m = std::min(bx, by) * 0.3f;
            float ox = bx + (k == QUAD ? 8 : 4), oy = by + (k == QUAD ? 8 : 4);
            if (k == QUAD) {
                float cx = 0, cy = 0;
                for (auto& pd : pads) { cx = std::max(cx, std::fabs(pd.pos.x) * MIL); cy = std::max(cy, std::fabs(pd.pos.y) * MIL); }
                // corner marks outside the pad rows
                float firstPad = std::fabs(pads.front().pos.y) * MIL + pads.front().size.y * MIL / 2 + gap;
                m = std::max(10.f, ox - firstPad);
            }
            for (int sx = -1; sx <= 1; sx += 2)
                for (int sy = -1; sy <= 1; sy += 2) {
                    pc.line(sx * ox, sy * oy, sx * (ox - m), sy * oy, SKW, SILK);
                    pc.line(sx * ox, sy * oy, sx * ox, sy * (oy - m), SKW, SILK);
                }
            pc.circle(-ox - 25, -oy - 25, 12, SILK);
            break;
        }
        case TAB: {
            float bx = std::max(pr.w, p.bodyY * MIL) / 2 + 10;
            pc.line(-bx, pr.y - 15, -bx, pr.b() + 15, SKW, SILK);
            pc.line(bx, pr.y - 15, bx, pr.b() + 15, SKW, SILK);
            break;
        }
        case TWO: {
            float h = pr.h / 2 + 15, x = pr.w / 2 - pads[0].size.x * MIL;
            pc.line(-x + gap, -h, x - gap, -h, SKW, SILK);
            pc.line(-x + gap, h, x - gap, h, SKW, SILK);
            pc.line(pr.x - 20, -h, pr.x - 20, h, SKW, SILK);  // pin-1 / cathode side
            break;
        }
        case DIP: {
            float w = (p.n / 2 - 1) * 100.f, row = p.bodyY > 10 ? 600.f : 300.f;
            pc.rect(-80, 60, w + 160, row - 120, 0, SILK, SKW);
            pc.arc(-80, row / 2, 40, 270, 450, SKW, SILK);
            break;
        }
        case INLINE: case HDR: {
            pc.rect(pr.x - 15, pr.y - 15, pr.w + 30, pr.h + 30, 0, SILK, SKW);
            break;
        }
        case AXIAL: {
            float x0 = pads[0].pos.x * MIL + 80, x1 = pads[1].pos.x * MIL - 80;
            pc.rect(x0, -50, std::max(40.f, x1 - x0), 100, 0, SILK, SKW);
            pc.line(x0 + 25, -50, x0 + 25, 50, SKW, SILK);
            break;
        }
        case RADIAL: {
            float r = std::max(p.bodyY / 2 * MIL, pr.w / 2 + 20);
            pc.circle((pads[0].pos.x + pads[1].pos.x) / 2 * MIL, 0, r, 0, SILK, SKW);
            break;
        }
    }
}

}  // namespace

// Builds the pad list for a package (positions in mm).
static std::vector<Pad> padsOf(const Package& p, const std::vector<std::string>& balls) {
    std::vector<Pad> out;
    const Family* f = findChip(p.family) ? findFamily("CHIP") : findFamily(p.family);
    if (!f) return out;
    switch (f->kind) {
        case DUAL: dualRow(p, *f, out); break;
        case QUAD: quad(p, *f, out); break;
        case SOT: sot(p, *f, out); break;
        case TAB: tabbed(p, *f, out); break;
        case TWO: two(p, *f, out); break;
        case BGA: {
            int maxR = 0, maxC = 0;
            std::vector<std::pair<int, int>> rc;
            for (auto& b : balls) {
                int r = 0, c = 0;
                if (!ballPos(b, r, c)) { rc.push_back({-1, -1}); continue; }
                rc.push_back({r, c});
                maxR = std::max(maxR, r); maxC = std::max(maxC, c);
            }
            float d = p.pitch >= 1 ? 0.5f : p.pitch >= 0.8f ? 0.4f : p.pitch >= 0.65f ? 0.3f : p.pitch * 0.5f;
            for (size_t i = 0; i < balls.size(); i++) {
                if (rc[i].first < 0) continue;
                Vec2 pos{(rc[i].second - maxC / 2.f) * p.pitch, (rc[i].first - maxR / 2.f) * p.pitch};
                out.push_back({balls[i], "", pos, {d, d}});
            }
            break;
        }
        default: tht(p, *f, out); break;
    }
    if (p.extra && (f->kind == DUAL || f->kind == QUAD))
        out.push_back({std::to_string(p.n + 1), "EP", {0, 0}, {f->kind == QUAD ? p.epX : p.epX, p.epY}});
    return out;
}

std::vector<PackagePad> packagePads(const Package& p, const std::vector<std::string>& balls) {
    std::vector<PackagePad> v;
    for (auto& pd : padsOf(p, balls)) v.push_back({pd.num, pd.alt});
    return v;
}

void layoutPackage(PartDef& d, const Package& p, const std::string& label, const std::string& style, const std::vector<std::string>& balls) {
    std::vector<Pad> pads = padsOf(p, balls);
    const Family* f = findChip(p.family) ? findFamily("CHIP") : findFamily(p.family);
    if (!f || pads.size() != d.pins.size()) return;
    // centre SMD footprints on the origin; THT keeps pin 1 at the origin so the
    // pins land on the 0.1" grid
    for (size_t i = 0; i < pads.size(); i++) {
        PinDef& pin = d.pins[i];
        pin.pos[PCB] = pads[i].pos * MIL;
        pin.pad = pads[i].size.x * MIL;
        pin.padH = std::fabs(pads[i].size.x - pads[i].size.y) < 1e-3f ? 0 : pads[i].size.y * MIL;
        pin.drill = pads[i].tht ? pads[i].drill * MIL : 0;
        pin.square = pads[i].tht && i == 0;
    }
    silk(d, p, f->kind, pads);
    // a tab shares its lead's number: on the breadboard it is that lead's pin
    std::vector<int> vis, prim(pads.size(), -1);
    for (size_t i = 0; i < pads.size(); i++) {
        if (pads[i].alt == "TAB")
            for (size_t j = 0; j < pads.size(); j++)
                if (j != i && pads[j].num == pads[i].num && pads[j].alt.empty()) { prim[i] = (int)j; break; }
        if (prim[i] < 0) vis.push_back((int)i);
    }
    switch (f->kind) {
        case DIP: dipBody(d, p, label); break;
        case INLINE: inlineBody(d, p, label); break;
        case HDR: headerBody(d, p); break;
        case AXIAL: case RADIAL: twoBody(d, p, label, style, vis); break;
        default: breakout(d, p, label, vis); break;
    }
    for (size_t i = 0; i < pads.size(); i++) if (prim[i] >= 0) d.pins[i].pos[BB] = d.pins[(size_t)prim[i]].pos[BB];
}

std::string packageSpecOf(const std::string& option) {
    size_t b = option.find(" [");
    return b == std::string::npos ? option : option.substr(0, b);
}

bool applyPackage(PartDef& d, const std::string& option, const std::vector<int>& padPins, const std::string& label, const std::string& style) {
    Package pk;
    if (!parsePackage(packageSpecOf(option), pk)) return false;
    std::vector<PackagePad> pads = packagePads(pk);
    // lay the package out on a scratch definition with one pin per pad
    PartDef tmp;
    for (size_t i = 0; i < pads.size(); i++) addPin(tmp, "p" + std::to_string(i), "", {0, 0}, {0, 0}, {0, 0});
    layoutPackage(tmp, pk, label, style);
    d.gfx[PCB] = tmp.gfx[PCB];
    d.holes.clear();
    if (!pk.tht) d.gfx[BB] = tmp.gfx[BB];
    for (size_t i = 0; i < pads.size(); i++) {
        int pin = i < padPins.size() ? padPins[i] : -1;
        if (pin < 0) {  // tab: an extra pin joined to the lead with the same number
            int lead = -1;
            for (size_t j = 0; j < pads.size(); j++)
                if (j != i && pads[j].num == pads[i].num && j < padPins.size() && padPins[j] >= 0) lead = padPins[j];
            if (lead < 0) continue;
            PinDef& t = addPin(d, d.pins[(size_t)lead].id + "_tab", d.pins[(size_t)lead].name + " (tab)", d.pins[(size_t)lead].pos[BB],
                               d.pins[(size_t)lead].pos[SCH], {0, 0});
            pin = (int)d.pins.size() - 1;
            d.buses.push_back({lead, pin});
            (void)t;
        }
        PinDef& dst = d.pins[(size_t)pin];
        const PinDef& src = tmp.pins[i];
        dst.pos[PCB] = src.pos[PCB];
        dst.pad = src.pad;
        dst.padH = src.padH;
        dst.drill = src.drill;
        dst.square = src.square;
        if (!pk.tht) dst.pos[BB] = src.pos[BB];
    }
    // a tab pin sits on its lead in the breadboard and schematic views
    for (auto& b : d.buses)
        if (b.size() == 2 && d.pins[(size_t)b[1]].id == d.pins[(size_t)b[0]].id + "_tab") {
            d.pins[(size_t)b[1]].pos[BB] = d.pins[(size_t)b[0]].pos[BB];
            d.pins[(size_t)b[1]].pos[SCH] = d.pins[(size_t)b[0]].pos[SCH];
        }
    return true;
}

}  // namespace plib
