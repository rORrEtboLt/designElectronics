// Panels: top bar + menus, parts bin, inspector, bottom toolbar, canvas overlays.
#include <cmath>
#include <cstdio>

#include "app.h"
#include "logo.h"

namespace {
MenuItem item(const std::string& l, std::function<void()> a, const std::string& sc = "", bool enabled = true, bool checked = false) {
    MenuItem m;
    m.label = l;
    m.action = std::move(a);
    m.shortcut = sc;
    m.enabled = enabled;
    m.checked = checked;
    return m;
}
MenuItem sub(const std::string& l, std::vector<MenuItem> children) {
    MenuItem m;
    m.label = l;
    m.children = std::move(children);
    return m;
}
std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
#if defined(__APPLE__)
const char* kCmd = "⌘";
#else
const char* kCmd = "Ctrl+";
#endif
std::string sc(const char* k) { return std::string(kCmd) + k; }
}  // namespace

// ---------------------------------------------------------------------------
// Menus
// ---------------------------------------------------------------------------

std::vector<MenuItem> App::fileMenu() {
    std::vector<MenuItem> ex;
    for (size_t i = 0; i < examples().size(); i++) ex.push_back(item(examples()[i].title, [this, i]() { loadExample(i); }));
    std::vector<MenuItem> exp = {
        item("Video or GIF for social media…", [this]() { videoView = view; openVideoSheet(); }),
        MenuItem::sep(),
        item("Gerber files for PCB manufacturing (.zip)", [this]() { exportFile("gerber"); }),
        item("SVG image of this view", [this]() { exportFile("svg"); }),
        item("PNG image of this view", [this]() { exportFile("png"); }),
        MenuItem::sep(),
        item("Bill of Materials (.csv)", [this]() { exportFile("bom"); }),
        item("Netlist (.xml)", [this]() { exportFile("netlist"); }),
        item("SPICE netlist (.cir)", [this]() { exportFile("spice"); }),
    };
    std::vector<MenuItem> m = {
        item("New", [this]() { newDoc(); }, sc("N")),
        item("Open…", [this]() { openDialog(); }, sc("O")),
        sub("Examples", ex),
        MenuItem::sep(),
        item("Save", [this]() { save(); }, sc("S")),
        item("Save As / Share…", [this]() { saveAs(); }, sc("⇧S")),
        item("Embed in a Website…", [this]() { openEmbedSheet(); }),
        MenuItem::sep(),
        item("Version History…", [this]() { openHistory(); }, sc("⇧H")),
        item("Commit to GitHub…", [this]() { openCommitSheet(); }, sc("⇧K")),
        item(githubConnected() ? "GitHub Account (@" + gh.login + ")…" : "Connect GitHub…", [this]() { openGitHubAccount(); }),
        MenuItem::sep(),
        sub("Export", exp),
        item("Import Part (.dezpart)…", [this]() { importPartDialog(); }),
    };
#if !defined(SDL_PLATFORM_IOS) && !defined(SDL_PLATFORM_ANDROID)
    m.push_back(MenuItem::sep());
    m.push_back(item("Quit", [this]() {
        SDL_Event e;
        e.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&e);
    }, sc("Q")));
#endif
    return m;
}

std::vector<MenuItem> App::editMenu() {
    bool any = !selParts.empty() || !selWires.empty() || !selNotes.empty();
    return {
        item("Undo", [this]() { undo(); }, sc("Z"), !undoStack.empty()),
        item("Redo", [this]() { redo(); }, sc("⇧Z"), !redoStack.empty()),
        MenuItem::sep(),
        item("Cut", [this]() { copySelection(true); }, sc("X"), any),
        item("Copy", [this]() { copySelection(false); }, sc("C"), any),
        item("Paste", [this]() { paste(); }, sc("V")),
        item("Duplicate", [this]() { duplicate(); }, sc("D"), any),
        item("Delete", [this]() { deleteSelection(); }, "Del", any),
        MenuItem::sep(),
        item("Select All", [this]() { selectAll(); }, sc("A")),
        item("Add Note", [this]() { addNote(); }),
    };
}

std::vector<MenuItem> App::partMenu() {
    bool any = !selParts.empty();
    return {
        item("Rotate 90° Clockwise", [this]() { rotateSelection(1); }, "R", any),
        item("Rotate 90° Counter-clockwise", [this]() { rotateSelection(-1); }, "⇧R", any),
        item("Flip Horizontal", [this]() { flipSelection(); }, "F", any),
        MenuItem::sep(),
        item("Bring to Front", [this]() { bringToFront(true); }, "", any),
        item("Send to Back", [this]() { bringToFront(false); }, "", any),
        MenuItem::sep(),
        item("Export Part Definition…", [this]() { exportFile("part"); }, "", selParts.size() == 1),
        item("Import Part…", [this]() { importPartDialog(); }),
    };
}

std::vector<MenuItem> App::viewMenu() {
    auto scaleItem = [this](const char* l, float v) { return item(l, [this, v]() { userScale = v; }, "", true, std::fabs(userScale - v) < 0.01f); };
    return {
        item("Breadboard View", [this]() { setView(BB); }, sc("1"), true, view == BB),
        item("Schematic View", [this]() { setView(SCH); }, sc("2"), true, view == SCH),
        item("PCB View", [this]() { setView(PCB); }, sc("3"), true, view == PCB),
        MenuItem::sep(),
        item("Zoom In", [this]() { zoomAt(1.25f, rCanvas.center()); }, "+"),
        item("Zoom Out", [this]() { zoomAt(0.8f, rCanvas.center()); }, "−"),
        item("Fit in Window", [this]() { fitView(view, rCanvas); }, "0"),
        MenuItem::sep(),
        item("Show Grid", [this]() { showGrid = !showGrid; }, "", true, showGrid),
        item("Snap to Grid", [this]() { snapToGrid = !snapToGrid; }, "", true, snapToGrid),
        item("Show Ratsnest Lines", [this]() { showRatsnest = !showRatsnest; }, "", true, showRatsnest),
        item("Show Part Labels", [this]() { showPartLabels = !showPartLabels; }, "", true, showPartLabels),
        MenuItem::sep(),
        sub("Interface Size", {scaleItem("Small", 0.85f), scaleItem("Normal", 1.0f), scaleItem("Large", 1.2f), scaleItem("Extra Large", 1.45f)}),
    };
}

