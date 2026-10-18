// Exporters: Gerber RS-274X + Excellon (zipped, fab-ready), SVG, BOM, netlist, SPICE.
#include <cmath>
#include <cstdio>
#include <ctime>
#include <map>
#include <set>

#include "tools.h"

// ---------------------------------------------------------------------------
// ZIP (store only)
// ---------------------------------------------------------------------------

static uint32_t crc32(const std::string& d) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : d) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

std::string makeZip(const std::vector<std::pair<std::string, std::string>>& files) {
    std::string out, central;
    auto u16 = [](std::string& s, uint16_t v) { s += (char)(v & 0xFF); s += (char)(v >> 8); };
    auto u32 = [](std::string& s, uint32_t v) { for (int i = 0; i < 4; i++) s += (char)((v >> (8 * i)) & 0xFF); };
    time_t t = time(nullptr);
    struct tm* tm = localtime(&t);
    uint16_t dosTime = (uint16_t)((tm->tm_hour << 11) | (tm->tm_min << 5) | (tm->tm_sec / 2));
    uint16_t dosDate = (uint16_t)(((tm->tm_year - 80) << 9) | ((tm->tm_mon + 1) << 5) | tm->tm_mday);
    for (auto& f : files) {
        uint32_t crc = crc32(f.second);
        uint32_t off = (uint32_t)out.size();
        u32(out, 0x04034b50); u16(out, 20); u16(out, 0); u16(out, 0); u16(out, dosTime); u16(out, dosDate);
        u32(out, crc); u32(out, (uint32_t)f.second.size()); u32(out, (uint32_t)f.second.size());
        u16(out, (uint16_t)f.first.size()); u16(out, 0);
        out += f.first;
        out += f.second;
        u32(central, 0x02014b50); u16(central, 20); u16(central, 20); u16(central, 0); u16(central, 0);
        u16(central, dosTime); u16(central, dosDate); u32(central, crc);
        u32(central, (uint32_t)f.second.size()); u32(central, (uint32_t)f.second.size());
        u16(central, (uint16_t)f.first.size()); u16(central, 0); u16(central, 0); u16(central, 0); u16(central, 0);
        u32(central, 0); u32(central, off);
        central += f.first;
    }
    uint32_t cdOff = (uint32_t)out.size();
    out += central;
    u32(out, 0x06054b50); u16(out, 0); u16(out, 0); u16(out, (uint16_t)files.size()); u16(out, (uint16_t)files.size());
    u32(out, (uint32_t)central.size()); u32(out, cdOff); u16(out, 0);
    return out;
}

// ---------------------------------------------------------------------------
// Stroke font (for silkscreen) — 4x6 grid, polylines encoded as digit pairs.
// ---------------------------------------------------------------------------

static const char* glyph(char c) {
    switch (c) {
        case 'A': return "0602204246 0343";
        case 'B': return "06003041423303 3344453606";
        case 'C': return "4130100105163645";
        case 'D': return "00304145360600";
        case 'E': return "40000646 0333";
        case 'F': return "400006 0333";
        case 'G': return "41301001051636454323";
        case 'H': return "0006 4046 0343";
        case 'I': return "1030 2026 1636";
        case 'J': return "4045361605";
        case 'K': return "0006 4004 1346";
        case 'L': return "000646";
        case 'M': return "0600234046";
        case 'N': return "06004640";
        case 'O': return "103041453616050110";
        case 'P': return "06003041423303";
        case 'Q': return "103041453616050110 2446";
        case 'R': return "06003041423303 2346";
        case 'S': return "413010010213334445361605";
        case 'T': return "0040 2026";
        case 'U': return "000516364540";
        case 'V': return "002640";
        case 'W': return "0016233640";
        case 'X': return "0046 4006";
        case 'Y': return "002340 2326";
        case 'Z': return "00400646";
        case '0': return "103041453616050110 4105";
        case '1': return "112026 1636";
        case '2': return "01103041420646";
        case '3': return "01103041423313 334445361605";
        case '4': return "36300444";
        case '5': return "4000033344453606";
        case '6': return "30100105163645443303";
        case '7': return "004016";
        case '8': return "13020110304142331304051636454433";
        case '9': return "43130201103041453616";
        case '-': return "1333";
        case '+': return "0343 2125";
        case '.': return "2526";
        case ',': return "2516";
        case ':': return "2122 2526";
        case '/': return "0640";
        case '(': return "30212536";
        case ')': return "10212516";
        case '_': return "0646";
        case '=': return "0242 0444";
        case '~': return "03122132";
        case '\'': return "2021";
        case '*': return "1135 3115 0343";
        case ' ': return "";
        default: return "0040460600";
    }
}

