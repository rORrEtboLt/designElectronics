#include "app.h"
#include "logo.h"

#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// Startup / shutdown
// ---------------------------------------------------------------------------

static void newProjectContents(Document& d) {
    d.clear();
    d.addPart("breadboard", {{"size", "half+"}}, {0, 0});
    d.addPart("pcb_board", {}, {0, 0});
}

bool App::init(int argc, char** argv) {
    SDL_SetAppMetadata("designElectronics", kAppVersion, "org.designelectronics.app");
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_URL_STRING, "https://github.com/rORrEtboLt/designElectronics");
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_TYPE_STRING, "application");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    win = SDL_CreateWindow("designElectronics", 1360, 860, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!win) { SDL_Log("CreateWindow failed: %s", SDL_GetError()); return false; }
    SDL_SetWindowMinimumSize(win, 360, 480);
    loadGitHubSettings();
    if (SDL_Surface* icon = makeLogoSurface(256)) {
        SDL_SetWindowIcon(win, icon);
        SDL_DestroySurface(icon);
    }
    ren = SDL_CreateRenderer(win, nullptr);
    if (!ren) { SDL_Log("CreateRenderer failed: %s", SDL_GetError()); return false; }
    SDL_SetRenderVSync(ren, 1);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    g.init(ren);
    ui.g = &g;
    ui.in = &in;
    ui.window = win;

    std::string openPath;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a.rfind("-", 0) == 0) continue;
        openPath = a;
    }
    bool loaded = false;
    if (!openPath.empty()) loaded = loadFile(openPath);
    if (!loaded) {
        bool ok = false;
        std::string text = readTextFile(autosavePath(), &ok);
        if (ok) {
            Json j = Json::parse(text);
            std::string err;
            if (doc.fromJson(j, &err)) {
                filePath = j["autosaveOf"].str();
                savedRevision = j["autosaveDirty"].boolean() ? -1 : doc.revision;
                loaded = true;
                for (auto& lj : j["library"].items())
                    if (isCustomType(lj["type"].str())) customEntries.push_back({lj["type"].str(), lj["title"].str(), "Custom", {}});
            }
        }
    }
    if (!loaded) {
        newProjectContents(doc);
        savedRevision = doc.revision;
    }
    autosaveRevision = doc.revision;
    return true;
}

void App::shutdown() {
    autosave(true);
    g.shutdown();
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
}

std::string App::autosavePath() const {
    char* pref = SDL_GetPrefPath("designElectronics", "designElectronics");
    std::string p = pref ? pref : "";
    SDL_free(pref);
    return p + "autosave.dez";
}

void App::autosave(bool force) {
    if (doc.revision == autosaveRevision) return;
    Uint64 now = SDL_GetTicks();
    if (!force && now - lastAutosave < 2500) return;
    if (!force && drag != DRAG_NONE) return;
    Json j = doc.toJson(true);
    j.set("autosaveOf", filePath);
    j.set("autosaveDirty", dirty());
    writeTextFile(autosavePath(), j.dump(-1));
    lastAutosave = now;
    autosaveRevision = doc.revision;
}