std::vector<MenuItem> App::routingMenu() {
    std::vector<MenuItem> widths;
    const std::pair<const char*, float> ws[] = {{"Extra thin (8 mil)", 8}, {"Thin (12 mil)", 12}, {"Standard (24 mil)", 24}, {"Thick (32 mil)", 32}, {"Extra thick (48 mil)", 48}};
    for (auto& w : ws) {
        float v = w.second;
        widths.push_back(item(w.first, [this, v]() { doc.rules.traceWidth = v; changed(); }, "", true, std::fabs(doc.rules.traceWidth - v) < 0.1f));
    }
    return {
        item("Autoroute PCB", [this]() { setView(PCB); runAutoroute(); }),
        item("Delete Autorouted Traces", [this]() {
            checkpoint();
            doc.wires.erase(std::remove_if(doc.wires.begin(), doc.wires.end(), [](const Wire& w) { return w.view == PCB && w.autorouted; }), doc.wires.end());
            changed();
        }),
        item("Design Rules Check (DRC)", [this]() { setView(PCB); runDrc(); }),
        MenuItem::sep(),
        sub("Active Layer", {item("Top", [this]() { activeLayer = TOP; }, "", true, activeLayer == TOP),
                             item("Bottom", [this]() { activeLayer = BOTTOM; }, "", true, activeLayer == BOTTOM)}),
        sub("Trace Width", widths),
        MenuItem::sep(),
        item(simOn ? "Stop Simulator" : "Start Simulator", [this]() { toggleSim(); }),
    };
}

std::vector<MenuItem> App::helpMenu() {
    return {
        item("Keyboard & Touch Shortcuts", [this]() { showShortcuts(); }),
        item("About the .dez File Format", [this]() {
            ui.dialog("The .dez file format",
                      {"A .dez file is plain UTF-8 JSON. It contains the parts (with their position in each view), wires and traces, notes, design rules — and a \"library\" section that embeds the full definition of every part used.",
                       "",
                       "That makes a project completely self-contained: send it by email, chat, AirDrop or USB stick and it opens in any designElectronics app, offline, with no server and no extra downloads.",
                       "",
                       "Custom parts can be shared on their own as .dezpart files (File ▸ Import Part). See docs/FILE_FORMAT.md for the full specification."},
                      {});
        }),
        item("About designElectronics", [this]() { showAbout(); }),
    };
}

std::vector<MenuItem> App::allMenus() {
    return {sub("File", fileMenu()), sub("Edit", editMenu()), sub("Part", partMenu()), sub("View", viewMenu()), sub("Routing & Simulation", routingMenu()), sub("Help", helpMenu())};
}

// ---------------------------------------------------------------------------
// Top bar
// ---------------------------------------------------------------------------

void App::drawTopBar() {
    float s = ui.s;
    Rect r = rTop;
    g.fillRect(Rect(0, 0, (float)winW, r.b()), ui.th.panel2);
    g.fillRect({0, r.b() - std::max(1.f, s), (float)winW, std::max(1.f, s)}, ui.th.border);
    float x = r.x + 8 * s;
    float h = 36 * s, y = r.y + (r.h - h) / 2;
    if (compact) {
        Rect mb(x, y, h, h);
        if (ui.iconButton("menu", mb, "menu", "", false, true, "Menu")) ui.openMenu(mb, allMenus());
        x += h + 6 * s;
    } else {
        // logo mark
        Vec2 c(x + 14 * s, r.center().y);
        drawLogo(g, Rect(c.x - 15 * s, c.y - 15 * s, 30 * s, 30 * s));
        x += 34 * s;
        const char* names[] = {"File", "Edit", "Part", "View", "Routing", "Help"};
        for (int i = 0; i < 6; i++) {
            float w = g.textWidth(names[i], 14 * s) + 20 * s;
            Rect b(x, y, w, h);
            if (ui.button(std::string("mnu") + names[i], b, names[i], BTN_FLAT)) {
                std::vector<MenuItem> m = i == 0 ? fileMenu() : i == 1 ? editMenu() : i == 2 ? partMenu() : i == 3 ? viewMenu() : i == 4 ? routingMenu() : helpMenu();
                ui.openMenu(b, m);
            }
            x += w;
        }
    }
    // view tabs (segmented control)
    const char* full[] = {"Breadboard", "Schematic", "PCB"};
    const char* shortN[] = {"Bread", "Schem", "PCB"};
    float rightW = compact ? 2 * h + 8 * s : 2 * h + 230 * s;
    float avail = r.r() - rightW - x - 8 * s;
    float tabW = std::min(130 * s, avail / 3);
    bool useShort = tabW < 98 * s;
    float tx = compact ? x + std::max(0.f, (avail - tabW * 3) / 2) : std::max(x + 12 * s, r.center().x - tabW * 1.5f);
    if (!compact && tx + tabW * 3 > r.r() - rightW) tx = x + 12 * s;
    Rect seg(tx - 3 * s, y - 2 * s, tabW * 3 + 6 * s, h + 4 * s);
    g.fillRoundRect(seg, 9 * s, rgba(214, 214, 218));
    for (int v = 0; v < 3; v++) {
        Rect b(tx + tabW * v, y + 1 * s, tabW, h - 2 * s);
        bool selected = view == v;
        if (selected) g.fillRoundRect(b, 7 * s, rgba(255, 255, 255));
        if (ui.button(std::string("tab") + viewKey(v), b, "", BTN_FLAT)) setView(v);
        g.text(b.center(), useShort ? shortN[v] : full[v], 14 * s, selected ? ui.th.text : ui.th.textDim, 1);
    }
    // right side: title + undo/redo
    float rx = r.r() - 8 * s;
    Rect redoR(rx - h, y, h, h);
    Rect undoR(rx - 2 * h - 4 * s, y, h, h);
    if (ui.iconButton("redo", redoR, "redo", "", false, !redoStack.empty(), "Redo")) redo();
    if (ui.iconButton("undo", undoR, "undo", "", false, !undoStack.empty(), "Undo")) undo();
    if (!compact) {
        std::string t = doc.title + (dirty() ? " •" : "");
        float maxW = 210 * s;
        while (t.size() > 4 && g.textWidth(t, 13.5f * s) > maxW) t = t.substr(0, t.size() - 4) + "…";
        g.text({undoR.x - 10 * s, r.center().y}, t, 13.5f * s, ui.th.textDim, 2);
    }
}