// Returns line segments for `text` with cap height `size`, anchored at its centre-left baseline area.
static std::vector<std::pair<Vec2, Vec2>> strokeText(const std::string& text, Vec2 at, float size, int align) {
    std::vector<std::pair<Vec2, Vec2>> segs;
    float u = size / 6;
    std::string t;
    for (size_t i = 0; i < text.size(); i++) {
        unsigned char c = (unsigned char)text[i];
        if (c >= 0x80) {
            if (c == 0xCE && i + 1 < text.size() && (unsigned char)text[i + 1] == 0xA9) { t += "R"; i++; continue; }  // Ω
            if (c == 0xC2 && i + 1 < text.size() && (unsigned char)text[i + 1] == 0xB5) { t += "U"; i++; continue; }  // µ
            while (i + 1 < text.size() && ((unsigned char)text[i + 1] & 0xC0) == 0x80) i++;
            t += "?";
            continue;
        }
        t += (char)std::toupper(c);
    }
    float w = (float)t.size() * 6 * u - 2 * u;
    float x0 = align == 0 ? at.x : align == 1 ? at.x - w / 2 : at.x - w;
    float y0 = at.y - 3 * u;
    for (size_t i = 0; i < t.size(); i++) {
        const char* g = glyph(t[i]);
        float gx = x0 + (float)i * 6 * u;
        std::vector<Vec2> pl;
        auto flush = [&]() {
            for (size_t k = 0; k + 1 < pl.size(); k++) segs.push_back({pl[k], pl[k + 1]});
            if (pl.size() == 1) segs.push_back({pl[0], pl[0]});
            pl.clear();
        };
        for (const char* p = g; *p;) {
            if (*p == ' ') { flush(); p++; continue; }
            if (!p[1]) break;
            pl.push_back({gx + (float)(p[0] - '0') * u, y0 + (float)(p[1] - '0') * u});
            p += 2;
        }
        flush();
    }
    return segs;
}

// ---------------------------------------------------------------------------
// Gerber
// ---------------------------------------------------------------------------
namespace {

class Gerber {
public:
    Gerber(Rect board, const std::string& comment) : board_(board) {
        body_ += "G04 designElectronics " + std::string(kAppVersion) + " - " + comment + "*\n";
        body_ += "%FSLAX24Y24*%\n%MOIN*%\n%LPD*%\n";
    }
    void line(Vec2 a, Vec2 b, float w) { sel(circ(w)); move(a, 2); move(b, 1); }
    void flashCircle(Vec2 c, float d) { sel(circ(d)); move(c, 3); }
    void flashRect(Vec2 c, float w, float h) { sel(rectAp(w, h)); move(c, 3); }
    void flashOval(Vec2 c, float w, float h) { sel(ovalAp(w, h)); move(c, 3); }
    std::string str() const {
        std::string head;
        for (auto& kv : apsOrdered_) head += kv;
        return body_.substr(0, headerEnd()) + head + body_.substr(headerEnd()) + "M02*\n";
    }

private:
    Rect board_;
    std::string body_;
    std::map<std::string, int> aps_;
    std::vector<std::string> apsOrdered_;
    int cur_ = -1;
    size_t headerEnd() const { return body_.find("%LPD*%\n") + 7; }
    static long coord(float mil) { return std::lround(mil * 10); }  // 1e-4 inch
    int ap(const std::string& def) {
        auto it = aps_.find(def);
        if (it != aps_.end()) return it->second;
        int n = 10 + (int)aps_.size();
        aps_[def] = n;
        apsOrdered_.push_back("%ADD" + std::to_string(n) + def + "*%\n");
        return n;
    }
    int circ(float d) { char b[32]; snprintf(b, sizeof b, "C,%.4f", std::max(d, 1.f) / 1000); return ap(b); }
    int rectAp(float w, float h) { char b[48]; snprintf(b, sizeof b, "R,%.4fX%.4f", w / 1000, h / 1000); return ap(b); }
    int ovalAp(float w, float h) { char b[48]; snprintf(b, sizeof b, "O,%.4fX%.4f", w / 1000, h / 1000); return ap(b); }
    void sel(int a) { if (a != cur_) { body_ += "D" + std::to_string(a) + "*\n"; cur_ = a; } }
    void move(Vec2 p, int op) {
        char b[64];
        snprintf(b, sizeof b, "X%ldY%ldD0%d*\n", coord(p.x - board_.x), coord(board_.b() - p.y), op);
        body_ += b;
    }
};

void silkPrim(Gerber& g, const Prim& p, const Xform& xf) {
    float w = std::max(6.f, p.w > 0 ? p.w : 8.f);
    auto T = [&](Vec2 v) { return xf.apply(v); };
    switch (p.kind) {
        case Prim::LINE: g.line(T(p.a), T(p.b), w); break;
        case Prim::RECT: {
            if (!(p.stroke & 0xFF)) break;
            Vec2 a = T(p.a), b = T({p.b.x, p.a.y}), c = T(p.b), d = T({p.a.x, p.b.y});
            g.line(a, b, w); g.line(b, c, w); g.line(c, d, w); g.line(d, a, w);
            break;
        }
        case Prim::CIRCLE:
        case Prim::ARC: {
            if (p.kind == Prim::CIRCLE && !(p.stroke & 0xFF)) break;
            float a0 = p.kind == Prim::ARC ? p.a0 : 0, a1 = p.kind == Prim::ARC ? p.a1 : 360;
            int n = std::max(12, (int)(std::fabs(a1 - a0) / 10));
            Vec2 prev;
            for (int i = 0; i <= n; i++) {
                float a = (a0 + (a1 - a0) * i / n) * (float)M_PI / 180;
                Vec2 q = T(p.a + Vec2(std::cos(a), std::sin(a)) * p.r);
                if (i) g.line(prev, q, w);
                prev = q;
            }
            break;
        }
        case Prim::POLY:
            for (size_t i = 0; i + 1 < p.pts.size(); i++) g.line(T(p.pts[i]), T(p.pts[i + 1]), w);
            if (p.fill & 0xFF && p.pts.size() > 2) g.line(T(p.pts.back()), T(p.pts.front()), w);
            break;
        case Prim::TEXT:
            for (auto& s : strokeText(p.text, p.a, p.size, p.align)) g.line(T(s.first), T(s.second), std::max(5.f, p.size / 8));
            break;
    }
}

}  // namespace

