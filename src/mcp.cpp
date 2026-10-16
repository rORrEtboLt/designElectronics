// designElectronics — Model Context Protocol (MCP) server.
//
// `designElectronics --mcp` speaks MCP (JSON-RPC 2.0, one message per line) on
// stdin/stdout so AI assistants can build circuits: browse the parts library,
// add and wire parts, plug them into breadboards, simulate, autoroute, run DRC
// and export Gerbers. The design lives in memory and is read from / written to
// ordinary .dez files, so whatever the assistant builds opens in the app.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>

#ifdef _WIN32
#include <io.h>
#define DE_DUP _dup
#define DE_DUP2 _dup2
#define DE_FILENO _fileno
#define DE_FDOPEN _fdopen
#else
#include <unistd.h>
#define DE_DUP dup
#define DE_DUP2 dup2
#define DE_FILENO fileno
#define DE_FDOPEN fdopen
#endif

#include "doc.h"
#include "history.h"
#include "tools.h"

namespace {

constexpr const char* kProtocolVersion = "2025-06-18";

struct ToolError {
    std::string msg;
};
[[noreturn]] void fail(const std::string& m) { throw ToolError{m}; }

std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

int viewFromName(const std::string& s, int def = -1) {
    std::string v = lower(s);
    if (v == "breadboard" || v == "bb") return BB;
    if (v == "schematic" || v == "sch") return SCH;
    if (v == "pcb") return PCB;
    if (def >= 0 && v.empty()) return def;
    fail("unknown view '" + s + "' (use breadboard, schematic or pcb)");
}

Props propsFromJson(const Json& j) {
    Props p;
    for (auto& kv : j.members()) p[kv.first] = kv.second.isString() ? kv.second.str() : kv.second.dump();
    return p;
}

// ---- schema helpers ----
Json prop(const char* type, const char* desc) {
    Json j = Json::object();
    j.set("type", type);
    j.set("description", desc);
    return j;
}
Json schema(std::initializer_list<std::pair<const char*, Json>> props, std::initializer_list<const char*> required = {}) {
    Json s = Json::object();
    s.set("type", "object");
    Json p = Json::object();
    for (auto& kv : props) p.set(kv.first, kv.second);
    s.set("properties", p);
    Json r = Json::array();
    for (auto* k : required) r.push(k);
    if (r.size()) s.set("required", r);
    return s;
}

class Server {
public:
    Json handle(const Json& msg, bool& reply);

private:
    Document doc_;
    std::string path_;

    Json tools() const;
    Json call(const std::string& name, const Json& a);

    // ---- lookups ----
    PartInst& findPart(const Json& ref) {
        if (ref.isNumber()) {
            if (auto* p = doc_.part(ref.i())) return *p;
        } else if (ref.isString()) {
            for (auto& p : doc_.parts) if (p.label == ref.str() || std::to_string(p.id) == ref.str()) return p;
        }
        fail("no part " + ref.dump() + " in the design (use get_design to list parts)");
    }
    static int findPin(const PartInst& p, const Json& ref) {
        const auto& pins = p.def->pins;
        std::string tok = ref.isString() ? ref.str() : ref.dump();
        for (size_t i = 0; i < pins.size(); i++) if (pins[i].id == tok) return (int)i;
        for (size_t i = 0; i < pins.size(); i++) if (pins[i].name == tok) return (int)i;
        for (size_t i = 0; i < pins.size(); i++) {
            const std::string& n = pins[i].name;
            if (n.size() > tok.size() && n.compare(0, tok.size(), tok) == 0 && n[tok.size()] == '/') return (int)i;
        }
        for (size_t i = 0; i < pins.size(); i++) if (lower(pins[i].name) == lower(tok) || lower(pins[i].id) == lower(tok)) return (int)i;
        if (ref.isNumber() && ref.i() >= 1 && ref.i() <= (int)pins.size()) return ref.i() - 1;
        std::string list;
        for (size_t i = 0; i < pins.size() && i < 60; i++) list += (i ? ", " : "") + pins[i].id;
        fail("part " + p.label + " has no pin '" + tok + "'. Pins: " + list + (pins.size() > 60 ? ", …" : ""));
    }

    Connectivity conn() const {
        Connectivity c;
        c.build(doc_);
        return c;
    }