// ---------------------------------------------------------------------------
// Parts bin
// ---------------------------------------------------------------------------

void App::drawPartsBin(Rect r) {
    float s = ui.s;
    g.fillRect(r, ui.th.panel);
    g.fillRect({r.x, r.y, std::max(1.f, s), r.h}, ui.th.border);
    float pad = 10 * s;
    float y = r.y + pad;
    if (compact) {
        g.text({r.x + pad + 2 * s, y + 11 * s}, "Parts", 16 * s, ui.th.text, 0);
        Rect cb(r.r() - 40 * s, y - 4 * s, 32 * s, 32 * s);
        if (ui.iconButton("closeparts", cb, "close", "")) drawerParts = false;
    } else {
        drawRightTabs(r, y);
    }
    y += 30 * s;
    Rect sf(r.x + pad, y, r.w - 2 * pad, 34 * s);
    ui.textField("partsearch", sf, search, "Search parts…", true);
    y += 42 * s;

    // category dropdown (the catalog has too many categories for chips)
    std::vector<std::string> cats = {"All parts"};
    for (auto& c : partCategories()) cats.push_back(c);
    if (!customEntries.empty()) cats.push_back("Custom");
    std::string cur = category == -1 ? "All parts" : category == -2 ? "Custom" : partCategories()[(size_t)category];
    if (ui.comboField("catdd", Rect(r.x + pad, y, r.w - 2 * pad, 34 * s), cur, cats, false)) {
        category = -1;
        if (cur == "Custom") category = -2;
        for (size_t i = 0; i < partCategories().size(); i++) if (partCategories()[i] == cur) category = (int)i;
    }
    y += 44 * s;

    // entries
    std::vector<LibEntry> all = partLibrary();
    all.insert(all.end(), customEntries.begin(), customEntries.end());
    std::vector<int> shown;
    std::string q = lower(search);
    for (size_t i = 0; i < all.size(); i++) {
        const LibEntry& e = all[i];
        bool isCustom = i >= partLibrary().size();
        if (category == -2 && !isCustom) continue;
        if (category >= 0 && e.category != partCategories()[(size_t)category]) continue;
        if (!q.empty() && lower(e.title).find(q) == std::string::npos && lower(e.type).find(q) == std::string::npos && lower(e.category).find(q) == std::string::npos) continue;
        shown.push_back((int)i);
    }
    Rect area(r.x, y, r.w, r.b() - y);
    float cellW = 88 * s, cellH = 100 * s;
    int cols = std::max(2, (int)((area.w - pad) / cellW));
    cellW = (area.w - pad * 2) / (float)cols;
    int rows = ((int)shown.size() + cols - 1) / cols;
    float contentH = rows * cellH + pad;
    float oy = ui.beginScroll("bin", area, contentH);
    for (size_t k = 0; k < shown.size(); k++) {
        int idx = shown[k];
        const LibEntry& e = all[(size_t)idx];
        Rect cell(area.x + pad + (float)(k % cols) * cellW, area.y + oy + (float)(k / cols) * cellH, cellW - 4 * s, cellH - 4 * s);
        if (cell.b() < area.y || cell.y > area.b()) continue;
        std::string id = "bin" + std::to_string(idx);
        bool clicked = ui.button(id, cell, "", BTN_FLAT);
        PartDefPtr def = getPartDef(e.type, e.props);
        Rect ir(cell.x + 10 * s, cell.y + 6 * s, cell.w - 20 * s, cell.h - 44 * s);
        if (def) drawPartIcon(def, ir);
        auto lines = wrapText(g, e.title, 11.5f * s, cell.w - 6 * s);
        for (size_t li = 0; li < lines.size() && li < 2; li++)
            g.text({cell.center().x, cell.b() - 30 * s + (float)li * 14 * s}, lines[li], 11.5f * s, ui.th.text, 1);
        ui.tooltip(cell, def ? def->title + " — " + def->description : e.title);
        // start dragging onto the canvas (mouse); touch uses tap-to-add
        if (!in.touch && in.down && binDrag < 0 && cell.contains(in.pressPos) && area.contains(in.pressPos) && dist(in.mouse, in.pressPos) > 6 * s && !ui.blocking())
            binDrag = idx;
        if (clicked && binDrag < 0) {
            Vec2 at = toWorld(rCanvas.center());
            addPart(e, at);
            if (compact) drawerParts = false;
            ui.toast("Added " + (def ? def->title : e.title));
        }
    }
    ui.endScroll();
    if (shown.empty()) g.text({area.center().x, area.y + 30 * s}, "No parts match \"" + search + "\"", 13 * s, ui.th.textDim, 1);

    if (binDrag >= 0 && in.released) {
        if (rCanvas.contains(in.mouse) && !(compact && rRight.contains(in.mouse)) && binDrag < (int)all.size()) addPart(all[(size_t)binDrag], toWorld(in.mouse));
        binDrag = -1;
    }
}

// ---------------------------------------------------------------------------
// Inspector
// ---------------------------------------------------------------------------

static bool editableProp(const PropDef& pd) {
    static const char* keys[] = {"resistance", "capacitance", "inductance", "voltage", "frequency", "net", "outputs", "width", "height", "part", "wiper", "light", "chip label", "pin names", "pins", "diameter"};
    if (pd.options.empty()) return true;
    for (auto* k : keys) if (pd.key == k) return true;
    return false;
}