bool exportGerberZip(const Document& doc, const Connectivity& conn, const std::string& zipPath, std::string* msg) {
    Rect board;
    bool hasBoard = false, single = false;
    for (auto& p : doc.parts)
        if (p.def && p.def->isBoard) {
            board = hasBoard ? board.united(p.worldBounds(PCB)) : p.worldBounds(PCB);
            hasBoard = true;
            if (p.prop("layers").find("one") != std::string::npos) single = true;
        }
    if (!hasBoard) { if (msg) *msg = "Add a PCB board in the PCB view before exporting Gerber files."; return false; }

    std::string base = fileBaseName(zipPath);
    Gerber top(board, "Copper top"), bot(board, "Copper bottom"), maskT(board, "Solder mask top"), maskB(board, "Solder mask bottom"),
        paste(board, single ? "Solder paste bottom" : "Solder paste top"), silk(board, "Silkscreen top"), outline(board, "Board outline");
    std::map<float, std::vector<Vec2>> plated, npth;
    bool anySmd = false;
    for (auto& p : doc.parts) {
        if (!p.inView(PCB) || p.def->isBoard) continue;
        for (size_t i = 0; i < p.def->pins.size(); i++) {
            const PinDef& pin = p.def->pins[i];
            Vec2 c = p.pinPos((int)i, PCB), sz = pin.padSize(p.place[PCB]);
            auto flash = [&](Gerber& g, float grow) {
                Vec2 s = sz + Vec2(grow, grow);
                if (pin.rectPad()) g.flashRect(c, s.x, s.y);
                else if (s.x != s.y) g.flashOval(c, s.x, s.y);
                else g.flashCircle(c, s.x);
            };
            if (pin.smd()) {  // component side only: top, or bottom on a single-sided board
                anySmd = true;
                flash(single ? bot : top, 0);
                flash(single ? maskB : maskT, 8);
                flash(paste, 0);
                continue;
            }
            if (!single) flash(top, 0);
            flash(bot, 0);
            flash(maskT, 8);
            flash(maskB, 8);
            plated[std::round(pin.drill)].push_back(c);
        }
        for (auto& h : p.def->holes) npth[std::round(h.second)].push_back(p.place[PCB].apply(h.first));
        for (auto& prim : p.def->gfx[PCB]) silkPrim(silk, prim, p.place[PCB]);
        if (p.type != "via" && p.type != "hole") {
            Rect r = p.worldBounds(PCB);
            for (auto& s : strokeText(p.label, {r.x, r.y - 45}, 50, 0)) silk.line(s.first, s.second, 7);
        }
    }
    for (auto& w : doc.wires) {
        if (w.view != PCB) continue;
        Gerber& g = (w.layer == TOP && !single) ? top : bot;
        for (size_t i = 0; i + 1 < w.pts.size(); i++) g.line(w.pts[i], w.pts[i + 1], w.width);
    }
    for (auto& p : doc.parts) {
        if (!p.def || !p.def->isBoard) continue;
        Rect r = p.worldBounds(PCB);
        Vec2 a{r.x, r.y}, b{r.r(), r.y}, c{r.r(), r.b()}, d{r.x, r.b()};
        outline.line(a, b, 10); outline.line(b, c, 10); outline.line(c, d, 10); outline.line(d, a, 10);
    }
    // Excellon drill file (inches, 2.4 leading zeros suppressed via decimal coords)
    std::string drill = "M48\n; designElectronics drill file\nFMAT,2\nINCH,TZ\n";
    int tool = 1;
    std::map<std::pair<bool, float>, int> tools;
    char buf[96];
    for (auto* m : {&plated, &npth}) {
        for (auto& kv : *m) {
            tools[{m == &npth, kv.first}] = tool;
            snprintf(buf, sizeof buf, "T%dC%.4f%s\n", tool, kv.first / 1000, m == &npth ? " ; non-plated" : "");
            drill += buf;
            tool++;
        }
    }
    drill += "%\nG90\nG05\n";
    for (auto* m : {&plated, &npth}) {
        for (auto& kv : *m) {
            snprintf(buf, sizeof buf, "T%d\n", tools[{m == &npth, kv.first}]);
            drill += buf;
            for (auto& pt : kv.second) {
                snprintf(buf, sizeof buf, "X%.4fY%.4f\n", (pt.x - board.x) / 1000, (board.b() - pt.y) / 1000);
                drill += buf;
            }
        }
    }
    drill += "T0\nM30\n";

    std::vector<std::pair<std::string, std::string>> files;
    if (!single) files.push_back({base + "_copperTop.gtl", top.str()});
    files.push_back({base + "_copperBottom.gbl", bot.str()});
    files.push_back({base + "_maskTop.gts", maskT.str()});
    files.push_back({base + "_maskBottom.gbs", maskB.str()});
    if (anySmd) files.push_back({base + (single ? "_pasteBottom.gbp" : "_pasteTop.gtp"), paste.str()});
    files.push_back({base + "_silkTop.gto", silk.str()});
    files.push_back({base + "_contour.gm1", outline.str()});
    files.push_back({base + "_drill.txt", drill});
    char readme[512];
    snprintf(readme, sizeof readme,
             "Gerber RS-274X files exported by designElectronics %s\nBoard size: %.1f x %.1f mm, %s\nUnits: inches (2.4)\n"
             "Upload this zip to your PCB manufacturer.\n",
             kAppVersion, board.w * 0.0254, board.h * 0.0254, single ? "single-sided" : "double-sided");
    files.push_back({"README.txt", readme});
    (void)conn;
    if (!writeTextFile(zipPath, makeZip(files))) { if (msg) *msg = "Could not write " + zipPath; return false; }
    if (msg) *msg = "Exported " + std::to_string(files.size() - 1) + " Gerber/drill files to " + zipPath;
    return true;
}

