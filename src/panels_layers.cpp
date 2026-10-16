// designElectronics — the Layers panel: the view's drawing layers (eye toggles)
// and a Photoshop-style list of everything in the design, with parts plugged
// into a breadboard grouped under it. Rows select, double-click zooms, and each
// part can be hidden in this view or locked.
#include <algorithm>
#include <cctype>

#include "app.h"

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

const char* layerName(int l, int v) {
    switch (l) {
        case LY_PARTS: return "Parts";
        case LY_WIRES: return v == BB ? "Wires" : "Wires";
        case LY_LABELS: return "Labels";
        case LY_NOTES: return "Notes";
        case LY_RATSNEST: return "Ratsnest";
        case LY_BOARD: return "Board";
        case LY_COPPER_BOTTOM: return "Bottom copper";
        case LY_COPPER_TOP: return "Top copper";
        case LY_PADS: return "Pads";
        case LY_SILK: return "Silkscreen";
        default: return "?";
    }
}

std::vector<int> viewLayers(int v) {
    if (v == BB) return {LY_PARTS, LY_WIRES, LY_NOTES, LY_RATSNEST};
    if (v == SCH) return {LY_PARTS, LY_WIRES, LY_LABELS, LY_NOTES, LY_RATSNEST};
    return {LY_BOARD, LY_COPPER_TOP, LY_COPPER_BOTTOM, LY_PADS, LY_SILK, LY_LABELS, LY_NOTES, LY_RATSNEST};
}

}  // namespace

// Flat icon toggle used in the list rows (no button frame).
static bool iconToggle(UI& ui, const std::string& id, Rect r, const std::string& icon, uint32_t col, const std::string& tip) {
    bool clicked = ui.button(id, r, "", BTN_FLAT);
    if (!icon.empty()) ui.drawIcon(icon, r.inset(r.w * 0.16f), col);
    ui.tooltip(r, tip);
    return clicked;
}

void App::setPartHidden(int partId, int v, bool hide) {
    PartInst* p = doc.part(partId);
    if (!p || p->hiddenIn(v) == hide) return;
    checkpoint();
    if (hide) { p->hidden |= 1u << v; selParts.erase(partId); }
    else p->hidden &= ~(1u << v);
    changed();
}

void App::setPartLocked(int partId, bool lock) {
    PartInst* p = doc.part(partId);
    if (!p || p->locked == lock) return;
    checkpoint();
    p->locked = lock;
    changed();
}

void App::zoomToPart(int partId) {
    const PartInst* p = doc.part(partId);
    if (!p || !p->inView(view)) return;
    Rect b = p->worldBounds(view);
    Camera& c = cam[view];
    // zoom out if the part doesn't fit, never zoom in past 1:1-ish
    float fit = std::min(rCanvas.w / std::max(1.f, b.w * 1.6f), rCanvas.h / std::max(1.f, b.h * 1.6f));
    if (fit < c.zoom) c.zoom = fit;
    c.pan = rCanvas.center() - b.center() * c.zoom;
}