void App::drawInspector(Rect r) {
    float s = ui.s;
    g.fillRect(r, ui.th.panel);
    g.fillRect({r.x, r.y, std::max(1.f, s), r.h}, ui.th.border);
    g.fillRect({r.x, r.y, r.w, std::max(1.f, s)}, ui.th.border);
    float pad = 12 * s;
    g.text({r.x + pad, r.y + 22 * s}, "Inspector", 16 * s, ui.th.text, 0);
    if (compact) {
        Rect cb(r.r() - 40 * s, r.y + 6 * s, 32 * s, 32 * s);
        if (ui.iconButton("closeinsp", cb, "close", "")) drawerInspector = false;
    }
    Rect area(r.x, r.y + 42 * s, r.w, r.h - 42 * s);
    static float lastH = 400;
    float oy = ui.beginScroll("inspector", area, lastH);
    float x = area.x + pad, w = area.w - 2 * pad;
    float y = area.y + oy + 4 * s;
    float rowH = 34 * s;
    auto heading = [&](const std::string& t) {
        y += 8 * s;
        g.text({x, y + 9 * s}, t, 12 * s, ui.th.textDim, 0);
        y += 22 * s;
    };
    auto label = [&](const std::string& t) {
        g.text({x, y + rowH / 2}, t, 13 * s, ui.th.textDim, 0);
    };
    float lw = std::min(110 * s, w * 0.38f);

    size_t nsel = selParts.size() + selWires.size() + selNotes.size();
    if (nsel == 1 && selParts.size() == 1) {
        PartInst* p = doc.part(*selParts.begin());
        if (p) {
            int pid = p->id;
            Rect icon(x, y, 72 * s, 72 * s);
            g.fillRoundRect(icon, 8 * s, rgba(255, 255, 255));
            g.strokeRoundRect(icon, 8 * s, std::max(1.f, s), ui.th.border);
            drawPartIcon(p->def, icon.inset(6 * s));
            g.text({x + 84 * s, y + 18 * s}, p->def->title, 16 * s, ui.th.text, 0);
            g.text({x + 84 * s, y + 42 * s}, p->label + "  ·  " + p->def->category, 12.5f * s, ui.th.textDim, 0);
            y += 82 * s;
            float dh = ui.wrappedText(Rect(x, y, w, 0), p->def->description, ui.th.textDim, 12.5f * s);
            y += dh + 6 * s;

            heading("PROPERTIES");
            label("Label");
            std::string lbl = p->label;
            if (ui.textField("lbl" + std::to_string(pid), Rect(x + lw, y, w - lw, rowH - 4 * s), lbl) && lbl != p->label && !lbl.empty()) {
                checkpoint();
                if (PartInst* pp = doc.part(pid)) pp->label = lbl;
                changed();
            }
            y += rowH + 2 * s;
            Props merged = mergedProps(p->type, p->props);
            PartDefPtr def = p->def;  // keep alive while editing
            for (auto& pd : def->propDefs) {
                label(pd.label);
                std::string val = merged[pd.key];
                if (ui.comboField("prop" + std::to_string(pid) + pd.key, Rect(x + lw, y, w - lw, rowH - 4 * s), val, pd.options, editableProp(pd)))
                    setPartProp(pid, pd.key, val);
                y += rowH + 2 * s;
            }
            p = doc.part(pid);
            if (!p) { ui.endScroll(); return; }

            if (p->def->sim == "board" || p->def->sim == "mcu") {
                heading("PROGRAM");
                std::string info;
                if (p->code.empty()) info = "Drives the pins listed in Pin outputs. Write an Arduino sketch to program it instead.";
                else {
                    int lines = 1;
                    for (char ch : p->code) lines += ch == '\n';
                    info = std::to_string(lines) + " lines · Arduino sketch · runs when you Simulate";
                }
                y += ui.wrappedText(Rect(x, y, w, 0), info, ui.th.textDim, 12.5f * s) + 6 * s;
                float bw = p->code.empty() ? w : (w - 8 * s) * 0.62f;
                if (ui.button("prog-edit", Rect(x, y, bw, rowH), p->code.empty() ? "Write a program…" : "Edit program…", BTN_PRIMARY)) openCode(pid);
                if (!p->code.empty() && ui.button("prog-up", Rect(x + bw + 8 * s, y, w - bw - 8 * s, rowH), "Upload…", BTN_NORMAL)) openUploadSheet(pid);
                y += rowH + 8 * s;
                p = doc.part(pid);
                if (!p) { ui.endScroll(); return; }
            }

            heading(std::string("PLACEMENT · ") + viewName(view));
            if (p->inView(view)) {
                char buf[96];
                Vec2 pos = p->place[view].pos;
                snprintf(buf, sizeof buf, "x %.1f mm   y %.1f mm   %d°%s", pos.x * 0.0254, pos.y * 0.0254, p->place[view].rot * 90, p->place[view].flip ? "  flipped" : "");
                g.text({x, y + 10 * s}, buf, 12.5f * s, ui.th.text, 0, FONT_MONO);
                y += 26 * s;
                float bw = (w - 12 * s) / 3;
                if (ui.button("rotl", Rect(x, y, bw, rowH), "⟲ Rotate", BTN_NORMAL, false, !p->def->isBoard)) rotateSelection(-1);
                if (ui.button("rotr", Rect(x + bw + 6 * s, y, bw, rowH), "Rotate ⟳", BTN_NORMAL, false, !p->def->isBoard)) rotateSelection(1);
                if (ui.button("flip", Rect(x + 2 * (bw + 6 * s), y, bw, rowH), "Flip", BTN_NORMAL, false, !p->def->isBoard)) flipSelection();
                y += rowH + 6 * s;
            } else {
                y += ui.wrappedText(Rect(x, y, w, 0), std::string("This part is not shown in the ") + viewName(view) + " view.", ui.th.textDim, 12.5f * s) + 6 * s;
            }

            heading("CONNECTIONS");
            ensureConn();
            int connected = 0;
            for (size_t i = 0; i < p->def->pins.size(); i++) if (conn.pinConnectedInView(view, p->id, (int)i)) connected++;
            char buf[128];
            snprintf(buf, sizeof buf, "%zu pins · %d connected in this view", p->def->pins.size(), connected);
            g.text({x, y + 10 * s}, buf, 12.5f * s, ui.th.text, 0);
            y += 26 * s;
            if (!p->def->isBreadboard && p->def->pins.size() <= 40) {
                for (size_t i = 0; i < p->def->pins.size(); i++) {
                    int net = conn.netOf(p->id, (int)i);
                    std::string line = p->def->pins[i].name + "  →  " + conn.netName(net);
                    if (simOn && net >= 0 && net < (int)sim.netVoltage.size()) {
                        snprintf(buf, sizeof buf, "  (%.2f V)", sim.netVoltage[(size_t)net]);
                        line += buf;
                    }
                    g.text({x + 6 * s, y + 9 * s}, line, 12 * s, conn.pinConnectedInView(view, p->id, (int)i) ? ui.th.text : ui.th.textDim, 0);
                    y += 19 * s;
                }
            }
            y += 8 * s;
            if (ui.button("delpart", Rect(x, y, w, rowH), "Delete Part", BTN_NORMAL)) deleteSelection();
            y += rowH + 12 * s;
        }
    } else if (nsel == 1 && selWires.size() == 1) {
        Wire* wr = doc.wire(*selWires.begin());
        if (wr) {
            int wid = wr->id;
            g.text({x, y + 14 * s}, view == PCB ? "Trace" : "Wire", 16 * s, ui.th.text, 0);
            y += 34 * s;
            float length = 0;
            for (size_t i = 0; i + 1 < wr->pts.size(); i++) length += dist(wr->pts[i], wr->pts[i + 1]);
            char buf[128];
            ensureConn();
            snprintf(buf, sizeof buf, "Length %.1f mm · %zu bend point(s) · net %s", length * 0.0254, wr->pts.size() - 2, conn.netName(conn.wireNet(wid)).c_str());
            y += ui.wrappedText(Rect(x, y, w, 0), buf, ui.th.textDim, 12.5f * s) + 6 * s;
            for (int e = 0; e < 2; e++) {
                const WireEnd& end = wr->ends[e];
                std::string t = e == 0 ? "From: " : "To: ";
                if (end.attached()) {
                    const PartInst* p = doc.part(end.part);
                    t += p ? p->label + " · " + p->def->pins[(size_t)end.pin].name : "?";
                } else t += "(not attached)";
                g.text({x, y + 9 * s}, t, 12.5f * s, end.attached() ? ui.th.text : ui.th.danger, 0);
                y += 20 * s;
            }
            if (wr->view == BB) {
                heading("COLOR");
                const uint32_t cs[] = {rgba(65, 140, 225), rgba(220, 50, 50), rgba(40, 40, 40), rgba(240, 200, 40), rgba(40, 170, 80),
                                       rgba(245, 140, 30), rgba(240, 240, 240), rgba(150, 150, 150), rgba(140, 70, 190)};
                float sw = 30 * s;
                float cx = x;
                for (int i = 0; i < 9; i++) {
                    if (cx + sw > x + w) { cx = x; y += sw + 6 * s; }
                    Rect b(cx, y, sw, sw);
                    bool cur = wr->color == cs[i];
                    if (ui.button("wcol" + std::to_string(i), b, "", BTN_FLAT)) {
                        checkpoint();
                        if (Wire* ww = doc.wire(wid)) ww->color = cs[i];
                        changed();
                    }
                    g.fillCircle(b.center(), sw * 0.38f, cs[i]);
                    g.strokeCircle(b.center(), sw * 0.38f, std::max(1.f, s), rgba(0, 0, 0, 60));
                    if (cur) g.strokeCircle(b.center(), sw * 0.48f, 2 * s, ui.th.accent);
                    cx += sw + 6 * s;
                }
                y += sw + 8 * s;
            } else if (wr->view == PCB) {
                heading("LAYER");
                float bw = (w - 6 * s) / 2;
                if (ui.button("ltop", Rect(x, y, bw, rowH), "Top", BTN_NORMAL, wr->layer == TOP)) { checkpoint(); doc.wire(wid)->layer = TOP; changed(); }
                if (ui.button("lbot", Rect(x + bw + 6 * s, y, bw, rowH), "Bottom", BTN_NORMAL, wr->layer == BOTTOM)) { checkpoint(); doc.wire(wid)->layer = BOTTOM; changed(); }
                y += rowH + 6 * s;
                wr = doc.wire(wid);
                heading("WIDTH");
                const float ws[] = {8, 12, 16, 24, 32, 48};
                float cx = x;
                for (float tw : ws) {
                    std::string lbl = std::to_string((int)tw) + " mil";
                    float bwid = g.textWidth(lbl, 12.5f * s) + 18 * s;
                    if (cx + bwid > x + w) { cx = x; y += 32 * s; }
                    if (ui.button("tw" + lbl, Rect(cx, y, bwid, 26 * s), lbl, BTN_CHIP, std::fabs(wr->width - tw) < 0.1f)) {
                        checkpoint();
                        doc.wire(wid)->width = tw;
                        changed();
                    }
                    cx += bwid + 6 * s;
                }
                y += 36 * s;
            }
            if (ui.button("straight", Rect(x, y, w, rowH), "Straighten (remove bends)", BTN_NORMAL)) {
                checkpoint();
                if (Wire* ww = doc.wire(wid)) ww->pts = {ww->pts.front(), ww->pts.back()};
                changed();
            }
            y += rowH + 6 * s;
            if (ui.button("delwire", Rect(x, y, w, rowH), view == PCB ? "Delete Trace" : "Delete Wire", BTN_NORMAL)) deleteSelection();
            y += rowH + 12 * s;
        }
    } else if (nsel == 1 && selNotes.size() == 1) {
        Note* n = doc.note(*selNotes.begin());
        if (n) {
            int nid = n->id;
            g.text({x, y + 14 * s}, "Note", 16 * s, ui.th.text, 0);
            y += 34 * s;
            std::string t = n->text;
            if (ui.textField("note" + std::to_string(nid), Rect(x, y, w, rowH), t, "Note text")) {
                checkpoint();
                if (Note* nn = doc.note(nid)) nn->text = t;
                changed();
            }
            y += rowH + 6 * s;
            y += ui.wrappedText(Rect(x, y, w, 0), "Tip: type \\n for a line break. Drag the bottom-right corner on the canvas to resize.", ui.th.textDim, 12 * s) + 6 * s;
            if (ui.button("delnote", Rect(x, y, w, rowH), "Delete Note", BTN_NORMAL)) deleteSelection();
            y += rowH + 12 * s;
            if (Note* nn = doc.note(nid)) {
                size_t pos;
                while ((pos = nn->text.find("\\n")) != std::string::npos) nn->text.replace(pos, 2, "\n");
            }
        }
    } else if (nsel > 1) {
        g.text({x, y + 14 * s}, std::to_string(nsel) + " items selected", 16 * s, ui.th.text, 0);
        y += 40 * s;
        float bw = (w - 12 * s) / 3;
        if (ui.button("mrotl", Rect(x, y, bw, rowH), "⟲ Rotate")) rotateSelection(-1);
        if (ui.button("mrotr", Rect(x + bw + 6 * s, y, bw, rowH), "Rotate ⟳")) rotateSelection(1);
        if (ui.button("mflip", Rect(x + 2 * (bw + 6 * s), y, bw, rowH), "Flip")) flipSelection();
        y += rowH + 6 * s;
        if (ui.button("mdup", Rect(x, y, w, rowH), "Duplicate")) duplicate();
        y += rowH + 6 * s;
        if (ui.button("mdel", Rect(x, y, w, rowH), "Delete Selection")) deleteSelection();
        y += rowH + 12 * s;
    } else {
        heading("PROJECT");
        label("Title");
        std::string t = doc.title;
        if (ui.textField("ptitle", Rect(x + lw, y, w - lw, rowH - 4 * s), t) && t != doc.title) { checkpoint(); doc.title = t; changed(); }
        y += rowH + 2 * s;
        label("Author");
        std::string a = doc.author;
        if (ui.textField("pauthor", Rect(x + lw, y, w - lw, rowH - 4 * s), a, "Your name") && a != doc.author) { checkpoint(); doc.author = a; changed(); }
        y += rowH + 2 * s;
        label("Notes");
        std::string nt = doc.notesText;
        if (ui.textField("pnotes", Rect(x + lw, y, w - lw, rowH - 4 * s), nt, "Description") && nt != doc.notesText) { checkpoint(); doc.notesText = nt; changed(); }
        y += rowH + 2 * s;
        if (!doc.notesText.empty()) y += ui.wrappedText(Rect(x, y, w, 0), doc.notesText, ui.th.textDim, 12.5f * s) + 4 * s;

        ensureConn();
        heading("STATUS");
        RoutingStatus st;
        conn.ratsnest(doc, view, &st);
        int nparts = 0, nwires = 0;
        for (auto& p : doc.parts) if (p.inView(view)) nparts++;
        for (auto& wr : doc.wires) if (wr.view == view) nwires++;
        char buf[160];
        snprintf(buf, sizeof buf, "%d parts and %d %s in this view", nparts, nwires, view == PCB ? "traces" : "wires");
        g.text({x, y + 9 * s}, buf, 12.5f * s, ui.th.text, 0);
        y += 20 * s;
        if (st.connectionsToRoute == 0) snprintf(buf, sizeof buf, st.nets ? "Routing completed (%d nets)" : "No connections yet", st.nets);
        else snprintf(buf, sizeof buf, "%d of %d nets routed · %d connection(s) still to be routed", st.routedNets, st.nets, st.connectionsToRoute);
        y += ui.wrappedText(Rect(x, y, w, 0), buf, st.connectionsToRoute ? ui.th.danger : ui.th.ok, 12.5f * s) + 2 * s;
        std::string fileLine = filePath.empty() ? "Not saved yet" : filePath;
        y += ui.wrappedText(Rect(x, y, w, 0), fileLine + (dirty() ? " — unsaved changes" : ""), ui.th.textDim, 12 * s) + 4 * s;

        if (view == PCB) {
            heading("DESIGN RULES");
            label("Trace width");
            std::string tw = std::to_string((int)doc.rules.traceWidth);
            if (ui.comboField("rtw", Rect(x + lw, y, w - lw, rowH - 4 * s), tw, {"8", "12", "16", "24", "32", "48"}, true)) {
                checkpoint();
                doc.rules.traceWidth = std::clamp((float)std::atof(tw.c_str()), 4.f, 200.f);
                changed();
            }
            y += rowH + 2 * s;
            label("Clearance");
            std::string cl = std::to_string((int)doc.rules.clearance);
            if (ui.comboField("rcl", Rect(x + lw, y, w - lw, rowH - 4 * s), cl, {"6", "8", "10", "12", "16", "20"}, true)) {
                checkpoint();
                doc.rules.clearance = std::clamp((float)std::atof(cl.c_str()), 2.f, 100.f);
                changed();
            }
            y += rowH + 2 * s;
            y += ui.wrappedText(Rect(x, y, w, 0), "Values in mils (1/1000 inch).", ui.th.textDim, 12 * s) + 4 * s;
        }
        heading("TIPS");
        const char* tips = view == BB
            ? "Drag parts onto the breadboard — their legs snap into the holes, and holes in use turn green. Drag from any hole or pin to draw a wire."
            : view == SCH
            ? "Connections you made on the breadboard appear here as dashed ratsnest lines. Click one to turn it into a wire, or drag from pin to pin."
            : "Arrange the footprints on the board, then press Autoroute (or click ratsnest lines to make traces). Run DRC before exporting Gerbers.";
        y += ui.wrappedText(Rect(x, y, w, 0), tips, ui.th.textDim, 12.5f * s) + 8 * s;
    }
    lastH = y - (area.y + oy) + 20 * s;
    ui.endScroll();
}