void App::runOnMain(std::function<void()> f) {
    std::lock_guard<std::mutex> lk(pendingMutex);
    pending.push_back(std::move(f));
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void App::layout() {
    SDL_GetWindowSizeInPixels(win, &winW, &winH);
    float density = SDL_GetWindowPixelDensity(win);
    float display = SDL_GetWindowDisplayScale(win);
    float base = std::max(1.0f, display);
    ui.s = base * userScale;
    float s = ui.s;
    compact = (float)winW / s < 860;
    SDL_Rect safe{0, 0, winW, winH};
    if (SDL_GetWindowSafeArea(win, &safe)) {
        safe.x = (int)(safe.x * density); safe.y = (int)(safe.y * density);
        safe.w = (int)(safe.w * density); safe.h = (int)(safe.h * density);
    } else safe = {0, 0, winW, winH};
    float top = (float)safe.y, bottom = (float)(safe.y + safe.h);
    rTop = Rect((float)safe.x, top, (float)safe.w, 48 * s);
    float tbH = (compact ? 62.f : 60.f) * s;
    rBottom = Rect((float)safe.x, bottom - tbH, (float)safe.w, tbH);
    float mid = rBottom.y - rTop.b();
    if (!compact) {
        float rw = std::min(330 * s, (float)safe.w * 0.32f);
        rRight = Rect((float)(safe.x + safe.w) - rw, rTop.b(), rw, mid);
        rCanvas = Rect((float)safe.x, rTop.b(), rRight.x - safe.x, mid);
        float binH = std::floor(mid * 0.54f);
        rBin = Rect(rRight.x, rRight.y, rRight.w, binH);
        rInspector = Rect(rRight.x, rRight.y + binH, rRight.w, mid - binH);
    } else {
        rCanvas = Rect((float)safe.x, rTop.b(), (float)safe.w, mid);
        float dw = std::min(380 * s, (float)safe.w * 0.9f);
        rRight = Rect((float)(safe.x + safe.w) - dw, rTop.b(), dw, mid);
        rBin = rInspector = rRight;
    }
}

void App::ensureConn() {
    if (connRev == doc.revision) return;
    conn.build(doc);
    connRev = doc.revision;
}

// ---------------------------------------------------------------------------
// Undo / change tracking
// ---------------------------------------------------------------------------

void App::changed() { doc.touch(); }

void App::checkpoint() {
    undoStack.push_back(doc.snapshot());
    if (undoStack.size() > 150) undoStack.erase(undoStack.begin());
    redoStack.clear();
}

void App::beginEdit() {
    editSnapshot = doc.snapshot();
    editOpen = true;
}

void App::endEdit() {
    if (!editOpen) return;
    editOpen = false;
    if (doc.snapshot() != editSnapshot) {
        undoStack.push_back(editSnapshot);
        if (undoStack.size() > 150) undoStack.erase(undoStack.begin());
        redoStack.clear();
    }
    editSnapshot.clear();
}

static void restoreSnapshot(Document& doc, const std::string& s) {
    int rev = doc.revision;
    std::string title = doc.title;
    std::string err;
    doc.deserialize(s, &err);
    doc.revision = rev + 1;
}

void App::undo() {
    if (undoStack.empty()) { ui.toast("Nothing to undo"); return; }
    redoStack.push_back(doc.snapshot());
    restoreSnapshot(doc, undoStack.back());
    undoStack.pop_back();
    pruneSelection();
}

void App::redo() {
    if (redoStack.empty()) { ui.toast("Nothing to redo"); return; }
    undoStack.push_back(doc.snapshot());
    restoreSnapshot(doc, redoStack.back());
    redoStack.pop_back();
    pruneSelection();
}

void App::pruneSelection() {
    for (auto it = selParts.begin(); it != selParts.end();) it = doc.part(*it) ? std::next(it) : selParts.erase(it);
    for (auto it = selWires.begin(); it != selWires.end();) it = doc.wire(*it) ? std::next(it) : selWires.erase(it);
    for (auto it = selNotes.begin(); it != selNotes.end();) it = doc.note(*it) ? std::next(it) : selNotes.erase(it);
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

void App::confirmDiscard(std::function<void()> then) {
    if (!dirty()) { then(); return; }
    ui.dialog("Save changes?", {"\"" + doc.title + "\" has unsaved changes. Do you want to save them first?"},
              {{"Don't Save", then, false},
               {"Cancel", nullptr, false},
               {"Save", [this, then]() {
                    if (filePath.empty()) { saveAs(); return; }
                    save();
                    then();
                }, true}});
}

void App::newDoc() {
    confirmDiscard([this]() {
        clearCustomParts();
        customEntries.clear();
        newProjectContents(doc);
        filePath.clear();
        savedRevision = doc.revision;
        undoStack.clear();
        redoStack.clear();
        selParts.clear(); selWires.clear(); selNotes.clear();
        for (auto& c : cam) c.valid = false;
        simOn = false;
        drcIssues.clear();
    });
}

static bool endsWith(const std::string& s, const std::string& suf) {
    if (s.size() < suf.size()) return false;
    std::string a = s.substr(s.size() - suf.size());
    for (auto& c : a) c = (char)std::tolower((unsigned char)c);
    return a == suf;
}

bool App::loadFile(const std::string& path) {
    bool ok = false;
    std::string text = readTextFile(path, &ok);
    if (!ok) { ui.dialog("Could not open file", {path, SDL_GetError()}, {}); return false; }
    std::string perr;
    Json j = Json::parse(text, &perr);
    if (!perr.empty()) { ui.dialog("Could not open file", {"The file is not valid designElectronics JSON.", perr}, {}); return false; }
    if (j["format"].str() == "designElectronics-part" || (j.has("pins") && j.has("graphics"))) {
        PartDef def;
        std::string err;
        const Json& pj = j.has("part") ? j["part"] : j;
        if (!partDefFromJson(pj, def, &err)) { ui.dialog("Could not import part", {err}, {}); return false; }
        registerCustomPart(def);
        bool exists = false;
        for (auto& e : customEntries) if (e.type == def.type) exists = true;
        if (!exists) customEntries.push_back({def.type, def.title, "Custom", {}});
        category = -2;
        ui.toast("Imported part \"" + def.title + "\" — find it under Custom in the parts bin.");
        return true;
    }
    std::string err;
    std::vector<std::string> before;
    if (!doc.fromJson(j, &err)) { ui.dialog("Could not open file", {err}, {}); return false; }
    customEntries.clear();
    for (auto& lj : j["library"].items())
        if (isCustomType(lj["type"].str())) customEntries.push_back({lj["type"].str(), lj["title"].str(), "Custom", {}});
    filePath = path;
    savedRevision = doc.revision;
    undoStack.clear();
    redoStack.clear();
    selParts.clear(); selWires.clear(); selNotes.clear();
    for (auto& c : cam) c.valid = false;
    drcIssues.clear();
    simOn = false;
    ui.toast("Opened " + doc.title);
    return true;
}

bool App::saveFile(const std::string& pathIn, const std::string& versionMessage) {
    std::string path = pathIn;
    bool uri = path.rfind("content://", 0) == 0;
    if (!uri && !endsWith(path, kFileExt)) path += kFileExt;
    if (doc.title == "Untitled" || doc.title.empty()) doc.title = uri ? doc.title : fileBaseName(path);
    history::ensureUid(doc);
    std::string data = doc.serialize(true);
    if (!writeTextFile(path, data)) {
        ui.dialog("Could not save", {path, SDL_GetError()}, {});
        return false;
    }
    filePath = path;
    savedRevision = doc.revision;
    autosave(true);
    ui.toast("Saved " + (uri ? doc.title : path));
    recordVersion(data, versionMessage);  // local version history (+ GitHub when auto-push is on)
    return true;
}

void App::save() {
    if (filePath.empty() || filePath.rfind("content://", 0) == 0) { saveAs(); return; }
    saveFile(filePath);
}

static std::string documentsDir() {
    const char* d = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
    if (d && *d) return d;
    char* pref = SDL_GetPrefPath("designElectronics", "designElectronics");
    std::string p = pref ? pref : "";
    SDL_free(pref);
    return p;
}

static std::string sanitize(std::string s) {
    for (auto& c : s) if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
    return s.empty() ? "Untitled" : s;
}

struct DialogCtx {
    App* app;
    std::function<void(const std::string&)> onPath;
    std::function<void()> onUnsupported;
};

static void SDLCALL dialogCallback(void* userdata, const char* const* filelist, int) {
    auto* ctx = (DialogCtx*)userdata;
    App* app = ctx->app;
    if (!filelist) {
        std::string err = SDL_GetError();
        auto fallback = ctx->onUnsupported;
        app->runOnMain([app, err, fallback]() {
            if (fallback) fallback();
            else app->ui.toast("File dialog unavailable: " + err);
        });
    } else if (filelist[0]) {
        std::string path = filelist[0];
        auto f = ctx->onPath;
        app->runOnMain([f, path]() { f(path); });
    }
    delete ctx;
}

static void showSave(App* app, const std::string& defName, const SDL_DialogFileFilter* filters, int n,
                     std::function<void(const std::string&)> onPath) {
    std::string def = documentsDir();
    if (!def.empty() && def.back() != '/' && def.back() != '\\') def += "/";
    def += defName;
    auto* ctx = new DialogCtx{app, onPath, [app, def, onPath]() {
        // No native dialog (e.g. iOS): save into the app's Documents folder, which
        // is visible in the Files app and can be shared from there.
        onPath(def);
        app->ui.toast("Saved to " + def);
    }};
    SDL_ShowSaveFileDialog(dialogCallback, ctx, app->win, filters, n, def.c_str());
}

void App::saveAs() {
    static const SDL_DialogFileFilter f[] = {{"designElectronics project", "dez"}};
    std::string name = sanitize(doc.title) + kFileExt;
    showSave(this, name, f, 1, [this](const std::string& p) { saveFile(p); });
}

void App::openDialog() {
    confirmDiscard([this]() {
        static const SDL_DialogFileFilter f[] = {{"designElectronics files", "dez;dezpart;json"}, {"All files", "*"}};
        auto* ctx = new DialogCtx{this, [this](const std::string& p) { loadFile(p); }, nullptr};
        std::string dir = documentsDir();
        SDL_ShowOpenFileDialog(dialogCallback, ctx, win, f, 2, dir.c_str(), false);
    });
}

void App::importPartDialog() {
    static const SDL_DialogFileFilter f[] = {{"designElectronics part", "dezpart;json"}};
    auto* ctx = new DialogCtx{this, [this](const std::string& p) { loadFile(p); }, nullptr};
    SDL_ShowOpenFileDialog(dialogCallback, ctx, win, f, 1, documentsDir().c_str(), false);
}

void App::exportFile(const std::string& kind) {
    ensureConn();
    std::string base = sanitize(doc.title);
    if (kind == "gerber") {
        static const SDL_DialogFileFilter f[] = {{"Zip archive", "zip"}};
        showSave(this, base + "_gerber.zip", f, 1, [this](const std::string& p) {
            std::string path = p;
            if (path.rfind("content://", 0) != 0 && !endsWith(path, ".zip")) path += ".zip";
            ensureConn();
            std::string msg;
            if (exportGerberZip(doc, conn, path, &msg)) ui.toast(msg, 5);
            else ui.dialog("Export failed", {msg}, {});
        });
    } else if (kind == "svg") {
        static const SDL_DialogFileFilter f[] = {{"SVG image", "svg"}};
        int v = view;
        showSave(this, base + "_" + viewKey(v) + ".svg", f, 1, [this, v](const std::string& p) {
            std::string path = p;
            if (path.rfind("content://", 0) != 0 && !endsWith(path, ".svg")) path += ".svg";
            ensureConn();
            if (writeTextFile(path, exportSvg(doc, conn, v))) ui.toast("Exported " + path);
            else ui.dialog("Export failed", {SDL_GetError()}, {});
        });
    } else if (kind == "png") {
        static const SDL_DialogFileFilter f[] = {{"PNG image", "png"}};
        int v = view;
        showSave(this, base + "_" + viewKey(v) + ".png", f, 1, [this, v](const std::string& p) {
            std::string path = p;
            if (path.rfind("content://", 0) != 0 && !endsWith(path, ".png")) path += ".png";
            std::string msg;
            if (renderViewPng(v, path, &msg)) ui.toast("Exported " + path);
            else ui.dialog("Export failed", {msg}, {});
        });
    } else if (kind == "bom" || kind == "netlist" || kind == "spice") {
        const char* ext = kind == "bom" ? "csv" : kind == "netlist" ? "xml" : "cir";
        static SDL_DialogFileFilter f[1];
        f[0] = {kind == "bom" ? "CSV spreadsheet" : kind == "netlist" ? "XML netlist" : "SPICE netlist", ext};
        std::string suffix = kind == "bom" ? "_bom.csv" : kind == "netlist" ? "_netlist.xml" : ".cir";
        showSave(this, base + suffix, f, 1, [this, kind, ext](const std::string& p) {
            std::string path = p;
            if (path.rfind("content://", 0) != 0 && !endsWith(path, std::string(".") + ext)) path += std::string(".") + ext;
            ensureConn();
            std::string data = kind == "bom" ? exportBomCsv(doc) : kind == "netlist" ? exportNetlistXml(doc, conn) : exportSpice(doc, conn);
            if (writeTextFile(path, data)) ui.toast("Exported " + path);
            else ui.dialog("Export failed", {SDL_GetError()}, {});
        });
    } else if (kind == "part") {
        if (selParts.size() != 1) { ui.toast("Select one part to export its definition."); return; }
        const PartInst* p = doc.part(*selParts.begin());
        if (!p) return;
        Json j = Json::object();
        j.set("format", "designElectronics-part");
        j.set("version", kFileVersion);
        PartDef def = *p->def;
        def.defaults = mergedProps(p->type, p->props);
        j.set("part", partDefToJson(def));
        std::string data = j.dump(1);
        static const SDL_DialogFileFilter f[] = {{"designElectronics part", "dezpart"}};
        showSave(this, sanitize(p->def->title) + ".dezpart", f, 1, [this, data](const std::string& path0) {
            std::string path = path0;
            if (path.rfind("content://", 0) != 0 && !endsWith(path, ".dezpart")) path += ".dezpart";
            if (writeTextFile(path, data)) ui.toast("Exported part definition to " + path);
        });
    }
}

void App::loadExample(size_t i) {
    confirmDiscard([this, i]() {
        doc.clear();
        examples()[i].build(doc);
        doc.syncWireEnds();
        doc.touch();
        filePath.clear();
        savedRevision = doc.revision;
        undoStack.clear();
        redoStack.clear();
        selParts.clear(); selWires.clear(); selNotes.clear();
        for (auto& c : cam) c.valid = false;
        drcIssues.clear();
        simOn = false;
        ui.toast("Loaded example: " + examples()[i].title);
    });
}

void App::showAbout() {
    ui.dialog("designElectronics " + std::string(kAppVersion),
              {"An open-source electronics design app for desktop and mobile, inspired by Fritzing.",
               "",
               "Design on a breadboard, draw the schematic and lay out a PCB — every connection you make in one view appears in the others.",
               "",
               "Projects are single .dez files (plain JSON) that embed every part they use, so they open offline in any copy of designElectronics. No account, no server.",
               "",
               "Built with SDL3. Fonts: Lato and Source Code Pro (SIL Open Font License)."},
              {});
}

void App::showShortcuts() {
    ui.dialog("Keyboard & touch shortcuts",
              {"Ctrl/⌘ + N, O, S — new, open, save (Shift+S: save as)",
               "Ctrl/⌘ + Z / Shift+Z / Y — undo / redo",
               "Ctrl/⌘ + C, X, V, D — copy, cut, paste, duplicate",
               "Ctrl/⌘ + A — select all;  Delete/Backspace — delete",
               "R / Shift+R — rotate 90° clockwise / counter-clockwise;  F — flip",
               "Arrow keys — nudge (Shift = 10×)",
               "Ctrl/⌘ + 1, 2, 3 — breadboard, schematic, PCB view",
               "+ / − / 0 — zoom in / out / fit;  wheel or pinch to zoom",
               "Space + drag, middle or right drag — pan",
               "",
               "Drag from a pin or breadboard hole to draw a wire.",
               "Drag a wire to bend it; double-click a bend point to remove it.",
               "Click a dashed ratsnest line to turn it into a wire or trace.",
               "Touch: one finger pans empty space, two fingers pinch-zoom, long-press for the context menu."},
              {});
}

// ---------------------------------------------------------------------------
// Editing commands
// ---------------------------------------------------------------------------

void App::deleteSelection() {
    if (selParts.empty() && selWires.empty() && selNotes.empty()) return;
    checkpoint();
    for (int id : selWires) doc.removeWire(id);
    for (int id : selNotes) doc.removeNote(id);
    for (int id : selParts) doc.removePart(id);
    selParts.clear(); selWires.clear(); selNotes.clear();
    changed();
}

static void snapPart(PartInst& p, int v, float grid) {
    if (!p.def || p.def->pins.empty() || grid <= 0) { p.place[v].pos = snapv(p.place[v].pos, grid); return; }
    Vec2 pin0 = p.pinPos(0, v);
    p.place[v].pos += snapv(pin0, grid) - pin0;
}

void App::rotateSelection(int dir) {
    if (selParts.empty()) return;
    checkpoint();
    for (int id : selParts) {
        PartInst* p = doc.part(id);
        if (!p || !p->inView(view) || p->def->isBoard) continue;
        Vec2 c = p->worldBounds(view).center();
        p->place[view].rot = ((p->place[view].rot + dir) % 4 + 4) % 4;
        p->place[view].pos += c - p->worldBounds(view).center();
        if (snapToGrid) snapPart(*p, view, gridSize(view));
    }
    doc.syncWireEnds();
    changed();
}

void App::flipSelection() {
    if (selParts.empty()) return;
    checkpoint();
    for (int id : selParts) {
        PartInst* p = doc.part(id);
        if (!p || !p->inView(view) || p->def->isBoard) continue;
        Vec2 c = p->worldBounds(view).center();
        p->place[view].flip = !p->place[view].flip;
        p->place[view].pos += c - p->worldBounds(view).center();
        if (snapToGrid) snapPart(*p, view, gridSize(view));
    }
    doc.syncWireEnds();
    changed();
}

void App::copySelection(bool cut) {
    if (selParts.empty() && selWires.empty() && selNotes.empty()) return;
    Document clip;
    clip.title = "clipboard";
    for (auto& p : doc.parts) if (selParts.count(p.id)) clip.parts.push_back(p);
    for (auto& w : doc.wires) {
        bool both = w.ends[0].attached() && w.ends[1].attached() && selParts.count(w.ends[0].part) && selParts.count(w.ends[1].part);
        if (both || selWires.count(w.id)) {
            Wire c = w;
            for (auto& e : c.ends) if (e.attached() && !selParts.count(e.part)) e = WireEnd();
            clip.wires.push_back(c);
        }
    }
    for (auto& n : doc.notes) if (selNotes.count(n.id)) clip.notes.push_back(n);
    clip.nextId = doc.nextId;
    SDL_SetClipboardText(clip.serialize(false).c_str());
    if (cut) deleteSelection();
    else ui.toast("Copied " + std::to_string(clip.parts.size() + clip.wires.size() + clip.notes.size()) + " item(s)");
}

void App::paste() {
    char* t = SDL_GetClipboardText();
    std::string text = t ? t : "";
    SDL_free(t);
    Document clip;
    std::string err;
    if (text.empty() || !clip.deserialize(text, &err)) { ui.toast("Nothing to paste"); return; }
    checkpoint();
    std::map<int, int> idMap;
    selParts.clear(); selWires.clear(); selNotes.clear();
    Vec2 off(200, 200);
    for (auto& p : clip.parts) {
        PartInst n = p;
        n.id = doc.newId();
        idMap[p.id] = n.id;
        for (auto& pl : n.place) pl.pos += off;
        // keep labels unique
        bool clash = false;
        for (auto& q : doc.parts) if (q.label == n.label) clash = true;
        if (clash) n.label = doc.nextLabel(n.def ? n.def->labelPrefix : "U");
        if (n.def && (n.def->isBoard || n.def->isBreadboard)) doc.parts.insert(doc.parts.begin(), n);
        else doc.parts.push_back(n);
        selParts.insert(n.id);
    }
    for (auto& w : clip.wires) {
        Wire n = w;
        n.id = doc.newId();
        for (auto& pt : n.pts) pt += off;
        for (auto& e : n.ends) {
            if (!e.attached()) continue;
            auto it = idMap.find(e.part);
            if (it == idMap.end()) e = WireEnd(); else e.part = it->second;
        }
        doc.wires.push_back(n);
        if (n.view == view) selWires.insert(n.id);
    }
    for (auto& nt : clip.notes) {
        Note n = nt;
        n.id = doc.newId();
        n.pos += off;
        doc.notes.push_back(n);
        selNotes.insert(n.id);
    }
    doc.syncWireEnds();
    changed();
}

void App::duplicate() {
    char* t = SDL_GetClipboardText();
    std::string saved = t ? t : "";
    SDL_free(t);
    copySelection(false);
    paste();
    SDL_SetClipboardText(saved.c_str());
}

void App::selectAll() {
    selParts.clear(); selWires.clear(); selNotes.clear();
    for (auto& p : doc.parts) if (p.inView(view)) selParts.insert(p.id);
    for (auto& w : doc.wires) if (w.view == view) selWires.insert(w.id);
    for (auto& n : doc.notes) if (n.view == view) selNotes.insert(n.id);
}

void App::addPart(const LibEntry& e, Vec2 world) {
    PartDefPtr def = getPartDef(e.type, e.props);
    if (!def) return;
    if (!def->inView(view)) {
        for (int v = 0; v < NUM_VIEWS; v++)
            if (def->inView(v)) {
                ui.toast(def->title + " only exists in the " + viewName(v) + " view — switched views.");
                setView(v);
                Rect vp = rCanvas;
                if (!cam[v].valid) fitView(v, vp);
                world = (vp.center() - cam[v].pan) / cam[v].zoom;
                break;
            }
    }
    checkpoint();
    PartInst& p = doc.addPart(e.type, e.props, {0, 0});
    int cascade = (int)(doc.parts.size() % 8);
    for (int v = 0; v < NUM_VIEWS; v++) {
        Vec2 pos;
        if (v == view) pos = world - p.def->bounds[v].center();
        else if (cam[v].valid) pos = (rCanvas.center() - cam[v].pan) / cam[v].zoom - p.def->bounds[v].center() + Vec2(cascade * 100.f, cascade * 100.f);
        else pos = Vec2(cascade * 300.f, cascade * 200.f);
        p.place[v].pos = pos;
        snapPart(p, v, gridSize(v));
    }
    selParts = {p.id};
    selWires.clear(); selNotes.clear();
    changed();
}

void App::addNote() {
    checkpoint();
    Note n;
    n.id = doc.newId();
    n.view = view;
    n.size = Vec2(1600, 700);
    n.pos = toWorld(rCanvas.center()) - n.size / 2;
    n.text = "Double-click to edit this note";
    doc.notes.push_back(n);
    selParts.clear(); selWires.clear();
    selNotes = {n.id};
    changed();
    if (compact) drawerInspector = true;
}

void App::setPartProp(int partId, const std::string& key, const std::string& value) {
    PartInst* p = doc.part(partId);
    if (!p || p->prop(key) == value) return;
    checkpoint();
    p->props[key] = value;
    p->refresh();
    if (!p->def) { p->props.erase(key); p->refresh(); }
    doc.syncWireEnds();
    changed();
}

void App::bringToFront(bool front) {
    if (selParts.empty()) return;
    checkpoint();
    std::vector<PartInst> moved, rest;
    for (auto& p : doc.parts) (selParts.count(p.id) && !p.def->isBoard && !p.def->isBreadboard ? moved : rest).push_back(p);
    doc.parts.clear();
    if (front) { doc.parts = rest; doc.parts.insert(doc.parts.end(), moved.begin(), moved.end()); }
    else {
        // keep boards/breadboards at the very back
        size_t k = 0;
        while (k < rest.size() && (rest[k].def->isBoard || rest[k].def->isBreadboard)) k++;
        doc.parts.assign(rest.begin(), rest.begin() + (long)k);
        doc.parts.insert(doc.parts.end(), moved.begin(), moved.end());
        doc.parts.insert(doc.parts.end(), rest.begin() + (long)k, rest.end());
    }
    changed();
}

void App::runAutoroute() {
    ensureConn();
    checkpoint();
    RouteResult r = autoroute(doc, conn, true);
    doc.touch();
    ensureConn();
    ui.toast(r.message, 5);
}

void App::runDrc() {
    ensureConn();
    drcIssues = ::runDrc(doc, conn);
    drcRev = doc.revision;
    if (drcIssues.empty()) {
        ui.dialog("Design Rules Check", {"No problems found. Your board is ready for production — export Gerber files from File ▸ Export."}, {});
        return;
    }
    std::vector<std::string> lines;
    lines.push_back(std::to_string(drcIssues.size()) + " issue(s) found (highlighted in red on the board):");
    lines.push_back("");
    for (auto& i : drcIssues) lines.push_back("• " + i.message);
    ui.dialog("Design Rules Check", lines, {});
}

void App::toggleSim() {
    simOn = !simOn;
    simRev = -1;
    simulator.reset();
    lastSimTicks = 0;
    simPressed.clear();
    if (simOn && view == PCB) setView(BB);
    ui.toast(simOn ? "Simulator on — LEDs light up, meters read, overloaded parts smoke. Hold buttons to press them." : "Simulator off");
}

void App::setView(int v) {
    if (v == view) return;
    view = v;
    ui.clearFocus();
    ui.closeMenus();
}

void App::handleShortcuts() {
    bool c = in.cmd(), sh = in.shift();
    for (auto k : in.keys) {
        if (c) {
            switch (k) {
                case SDLK_Z: sh ? redo() : undo(); break;
                case SDLK_Y: redo(); break;
                case SDLK_S: sh ? saveAs() : save(); break;
                case SDLK_O: openDialog(); break;
                case SDLK_N: newDoc(); break;
                case SDLK_H: if (sh) openHistory(); break;
                case SDLK_K: if (sh) openCommitSheet(); break;
                case SDLK_C: copySelection(false); break;
                case SDLK_X: copySelection(true); break;
                case SDLK_V: paste(); break;
                case SDLK_D: duplicate(); break;
                case SDLK_A: selectAll(); break;
                case SDLK_R: rotateSelection(sh ? -1 : 1); break;
                case SDLK_1: setView(BB); break;
                case SDLK_2: setView(SCH); break;
                case SDLK_3: setView(PCB); break;
                case SDLK_0: fitView(view, rCanvas); break;
                case SDLK_EQUALS: case SDLK_PLUS: zoomAt(1.25f, rCanvas.center()); break;
                case SDLK_MINUS: zoomAt(0.8f, rCanvas.center()); break;
                default: break;
            }
            continue;
        }
        switch (k) {
            case SDLK_DELETE: case SDLK_BACKSPACE: deleteSelection(); break;
            case SDLK_R: rotateSelection(sh ? -1 : 1); break;
            case SDLK_F: flipSelection(); break;
            case SDLK_EQUALS: case SDLK_PLUS: case SDLK_KP_PLUS: zoomAt(1.25f, rCanvas.center()); break;
            case SDLK_MINUS: case SDLK_KP_MINUS: zoomAt(0.8f, rCanvas.center()); break;
            case SDLK_0: fitView(view, rCanvas); break;
            case SDLK_ESCAPE:
                if (drag == DRAG_NONE) { selParts.clear(); selWires.clear(); selNotes.clear(); }
                break;
            case SDLK_LEFT: case SDLK_RIGHT: case SDLK_UP: case SDLK_DOWN: {
                if (selParts.empty() && selWires.empty() && selNotes.empty()) break;
                float step = gridSize(view) * (sh ? 10 : 1);
                Vec2 d = k == SDLK_LEFT ? Vec2(-step, 0) : k == SDLK_RIGHT ? Vec2(step, 0) : k == SDLK_UP ? Vec2(0, -step) : Vec2(0, step);
                checkpoint();
                for (int id : selParts) if (auto* p = doc.part(id)) p->place[view].pos += d;
                for (int id : selWires) if (auto* w = doc.wire(id)) for (size_t i = 0; i < w->pts.size(); i++) {
                    bool endAttached = (i == 0 && w->ends[0].attached()) || (i + 1 == w->pts.size() && w->ends[1].attached());
                    if (!endAttached) w->pts[i] += d;
                }
                for (int id : selNotes) if (auto* n = doc.note(id)) n->pos += d;
                doc.syncWireEnds();
                changed();
                break;
            }
            default: break;
        }
    }
}

// ---------------------------------------------------------------------------
// Events & frame
// ---------------------------------------------------------------------------

SDL_AppResult App::event(const SDL_Event& e) {
    float density = win ? SDL_GetWindowPixelDensity(win) : 1.f;
    switch (e.type) {
        case SDL_EVENT_QUIT:
            if (dirty() && !quitNow) {
                confirmDiscard([this]() { quitNow = true; });
                return SDL_APP_CONTINUE;
            }
            return SDL_APP_SUCCESS;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
            // keep the same world point in the middle of the canvas
            Rect before = rCanvas;
            layout();
            for (auto& c : cam) if (c.valid) c.pan += rCanvas.center() - before.center();
            break;
        }
        case SDL_EVENT_TERMINATING:
        case SDL_EVENT_DID_ENTER_BACKGROUND:
        case SDL_EVENT_LOW_MEMORY:
            autosave(true);
            break;
        case SDL_EVENT_MOUSE_MOTION:
            in.mouse = Vec2(e.motion.x, e.motion.y) * density;
            in.touch = e.motion.which == SDL_TOUCH_MOUSEID;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            in.mouse = Vec2(e.button.x, e.button.y) * density;
            in.touch = e.button.which == SDL_TOUCH_MOUSEID;
            if (e.button.button == SDL_BUTTON_LEFT) {
                in.down = true;
                in.pressed = true;
                in.pressPos = in.mouse;
                in.pressTime = SDL_GetTicks();
                in.doubleClick = e.button.clicks >= 2;
            } else if (e.button.button == SDL_BUTTON_RIGHT) {
                in.rightPressed = true;
                in.rightDown = true;
                in.pressPos = in.mouse;
            } else if (e.button.button == SDL_BUTTON_MIDDLE) {
                in.middleDown = true;
                in.pressPos = in.mouse;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            in.mouse = Vec2(e.button.x, e.button.y) * density;
            if (e.button.button == SDL_BUTTON_LEFT) { in.down = false; in.released = true; }
            else if (e.button.button == SDL_BUTTON_RIGHT) in.rightDown = false;
            else if (e.button.button == SDL_BUTTON_MIDDLE) in.middleDown = false;
            break;
        case SDL_EVENT_MOUSE_WHEEL: {
            float dir = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.f : 1.f;
            in.wheelX += e.wheel.x * dir;
            in.wheelY += e.wheel.y * dir;
            if (e.wheel.integer_y == 0 && e.wheel.integer_x == 0) in.wheelPrecise = true;
            if (std::fabs(e.wheel.x) > 0) in.wheelPrecise = true;
            in.mouse = Vec2(e.wheel.mouse_x, e.wheel.mouse_y) * density;
            break;
        }
        case SDL_EVENT_PINCH_UPDATE:
            in.pinchScale *= e.pinch.scale;
            in.pinchCenter = in.mouse;
            break;
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED: {
            Vec2 p(e.tfinger.x * (float)winW, e.tfinger.y * (float)winH);
            std::map<SDL_FingerID, Vec2> before = fingers;
            if (e.type == SDL_EVENT_FINGER_UP || e.type == SDL_EVENT_FINGER_CANCELED) fingers.erase(e.tfinger.fingerID);
            else fingers[e.tfinger.fingerID] = p;
            if (fingers.size() >= 2 && before.size() >= 2 && e.type == SDL_EVENT_FINGER_MOTION) {
                auto stats = [](const std::map<SDL_FingerID, Vec2>& f, Vec2& c, float& d) {
                    auto it = f.begin();
                    Vec2 a = it->second, b = std::next(it)->second;
                    c = (a + b) / 2;
                    d = dist(a, b);
                };
                Vec2 c0, c1;
                float d0, d1;
                stats(before, c0, d0);
                stats(fingers, c1, d1);
                if (d0 > 1) in.pinchScale *= d1 / d0;
                in.pinchPan += c1 - c0;
                in.pinchCenter = c1;
            }
            in.pinching = fingers.size() >= 2;
            break;
        }
        case SDL_EVENT_TEXT_INPUT:
            in.text += e.text.text;
            break;
        case SDL_EVENT_KEY_DOWN:
            in.keys.push_back(e.key.key);
            in.mods = e.key.mod;
            if (e.key.key == SDLK_SPACE && !ui.editingText()) spaceDown = true;
            if (e.key.key == SDLK_AC_BACK) {  // Android back button
                if (ui.hasMenu()) ui.closeMenus();
                else if (drawerParts || drawerInspector) drawerParts = drawerInspector = false;
            }
            break;
        case SDL_EVENT_KEY_UP:
            in.mods = e.key.mod;
            if (e.key.key == SDLK_SPACE) spaceDown = false;
            break;
        case SDL_EVENT_DROP_FILE: {
            std::string path = e.drop.data ? e.drop.data : "";
            if (!path.empty()) runOnMain([this, path]() {
                if (endsWith(path, ".dezpart")) loadFile(path);
                else confirmDiscard([this, path]() { loadFile(path); });
            });
            break;
        }
        default:
            break;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult App::iterate() {
    layout();
    std::vector<std::function<void()>> work;
    {
        std::lock_guard<std::mutex> lk(pendingMutex);
        work.swap(pending);
    }
    for (auto& f : work) f();
    if (quitNow) return SDL_APP_SUCCESS;

    ensureConn();
    if (simOn) {
        Uint64 now = SDL_GetTicks();
        double dtReal = lastSimTicks ? std::clamp((now - lastSimTicks) / 1000.0, 0.001, 0.06) : 0.016;
        lastSimTicks = now;
        sim = simulator.step(doc, conn, simPressed, dtReal);
        simRev = doc.revision;
        simPressedLast = simPressed;
    }
    for (int v = 0; v < NUM_VIEWS; v++) if (!cam[v].valid) fitView(v, rCanvas);

    ui.begin();
    if (!ui.editingText() && !ui.blocking()) handleShortcuts();
    canvasInput();

    SDL_SetRenderDrawColor(ren, 226, 226, 228, 255);
    SDL_RenderClear(ren);
    drawCanvas(view, cam[view], rCanvas, false);
    drawCanvasOverlay();
    drawTopBar();
    if (!compact) {
        drawPartsBin(rBin);
        drawInspector(rInspector);
    } else if (drawerParts || drawerInspector) {
        // drawers slide over the canvas
        g.fillRect(rCanvas, rgba(0, 0, 0, 50));
        g.fillRect({rRight.x - 6 * ui.s, rRight.y, 6 * ui.s, rRight.h}, rgba(0, 0, 0, 25));
        if (drawerParts) drawPartsBin(rRight);
        else drawInspector(rRight);
        if (in.pressed && rCanvas.contains(in.mouse) && !rRight.contains(in.mouse) && !ui.blocking() && binDrag < 0)
            drawerParts = drawerInspector = false;
    }
    drawToolbar();
    if (binDrag >= 0) {
        std::vector<LibEntry> all = partLibrary();
        all.insert(all.end(), customEntries.begin(), customEntries.end());
        if (binDrag < (int)all.size()) {
            PartDefPtr def = getPartDef(all[(size_t)binDrag].type, all[(size_t)binDrag].props);
            float sz = 72 * ui.s;
            Rect r(in.mouse.x - sz / 2, in.mouse.y - sz / 2, sz, sz);
            g.fillRoundRect(r, 8 * ui.s, rgba(255, 255, 255, 200));
            if (def) drawPartIcon(def, r.inset(6 * ui.s));
        }
    }
    ui.end();
    g.flush();
    if (!capturePath.empty()) {
        SDL_Surface* shot = SDL_RenderReadPixels(ren, nullptr);
        captureOk = shot && SDL_SavePNG(shot, capturePath.c_str());
        if (shot) SDL_DestroySurface(shot);
        capturePath.clear();
    }
    SDL_RenderPresent(ren);
    autosave(false);
    in.endFrame();
    if (in.released) {}
    return SDL_APP_CONTINUE;
}
