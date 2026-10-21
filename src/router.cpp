// Grid-based PCB autorouter: A* over a two-layer occupancy grid with
// clearance-aware obstacles, turn penalties and vias.
#include <cmath>
#include <queue>
#include <set>
#include <unordered_map>

#include "tools.h"

namespace {

constexpr float kCell = 25;  // mils

struct Grid {
    int w = 0, h = 0, layers = 2;
    Vec2 origin;
    std::vector<int> owner;  // -1 free, -2 blocked, else net id

    int idx(int x, int y, int l) const { return (l * h + y) * w + x; }
    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
    Vec2 pos(int x, int y) const { return origin + Vec2(x * kCell, y * kCell); }
    int cx(float wx) const { return (int)std::lround((wx - origin.x) / kCell); }
    int cy(float wy) const { return (int)std::lround((wy - origin.y) / kCell); }

    void claim(int x, int y, int l, int net) {
        if (!inside(x, y)) return;
        int& o = owner[(size_t)idx(x, y, l)];
        if (o == -1) o = net;
        else if (o != net) o = -2;
    }
    // mark cells whose centre is within `r` of the segment a-b
    void markSeg(Vec2 a, Vec2 b, float r, int l, int net) {
        int x0 = cx(std::min(a.x, b.x) - r) - 1, x1 = cx(std::max(a.x, b.x) + r) + 1;
        int y0 = cy(std::min(a.y, b.y) - r) - 1, y1 = cy(std::max(a.y, b.y) + r) + 1;
        for (int y = std::max(0, y0); y <= std::min(h - 1, y1); y++)
            for (int x = std::max(0, x0); x <= std::min(w - 1, x1); x++)
                if (distPointSeg(pos(x, y), a, b) < r) claim(x, y, l, net);
    }
    bool usable(int x, int y, int l, int net) const {
        int o = owner[(size_t)idx(x, y, l)];
        return o == -1 || o == net;
    }
};

Rect boardRect(const Document& doc, bool* singleSided) {
    Rect r;
    bool any = false;
    *singleSided = false;
    for (auto& p : doc.parts) {
        if (!p.def || !p.def->isBoard) continue;
        Rect b = p.worldBounds(PCB);
        r = any ? r.united(b) : b;
        any = true;
        if (p.prop("layers").find("one") != std::string::npos) *singleSided = true;
    }
    if (any) return r;
    for (auto& p : doc.parts) {
        if (!p.inView(PCB)) continue;
        Rect b = p.worldBounds(PCB);
        r = any ? r.united(b) : b;
        any = true;
    }
    return any ? Rect(r.x - 300, r.y - 300, r.w + 600, r.h + 600) : Rect(0, 0, 2000, 2000);
}

struct Node {
    float f;
    int state;
    bool operator<(const Node& o) const { return f > o.f; }
};

// Surface-mount pads are entered along their long axis (outwards, away from
// the part's centre) from a point just past the pad's end, clear of the
// neighbouring pads; the trace runs from there straight onto the pad. Returns
// the pad centre itself for through-hole and square pads.
Vec2 escapePoint(const PartInst& p, int pin, float clr, float tw) {
    const PinDef& pd = p.def->pins[(size_t)pin];
    Vec2 c = p.pinPos(pin, PCB);
    if (!pd.smd()) return c;
    Vec2 sz = pd.padSize(p.place[PCB]), from = c - p.place[PCB].pos;
    bool alongX = p.def->pins.size() <= 2 ? std::fabs(from.x) >= std::fabs(from.y) : sz.x > sz.y;
    if (p.def->pins.size() > 2 && std::fabs(sz.x - sz.y) < 1) return c;  // exposed pad / square
    Vec2 dir = alongX ? Vec2(from.x >= 0 ? 1.f : -1.f, 0) : Vec2(0, from.y >= 0 ? 1.f : -1.f);
    float lng = alongX ? sz.x : sz.y, shrt = alongX ? sz.y : sz.x;
    return c + dir * (lng / 2 + shrt / 2 * 1.2f + clr + tw / 2 + kCell);
}

const int DX[8] = {1, 1, 0, -1, -1, -1, 0, 1};
const int DY[8] = {0, 1, 1, 1, 0, -1, -1, -1};

}  // namespace

