// designElectronics — tidy wiring: re-routes a wire as horizontal and vertical runs
// on the view's grid (A* with a turn penalty), around part bodies and clear of
// other nets' pins and wires, so circuits read like a neatly laid-out board.
#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>

#include "tools.h"

namespace {

float pitchFor(int view) { return view == PCB ? 25.f : 100.f; }

// Per-cell occupancy by other wires: which nets run through it horizontally / vertically.
struct Occ {
    int hNet = -1, vNet = -1;  // -1 none, -2 several nets
    bool corner = false;        // another wire bends or ends here
    float pcbBlock = 0;         // PCB: copper of another net within clearance
    float body = 0;             // cost of crossing a part body
    bool pinOther = false;      // another net's pin sits here
};

struct Search {
    Vec2 origin;
    float pitch;
    int x0, y0, w, h;  // cell window
    std::vector<Occ> occ;
    bool in(int x, int y) const { return x >= x0 && y >= y0 && x < x0 + w && y < y0 + h; }
    Occ& at(int x, int y) { return occ[(size_t)((y - y0) * w + (x - x0))]; }
    Vec2 pos(int x, int y) const { return origin + Vec2((float)x * pitch, (float)y * pitch); }
    int cx(float wx) const { return (int)std::lround((wx - origin.x) / pitch); }
    int cy(float wy) const { return (int)std::lround((wy - origin.y) / pitch); }
};

void addNet(int& slot, int net) { slot = slot == -1 || slot == net ? net : -2; }

const int DX[4] = {1, 0, -1, 0};
const int DY[4] = {0, 1, 0, -1};

struct Node {
    float f;
    int state;
    bool operator<(const Node& o) const { return f > o.f; }
};

bool routeOne(Document& doc, const Connectivity& conn, Wire& wr) {
    const int view = wr.view;
    const PartInst* pa = doc.part(wr.ends[0].part);
    const PartInst* pb = doc.part(wr.ends[1].part);
    if (!pa || !pb) return false;
    Vec2 a = pa->pinPos(wr.ends[0].pin, view), b = pb->pinPos(wr.ends[1].pin, view);
    int net = conn.netOf(pa->id, wr.ends[0].pin);

    Search S;
    S.pitch = pitchFor(view);
    S.origin = a;
    int bx = S.cx(b.x), by = S.cy(b.y);
    int margin = view == PCB ? 40 : 12;
    S.x0 = std::min(0, bx) - margin;
    S.y0 = std::min(0, by) - margin;
    S.w = std::abs(bx) + 2 * margin + 1;
    S.h = std::abs(by) + 2 * margin + 1;
    if ((long)S.w * S.h > 400000) return false;
    S.occ.assign((size_t)(S.w * S.h), Occ());

    const DesignRules& R = doc.rules;
    float tw = view == PCB ? wr.width : 0;
    auto cells = [&](Rect r, auto fn) {
        int xa = std::max(S.x0, (int)std::floor((r.x - S.origin.x) / S.pitch)), xb = std::min(S.x0 + S.w - 1, (int)std::ceil((r.r() - S.origin.x) / S.pitch));
        int ya = std::max(S.y0, (int)std::floor((r.y - S.origin.y) / S.pitch)), yb = std::min(S.y0 + S.h - 1, (int)std::ceil((r.b() - S.origin.y) / S.pitch));
        for (int y = ya; y <= yb; y++)
            for (int x = xa; x <= xb; x++) fn(x, y);
    };

    // part bodies and other nets' pins
    for (auto& p : doc.parts) {
        if (!p.inView(view) || p.def->isBreadboard || p.def->isBoard) continue;
        if (view != PCB) {
            Rect bnd = p.worldBounds(view);
            bool mine = p.id == pa->id || p.id == pb->id;
            Rect in(bnd.x + 10, bnd.y + 10, bnd.w - 20, bnd.h - 20);  // pins sit on the outline
            cells(in, [&](int x, int y) {
                if (in.contains(S.pos(x, y))) S.at(x, y).body = std::max(S.at(x, y).body, mine && view == BB ? 6.f : 25.f);  // breadboard pins sit under the body edge
            });
        }
        for (size_t i = 0; i < p.def->pins.size(); i++) {
            int pn = conn.netOf(p.id, (int)i);
            if (pn == net && net >= 0) continue;
            Vec2 c = p.pinPos((int)i, view);
            if (view == PCB) {
                Vec2 sz = p.def->pins[i].padSize(p.place[PCB]);
                float r = std::max(sz.x, sz.y) / 2 + R.clearance + tw / 2;
                cells(Rect(c.x - r, c.y - r, 2 * r, 2 * r), [&](int x, int y) {
                    Vec2 q = S.pos(x, y);
                    if (std::fabs(q.x - c.x) < sz.x / 2 + R.clearance + tw / 2 && std::fabs(q.y - c.y) < sz.y / 2 + R.clearance + tw / 2) S.at(x, y).pcbBlock = 1;
                });
            } else {
                int x = S.cx(c.x), y = S.cy(c.y);
                if (S.in(x, y) && dist(S.pos(x, y), c) < 20) S.at(x, y).pinOther = true;
            }
        }
    }
    // other wires
    for (auto& o : doc.wires) {
        if (o.view != view || o.id == wr.id || o.pts.size() < 2) continue;
        int on = conn.wireNet(o.id);
        if (view == PCB) {
            if (o.layer != wr.layer || (on == net && net >= 0)) continue;
            float r = o.width / 2 + R.clearance + tw / 2;
            for (size_t i = 0; i + 1 < o.pts.size(); i++) {
                Vec2 p = o.pts[i], q = o.pts[i + 1];
                cells(Rect::fromPoints(p, q).inset(-r), [&](int x, int y) {
                    if (distPointSeg(S.pos(x, y), p, q) < r) S.at(x, y).pcbBlock = 1;
                });
            }
            continue;
        }
        for (size_t i = 0; i + 1 < o.pts.size(); i++) {
            Vec2 p = o.pts[i], q = o.pts[i + 1];
            bool horiz = std::fabs(p.y - q.y) < 1, vert = std::fabs(p.x - q.x) < 1;
            float len = dist(p, q);
            int n = std::max(1, (int)std::ceil(len / (S.pitch / 2)));
            for (int k = 0; k <= n; k++) {
                Vec2 t = p + (q - p) * ((float)k / (float)n);
                int x = S.cx(t.x), y = S.cy(t.y);
                if (!S.in(x, y) || dist(S.pos(x, y), t) > S.pitch * 0.35f) continue;
                Occ& c = S.at(x, y);
                if (horiz || !vert) addNet(c.hNet, on);
                if (vert || !horiz) addNet(c.vNet, on);
            }
        }
        for (size_t i = 0; i < o.pts.size(); i++) {
            int x = S.cx(o.pts[i].x), y = S.cy(o.pts[i].y);
            if (S.in(x, y)) S.at(x, y).corner = true;
        }
    }

    // A* over (cell, heading); heading 4 = none yet
    auto sid = [&](int x, int y, int d) { return (((y - S.y0) * S.w + (x - S.x0)) * 5) + d; };
    size_t nst = (size_t)S.w * S.h * 5;
    std::vector<float> cost(nst, 1e30f);
    std::vector<int> from(nst, -1);
    std::priority_queue<Node> pq;
    auto heur = [&](int x, int y) { return (float)(std::abs(bx - x) + std::abs(by - y)); };
    cost[(size_t)sid(0, 0, 4)] = 0;
    pq.push({heur(0, 0), sid(0, 0, 4)});
    const float turn = view == PCB ? 2.f : 4.f;
    int goal = -1, expanded = 0;
    auto otherNet = [&](int slot) { return slot != -1 && !(slot == net && net >= 0); };
    while (!pq.empty() && expanded < 300000) {
        Node nd = pq.top();
        pq.pop();
        int s = nd.state, d = s % 5, cell = s / 5;
        int x = cell % S.w + S.x0, y = cell / S.w + S.y0;
        float c = cost[(size_t)s];
        if (nd.f - heur(x, y) > c + 1e-3f) continue;
        expanded++;
        if (x == bx && y == by) { goal = s; break; }
        for (int k = 0; k < 4; k++) {
            if (d != 4 && k == (d + 2) % 4) continue;  // no U-turns
            int nx = x + DX[k], ny = y + DY[k];
            if (!S.in(nx, ny)) continue;
            bool atGoal = nx == bx && ny == by;
            Occ& o = S.at(nx, ny);
            bool horiz = k % 2 == 0;
            float step = 1;
            if (d != 4 && d != k) {
                step += turn;
                // bending on top of another net's wire would look like a junction
                Occ& here = S.at(x, y);
                if (otherNet(here.hNet) || otherNet(here.vNet)) step += view == BB ? 15 : 1e6f;
            }
            if (view == PCB) {
                if (o.pcbBlock > 0 && !atGoal) continue;
            } else {
                if (o.pinOther && !atGoal) continue;
                step += o.body;
                int along = horiz ? o.hNet : o.vNet, across = horiz ? o.vNet : o.hNet;
                if (otherNet(along)) step += view == BB ? 15 : 1e6f;       // running on top of another net
                else if (along != -1) step += 3;                            // same net: avoid doubling up
                if (otherNet(across)) step += view == BB ? 1.5f : 1.f;     // crossing is fine, just not free
                if (o.corner && !atGoal) step += view == BB ? 4 : 1e6f;    // through another wire's end/bend
            }
            if (step >= 1e6f) continue;
            int ns = sid(nx, ny, k);
            if (c + step < cost[(size_t)ns]) {
                cost[(size_t)ns] = c + step;
                from[(size_t)ns] = s;
                pq.push({c + step + heur(nx, ny), ns});
            }
        }
    }
    if (goal < 0) return false;

    std::vector<Vec2> cellsPath;
    for (int s = goal; s >= 0; s = from[(size_t)s]) {
        int cell = s / 5;
        cellsPath.push_back(S.pos(cell % S.w + S.x0, cell / S.w + S.y0));
    }
    std::reverse(cellsPath.begin(), cellsPath.end());
    // the end pin may sit off the grid: finish with a right-angle jog onto it
    if (dist(cellsPath.back(), b) > 0.5f) {
        Vec2 last = cellsPath.back();
        if (std::fabs(last.x - b.x) > 0.5f && std::fabs(last.y - b.y) > 0.5f) cellsPath.push_back(Vec2(b.x, last.y));
        cellsPath.push_back(b);
    }
    cellsPath.front() = a;
    std::vector<Vec2> pts;
    for (Vec2 p : cellsPath) {
        if (!pts.empty() && dist(pts.back(), p) < 0.5f) continue;
        if (pts.size() >= 2) {
            Vec2 d1 = pts.back() - pts[pts.size() - 2], d2 = p - pts.back();
            if (std::fabs(cross(d1, d2)) < 1e-3f && dot(d1, d2) > 0) { pts.back() = p; continue; }
        }
        pts.push_back(p);
    }
    if (pts.size() < 2) pts = {a, b};
    wr.pts = pts;
    return true;
}

}  // namespace

bool tidyWire(Document& doc, int wireId) {
    Wire* w = doc.wire(wireId);
    if (!w || !w->ends[0].attached() || !w->ends[1].attached()) return false;
    Connectivity conn;
    conn.build(doc);
    w->tidy = true;
    return routeOne(doc, conn, *w);
}

int tidyWires(Document& doc, int view, const std::set<int>* only) {
    Connectivity conn;
    conn.build(doc);
    // shortest first, so short hops take the direct lanes and longer runs go around them
    std::vector<std::pair<float, int>> order;
    for (auto& w : doc.wires) {
        if (w.view != view || !w.ends[0].attached() || !w.ends[1].attached()) continue;
        if (only && !only->count(w.id)) continue;
        order.push_back({dist(w.pts.front(), w.pts.back()), w.id});
    }
    std::sort(order.begin(), order.end());
    int n = 0;
    for (auto& o : order) {
        Wire* w = doc.wire(o.second);
        w->tidy = true;
        if (routeOne(doc, conn, *w)) n++;
    }
    return n;
}