// ---------------------------------------------------------------------------
// Bottom toolbar
// ---------------------------------------------------------------------------

void App::drawToolbar() {
    float s = ui.s;
    Rect r = rBottom;
    g.fillRect(Rect(0, r.y, (float)winW, (float)winH - r.y), ui.th.panel2);
    g.fillRect({0, r.y, (float)winW, std::max(1.f, s)}, ui.th.border);
    struct Tool { std::string id, icon, label; std::function<void()> fn; bool sel; bool enabled; std::string tip; };
    std::vector<Tool> tools;
    bool anySel = !selParts.empty() || !selWires.empty() || !selNotes.empty();
    if (compact) {
        tools.push_back({"t-parts", "parts", "Parts", [this]() { drawerParts = !drawerParts; drawerInspector = drawerLayers = false; }, drawerParts, true, "Parts bin"});
        tools.push_back({"t-layers", "layer", "Layers", [this]() { drawerLayers = !drawerLayers; drawerInspector = drawerParts = drawerAI = false; }, drawerLayers, true, "Layers"});
        if (agentsSupported())
            tools.push_back({"t-ai", "bolt", "AI", [this]() { drawerAI = !drawerAI; drawerInspector = drawerParts = drawerLayers = false; }, drawerAI, true, "AI assistant"});
        tools.push_back({"t-insp", "inspector", "Inspect", [this]() { drawerInspector = !drawerInspector; drawerParts = drawerLayers = false; }, drawerInspector, true, "Inspector"});
    }
    if (!compact || view != PCB) tools.push_back({"t-note", "note", "Note", [this]() { addNote(); }, false, true, "Add a note"});
    tools.push_back({"t-rot", "rotate", "Rotate", [this]() { rotateSelection(1); }, false, !selParts.empty(), "Rotate 90° (R)"});
    if (!compact) tools.push_back({"t-flip", "flip", "Flip", [this]() { flipSelection(); }, false, !selParts.empty(), "Flip horizontal (F)"});
    tools.push_back({"t-del", "delete", "Delete", [this]() { deleteSelection(); }, false, anySel, "Delete selection"});
    if (view == PCB) {
        tools.push_back({"t-layer", "layer", activeLayer == TOP ? "Top" : "Bottom", [this]() { activeLayer = 1 - activeLayer; }, false, true,
                         "Active copper layer for new traces (click to switch)"});
        tools.push_back({"t-route", "route", "Autoroute", [this]() { runAutoroute(); }, false, true, "Route all connections automatically"});
        tools.push_back({"t-drc", "drc", "DRC", [this]() { runDrc(); }, false, true, "Design rules check"});
    } else {
        tools.push_back({"t-sim", simOn ? "stop" : "play", simOn ? "Stop" : "Simulate", [this]() { toggleSim(); }, simOn, true, "Run the circuit simulator"});
    }
    tools.push_back({"t-share", "share", compact ? "Share" : "Export", [this]() {}, false, true, "Save, share and export"});

    float bw = compact ? std::min(66 * s, r.w / (float)tools.size()) : 70 * s;
    float bh = r.h - 8 * s;
    float x = r.x + (compact ? (r.w - bw * (float)tools.size()) / 2 : 10 * s);
    for (auto& t : tools) {
        Rect b(x, r.y + 4 * s, bw, bh);
        if (ui.iconButton(t.id, b, t.icon, t.label, t.sel, t.enabled, t.tip)) {
            if (t.id == "t-share") {
                std::vector<MenuItem> m = {
                    item("Save / Share Project File (.dez)…", [this]() { saveAs(); }),
                    MenuItem::sep(),
                    item("Gerber for PCB Manufacturing (.zip)", [this]() { exportFile("gerber"); }),
                    item("SVG Image of This View", [this]() { exportFile("svg"); }),
                    item("PNG Image of This View", [this]() { exportFile("png"); }),
                    item("Bill of Materials (.csv)", [this]() { exportFile("bom"); }),
                    item("Netlist (.xml)", [this]() { exportFile("netlist"); }),
                    item("SPICE Netlist (.cir)", [this]() { exportFile("spice"); }),
                };
                ui.openMenu(b, m, true);
            } else if (t.fn) t.fn();
        }
        x += bw;
    }
    if (!compact) {
        // zoom controls on the right
        float zx = r.r() - 10 * s;
        float h = 34 * s, y = r.center().y - h / 2;
        Rect fit(zx - h, y, h, h);
        if (ui.iconButton("z-fit", fit, "fit", "", false, true, "Fit in window (0)")) fitView(view, rCanvas);
        Rect zin(fit.x - h - 2 * s, y, h, h);
        if (ui.iconButton("z-in", zin, "zoomin", "", false, true, "Zoom in")) zoomAt(1.25f, rCanvas.center());
        char buf[32];
        snprintf(buf, sizeof buf, "%d%%", (int)std::lround(cam[view].zoom / 0.1f * 100));
        float tw = 56 * s;
        g.text({zin.x - tw / 2 - 2 * s, r.center().y}, buf, 13 * s, ui.th.textDim, 1, FONT_MONO);
        Rect zout(zin.x - tw - h - 4 * s, y, h, h);
        if (ui.iconButton("z-out", zout, "zoomout", "", false, true, "Zoom out")) zoomAt(0.8f, rCanvas.center());
        // routing status in the middle (Fritzing-style)
        ensureConn();
        RoutingStatus st;
        conn.ratsnest(doc, view, &st);
        std::string msg;
        uint32_t col = ui.th.textDim;
        if (st.nets == 0) msg = "";
        else if (st.connectionsToRoute == 0) { msg = "Routing completed"; col = ui.th.ok; }
        else {
            msg = std::to_string(st.routedNets) + " of " + std::to_string(st.nets) + " nets routed - " + std::to_string(st.connectionsToRoute) +
                  " connection" + (st.connectionsToRoute == 1 ? "" : "s") + " still to be routed";
            col = ui.th.danger;
        }
        float left = x + 10 * s, right = zout.x - 10 * s;
        if (!msg.empty() && right - left > 120 * s) {
            while (msg.size() > 8 && g.textWidth(msg, 13 * s) > right - left) msg = msg.substr(0, msg.size() - 2);
            g.text({(left + right) / 2, r.center().y}, msg, 13 * s, col, 1);
        }
    }
}