void App::drawLayers(Rect r) {
    float s = ui.s;
    auto& th = ui.th;
    g.fillRect(r, th.panel);
    g.fillRect({r.x, r.y, std::max(1.f, s), r.h}, th.border);
    float pad = 10 * s;
    float y = r.y + pad;
    if (compact) {
        g.text({r.x + pad + 2 * s, y + 11 * s}, "Layers", 16 * s, th.text, 0);
        Rect cb(r.r() - 40 * s, y - 4 * s, 32 * s, 32 * s);
        if (ui.iconButton("closelayers", cb, "close", "")) drawerLayers = false;
    } else {
        float w0 = g.textWidth("Parts", 15 * s) + 24 * s, w1 = g.textWidth("Layers", 15 * s) + 24 * s;
        if (ui.button("rtab-parts-l", Rect(r.x + pad - 6 * s, y - 6 * s, w0, 30 * s), "Parts", BTN_TAB, false)) rightTab = 0;
        ui.button("rtab-layers-l", Rect(r.x + pad - 6 * s + w0, y - 6 * s, w1, 30 * s), "Layers", BTN_TAB, true);
    }
    y += 32 * s;

    // ---- view layers ----
    g.text({r.x + pad, y + 8 * s}, std::string(viewName(view)) + " layers", 12 * s, th.textDim, 0);
    y += 20 * s;
    auto ls = viewLayers(view);
    float colW = (r.w - 2 * pad) / 2;
    for (size_t i = 0; i < ls.size(); i++) {
        int l = ls[i];
        Rect cell(r.x + pad + (float)(i % 2) * colW, y + (float)(i / 2) * 30.f * s, colW - 4 * s, 28 * s);
        bool on = layerShown(view, l);
        if (ui.button("ly-" + std::to_string(view) + "-" + std::to_string(l), cell, "", BTN_FLAT)) layerOff[view][l] = on;
        ui.drawIcon(on ? "eye" : "eyeoff", Rect(cell.x + 4 * s, cell.center().y - 9 * s, 18 * s, 18 * s), on ? th.text : th.textDim);
        g.text({cell.x + 28 * s, cell.center().y}, layerName(l, view), 13.5f * s, on ? th.text : th.textDim, 0);
    }
    y += (float)((ls.size() + 1) / 2) * 30.f * s + 8 * s;
    g.fillRect({r.x + pad, y, r.w - 2 * pad, std::max(1.f, s)}, th.border);
    y += 10 * s;

    // ---- object list ----
    ui.textField("layerfilter", Rect(r.x + pad, y, r.w - 2 * pad, 32 * s), layerFilter, "Filter (R1, LED, GPIO…)", true);
    y += 40 * s;
    Rect list(r.x, y, r.w, r.b() - y - pad);

    ensureConn();
    std::set<int> unrouted;
    for (auto& rl : conn.ratsnest(doc, view)) { unrouted.insert(rl.pa.part); unrouted.insert(rl.pb.part); }

    // Build rows: front-most first. In the breadboard view, plugged-in parts nest under their breadboard.
    struct Row { int kind; int id; int depth; };  // kind 0 part, 1 wires group, 2 notes group
    std::vector<Row> rows;
    std::string f = lower(layerFilter);
    auto matches = [&](const PartInst& p) {
        if (f.empty()) return true;
        std::string hay = lower(p.label + " " + p.def->title + " " + p.type + " " + (p.def->valueProp.empty() ? "" : p.prop(p.def->valueProp)));
        for (auto& pin : p.def->pins) if (lower(pin.name).find(f) != std::string::npos) return true;
        return hay.find(f) != std::string::npos;
    };
    std::map<int, int> parentOf;  // part id -> breadboard id
    if (view == BB && f.empty()) {
        for (auto& bb : doc.parts) {
            if (!bb.def->isBreadboard || !bb.inView(BB)) continue;
            for (auto& p : doc.parts) {
                if (p.def->isBreadboard || !p.inView(BB) || parentOf.count(p.id)) continue;
                for (size_t k = 0; k < p.def->pins.size() && !parentOf.count(p.id); k++)
                    for (size_t hidx = 0; hidx < bb.def->pins.size(); hidx++)
                        if (dist(p.pinPos((int)k, BB), bb.pinPos((int)hidx, BB)) < 15) { parentOf[p.id] = bb.id; break; }
            }
        }
    }
    for (int i = (int)doc.parts.size() - 1; i >= 0; i--) {
        const PartInst& p = doc.parts[(size_t)i];
        if (!p.inView(view) || parentOf.count(p.id) || !matches(p)) continue;
        rows.push_back({0, p.id, 0});
        if (p.def->isBreadboard && !layerCollapsed.count(p.id))
            for (int j = (int)doc.parts.size() - 1; j >= 0; j--) {
                auto it = parentOf.find(doc.parts[(size_t)j].id);
                if (it != parentOf.end() && it->second == p.id) rows.push_back({0, doc.parts[(size_t)j].id, 1});
            }
    }
    int nWires = 0, nNotes = 0;
    for (auto& w : doc.wires) if (w.view == view) nWires++;
    for (auto& n : doc.notes) if (n.view == view) nNotes++;
    if (f.empty() && nWires) rows.push_back({1, -1, 0});
    if (f.empty() && nNotes) rows.push_back({2, -2, 0});

    const float rowH = 40 * s;
    if (rows.empty()) {
        ui.wrappedText(list.inset(pad), f.empty() ? "Nothing in this view yet." : "Nothing matches “" + layerFilter + "”.", th.textDim, 13 * s);
        return;
    }
    float oy = ui.beginScroll("layers-list", list, (float)rows.size() * rowH);
    Uint64 now = SDL_GetTicks();
    for (size_t i = 0; i < rows.size(); i++) {
        const Row& row = rows[i];
        Rect rr(list.x + 4 * s, list.y + oy + (float)i * rowH, list.w - 8 * s, rowH - 2 * s);
        if (rr.b() < list.y || rr.y > list.b()) continue;
        float x = rr.x + 8 * s + (float)row.depth * 18 * s;
        float right = rr.r() - 6 * s;
        if (row.kind != 0) {
            bool wiresGroup = row.kind == 1;
            std::string id = wiresGroup ? "lw" : "ln";
            int layer = wiresGroup ? (view == PCB ? LY_COPPER_TOP : LY_WIRES) : LY_NOTES;
            bool on = view == PCB && wiresGroup ? (layerShown(PCB, LY_COPPER_TOP) || layerShown(PCB, LY_COPPER_BOTTOM)) : layerShown(view, layer);
            if (ui.button("lrow-" + id, rr, "", BTN_FLAT)) {
                // select everything in the group
                selParts.clear(); selWires.clear(); selNotes.clear();
                if (wiresGroup) { for (auto& w : doc.wires) if (w.view == view && wireShown(w)) selWires.insert(w.id); }
                else for (auto& n : doc.notes) if (n.view == view) selNotes.insert(n.id);
            }
            Rect eye(right - 26 * s, rr.center().y - 13 * s, 26 * s, 26 * s);
            if (iconToggle(ui, "leye-" + id, eye, on ? "eye" : "eyeoff", on ? th.text : th.textDim, on ? "Hide" : "Show")) {
                if (view == PCB && wiresGroup) layerOff[PCB][LY_COPPER_TOP] = layerOff[PCB][LY_COPPER_BOTTOM] = on;
                else layerOff[view][layer] = on;
            }
            ui.drawIcon(wiresGroup ? "wire" : "note", Rect(x, rr.center().y - 11 * s, 22 * s, 22 * s), on ? th.textDim : withAlpha(th.textDim, 120));
            std::string t = (wiresGroup ? (view == PCB ? "Traces (" : "Wires (") : "Notes (") + std::to_string(wiresGroup ? nWires : nNotes) + ")";
            g.text({x + 32 * s, rr.center().y}, t, 14 * s, on ? th.text : th.textDim, 0);
            continue;
        }
        const PartInst* p = doc.part(row.id);
        if (!p) continue;
        bool sel = selParts.count(p->id) > 0;
        bool hidden = p->hiddenIn(view);
        if (sel) g.fillRoundRect(rr, 6 * s, withAlpha(th.accent, 40));
        std::string rid = std::to_string(p->id);
        if (ui.button("lrow-" + rid, rr, "", BTN_FLAT, sel)) {
            bool dbl = lastLayerClickPart == p->id && now - lastLayerClick < 400;
            lastLayerClick = now;
            lastLayerClickPart = p->id;
            if (in.shift()) { if (sel) selParts.erase(p->id); else selParts.insert(p->id); }
            else { selParts = {p->id}; selWires.clear(); selNotes.clear(); }
            if (hidden) selParts.erase(p->id);
            if (dbl) zoomToPart(p->id);
        }
        // eye + lock (drawn after the row so they take the click)
        Rect eye(right - 26 * s, rr.center().y - 13 * s, 26 * s, 26 * s);
        Rect lock(right - 56 * s, rr.center().y - 13 * s, 26 * s, 26 * s);
        if (iconToggle(ui, "leye-" + rid, eye, hidden ? "eyeoff" : "eye", hidden ? th.textDim : th.text,
                       hidden ? std::string("Show in ") + viewName(view) : std::string("Hide in ") + viewName(view)))
            setPartHidden(p->id, view, !hidden);
        bool rowHot = ui.hovered(rr);
        std::string lockIcon = p->locked ? "lock" : rowHot ? "unlock" : "";
        if (iconToggle(ui, "llock-" + rid, lock, lockIcon, p->locked ? th.text : th.textDim, p->locked ? "Unlock" : "Lock (can't be moved or deleted)"))
            setPartLocked(p->id, !p->locked);
        // collapse toggle for breadboards that hold parts
        if (p->def->isBreadboard && view == BB && f.empty()) {
            bool any = false;
            for (auto& kv : parentOf) if (kv.second == p->id) any = true;
            if (any) {
                bool coll = layerCollapsed.count(p->id) > 0;
                Rect ch(x - 6 * s, rr.center().y - 10 * s, 20 * s, 20 * s);
                if (ui.iconButton("lcol-" + rid, ch, coll ? "chevright" : "chevron", "", false, true, coll ? "Expand" : "Collapse")) {
                    if (coll) layerCollapsed.erase(p->id);
                    else layerCollapsed.insert(p->id);
                }
                x += 16 * s;
            }
        }
        uint32_t alpha = hidden ? 110 : 255;
        Rect icon(x, rr.y + 4 * s, rowH - 10 * s, rowH - 10 * s);
        g.fillRoundRect(icon, 5 * s, rgba(255, 255, 255, alpha));
        drawPartIcon(p->def, icon.inset(2 * s));
        if (hidden) g.fillRoundRect(icon, 5 * s, withAlpha(th.panel, 140));
        float tx = icon.r() + 8 * s;
        // badges (right to left, before the lock button)
        float bx = lock.x - 6 * s;
        auto badge = [&](uint32_t col, const std::string& tip, bool ring = false) {
            Rect b(bx - 12 * s, rr.center().y - 6 * s, 12 * s, 12 * s);
            if (ring) g.strokeCircle(b.center(), 5 * s, 1.5f * s, col);
            else g.fillCircle(b.center(), 5 * s, col);
            ui.tooltip(b.inset(-4 * s), tip);
            bx -= 16 * s;
        };
        if (simOn && sim.damaged.count(p->id)) badge(th.danger, "Damaged in simulation");
        auto br = sim.brightness.find(p->id);
        if (simOn && br != sim.brightness.end()) {
            uint32_t lc = p->def->sim == "led" ? ledDisplayColor(p->prop("color")) : th.ok;
            badge(withAlpha(lc, (int)(60 + 195 * std::clamp(br->second, 0.f, 1.f))), "Brightness " + std::to_string((int)std::lround(br->second * 100)) + "%");
        }
        if (unrouted.count(p->id)) badge(rgba(235, 140, 30), std::string("Has unrouted connections in ") + viewName(view));
        if (p->def->sim.empty() && !p->def->isBreadboard && !p->def->isBoard && p->def->netProp.empty() && p->type != "via" && p->type != "hole" &&
            p->type.rfind("header", 0) != 0)
            badge(th.textDim, "Not simulated (pinout only)", true);
        // label + value, clipped to the space left
        float tw = bx - tx;
        std::string l1 = p->label;
        std::string l2 = p->def->title;
        if (!p->def->valueProp.empty() && p->def->valueProp != "pins") l2 = p->prop(p->def->valueProp) + " · " + l2;
        auto clip = [&](std::string t, float px) {
            while (t.size() > 3 && g.textWidth(t, px) > tw) { t.resize(t.size() - 4); t += "…"; }
            return t;
        };
        g.text({tx, rr.y + 13 * s}, clip(l1, 14 * s), 14 * s, hidden ? th.textDim : th.text, 0);
        g.text({tx, rr.y + 29 * s}, clip(l2, 11.5f * s), 11.5f * s, th.textDim, 0);
    }
    ui.endScroll();
}