RouteResult autoroute(Document& doc, const Connectivity& connIn, bool ripUp) {
    RouteResult res;
    if (ripUp) {
        // remove previous autorouted traces and the vias they created
        std::set<int> viaParts;
        for (auto& w : doc.wires)
            if (w.view == PCB && w.autorouted)
                for (auto& e : w.ends)
                    if (e.attached()) { auto* p = doc.part(e.part); if (p && p->type == "via" && p->label.rfind("AV", 0) == 0) viaParts.insert(p->id); }
        doc.wires.erase(std::remove_if(doc.wires.begin(), doc.wires.end(), [](const Wire& w) { return w.view == PCB && w.autorouted; }), doc.wires.end());
        for (int id : viaParts) doc.removePart(id);
    }
    bool single = false;
    Rect board = boardRect(doc, &single);
    const DesignRules& R = doc.rules;
    float tw = R.traceWidth, clr = R.clearance;

    std::set<std::pair<PinRef, PinRef>> failed;
    Connectivity conn = connIn;
    for (int iter = 0; iter < 400; iter++) {
        conn.build(doc);
        auto rats = conn.ratsnest(doc, PCB);
        // choose the shortest connection not yet failed
        int best = -1;
        float bestLen = 1e30f;
        for (size_t i = 0; i < rats.size(); i++) {
            if (failed.count({rats[i].pa, rats[i].pb})) continue;
            float l = dist(rats[i].a, rats[i].b);
            if (l < bestLen) { bestLen = l; best = (int)i; }
        }
        if (best < 0) break;
        const RatLine rl = rats[(size_t)best];
        int net = rl.net;

        // --- build occupancy grid ---
        Grid g;
        g.origin = {std::floor(board.x / kCell) * kCell, std::floor(board.y / kCell) * kCell};
        g.w = (int)std::ceil((board.r() - g.origin.x) / kCell) + 1;
        g.h = (int)std::ceil((board.b() - g.origin.y) / kCell) + 1;
        if ((long)g.w * g.h > 4000000) { res.message = "Board too large for the autorouter grid."; return res; }
        g.owner.assign((size_t)(g.w * g.h * 2), -1);
        const int smdLayer = single ? BOTTOM : TOP;  // SMD parts sit on the copper side
        float edge = tw / 2 + clr;
        for (int l = 0; l < 2; l++)
            for (int y = 0; y < g.h; y++)
                for (int x = 0; x < g.w; x++) {
                    Vec2 p = g.pos(x, y);
                    if (p.x < board.x + edge || p.y < board.y + edge || p.x > board.r() - edge || p.y > board.b() - edge ||
                        (single && l == TOP))
                        g.owner[(size_t)g.idx(x, y, l)] = -2;
                }
        for (auto& p : doc.parts) {
            if (!p.inView(PCB) || p.def->isBoard) continue;
            for (size_t i = 0; i < p.def->pins.size(); i++) {
                const PinDef& pin = p.def->pins[i];
                int pn = conn.netOf(p.id, (int)i);
                Vec2 a, b;
                float r;
                padStadium(p.pinPos((int)i, PCB), pin.padSize(p.place[PCB]), a, b, r);
                r += clr + tw / 2;
                if (pin.rectPad()) r *= 1.2f;
                for (int l = 0; l < 2; l++)
                    if (!pin.smd() || l == smdLayer) g.markSeg(a, b, r, l, pn);
            }
            for (auto& hole : p.def->holes) {
                Vec2 c = p.place[PCB].apply(hole.first);
                for (int l = 0; l < 2; l++) g.markSeg(c, c, hole.second / 2 + clr + tw / 2, l, -3);
            }
        }
        for (auto& w : doc.wires) {
            if (w.view != PCB) continue;
            int wn = conn.wireNet(w.id);
            for (size_t i = 0; i + 1 < w.pts.size(); i++) g.markSeg(w.pts[i], w.pts[i + 1], w.width / 2 + clr + tw / 2, w.layer, wn);
        }

        // --- sources and targets: all pins of the two islands ---
        int ga = conn.viewGroup(PCB, rl.pa.part, rl.pa.pin), gb = conn.viewGroup(PCB, rl.pb.part, rl.pb.pin);
        std::unordered_map<int, PinRef> srcCells, dstCells;  // cell (layer 0 index) -> pin
        std::unordered_map<int, int> cellLayers;              // cell -> layer mask (SMD pads: one layer)
        std::unordered_map<int, Vec2> cellEscape;             // cell -> where the path meets it (SMD escape point)
        for (auto& pr : conn.netPins(net)) {
            const PartInst* p = doc.part(pr.part);
            if (!p || !p->inView(PCB) || p->def->isBreadboard) continue;
            int grp = conn.viewGroup(PCB, pr.part, pr.pin);
            Vec2 c = escapePoint(*p, pr.pin, clr, tw);
            int x = g.cx(c.x), y = g.cy(c.y);
            if (!g.inside(x, y)) continue;
            if (grp == ga) srcCells[g.idx(x, y, 0)] = pr;
            else if (grp == gb) dstCells[g.idx(x, y, 0)] = pr;
            else continue;
            cellLayers[g.idx(x, y, 0)] = p->def->pins[(size_t)pr.pin].smd() ? 1 << smdLayer : 3;
            cellEscape[g.idx(x, y, 0)] = c;
        }
        if (srcCells.empty() || dstCells.empty()) { failed.insert({rl.pa, rl.pb}); res.failed++; continue; }

        // --- A* ---
        int layers = 2;
        size_t nstates = (size_t)g.w * g.h * layers * 9;
        std::vector<float> cost(nstates, 1e30f);
        std::vector<int> from(nstates, -1);
        std::priority_queue<Node> pq;
        auto sid = [&](int x, int y, int l, int d) { return (((l * g.h + y) * g.w + x) * 9) + d; };
        auto heur = [&](int x, int y) {
            float best = 1e30f;
            for (auto& kv : dstCells) {
                int dx = kv.first % g.w, dy = (kv.first / g.w) % g.h;
                best = std::min(best, (float)(std::abs(dx - x) + std::abs(dy - y)));
            }
            return best;
        };
        for (auto& kv : srcCells) {
            int x = kv.first % g.w, y = (kv.first / g.w) % g.h;
            for (int l = 0; l < layers; l++) {
                if ((single && l == TOP) || !(cellLayers[kv.first] >> l & 1)) continue;
                int s = sid(x, y, l, 8);
                cost[(size_t)s] = 0;
                pq.push({heur(x, y), s});
            }
        }
        int goal = -1;
        int expanded = 0;
        while (!pq.empty() && expanded < 600000) {
            Node n = pq.top();
            pq.pop();
            int s = n.state;
            int d = s % 9, rest = s / 9;
            int x = rest % g.w, y = (rest / g.w) % g.h, l = rest / (g.w * g.h);
            float c = cost[(size_t)s];
            if (n.f - heur(x, y) > c + 1e-3f) continue;
            expanded++;
            if (dstCells.count(g.idx(x, y, 0)) && (cellLayers[g.idx(x, y, 0)] >> l & 1)) { goal = s; break; }
            for (int k = 0; k < 8; k += 2) {  // right angles only: tidy, Apple-I-style runs
                int nx = x + DX[k], ny = y + DY[k];
                if (!g.inside(nx, ny)) continue;
                bool isDst = dstCells.count(g.idx(nx, ny, 0)) > 0;
                if (!isDst && !g.usable(nx, ny, l, net)) continue;
                float step = 1.0f;
                if (d != 8 && d != k) step += 0.6f;
                if (l == TOP && !single) step += 0.02f;  // slight preference for bottom layer
                int ns = sid(nx, ny, l, k);
                if (c + step < cost[(size_t)ns]) {
                    cost[(size_t)ns] = c + step;
                    from[(size_t)ns] = s;
                    pq.push({c + step + heur(nx, ny), ns});
                }
            }
            // via
            if (!single) {
                int ol = 1 - l;
                bool ok = g.usable(x, y, ol, net);
                for (int k = 0; k < 8 && ok; k += 2)
                    if (!g.inside(x + DX[k], y + DY[k]) || !g.usable(x + DX[k], y + DY[k], 0, net) || !g.usable(x + DX[k], y + DY[k], 1, net)) ok = false;
                if (ok && !srcCells.count(g.idx(x, y, 0)) && !dstCells.count(g.idx(x, y, 0))) {
                    int ns = sid(x, y, ol, 8);
                    float nc = c + 12;
                    if (nc < cost[(size_t)ns]) {
                        cost[(size_t)ns] = nc;
                        from[(size_t)ns] = s;
                        pq.push({nc + heur(x, y), ns});
                    }
                }
            }
        }
        if (goal < 0) { failed.insert({rl.pa, rl.pb}); res.failed++; continue; }

        // --- reconstruct ---
        struct Step { int x, y, l; };
        std::vector<Step> path;
        for (int s = goal; s >= 0; s = from[(size_t)s]) {
            int rest = s / 9;
            path.push_back({rest % g.w, (rest / g.w) % g.h, rest / (g.w * g.h)});
        }
        std::reverse(path.begin(), path.end());
        PinRef srcPin = srcCells[g.idx(path.front().x, path.front().y, 0)];
        PinRef dstPin = dstCells[g.idx(path.back().x, path.back().y, 0)];

        // split into per-layer runs
        std::vector<std::vector<Step>> runs(1);
        for (size_t i = 0; i < path.size(); i++) {
            if (!runs.back().empty() && runs.back().back().l != path[i].l) runs.emplace_back();
            runs.back().push_back(path[i]);
        }
        WireEnd prevEnd{srcPin.part, srcPin.pin};
        const PartInst* sp = doc.part(srcPin.part);
        const PartInst* dp = doc.part(dstPin.part);
        Vec2 srcPos = sp->pinPos(srcPin.pin, PCB), dstPos = dp->pinPos(dstPin.pin, PCB);
        Vec2 srcEsc = cellEscape[g.idx(path.front().x, path.front().y, 0)], dstEsc = cellEscape[g.idx(path.back().x, path.back().y, 0)];
        Vec2 dstPosCopy = dstPos;
        // a trace onto a fine-pitch SMD pad is no wider than the pad
        float runW = tw;
        for (auto pr : {std::make_pair(sp, srcPin.pin), std::make_pair(dp, dstPin.pin)}) {
            const PinDef& pd = pr.first->def->pins[(size_t)pr.second];
            if (pd.smd()) { Vec2 sz = pd.padSize(); runW = std::min(runW, std::min(sz.x, sz.y)); }
        }
        std::vector<Wire> newWires;
        for (size_t r = 0; r < runs.size(); r++) {
            auto& run = runs[r];
            std::vector<Vec2> pts;
            for (size_t i = 0; i < run.size(); i++) {
                Vec2 p = g.pos(run[i].x, run[i].y);
                if (pts.size() >= 2) {
                    Vec2 d1 = normalize(pts.back() - pts[pts.size() - 2]), d2 = normalize(p - pts.back());
                    if (std::fabs(cross(d1, d2)) < 1e-4f && dot(d1, d2) > 0) { pts.back() = p; continue; }
                }
                if (!pts.empty() && pts.back() == p) continue;
                pts.push_back(p);
            }
            if (r == 0 && !pts.empty()) {
                pts.front() = srcEsc;
                if (srcEsc != srcPos) pts.insert(pts.begin(), srcPos);
            }
            bool last = r + 1 == runs.size();
            WireEnd endRef;
            if (last) {
                if (!pts.empty()) {
                    pts.back() = dstEsc;
                    if (dstEsc != dstPosCopy) pts.push_back(dstPosCopy);
                }
                endRef = {dstPin.part, dstPin.pin};
            } else {
                // via at the end of this run
                Vec2 vp = pts.empty() ? g.pos(run.back().x, run.back().y) : pts.back();
                PartInst& via = doc.addPart("via", {}, vp);
                via.label = doc.nextLabel("AV");
                endRef = {via.id, 0};
            }
            if (pts.size() == 1) pts.push_back(pts[0]);
            if (pts.size() >= 2 && !(pts.size() == 2 && pts[0] == pts[1])) {
                Wire w;
                w.id = doc.newId();
                w.view = PCB;
                w.pts = pts;
                w.ends[0] = prevEnd;
                w.ends[1] = endRef;
                w.layer = run.front().l;
                w.width = (r == 0 || last) ? runW : tw;
                w.autorouted = true;
                newWires.push_back(w);
            }
            prevEnd = endRef;
        }
        for (auto& w : newWires) doc.wires.push_back(w);
        doc.syncWireEnds();
        doc.touch();
        res.routed++;
    }
    conn.build(doc);
    RoutingStatus st;
    conn.ratsnest(doc, PCB, &st);
    if (st.connectionsToRoute == 0) res.message = "Routing completed: " + std::to_string(res.routed) + " connection(s) routed.";
    else res.message = "Routed " + std::to_string(res.routed) + " connection(s); " + std::to_string(st.connectionsToRoute) +
                       " could not be routed. Try moving parts apart, a bigger board, or thinner traces.";
    return res;
}