    // ---- JSON views of the model ----
    static Json pinList(const PartDef& d) {
        Json a = Json::array();
        for (auto& p : d.pins) {
            Json j = Json::object();
            j.set("id", p.id);
            if (p.name != p.id) j.set("name", p.name);
            a.push(j);
        }
        return a;
    }
    static Json partSummary(const PartInst& p) {
        Json j = Json::object();
        j.set("id", p.id);
        j.set("label", p.label);
        j.set("type", p.type);
        j.set("title", p.def ? p.def->title : p.type);
        Json props = Json::object();
        for (auto& kv : mergedProps(p.type, p.props)) props.set(kv.first, kv.second);
        j.set("properties", props);
        Json pos = Json::object();
        for (int v = 0; v < NUM_VIEWS; v++) {
            if (!p.inView(v)) continue;
            Json x = Json::object();
            x.set("x", p.place[v].pos.x);
            x.set("y", p.place[v].pos.y);
            if (p.place[v].rot) x.set("rotation", p.place[v].rot * 90);
            if (p.place[v].flip) x.set("flip", true);
            pos.set(viewKey(v), x);
        }
        j.set("position", pos);
        if (!p.code.empty()) j.set("code", p.code);
        Json hid = Json::array();
        for (int v = 0; v < NUM_VIEWS; v++) if (p.hiddenIn(v)) hid.push(viewKey(v));
        if (hid.size()) j.set("hiddenIn", hid);
        if (p.locked) j.set("locked", true);
        return j;
    }
    std::string pinRefName(const PinRef& r) const {
        const PartInst* p = doc_.part(r.part);
        if (!p) return "?";
        return p->label + "." + p->def->pins[(size_t)r.pin].id;
    }
    Json netsJson(const Connectivity& c, bool includeBreadboards) const {
        Json a = Json::array();
        for (int n = 0; n < c.numNets(); n++) {
            Json pins = Json::array();
            int real = 0;
            for (auto& r : c.netPins(n)) {
                const PartInst* p = doc_.part(r.part);
                if (!p) continue;
                if (p->def->isBreadboard && !includeBreadboards) continue;
                pins.push(pinRefName(r));
                real++;
            }
            if (real < 2) continue;
            Json j = Json::object();
            j.set("net", c.netName(n));
            j.set("pins", pins);
            a.push(j);
        }
        return a;
    }

    // Auto-placement: parts added without coordinates fill rows left to right,
    // wrapping to a new row, below whatever is already in the view.
    struct Cursor {
        bool init = false;
        float x0 = 0, x = 0, y = 0, rowH = 0;
    };
    Cursor cursor_[NUM_VIEWS];
    void resetCursors() { for (auto& c : cursor_) c = Cursor(); }

    Vec2 freeSpot(int view, const PartDef& def) {
        Cursor& c = cursor_[view];
        if (!c.init) {
            float minX = 1e9f, maxY = -1e9f;
            for (auto& p : doc_.parts) {
                if (!p.inView(view)) continue;
                Rect r = p.worldBounds(view);
                minX = std::min(minX, r.x);
                maxY = std::max(maxY, r.y + r.h);
            }
            c.init = true;
            c.x0 = c.x = minX < 1e8f ? std::round(minX / 100) * 100 : 0;
            c.y = maxY > -1e8f ? std::round((maxY + 500) / 100) * 100 : 0;
            c.rowH = 0;
        }
        const float rowWidth = view == SCH ? 6000.f : 5000.f;
        Rect b = def.bounds[view];
        if (c.x > c.x0 && c.x + b.w > c.x0 + rowWidth) {
            c.x = c.x0;
            c.y += c.rowH + 400;
            c.rowH = 0;
        }
        Vec2 pos(std::round((c.x - b.x) / 100) * 100, std::round((c.y - b.y) / 100) * 100);
        c.x += b.w + 300;
        c.rowH = std::max(c.rowH, b.h);
        return pos;
    }

    void addWire(int view, const PartInst& a, int pa, const PartInst& b, int pb, uint32_t color) {
        Wire w;
        w.id = doc_.newId();
        w.view = view;
        Vec2 p0 = a.pinPos(pa, view), p1 = b.pinPos(pb, view);
        w.pts = {p0};
        if (view == SCH && p0.x != p1.x && p0.y != p1.y) w.pts.push_back({p1.x, p0.y});
        w.pts.push_back(p1);
        w.ends[0] = {a.id, pa};
        w.ends[1] = {b.id, pb};
        w.color = color;
        w.width = doc_.rules.traceWidth;
        doc_.wires.push_back(w);
        doc_.touch();
    }

