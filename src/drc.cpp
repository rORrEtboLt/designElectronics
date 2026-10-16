// Design rule check for the PCB view: copper clearance, parts off-board,
// overlapping footprints and unrouted connections.
#include <cstdio>

#include "tools.h"

namespace {
struct Copper {
    Vec2 a, b;
    float r;      // half width / pad radius
    int layer;    // 0 top, 1 bottom, 2 both
    int net;
    std::string what;
};
}  // namespace

std::vector<DrcIssue> runDrc(const Document& doc, const Connectivity& conn) {
    std::vector<DrcIssue> out;
    float clr = doc.rules.clearance;
    std::vector<Copper> cu;
    Rect board;
    bool hasBoard = false;
    for (auto& p : doc.parts) {
        if (!p.inView(PCB)) continue;
        if (p.def->isBoard) { board = hasBoard ? board.united(p.worldBounds(PCB)) : p.worldBounds(PCB); hasBoard = true; continue; }
        for (size_t i = 0; i < p.def->pins.size(); i++) {
            Vec2 c = p.pinPos((int)i, PCB);
            cu.push_back({c, c, p.def->pins[i].pad / 2, 2, conn.netOf(p.id, (int)i), p.label + " pin " + p.def->pins[i].name});
        }
        for (auto& h : p.def->holes) {
            Vec2 c = p.place[PCB].apply(h.first);
            cu.push_back({c, c, h.second / 2, 2, -100 - p.id, p.label + " hole"});
        }
    }
    for (auto& w : doc.wires) {
        if (w.view != PCB) continue;
        int net = conn.wireNet(w.id);
        for (size_t i = 0; i + 1 < w.pts.size(); i++)
            cu.push_back({w.pts[i], w.pts[i + 1], w.width / 2, w.layer, net, std::string(w.layer == TOP ? "top" : "bottom") + " trace"});
    }

    char buf[256];
    for (size_t i = 0; i < cu.size(); i++) {
        for (size_t j = i + 1; j < cu.size(); j++) {
            const Copper &A = cu[i], &B = cu[j];
            if (A.net == B.net && A.net >= 0) continue;
            if (A.layer != 2 && B.layer != 2 && A.layer != B.layer) continue;
            float d = distSegSeg(A.a, A.b, B.a, B.b) - A.r - B.r;
            if (d < clr - 0.5f) {
                Vec2 pa = closestOnSeg(B.a, A.a, A.b), pb = closestOnSeg(pa, B.a, B.b);
                snprintf(buf, sizeof buf, "Too close: %s and %s (%.1f mil apart, minimum %.0f mil)", A.what.c_str(), B.what.c_str(), std::max(0.f, d), clr);
                out.push_back({(pa + pb) / 2, std::max(30.f, A.r + 10), buf});
                if (out.size() > 200) return out;
            }
        }
    }
    if (hasBoard) {
        for (auto& p : doc.parts) {
            if (!p.inView(PCB) || p.def->isBoard) continue;
            Rect r = p.worldBounds(PCB);
            if (r.x < board.x || r.y < board.y || r.r() > board.r() || r.b() > board.b()) {
                snprintf(buf, sizeof buf, "%s (%s) is not completely on the board", p.label.c_str(), p.def->title.c_str());
                out.push_back({r.center(), std::max(r.w, r.h) / 2, buf});
            }
        }
    } else {
        out.push_back({{0, 0}, 0, "There is no PCB board in the PCB view. Add one from the parts bin (PCB category)."});
    }
    // overlapping footprints
    for (size_t i = 0; i < doc.parts.size(); i++) {
        const PartInst& a = doc.parts[i];
        if (!a.inView(PCB) || a.def->isBoard || a.type == "via" || a.def->sim == "board") continue;  // shield outlines contain parts by design
        Rect ra = a.worldBounds(PCB).inset(10);
        for (size_t j = i + 1; j < doc.parts.size(); j++) {
            const PartInst& b = doc.parts[j];
            if (!b.inView(PCB) || b.def->isBoard || b.type == "via" || b.def->sim == "board") continue;
            Rect rb = b.worldBounds(PCB).inset(10);
            if (ra.overlaps(rb)) {
                snprintf(buf, sizeof buf, "Overlapping parts: %s and %s", a.label.c_str(), b.label.c_str());
                Rect u(std::max(ra.x, rb.x), std::max(ra.y, rb.y), 0, 0);
                u.w = std::min(ra.r(), rb.r()) - u.x;
                u.h = std::min(ra.b(), rb.b()) - u.y;
                out.push_back({u.center(), std::max(40.f, std::min(u.w, u.h) / 2), buf});
            }
        }
    }
    RoutingStatus st;
    conn.ratsnest(doc, PCB, &st);
    if (st.connectionsToRoute > 0) {
        snprintf(buf, sizeof buf, "%d connection(s) still to be routed", st.connectionsToRoute);
        out.push_back({{0, 0}, 0, buf});
    }
    return out;
}