// ---------------------------------------------------------------------------
// Canvas overlays (status pills, hints)
// ---------------------------------------------------------------------------

void App::drawCanvasOverlay() {
    float s = ui.s;
    Rect c = rCanvas;
    bool empty = true;
    for (auto& p : doc.parts) if (p.inView(view) && !p.def->isBoard && !p.def->isBreadboard) { empty = false; break; }
    if (empty && !readOnly) {
        std::string hint = compact ? "Tap Parts below to add components" : "Drag parts from the Parts bin onto the canvas";
        float w = g.textWidth(hint, 15 * s) + 36 * s;
        Rect r(c.center().x - w / 2, c.y + 18 * s, w, 38 * s);
        g.fillRoundRect(r, 19 * s, rgba(255, 255, 255, 230));
        g.strokeRoundRect(r, 19 * s, std::max(1.f, s), ui.th.border);
        g.text(r.center(), hint, 15 * s, ui.th.textDim, 1);
    }
    float y = c.y + 12 * s;
    simNotesRect = Rect();
    if (simOn) {
        std::string msg = sim.message.empty() ? "Simulating" : sim.message;
        float w = g.textWidth(msg, 13.5f * s) + 44 * s;
        Rect r(c.x + 12 * s, y, w, 30 * s);
        g.fillRoundRect(r, 15 * s, rgba(28, 116, 222, 235));
        g.fillCircle({r.x + 16 * s, r.center().y}, 5 * s, (SDL_GetTicks() / 500) % 2 ? rgba(255, 255, 255) : rgba(160, 200, 255));
        g.text({r.x + 28 * s, r.center().y}, msg, 13.5f * s, rgba(255, 255, 255), 0);
        y += 36 * s;
        // real problems in red; "not simulated" notes are informational, folded into one grey line
        std::vector<std::string> problems, notes;
        for (auto& wmsg : sim.warnings)
            (wmsg.find("isn't simulated") != std::string::npos || wmsg.rfind("Not simulated", 0) == 0 ? notes : problems).push_back(wmsg);
        auto box = [&](const std::string& wmsg, uint32_t bg, uint32_t fg) {
            auto lines = wrapText(g, wmsg, 13 * s, std::min(520 * s, c.w - 60 * s));
            float h = 12 * s + (float)lines.size() * 17 * s;
            float ww = 0;
            for (auto& l : lines) ww = std::max(ww, g.textWidth(l, 13 * s));
            Rect r2(c.x + 12 * s, y, ww + 24 * s, h);
            g.fillRoundRect(r2, 8 * s, bg);
            for (size_t i = 0; i < lines.size(); i++) g.text({r2.x + 12 * s, r2.y + 6 * s + 17 * s * ((float)i + 0.5f)}, lines[i], 13 * s, fg, 0);
            y += h + 6 * s;
        };
        for (auto& wmsg : problems) {
            box(wmsg, rgba(200, 50, 50, 235), rgba(255, 255, 255));
            if (y > c.b() - 80 * s) break;
        }
        if (!notes.empty() && y < c.b() - 80 * s) {
            std::string head = std::to_string(notes.size()) + (notes.size() == 1 ? " note" : " notes") +
                               ": some parts or libraries aren't simulated " + (simNotesOpen ? "\u25B4" : "\u25BE");
            float w = g.textWidth(head, 12.5f * s) + 24 * s;
            Rect r(c.x + 12 * s, y, w, 26 * s);
            simNotesRect = r;
            g.fillRoundRect(r, 13 * s, ui.hovered(r) ? rgba(90, 96, 108, 225) : rgba(110, 116, 128, 200));
            g.text({r.x + 12 * s, r.center().y}, head, 12.5f * s, rgba(255, 255, 255), 0);
            if (ui.hovered(r) && ui.in->pressed) {
                simNotesOpen = !simNotesOpen;
                ui.in->pressed = false;
            }
            y += 32 * s;
            if (simNotesOpen)
                for (auto& n : notes) {
                    box(n, rgba(245, 246, 248, 240), rgba(70, 76, 88));
                    if (y > c.b() - 80 * s) break;
                }
        }
    }
    if (view == PCB && !readOnly) {
        std::string lt = std::string("Active layer: ") + (activeLayer == TOP ? "Top" : "Bottom");
        float w = g.textWidth(lt, 12.5f * s) + 24 * s;
        Rect r(c.r() - w - 12 * s, c.y + 12 * s, w, 26 * s);
        g.fillRoundRect(r, 13 * s, rgba(0, 0, 0, 120));
        g.text(r.center(), lt, 12.5f * s, activeLayer == TOP ? rgba(255, 200, 120) : rgba(240, 220, 120), 1);
    }
    if (compact) {
        ensureConn();
        RoutingStatus st;
        conn.ratsnest(doc, view, &st);
        if (st.connectionsToRoute > 0 && !readOnly) {
            std::string m = std::to_string(st.connectionsToRoute) + " to route";
            float w = g.textWidth(m, 12.5f * s) + 24 * s;
            Rect r(c.x + 10 * s, c.b() - 38 * s, w, 28 * s);
            g.fillRoundRect(r, 14 * s, rgba(200, 50, 50, 220));
            g.text(r.center(), m, 12.5f * s, rgba(255, 255, 255), 1);
        }
        Rect fit(c.r() - 50 * s, c.b() - 50 * s, 40 * s, 40 * s);
        if (!(drawerParts || drawerInspector || drawerLayers || drawerAI) && !embedMode) {
            g.fillRoundRect(fit, 20 * s, rgba(255, 255, 255, 235));
            g.strokeRoundRect(fit, 20 * s, std::max(1.f, s), ui.th.border);
            if (ui.iconButton("cfit", fit.inset(4 * s), "fit", "")) fitView(view, rCanvas);
        }
    }
}