    static std::string text(const Json& j) { return j.dump(1); }
};

Json Server::tools() const {
    Json t = Json::array();
    auto add = [&](const char* name, const char* desc, Json in) {
        Json j = Json::object();
        j.set("name", name);
        j.set("description", desc);
        j.set("inputSchema", in);
        t.push(j);
    };
    Json partRef = prop("string", "Part label (e.g. \"R1\") or numeric id");
    Json pinRef = prop("string", "Pin id or name (e.g. \"anode\", \"GPIO2\", \"D13\", \"pin0\"); see get_part_info");
    add("list_parts",
        "Search the parts library (resistors, LEDs, ICs, Arduino/ESP32/Raspberry Pi boards, sensors, displays, breadboards…). "
        "Returns type ids to use with add_part.",
        schema({{"query", prop("string", "Words to match in the title or type, e.g. \"esp32\", \"74hc595\", \"led\"")},
                {"category", prop("string", "Optional category, e.g. Microcontroller, Logic, Sensors")}}));
    add("get_part_info", "Pins, properties (with allowed values) and simulation behaviour of a part type.",
        schema({{"type", prop("string", "Part type id from list_parts")}, {"properties", prop("object", "Optional property values, e.g. {\"pins\":\"16\"}")}},
               {"type"}));
    add("new_design", "Start a new, empty design (discards the in-memory design).",
        schema({{"title", prop("string", "Design title")}, {"notes", prop("string", "Project description")}}));
    add("open_design", "Open a .dez file.", schema({{"path", prop("string", "Path to a .dez file")}}, {"path"}));
    add("load_example", "Load a built-in example (LED with battery, Arduino LED, 555 blinker, ESP32 blink, Raspberry Pi LED, …).",
        schema({{"name", prop("string", "Example title or index; omit to list examples")}}));
    add("save_design",
        "Save the design as a .dez file (open it in the designElectronics app on any device). Every save is also kept as a version "
        "in the app's Version History.",
        schema({{"path", prop("string", "Output path ending in .dez; defaults to the last opened/saved path")},
                {"message", prop("string", "Optional note for this version, e.g. \"Added status LED\"")}}));
    add("get_design", "The current design: parts with labels, properties and positions, nets, and routing status of each view.", schema({}));
    add("add_part",
        "Add a part. Without positions it is placed in free space in every view. Returns its label and pins. "
        "Coordinates are mils (1/1000 inch); breadboard holes are 100 mil apart.",
        schema({{"type", prop("string", "Part type id from list_parts")},
                {"properties", prop("object", "Property overrides, e.g. {\"resistance\":\"220Ω\"} or {\"color\":\"Red\"}")},
                {"label", prop("string", "Optional label (default R1, LED1, U1…)")},
                {"x", prop("number", "Optional x for all views")},
                {"y", prop("number", "Optional y for all views")}},
               {"type"}));
    add("set_part_properties",
        "Change properties of a part (value, colour, Pin outputs of a dev board, …) and/or its label. For a microcontroller board, "
        "properties.code sets its program (Arduino C++ sketch) which runs in the simulator.",
        schema({{"part", partRef}, {"properties", prop("object", "Properties to set")}, {"label", prop("string", "New label")},
                {"hidden_in", prop("array", "Views to hide the part in (visual only), e.g. [\"breadboard\"]; [] shows it everywhere")},
                {"locked", prop("boolean", "Lock so it can't be moved or deleted in the editor")}},
               {"part"}));
    add("move_part", "Move/rotate a part in one view.",
        schema({{"part", partRef},
                {"view", prop("string", "breadboard, schematic or pcb")},
                {"x", prop("number", "x in mils")},
                {"y", prop("number", "y in mils")},
                {"rotation", prop("number", "0, 90, 180 or 270 (clockwise)")},
                {"flip", prop("boolean", "Mirror horizontally")}},
               {"part", "view"}));
    add("place_on_breadboard",
        "Plug a part into a breadboard: moves it in the breadboard view so the given pin sits in the given hole. Holes are a1…j30 "
        "(columns 1–30 on a half+ board; a–e and f–j are connected columns), power rails tp1/tn1 (top +/−) and bp1/bn1 (bottom +/−). "
        "The part's other pins land in the holes they line up with and connect automatically.",
        schema({{"part", partRef}, {"pin", pinRef}, {"breadboard", partRef}, {"hole", prop("string", "Hole id, e.g. \"e10\" or \"tp3\"")},
                {"rotation", prop("number", "Optional 0, 90, 180 or 270 rotation applied first")}},
               {"part", "pin", "breadboard", "hole"}));
    add("remove_part", "Delete a part.", schema({{"part", partRef}}, {"part"}));
    add("connect",
        "Connect two pins with a wire. Connections made in any view count in all views; views without a wire show ratsnest "
        "lines. Default views: schematic and breadboard (use autoroute for PCB traces).",
        schema({{"from_part", partRef}, {"from_pin", pinRef}, {"to_part", partRef}, {"to_pin", pinRef},
                {"views", prop("array", "Any of breadboard, schematic, pcb")},
                {"color", prop("string", "Breadboard wire colour, e.g. #dc3232")}},
               {"from_part", "from_pin", "to_part", "to_pin"}));
    add("remove_wire", "Delete a wire by id (see get_design wires).", schema({{"wire", prop("number", "Wire id")}}, {"wire"}));
    add("simulate",
        "Run the real-time circuit simulator. Returns LED brightness (0–1), multimeter readings, motor/buzzer activity, "
        "damaged parts and warnings, and net voltages. Dev boards drive the pins listed in their 'outputs' property "
        "(e.g. GPIO2=BLINK:500, D9=PWM:30), or run their program: set a board's Arduino sketch with set_part_properties "
        "{\"code\": \"void setup(){…} void loop(){…}\"}; Serial output and program errors are returned.",
        schema({{"seconds", prop("number", "Simulated time (default 0.5 s)")},
                {"pressed", prop("array", "Labels of pushbuttons held down during the run")}}));
    add("autoroute", "Route all PCB connections (two layers with vias). Fit the PCB board around the parts first.", schema({}));
    add("run_drc", "Design rules check of the PCB: clearances, parts off the board, overlaps.", schema({}));
    add("export",
        "Export the design. Formats: gerber (zip for PCB fabs; needs path), svg (view), bom (CSV), netlist (XML), spice. "
        "Without a path, text formats are returned inline.",
        schema({{"format", prop("string", "gerber, svg, bom, netlist or spice")}, {"path", prop("string", "Output file")},
                {"view", prop("string", "For svg: breadboard, schematic or pcb")}},
               {"format"}));
    return t;
}

Json Server::call(const std::string& name, const Json& a) {
    if (name == "list_parts") {
        std::string q = lower(a["query"].str()), cat = lower(a["category"].str());
        std::vector<std::string> words;
        for (size_t i = 0; i <= q.size();) {
            size_t e = q.find(' ', i);
            if (e == std::string::npos) e = q.size();
            if (e > i) words.push_back(q.substr(i, e - i));
            i = e + 1;
        }
        Json out = Json::array();
        int total = 0;
        for (auto& e : partLibrary()) {
            if (!cat.empty() && lower(e.category) != cat) continue;
            std::string hay = lower(e.title + " " + e.type + " " + e.category);
            bool ok = true;
            for (auto& w : words) if (hay.find(w) == std::string::npos) ok = false;
            if (!ok) continue;
            if (++total > 80) continue;
            Json j = Json::object();
            j.set("type", e.type);
            j.set("title", e.title);
            j.set("category", e.category);
            if (!e.props.empty()) {
                Json p = Json::object();
                for (auto& kv : e.props) p.set(kv.first, kv.second);
                j.set("properties", p);
            }
            out.push(j);
        }
        Json r = Json::object();
        r.set("parts", out);
        if (total > 80) r.set("note", std::to_string(total) + " matches; showing 80. Narrow the query.");
        if (q.empty() && cat.empty()) {
            Json cats = Json::array();
            for (auto& c : partCategories()) cats.push(c);
            r.set("categories", cats);
        }
        return r;
    }
    if (name == "get_part_info") {
        std::string type = a["type"].str();
        PartDefPtr d = getPartDef(type, propsFromJson(a["properties"]));
        if (!d) fail("unknown part type '" + type + "' (use list_parts)");
        Json j = Json::object();
        j.set("type", type);
        j.set("title", d->title);
        j.set("category", d->category);
        j.set("description", d->description);
        j.set("simulation", d->sim.empty() ? std::string("none (pinout only)") : d->sim);
        Json props = Json::object();
        for (auto& kv : mergedProps(type, propsFromJson(a["properties"]))) props.set(kv.first, kv.second);
        j.set("properties", props);
        Json defs = Json::array();
        for (auto& pd : d->propDefs) {
            Json x = Json::object();
            x.set("key", pd.key);
            x.set("label", pd.label);
            Json o = Json::array();
            for (auto& s : pd.options) o.push(s);
            if (o.size()) x.set("options", o);
            defs.push(x);
        }
        j.set("propertyDefinitions", defs);
        if (d->isBreadboard) {
            j.set("pins", "Holes a1…j" + std::to_string(d->pins.size() > 400 ? 63 : d->pins.size() > 200 ? 30 : 17) +
                              " (a–e and f–j columns connected); rails tp/tn (top +/−) and bp/bn (bottom +/−) numbered from 1.");
        } else {
            j.set("pins", pinList(*d));
        }
        return j;
    }
    if (name == "new_design") {
        doc_.clear();
        resetCursors();
        doc_.title = a["title"].str("Untitled");
        doc_.notesText = a["notes"].str();
        path_.clear();
        Json r = Json::object();
        r.set("ok", true);
        r.set("hint", "Add parts with add_part. Typical flow: breadboard + parts → place_on_breadboard / connect → simulate; "
                      "pcb_board + autoroute + run_drc → export gerber.");
        return r;
    }
    if (name == "open_design") {
        std::string p = a["path"].str();
        bool ok = false;
        std::string txt = readTextFile(p, &ok);
        if (!ok) fail("cannot read " + p);
        Document d;
        std::string err;
        if (!d.deserialize(txt, &err)) fail("not a designElectronics file: " + err);
        doc_ = std::move(d);
        resetCursors();
        path_ = p;
        return call("get_design", Json::object());
    }
    if (name == "load_example") {
        const auto& ex = examples();
        if (!a.has("name")) {
            Json list = Json::array();
            for (auto& e : ex) list.push(e.title);
            Json r = Json::object();
            r.set("examples", list);
            return r;
        }
        int idx = -1;
        const Json& n = a["name"];
        for (size_t i = 0; i < ex.size(); i++) if (lower(ex[i].title) == lower(n.str())) idx = (int)i;
        if (idx < 0 && n.isNumber()) idx = n.i();
        if (idx < 0 && !n.str().empty() && std::isdigit((unsigned char)n.str()[0])) idx = std::atoi(n.str().c_str());
        if (idx < 0 || idx >= (int)ex.size()) fail("no such example; call load_example without a name to list them");
        doc_.clear();
        resetCursors();
        ex[(size_t)idx].build(doc_);
        doc_.syncWireEnds();
        path_.clear();
        return call("get_design", Json::object());
    }
    if (name == "save_design") {
        std::string p = a["path"].str(path_);
        if (p.empty()) fail("give a path ending in .dez");
        if (p.size() < 4 || p.substr(p.size() - 4) != kFileExt) p += kFileExt;
        doc_.syncWireEnds();
        history::ensureUid(doc_);
        std::string data = doc_.serialize(true);
        if (!writeTextFile(p, data)) fail("cannot write " + p);
        path_ = p;
        // saves made by an assistant show up in the app's Version History too
        LocalVersion v = history::record(doc_, data, a["message"].str());
        Json r = Json::object();
        r.set("saved", p);
        if (v.n > 0) {
            r.set("version", v.n);
            r.set("changes", v.summary);
        }
        return r;
    }
    if (name == "get_design") {
        Connectivity c = conn();
        Json j = Json::object();
        j.set("title", doc_.title);
        if (!doc_.notesText.empty()) j.set("notes", doc_.notesText);
        if (!path_.empty()) j.set("path", path_);
        Json parts = Json::array();
        for (auto& p : doc_.parts) parts.push(partSummary(p));
        j.set("parts", parts);
        Json wires = Json::array();
        for (auto& w : doc_.wires) {
            Json x = Json::object();
            x.set("id", w.id);
            x.set("view", viewKey(w.view));
            std::string from = w.ends[0].attached() ? pinRefName({w.ends[0].part, w.ends[0].pin}) : "free";
            std::string to = w.ends[1].attached() ? pinRefName({w.ends[1].part, w.ends[1].pin}) : "free";
            x.set("from", from);
            x.set("to", to);
            wires.push(x);
        }
        j.set("wires", wires);
        j.set("nets", netsJson(c, false));
        Json st = Json::object();
        for (int v = 0; v < NUM_VIEWS; v++) {
            RoutingStatus s;
            auto rats = c.ratsnest(doc_, v, &s);
            Json x = Json::object();
            x.set("nets", s.nets);
            x.set("routed", s.routedNets);
            x.set("connectionsToRoute", s.connectionsToRoute);
            Json un = Json::array();
            for (auto& rl : rats) {
                if (un.size() >= 20) break;
                un.push(pinRefName(rl.pa) + " – " + pinRefName(rl.pb));
            }
            if (un.size()) x.set("unrouted", un);
            st.set(viewKey(v), x);
        }
        j.set("routing", st);
        return j;
    }
    if (name == "add_part") {
        std::string type = a["type"].str();
        if (!isKnownType(type)) fail("unknown part type '" + type + "' (use list_parts)");
        PartDefPtr def = getPartDef(type, propsFromJson(a["properties"]));
        if (!def) fail("invalid properties for " + type);
        Vec2 spots[NUM_VIEWS];
        for (int v = 0; v < NUM_VIEWS; v++) if (def->inView(v)) spots[v] = freeSpot(v, *def);
        PartInst& p = doc_.addPart(type, propsFromJson(a["properties"]), {0, 0});
        for (int v = 0; v < NUM_VIEWS; v++) p.place[v].pos = spots[v];
        if (a.has("x") || a.has("y"))
            for (int v = 0; v < NUM_VIEWS; v++) p.place[v].pos = {a["x"].f(spots[v].x), a["y"].f(spots[v].y)};
        if (a.has("label")) p.label = a["label"].str();
        doc_.touch();
        Json j = partSummary(p);
        if (!p.def->isBreadboard) j.set("pins", pinList(*p.def));
        if (p.def->sim.empty() && !p.def->isBreadboard && !p.def->isBoard) j.set("note", "No simulation model: pinout only.");
        return j;
    }
    if (name == "set_part_properties") {
        PartInst& p = findPart(a["part"]);
        for (auto& kv : propsFromJson(a["properties"])) {
            if (kv.first == "code") p.code = kv.second;  // the board's program (Arduino sketch)
            else p.props[kv.first] = kv.second;
        }
        p.refresh();
        if (!p.def) fail("invalid properties for " + p.type);
        if (a.has("label")) p.label = a["label"].str();
        if (a["hidden_in"].isArray()) {
            p.hidden = 0;
            for (auto& v : a["hidden_in"].items()) p.hidden |= 1u << viewFromName(v.str());
        }
        if (a.has("locked")) p.locked = a["locked"].boolean();
        doc_.syncWireEnds();
        doc_.touch();
        return partSummary(p);
    }
    if (name == "move_part") {
        PartInst& p = findPart(a["part"]);
        int v = viewFromName(a["view"].str());
        Xform& x = p.place[v];
        if (a.has("x")) x.pos.x = a["x"].f();
        if (a.has("y")) x.pos.y = a["y"].f();
        if (a.has("rotation")) x.rot = ((a["rotation"].i() / 90) % 4 + 4) % 4;
        if (a.has("flip")) x.flip = a["flip"].boolean();
        doc_.syncWireEnds();
        doc_.touch();
        return partSummary(p);
    }
    if (name == "place_on_breadboard") {
        PartInst& p = findPart(a["part"]);
        PartInst& bb = findPart(a["breadboard"]);
        if (!bb.def->isBreadboard) fail(bb.label + " is not a breadboard");
        int pin = findPin(p, a["pin"]);
        int hole = bb.def->pinIndex(a["hole"].str());
        if (hole < 0) fail("breadboard has no hole '" + a["hole"].str() + "' (holes a1…j30, rails tp1/tn1/bp1/bn1)");
        if (a.has("rotation")) p.place[BB].rot = ((a["rotation"].i() / 90) % 4 + 4) % 4;
        Vec2 target = bb.pinPos(hole, BB), cur = p.pinPos(pin, BB);
        p.place[BB].pos = p.place[BB].pos + (target - cur);
        doc_.syncWireEnds();
        doc_.touch();
        // report where every pin landed and what it now connects to, so shorts are visible
        Connectivity c = conn();
        Json landed = Json::object();
        Json warnings = Json::array();
        for (size_t i = 0; i < p.def->pins.size(); i++) {
            Vec2 pp = p.pinPos((int)i, BB);
            std::string where = "not in a hole";
            for (size_t h = 0; h < bb.def->pins.size(); h++) {
                Vec2 hp = bb.pinPos((int)h, BB);
                if (std::fabs(hp.x - pp.x) < 15 && std::fabs(hp.y - pp.y) < 15) { where = bb.def->pins[h].id; break; }
            }
            Json x = Json::object();
            x.set("hole", where);
            Json others = Json::array();
            int net = c.netOf(p.id, (int)i);
            for (auto& r : c.netPins(net)) {
                const PartInst* o = doc_.part(r.part);
                if (!o || o->def->isBreadboard || (r.part == p.id && r.pin == (int)i)) continue;
                if (r.part == p.id) warnings.push(p.label + "." + p.def->pins[i].id + " is shorted to its own pin " + o->def->pins[(size_t)r.pin].id);
                if (others.size() < 12) others.push(pinRefName(r));
            }
            if (others.size()) x.set("connectedTo", others);
            landed.set(p.def->pins[i].id, x);
            if (where.size() > 2 && (where[0] == 't' || where[0] == 'b') && (where[1] == 'p' || where[1] == 'n'))
                warnings.push(p.label + "." + p.def->pins[i].id + " is on a power rail (" + where + ")");
            if (where == "not in a hole") warnings.push(p.label + "." + p.def->pins[i].id + " is off the breadboard");
        }
        Json j = Json::object();
        j.set("part", p.label);
        j.set("pins", landed);
        if (warnings.size()) j.set("warnings", warnings);
        return j;
    }
    if (name == "remove_part") {
        PartInst& p = findPart(a["part"]);
        std::string l = p.label;
        doc_.removePart(p.id);
        Json j = Json::object();
        j.set("removed", l);
        return j;
    }
    if (name == "connect") {
        PartInst& pa = findPart(a["from_part"]);
        PartInst& pb = findPart(a["to_part"]);
        int ia = findPin(pa, a["from_pin"]), ib = findPin(pb, a["to_pin"]);
        std::vector<int> views;
        if (a["views"].isArray()) {
            for (auto& v : a["views"].items()) views.push_back(viewFromName(v.str()));
        } else {
            views = {SCH, BB};
        }
        uint32_t color = parseColor(a["color"].str("#418ce1"));
        int paId = pa.id, pbId = pb.id;
        for (int v : views) {
            const PartInst* A = doc_.part(paId);
            const PartInst* B = doc_.part(pbId);
            if (!A->inView(v) || !B->inView(v)) continue;
            addWire(v, *A, ia, *B, ib, color);
        }
        Connectivity c = conn();
        const PartInst* A = doc_.part(paId);
        Json j = Json::object();
        j.set("connected", pinRefName({paId, ia}) + " ↔ " + pinRefName({pbId, ib}));
        j.set("net", c.netName(c.netOf(A->id, ia)));
        return j;
    }
    if (name == "remove_wire") {
        if (!doc_.wire(a["wire"].i())) fail("no wire " + a["wire"].dump());
        doc_.removeWire(a["wire"].i());
        Json j = Json::object();
        j.set("removed", a["wire"].i());
        return j;
    }
    if (name == "simulate") {
        Connectivity c = conn();
        std::set<int> pressed;
        for (auto& b : a["pressed"].items()) pressed.insert(findPart(b).id);
        double secs = std::clamp(a["seconds"].num(0.5), 0.001, 30.0);
        Simulator sim;
        SimResult r;
        std::map<int, std::string> serial;
        for (double t = 0; t < secs - 1e-9; t += 0.05) {
            r = sim.step(doc_, c, pressed, std::min(0.05, secs - t));
            for (auto& kv : r.serial) serial[kv.first] += kv.second;
        }
        auto label = [&](int id) { const PartInst* p = doc_.part(id); return p ? p->label : std::to_string(id); };
        Json j = Json::object();
        j.set("status", r.message);
        j.set("simulatedSeconds", r.time);
        Json leds = Json::object();
        for (auto& kv : r.brightness) leds.set(label(kv.first), std::round(kv.second * 100) / 100);
        j.set("ledBrightness", leds);
        if (!r.segments.empty()) {
            Json seg = Json::object();
            for (auto& kv : r.segments) {
                std::string lit;
                const char* names = "abcdefgp";
                for (size_t i = 0; i < kv.second.size() && i < 8; i++) if (kv.second[i] > 0.3f) lit += names[i];
                seg.set(label(kv.first), lit.empty() ? "dark" : "lit segments: " + lit);
            }
            j.set("displays", seg);
        }
        Json meters = Json::object();
        for (auto& kv : r.meterText) meters.set(label(kv.first), kv.second);
        if (meters.size()) j.set("meters", meters);
        Json act = Json::object();
        for (auto& kv : r.activity) act.set(label(kv.first), std::round(kv.second * 100) / 100);
        if (act.size()) j.set("activity", act);
        Json dmg = Json::array();
        for (int id : r.damaged) dmg.push(label(id));
        if (dmg.size()) j.set("damaged", dmg);
        std::map<int, std::string> serialAll;
        (void)serialAll;
        Json warn = Json::array();
        for (auto& w : r.warnings) warn.push(w);
        if (warn.size()) j.set("warnings", warn);
        Json volts = Json::object();
        for (int n = 0; n < (int)r.netVoltage.size() && n < c.numNets(); n++) {
            int real = 0;
            for (auto& pr : c.netPins(n)) { const PartInst* p = doc_.part(pr.part); if (p && !p->def->isBreadboard) real++; }
            if (real < 2) continue;
            volts.set(c.netName(n), std::round(r.netVoltage[(size_t)n] * 1000) / 1000);
        }
        j.set("netVoltages", volts);
        if (!serial.empty()) {
            Json so = Json::object();
            for (auto& kv : serial) so.set(label(kv.first), kv.second.size() > 4000 ? kv.second.substr(kv.second.size() - 4000) : kv.second);
            j.set("serialOutput", so);
        }
        if (!r.code.empty()) {
            Json cs = Json::object();
            for (auto& kv : r.code) {
                Json x = Json::object();
                x.set("running", kv.second.running);
                if (kv.second.errorLine) { x.set("errorLine", kv.second.errorLine); x.set("error", kv.second.error); }
                cs.set(label(kv.first), x);
            }
            j.set("programs", cs);
        }
        return j;
    }
    if (name == "autoroute") {
        bool hasBoard = false;
        for (auto& p : doc_.parts) if (p.def->isBoard) hasBoard = true;
        if (!hasBoard) fail("add a PCB board first: add_part type pcb_board with properties width/height in mm, then move it under the parts in the pcb view");
        Connectivity c = conn();
        RouteResult rr = autoroute(doc_, c, true);
        doc_.touch();
        Json j = Json::object();
        j.set("routed", rr.routed);
        j.set("failed", rr.failed);
        j.set("message", rr.message);
        return j;
    }
    if (name == "run_drc") {
        Connectivity c = conn();
        Json issues = Json::array();
        for (auto& is : runDrc(doc_, c)) issues.push(is.message);
        Json j = Json::object();
        j.set("issues", issues);
        j.set("ok", issues.size() == 0);
        return j;
    }
    if (name == "export") {
        std::string fmt = lower(a["format"].str()), p = a["path"].str();
        Connectivity c = conn();
        std::string out;
        if (fmt == "gerber") {
            if (p.empty()) fail("gerber export needs a path (a .zip file)");
            std::string msg;
            if (!exportGerberZip(doc_, c, p, &msg)) fail(msg.empty() ? "gerber export failed" : msg);
            Json j = Json::object();
            j.set("saved", p);
            if (!msg.empty()) j.set("message", msg);
            return j;
        }
        if (fmt == "svg") out = exportSvg(doc_, c, viewFromName(a["view"].str(), PCB));
        else if (fmt == "bom") out = exportBomCsv(doc_);
        else if (fmt == "netlist") out = exportNetlistXml(doc_, c);
        else if (fmt == "spice") out = exportSpice(doc_, c);
        else fail("unknown format '" + fmt + "' (gerber, svg, bom, netlist, spice)");
        Json j = Json::object();
        if (!p.empty()) {
            if (!writeTextFile(p, out)) fail("cannot write " + p);
            j.set("saved", p);
        } else {
            j.set("content", out);
        }
        return j;
    }
    fail("unknown tool '" + name + "'");
}

Json Server::handle(const Json& msg, bool& reply) {
    std::string method = msg["method"].str();
    reply = msg.has("id");
    Json res = Json::object();
    res.set("jsonrpc", "2.0");
    if (reply) res.set("id", msg["id"]);
    auto error = [&](int code, const std::string& m) {
        Json e = Json::object();
        e.set("code", code);
        e.set("message", m);
        res.set("error", e);
        return res;
    };
    if (method == "initialize") {
        Json r = Json::object();
        std::string pv = msg["params"]["protocolVersion"].str(kProtocolVersion);
        r.set("protocolVersion", pv);
        Json caps = Json::object();
        caps.set("tools", Json::object());
        r.set("capabilities", caps);
        Json info = Json::object();
        info.set("name", "designElectronics");
        info.set("version", kAppVersion);
        r.set("serverInfo", info);
        r.set("instructions",
              "Design electronics: breadboard, schematic and PCB views that share one netlist. Find parts with list_parts, add them "
              "with add_part, wire pins with connect (or plug parts into a breadboard with place_on_breadboard), check behaviour with "
              "simulate, then for a PCB add a pcb_board, autoroute, run_drc and export gerber. Save with save_design to a .dez file "
              "the user can open in the designElectronics app.");
        res.set("result", r);
        return res;
    }
    if (method == "ping") { res.set("result", Json::object()); return res; }
    if (method == "tools/list") {
        Json r = Json::object();
        r.set("tools", tools());
        res.set("result", r);
        return res;
    }
    if (method == "tools/call") {
        const Json& p = msg["params"];
        Json content = Json::array();
        Json item = Json::object();
        item.set("type", "text");
        Json r = Json::object();
        try {
            Json out = call(p["name"].str(), p["arguments"].isObject() ? p["arguments"] : Json::object());
            item.set("text", text(out));
            r.set("isError", false);
        } catch (const ToolError& e) {
            item.set("text", "Error: " + e.msg);
            r.set("isError", true);
        }
        content.push(item);
        r.set("content", content);
        res.set("result", r);
        return res;
    }
    if (!reply) return res;  // notifications (initialized, cancelled, …)
    return error(-32601, "method not found: " + method);
}

}  // namespace

int runMcpServer() {
    // Protocol messages go to the real stdout; anything else printed by the
    // library code (diagnostics) is redirected to stderr so it can't corrupt
    // the JSON-RPC stream.
    fflush(stdout);
    int protoFd = DE_DUP(DE_FILENO(stdout));
    DE_DUP2(DE_FILENO(stderr), DE_FILENO(stdout));
    FILE* proto = DE_FDOPEN(protoFd, "w");
    if (!proto) return 1;

    Server server;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.find_first_not_of(" \t") == std::string::npos) continue;
        std::string err;
        Json msg = Json::parse(line, &err);
        Json out;
        bool reply = true;
        if (!err.empty() || !msg.isObject()) {
            out = Json::object();
            out.set("jsonrpc", "2.0");
            out.set("id", nullptr);
            Json e = Json::object();
            e.set("code", -32700);
            e.set("message", "parse error: " + err);
            out.set("error", e);
        } else {
            out = server.handle(msg, reply);
        }
        if (!reply) continue;
        std::string s = out.dump(-1);
        fwrite(s.data(), 1, s.size(), proto);
        fputc('\n', proto);
        fflush(proto);
    }
    return 0;
}