// ---------------------------------------------------------------------------
// SVG
// ---------------------------------------------------------------------------

static std::string svgColor(uint32_t c) {
    char b[32];
    snprintf(b, sizeof b, "#%06x", c >> 8);
    return b;
}
static std::string svgOpacity(uint32_t c, const char* attr) {
    if ((c & 0xFF) == 0xFF) return "";
    char b[48];
    snprintf(b, sizeof b, " %s-opacity=\"%.3f\"", attr, (c & 0xFF) / 255.0);
    return b;
}
static std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;"; else if (c == '&') o += "&amp;"; else if (c == '"') o += "&quot;"; else o += c;
    }
    return o;
}

static void svgPrim(std::string& o, const Prim& p, const Xform& xf) {
    char b[512];
    std::string fill = (p.fill & 0xFF) ? "fill=\"" + svgColor(p.fill) + "\"" + svgOpacity(p.fill, "fill") : "fill=\"none\"";
    std::string stroke = (p.stroke & 0xFF) && p.w > 0 ? "stroke=\"" + svgColor(p.stroke) + "\" stroke-width=\"" + std::to_string(p.w) + "\" stroke-linecap=\"round\" stroke-linejoin=\"round\"" + svgOpacity(p.stroke, "stroke") : "";
    auto T = [&](Vec2 v) { return xf.apply(v); };
    switch (p.kind) {
        case Prim::LINE: {
            Vec2 a = T(p.a), c = T(p.b);
            snprintf(b, sizeof b, "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" %s/>\n", a.x, a.y, c.x, c.y, stroke.c_str());
            o += b;
            break;
        }
        case Prim::RECT: {
            Rect r = Rect::fromPoints(T(p.a), T(p.b));
            snprintf(b, sizeof b, "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" rx=\"%.1f\" %s %s/>\n", r.x, r.y, r.w, r.h, p.r, fill.c_str(), stroke.c_str());
            o += b;
            break;
        }
        case Prim::CIRCLE: {
            Vec2 c = T(p.a);
            snprintf(b, sizeof b, "<circle cx=\"%.1f\" cy=\"%.1f\" r=\"%.1f\" %s %s/>\n", c.x, c.y, p.r, fill.c_str(), stroke.c_str());
            o += b;
            break;
        }
        case Prim::ARC: {
            std::string d;
            int n = std::max(8, (int)(std::fabs(p.a1 - p.a0) / 8));
            for (int i = 0; i <= n; i++) {
                float a = (p.a0 + (p.a1 - p.a0) * i / n) * (float)M_PI / 180;
                Vec2 q = T(p.a + Vec2(std::cos(a), std::sin(a)) * p.r);
                snprintf(b, sizeof b, "%s%.1f,%.1f ", i ? "L" : "M", q.x, q.y);
                d += b;
            }
            o += "<path d=\"" + d + "\" fill=\"none\" " + stroke + "/>\n";
            break;
        }
        case Prim::POLY: {
            std::string pts;
            for (auto& q : p.pts) { Vec2 t = T(q); snprintf(b, sizeof b, "%.1f,%.1f ", t.x, t.y); pts += b; }
            o += std::string((p.fill & 0xFF) ? "<polygon" : "<polyline") + " points=\"" + pts + "\" " + fill + " " + stroke + "/>\n";
            break;
        }
        case Prim::TEXT: {
            Vec2 a = T(p.a);
            int al = p.align;
            if (xf.flip) al = 2 - al;
            const char* anchor = al == 0 ? "start" : al == 1 ? "middle" : "end";
            snprintf(b, sizeof b, "<text x=\"%.1f\" y=\"%.1f\" font-family=\"Lato, Helvetica, Arial, sans-serif\" font-size=\"%.1f\" text-anchor=\"%s\" dominant-baseline=\"central\" fill=\"%s\">",
                     a.x, a.y, p.size, anchor, svgColor(p.fill).c_str());
            o += b;
            o += esc(p.text) + "</text>\n";
            break;
        }
    }
}

std::string exportSvg(const Document& doc, const Connectivity& conn, int view) {
    Rect bounds;
    bool any = false;
    for (auto& p : doc.parts) {
        if (!p.inView(view)) continue;
        Rect r = p.worldBounds(view);
        bounds = any ? bounds.united(r) : r;
        any = true;
    }
    for (auto& w : doc.wires)
        if (w.view == view) for (auto& pt : w.pts) { bounds = any ? bounds.expandTo(pt) : Rect(pt.x, pt.y, 0, 0); any = true; }
    if (!any) bounds = Rect(0, 0, 1000, 1000);
    bounds = Rect(bounds.x - 200, bounds.y - 200, bounds.w + 400, bounds.h + 400);
    char b[512];
    std::string o = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    snprintf(b, sizeof b,
             "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.3fin\" height=\"%.3fin\" viewBox=\"%.1f %.1f %.1f %.1f\">\n",
             bounds.w / 1000, bounds.h / 1000, bounds.x, bounds.y, bounds.w, bounds.h);
    o += b;
    o += "<title>" + esc(doc.title) + " — " + viewName(view) + " view (designElectronics)</title>\n";
    uint32_t bg = view == BB ? rgba(246, 246, 244) : view == SCH ? rgba(255, 255, 255) : rgba(70, 76, 84);
    snprintf(b, sizeof b, "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" fill=\"%s\"/>\n", bounds.x, bounds.y, bounds.w, bounds.h, svgColor(bg).c_str());
    o += b;
    auto drawWire = [&](const Wire& w) {
        std::string pts;
        for (auto& p : w.pts) { snprintf(b, sizeof b, "%.1f,%.1f ", p.x, p.y); pts += b; }
        if (view == BB) {
            uint32_t dark = rgba(((w.color >> 24) & 0xFF) * 6 / 10, ((w.color >> 16) & 0xFF) * 6 / 10, ((w.color >> 8) & 0xFF) * 6 / 10);
            o += "<polyline points=\"" + pts + "\" fill=\"none\" stroke=\"" + svgColor(dark) + "\" stroke-width=\"30\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>\n";
            o += "<polyline points=\"" + pts + "\" fill=\"none\" stroke=\"" + svgColor(w.color) + "\" stroke-width=\"22\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>\n";
        } else if (view == SCH) {
            o += "<polyline points=\"" + pts + "\" fill=\"none\" stroke=\"#303030\" stroke-width=\"10\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>\n";
        } else {
            std::string col = w.layer == TOP ? "#f2a33a" : "#d9b73c";
            o += "<polyline points=\"" + pts + "\" fill=\"none\" stroke=\"" + col + "\" stroke-width=\"" + std::to_string(w.width) + "\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>\n";
        }
    };
    if (view == PCB) {
        for (auto& p : doc.parts) if (p.inView(PCB) && p.def->isBoard) for (auto& pr : p.def->gfx[PCB]) svgPrim(o, pr, p.place[PCB]);
        for (auto& w : doc.wires) if (w.view == PCB && w.layer == BOTTOM) drawWire(w);
        for (auto& p : doc.parts) {
            if (!p.inView(PCB) || p.def->isBoard) continue;
            for (size_t i = 0; i < p.def->pins.size(); i++) {
                const PinDef& pin = p.def->pins[i];
                Vec2 c = p.pinPos((int)i, PCB), sz = pin.padSize(p.place[PCB]);
                float rad = pin.rectPad() ? 0 : std::min(sz.x, sz.y) / 2;
                snprintf(b, sizeof b, "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" rx=\"%.1f\" fill=\"#e9b949\"/>\n", c.x - sz.x / 2, c.y - sz.y / 2, sz.x, sz.y, rad);
                o += b;
                if (pin.smd()) continue;
                snprintf(b, sizeof b, "<circle cx=\"%.1f\" cy=\"%.1f\" r=\"%.1f\" fill=\"#1d1d1d\"/>\n", c.x, c.y, pin.drill / 2);
                o += b;
            }
        }
        for (auto& w : doc.wires) if (w.view == PCB && w.layer == TOP) drawWire(w);
        for (auto& p : doc.parts) {
            if (!p.inView(PCB) || p.def->isBoard) continue;
            for (auto& pr : p.def->gfx[PCB]) svgPrim(o, pr, p.place[PCB]);
            for (auto& h : p.def->holes) {
                Vec2 c = p.place[PCB].apply(h.first);
                snprintf(b, sizeof b, "<circle cx=\"%.1f\" cy=\"%.1f\" r=\"%.1f\" fill=\"#1d1d1d\"/>\n", c.x, c.y, h.second / 2);
                o += b;
            }
        }
    } else {
        for (auto& p : doc.parts) {
            if (!p.inView(view)) continue;
            o += "<g id=\"" + esc(p.label) + "\">\n";
            for (auto& pr : p.def->gfx[view]) svgPrim(o, pr, p.place[view]);
            if (view == SCH && !p.def->isBreadboard && p.def->netProp.empty()) {
                Rect r = p.worldBounds(SCH);
                std::string lbl = p.label;
                if (!p.def->valueProp.empty()) lbl += "  " + p.prop(p.def->valueProp);
                snprintf(b, sizeof b, "<text x=\"%.1f\" y=\"%.1f\" font-family=\"Lato, sans-serif\" font-size=\"55\" fill=\"#303030\">", r.x, r.y - 40);
                o += b;
                o += esc(lbl) + "</text>\n";
            }
            o += "</g>\n";
        }
        for (auto& w : doc.wires) if (w.view == view) drawWire(w);
    }
    for (auto& n : doc.notes) {
        if (n.view != view) continue;
        snprintf(b, sizeof b, "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" fill=\"#fff6b0\" stroke=\"#d8c860\" stroke-width=\"6\"/>\n", n.pos.x, n.pos.y, n.size.x, n.size.y);
        o += b;
        snprintf(b, sizeof b, "<text x=\"%.1f\" y=\"%.1f\" font-family=\"Lato, sans-serif\" font-size=\"70\" fill=\"#333\">", n.pos.x + 40, n.pos.y + 100);
        o += b;
        o += esc(n.text) + "</text>\n";
    }
    (void)conn;
    o += "</svg>\n";
    return o;
}

// ---------------------------------------------------------------------------
// BOM / netlist / SPICE
// ---------------------------------------------------------------------------

static std::string csvField(const std::string& s) {
    std::string o = "\"";
    for (char c : s) { if (c == '"') o += '"'; o += c; }
    return o + "\"";
}

std::string exportBomCsv(const Document& doc) {
    struct Row { std::string title, props; std::vector<std::string> labels; };
    std::map<std::string, Row> rows;
    for (auto& p : doc.parts) {
        if (!p.def || p.def->isBoard || p.type == "via" || !p.def->netProp.empty() || p.type == "multimeter") continue;
        std::string props;
        for (auto& kv : mergedProps(p.type, p.props)) {
            if (kv.first == "pressed" || kv.first == "wiper" || kv.first == "light" || kv.first == "position") continue;
            if (!props.empty()) props += "; ";
            props += kv.first + ": " + kv.second;
        }
        std::string key = p.def->title + "|" + props;
        Row& r = rows[key];
        r.title = p.def->title;
        r.props = props;
        r.labels.push_back(p.label);
    }
    std::string o = "Quantity,Labels,Part,Properties\n";
    for (auto& kv : rows) {
        std::string labels;
        for (size_t i = 0; i < kv.second.labels.size(); i++) labels += (i ? " " : "") + kv.second.labels[i];
        o += std::to_string(kv.second.labels.size()) + "," + csvField(labels) + "," + csvField(kv.second.title) + "," + csvField(kv.second.props) + "\n";
    }
    int bbWires = 0;
    for (auto& w : doc.wires) if (w.view == BB) bbWires++;
    if (bbWires) o += std::to_string(bbWires) + ",\"\",\"Jumper wire\",\"\"\n";
    return o;
}

std::string exportNetlistXml(const Document& doc, const Connectivity& conn) {
    char date[64];
    time_t t = time(nullptr);
    strftime(date, sizeof date, "%Y-%m-%d %H:%M:%S", localtime(&t));
    std::string o = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    o += "<!-- designElectronics netlist: one <net> per electrical connection -->\n";
    o += "<netlist sketch=\"" + esc(doc.title) + "\" date=\"" + date + "\">\n";
    for (int n = 0; n < conn.numNets(); n++) {
        std::vector<PinRef> pins;
        for (auto& pr : conn.netPins(n)) {
            const PartInst* p = doc.part(pr.part);
            if (p && !p->def->isBreadboard && p->def->netProp.empty()) pins.push_back(pr);
        }
        if (pins.size() < 2) continue;
        o += "  <net name=\"" + esc(conn.netName(n)) + "\">\n";
        for (auto& pr : pins) {
            const PartInst* p = doc.part(pr.part);
            const PinDef& pin = p->def->pins[(size_t)pr.pin];
            o += "    <connector id=\"" + esc(pin.id) + "\" name=\"" + esc(pin.name) + "\">\n";
            o += "      <part id=\"" + std::to_string(p->id) + "\" label=\"" + esc(p->label) + "\" title=\"" + esc(p->def->title) + "\"/>\n";
            o += "    </connector>\n";
        }
        o += "  </net>\n";
    }
    o += "</netlist>\n";
    return o;
}

std::string exportSpice(const Document& doc, const Connectivity& conn) {
    std::string o = "* " + doc.title + " — SPICE netlist exported by designElectronics\n";
    auto node = [&](const PartInst& p, const char* pin) -> std::string {
        int pi = p.def->pinIndex(pin);
        if (pi < 0) return "NC";
        int n = conn.netOf(p.id, pi);
        std::string name = conn.netName(n);
        return name == "GND" ? "0" : name;
    };
    bool needLed = false, needDiode = false, needNpn = false, needPnp = false;
    char b[256];
    for (auto& p : doc.parts) {
        if (!p.def) continue;
        const std::string& m = p.def->sim;
        if (m == "resistor") { snprintf(b, sizeof b, "R%s %s %s %g\n", p.label.c_str(), node(p, "pin0").c_str(), node(p, "pin1").c_str(), parseSI(p.prop("resistance"), 220)); o += b; }
        else if (m == "potentiometer") {
            double r = parseSI(p.prop("resistance"), 1e4), w = std::atof(p.prop("wiper").c_str()) / 100;
            snprintf(b, sizeof b, "R%sA %s %s %g\nR%sB %s %s %g\n", p.label.c_str(), node(p, "leg1").c_str(), node(p, "wiper").c_str(), std::max(0.5, r * w),
                     p.label.c_str(), node(p, "wiper").c_str(), node(p, "leg2").c_str(), std::max(0.5, r * (1 - w)));
            o += b;
        } else if (m == "capacitor") { snprintf(b, sizeof b, "C%s %s %s %g\n", p.label.c_str(), node(p, p.def->pinIndex("plus") >= 0 ? "plus" : "pin0").c_str(), node(p, p.def->pinIndex("minus") >= 0 ? "minus" : "pin1").c_str(), parseSI(p.prop("capacitance"), 1e-7)); o += b; }
        else if (m == "inductor") { snprintf(b, sizeof b, "L%s %s %s %g\n", p.label.c_str(), node(p, "pin0").c_str(), node(p, "pin1").c_str(), parseSI(p.prop("inductance"), 1e-4)); o += b; }
        else if (m == "battery") { snprintf(b, sizeof b, "V%s %s %s DC %g\n", p.label.c_str(), node(p, "plus").c_str(), node(p, "minus").c_str(), parseSI(p.prop("voltage"), 9)); o += b; }
        else if (m == "led") { snprintf(b, sizeof b, "D%s %s %s LED_%s\n", p.label.c_str(), node(p, "anode").c_str(), node(p, "cathode").c_str(), p.prop("color").c_str()); o += b; needLed = true; }
        else if (m == "diode") { snprintf(b, sizeof b, "D%s %s %s DGEN\n", p.label.c_str(), node(p, "anode").c_str(), node(p, "cathode").c_str()); o += b; needDiode = true; }
        else if (m == "npn") { snprintf(b, sizeof b, "Q%s %s %s %s QNPN\n", p.label.c_str(), node(p, "C").c_str(), node(p, "B").c_str(), node(p, "E").c_str()); o += b; needNpn = true; }
        else if (m == "pnp") { snprintf(b, sizeof b, "Q%s %s %s %s QPNP\n", p.label.c_str(), node(p, "C").c_str(), node(p, "B").c_str(), node(p, "E").c_str()); o += b; needPnp = true; }
        else if (m == "board") {
            // one DC source per power rail named in simParams ("RAIL 5V=5;RAIL 3V3=3.3")
            std::vector<std::string> items;
            for (size_t a = 0, e; a <= p.def->simParams.size(); a = e + 1) {
                e = p.def->simParams.find(';', a);
                if (e == std::string::npos) e = p.def->simParams.size();
                items.push_back(p.def->simParams.substr(a, e - a));
            }
            for (auto& item : items) {
                if (item.rfind("RAIL ", 0) != 0) continue;
                size_t eq = item.rfind('=');
                if (eq == std::string::npos) continue;
                std::string rail = item.substr(5, eq - 5);
                int pin = -1;
                for (size_t i = 0; i < p.def->pins.size(); i++) if (p.def->pins[i].name == rail) { pin = (int)i; break; }
                if (pin < 0) continue;
                snprintf(b, sizeof b, "V%s_%s %s %s DC %s\n", p.label.c_str(), p.def->pins[(size_t)pin].id.c_str(), node(p, p.def->pins[(size_t)pin].id.c_str()).c_str(),
                         node(p, "GND").c_str(), item.substr(eq + 1).c_str());
                o += b;
            }
        }
        else if (m == "motor") { snprintf(b, sizeof b, "R%s %s %s 10 ; DC motor approximated as 10 ohm\n", p.label.c_str(), node(p, "pin1").c_str(), node(p, "pin0").c_str()); o += b; }
    }
    if (needLed) {
        for (const char* c : {"Red", "Green", "Blue", "Yellow", "White", "Orange"}) {
            double n = std::string(c) == "Blue" || std::string(c) == "White" ? 3.5 : 1.9;
            snprintf(b, sizeof b, ".model LED_%s D(IS=1e-22 N=%.2f RS=10)\n", c, n);
            o += b;
        }
    }
    if (needDiode) o += ".model DGEN D(IS=2.5n RS=0.04 N=1.75)\n";
    if (needNpn) o += ".model QNPN NPN(BF=200 IS=1e-14)\n";
    if (needPnp) o += ".model QPNP PNP(BF=200 IS=1e-14)\n";
    o += ".op\n.end\n";
    return o;
}
