#include "doc.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <set>

// ---------------------------------------------------------------------------
// File helpers (SDL IO works with Android assets / content URIs too)
// ---------------------------------------------------------------------------

std::string readTextFile(const std::string& path, bool* ok) {
    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (!data) { if (ok) *ok = false; return {}; }
    std::string s((const char*)data, size);
    SDL_free(data);
    if (ok) *ok = true;
    return s;
}

bool writeTextFile(const std::string& path, const std::string& data) {
    return SDL_SaveFile(path.c_str(), data.data(), data.size());
}

std::string fileBaseName(const std::string& path) {
    size_t s = path.find_last_of("/\\");
    std::string n = s == std::string::npos ? path : path.substr(s + 1);
    size_t d = n.find_last_of('.');
    return d == std::string::npos ? n : n.substr(0, d);
}

// ---------------------------------------------------------------------------
// Document
// ---------------------------------------------------------------------------

void Document::clear() {
    uid.clear();
    git = GitLink();
    title = "Untitled";
    author.clear();
    notesText.clear();
    nextId = 1;
    parts.clear();
    wires.clear();
    notes.clear();
    rules = DesignRules();
    touch();
}

PartInst* Document::part(int id) {
    for (auto& p : parts) if (p.id == id) return &p;
    return nullptr;
}
const PartInst* Document::part(int id) const {
    for (auto& p : parts) if (p.id == id) return &p;
    return nullptr;
}
Wire* Document::wire(int id) {
    for (auto& w : wires) if (w.id == id) return &w;
    return nullptr;
}
Note* Document::note(int id) {
    for (auto& n : notes) if (n.id == id) return &n;
    return nullptr;
}
int Document::partIndex(int id) const {
    for (size_t i = 0; i < parts.size(); i++) if (parts[i].id == id) return (int)i;
    return -1;
}

std::string Document::nextLabel(const std::string& prefix) const {
    int maxN = 0;
    for (auto& p : parts) {
        if (p.label.size() > prefix.size() && p.label.compare(0, prefix.size(), prefix) == 0) {
            int n = std::atoi(p.label.c_str() + prefix.size());
            maxN = std::max(maxN, n);
        }
    }
    return prefix + std::to_string(maxN + 1);
}

PartInst& Document::addPart(const std::string& type, const Props& props, Vec2 pos) {
    PartInst p;
    p.id = newId();
    p.type = type;
    p.props = props;
    p.refresh();
    p.label = nextLabel(p.def ? p.def->labelPrefix : "U");
    for (int v = 0; v < NUM_VIEWS; v++) p.place[v].pos = pos;
    // Boards and breadboards go to the back so parts sit on top of them.
    if (p.def && (p.def->isBoard || p.def->isBreadboard)) parts.insert(parts.begin(), p);
    else parts.push_back(p);
    touch();
    return *part(p.id);
}

void Document::removePart(int id) {
    parts.erase(std::remove_if(parts.begin(), parts.end(), [&](const PartInst& p) { return p.id == id; }), parts.end());
    for (auto& w : wires)
        for (auto& e : w.ends)
            if (e.part == id) e = WireEnd();
    touch();
}

void Document::removeWire(int id) {
    wires.erase(std::remove_if(wires.begin(), wires.end(), [&](const Wire& w) { return w.id == id; }), wires.end());
    touch();
}

void Document::removeNote(int id) {
    notes.erase(std::remove_if(notes.begin(), notes.end(), [&](const Note& n) { return n.id == id; }), notes.end());
    touch();
}

void Document::syncWireEnds() {
    for (auto& w : wires) {
        for (int e = 0; e < 2; e++) {
            WireEnd& end = w.ends[e];
            if (!end.attached()) continue;
            const PartInst* p = part(end.part);
            if (!p || !p->def || end.pin >= (int)p->def->pins.size() || !p->inView(w.view)) { end = WireEnd(); continue; }
            Vec2 pos = p->pinPos(end.pin, w.view);
            if (e == 0) w.pts.front() = pos; else w.pts.back() = pos;
        }
    }
}

static Json vec(Vec2 v) { Json a = Json::array(); a.push(Json(std::round(v.x * 100) / 100)); a.push(Json(std::round(v.y * 100) / 100)); return a; }
static Vec2 vecOf(const Json& j) { return {j[(size_t)0].f(), j[(size_t)1].f()}; }

Json Document::toJson(bool full) const {
    Json j = Json::object();
    j.set("format", kFileFormat);
    j.set("version", kFileVersion);
    j.set("app", std::string("designElectronics ") + kAppVersion);
    if (!uid.empty()) j.set("uid", uid);
    j.set("title", title);
    if (!author.empty()) j.set("author", author);
    if (!notesText.empty()) j.set("notes", notesText);
    if (full) {
        char buf[64];
        time_t t = time(nullptr);
        strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", gmtime(&t));
        j.set("saved", buf);
    }
    if (!git.repo.empty()) {
        Json g = Json::object();
        g.set("repo", git.repo);
        g.set("path", git.path);
        if (!git.branch.empty()) g.set("branch", git.branch);
        j.set("github", g);
    }
    Json rj = Json::object();
    rj.set("traceWidth", rules.traceWidth);
    rj.set("clearance", rules.clearance);
    rj.set("viaPad", rules.viaPad);
    rj.set("viaDrill", rules.viaDrill);
    j.set("rules", rj);
    j.set("nextId", nextId);

    Json parr = Json::array();
    for (auto& p : parts) {
        Json o = Json::object();
        o.set("id", p.id);
        o.set("type", p.type);
        o.set("label", p.label);
        Json props = Json::object();
        for (auto& kv : mergedProps(p.type, p.props)) props.set(kv.first, kv.second);
        o.set("props", props);
        Json views = Json::object();
        for (int v = 0; v < NUM_VIEWS; v++) {
            if (!p.inView(v)) continue;
            Json pv = Json::object();
            pv.set("at", vec(p.place[v].pos));
            if (p.place[v].rot) pv.set("rot", p.place[v].rot * 90);
            if (p.place[v].flip) pv.set("flip", true);
            if (p.hiddenIn(v)) pv.set("hidden", true);
            views.set(viewKey(v), pv);
        }
        o.set("views", views);
        if (p.locked) o.set("locked", true);
        if (!p.code.empty()) o.set("code", p.code);
        parr.push(o);
    }
    j.set("parts", parr);

    Json warr = Json::array();
    for (auto& w : wires) {
        Json o = Json::object();
        o.set("id", w.id);
        o.set("view", viewKey(w.view));
        Json pts = Json::array();
        for (auto& p : w.pts) pts.push(vec(p));
        o.set("points", pts);
        for (int e = 0; e < 2; e++) {
            const WireEnd& end = w.ends[e];
            const PartInst* p = end.attached() ? part(end.part) : nullptr;
            if (p && p->def && end.pin < (int)p->def->pins.size()) {
                Json c = Json::object();
                c.set("part", end.part);
                c.set("pin", p->def->pins[end.pin].id);
                o.set(e == 0 ? "from" : "to", c);
            }
        }
        if (w.view == BB) o.set("color", colorToHex(w.color));
        if (w.view == PCB) {
            o.set("width", w.width);
            o.set("layer", w.layer == TOP ? "top" : "bottom");
            if (w.autorouted) o.set("autorouted", true);
        }
        if (w.tidy) o.set("tidy", true);
        warr.push(o);
    }
    j.set("wires", warr);

    Json narr = Json::array();
    for (auto& n : notes) {
        Json o = Json::object();
        o.set("id", n.id);
        o.set("view", viewKey(n.view));
        o.set("at", vec(n.pos));
        o.set("size", vec(n.size));
        o.set("text", n.text);
        narr.push(o);
    }
    j.set("notes_", narr);

    // Embed every part definition used so the file opens anywhere, offline,
    // even on an install that doesn't have these parts.
    if (!full) return j;
    Json lib = Json::array();
    std::set<std::string> seen;
    for (auto& p : parts) {
        if (!p.def || seen.count(p.type)) continue;
        seen.insert(p.type);
        lib.push(partDefToJson(*p.def));
    }
    j.set("library", lib);
    return j;
}

std::string Document::compactJson() const {
    Json j = toJson(false);
    Json lib = Json::array();
    std::set<std::string> seen;
    for (auto& p : parts) {
        if (!p.def || seen.count(p.type) || !isCustomType(p.type)) continue;
        seen.insert(p.type);
        lib.push(partDefToJson(*p.def));
    }
    if (lib.size()) j.set("library", lib);
    return j.dump(-1);
}

static int viewFromKey(const std::string& s) {
    for (int v = 0; v < NUM_VIEWS; v++) if (s == viewKey(v)) return v;
    return -1;
}

bool Document::fromJson(const Json& j, std::string* err) {
    if (!j.isObject() || j["format"].str() != kFileFormat) {
        if (err) *err = "Not a designElectronics file.";
        return false;
    }
    if (j["version"].i() > kFileVersion) {
        // Newer files are still read best-effort; unknown fields are ignored.
        SDL_Log("File version %d is newer than supported %d", j["version"].i(), kFileVersion);
    }
    Document d;
    d.uid = j["uid"].str();
    d.git.repo = j["github"]["repo"].str();
    d.git.path = j["github"]["path"].str();
    d.git.branch = j["github"]["branch"].str();
    d.title = j["title"].str("Untitled");
    d.author = j["author"].str();
    d.notesText = j["notes"].str();
    const Json& rj = j["rules"];
    d.rules.traceWidth = rj["traceWidth"].f(24);
    d.rules.clearance = rj["clearance"].f(12);
    d.rules.viaPad = rj["viaPad"].f(40);
    d.rules.viaDrill = rj["viaDrill"].f(20);

    // Register embedded definitions for parts this install doesn't know,
    // and always for custom parts (the file is their source of truth).
    for (auto& lj : j["library"].items()) {
        std::string type = lj["type"].str();
        if (type.empty()) continue;
        if (!isKnownType(type) || isCustomType(type)) {
            PartDef def;
            std::string e;
            if (partDefFromJson(lj, def, &e)) registerCustomPart(def);
        }
    }

    int maxId = 0;
    for (auto& o : j["parts"].items()) {
        PartInst p;
        p.id = o["id"].i();
        p.type = o["type"].str();
        p.label = o["label"].str();
        for (auto& kv : o["props"].members()) p.props[kv.first] = kv.second.isString() ? kv.second.str() : kv.second.dump();
        p.refresh();
        if (!p.def) { SDL_Log("Skipping unknown part type '%s'", p.type.c_str()); continue; }
        for (auto& kv : o["views"].members()) {
            int v = viewFromKey(kv.first);
            if (v < 0) continue;
            p.place[v].pos = vecOf(kv.second["at"]);
            p.place[v].rot = ((kv.second["rot"].i() / 90) % 4 + 4) % 4;
            p.place[v].flip = kv.second["flip"].boolean();
            if (kv.second["hidden"].boolean()) p.hidden |= 1u << v;
        }
        p.locked = o["locked"].boolean();
        p.code = o["code"].str();
        if (p.code.empty() && p.props.count("code")) { p.code = p.props["code"]; p.props.erase("code"); }
        maxId = std::max(maxId, p.id);
        d.parts.push_back(p);
    }
    for (auto& o : j["wires"].items()) {
        Wire w;
        w.id = o["id"].i();
        w.view = std::max(0, viewFromKey(o["view"].str("breadboard")));
        for (auto& pt : o["points"].items()) w.pts.push_back(vecOf(pt));
        if (w.pts.size() < 2) continue;
        const char* keys[2] = {"from", "to"};
        for (int e = 0; e < 2; e++) {
            const Json& c = o[keys[e]];
            if (!c.isObject()) continue;
            const PartInst* p = d.part(c["part"].i());
            if (!p) continue;
            int pin = p->def->pinIndex(c["pin"].str());
            if (pin >= 0) { w.ends[e].part = p->id; w.ends[e].pin = pin; }
        }
        if (o.has("color")) w.color = parseColor(o["color"].str(), w.color);
        w.width = o["width"].f(24);
        w.layer = o["layer"].str() == "bottom" ? BOTTOM : TOP;
        w.autorouted = o["autorouted"].boolean();
        w.tidy = o["tidy"].boolean();
        maxId = std::max(maxId, w.id);
        d.wires.push_back(w);
    }
    for (auto& o : j["notes_"].items()) {
        Note n;
        n.id = o["id"].i();
        n.view = std::max(0, viewFromKey(o["view"].str("breadboard")));
        n.pos = vecOf(o["at"]);
        n.size = o.has("size") ? vecOf(o["size"]) : Vec2(1500, 800);
        n.text = o["text"].str();
        maxId = std::max(maxId, n.id);
        d.notes.push_back(n);
    }
    d.nextId = std::max(j["nextId"].i(1), maxId + 1);
    d.syncWireEnds();
    d.revision = revision + 1;
    *this = std::move(d);
    return true;
}

bool Document::deserialize(const std::string& text, std::string* err) {
    std::string perr;
    Json j = Json::parse(text, &perr);
    if (!perr.empty()) { if (err) *err = "Could not read file: " + perr; return false; }
    return fromJson(j, err);
}

// ---------------------------------------------------------------------------
// Connectivity
// ---------------------------------------------------------------------------

namespace {
struct UF {
    std::vector<int> p;
    explicit UF(int n) : p((size_t)n) { for (int i = 0; i < n; i++) p[(size_t)i] = i; }
    int find(int x) { while (p[(size_t)x] != x) { p[(size_t)x] = p[(size_t)p[(size_t)x]]; x = p[(size_t)x]; } return x; }
    void unite(int a, int b) { a = find(a); b = find(b); if (a != b) p[(size_t)std::max(a, b)] = std::min(a, b); }
};

struct SpatialHash {
    float cell;
    std::unordered_map<long long, std::vector<std::pair<Vec2, int>>> m;
    explicit SpatialHash(float c) : cell(c) {}
    static long long key(int x, int y) { return ((long long)x << 32) ^ (unsigned)y; }
    void add(Vec2 p, int id) { m[key((int)std::floor(p.x / cell), (int)std::floor(p.y / cell))].push_back({p, id}); }
    template <class F> void query(Vec2 p, float r, F f) const {
        int x0 = (int)std::floor((p.x - r) / cell), x1 = (int)std::floor((p.x + r) / cell);
        int y0 = (int)std::floor((p.y - r) / cell), y1 = (int)std::floor((p.y + r) / cell);
        for (int x = x0; x <= x1; x++)
            for (int y = y0; y <= y1; y++) {
                auto it = m.find(key(x, y));
                if (it == m.end()) continue;
                for (auto& e : it->second) if (dist(e.first, p) <= r) f(e.second);
            }
    }
};

constexpr float kPinSnap = 15;   // pins within this distance (mils) touch
}  // namespace

int Connectivity::node(int partId, int pin) const {
    auto it = partBase_.find(partId);
    return it == partBase_.end() ? -1 : it->second + pin;
}

void Connectivity::build(const Document& doc) {
    partBase_.clear();
    wireNode_.clear();
    int n = 0;
    for (auto& p : doc.parts) {
        partBase_[p.id] = n;
        n += p.def ? (int)p.def->pins.size() : 0;
    }
    for (auto& w : doc.wires) wireNode_[w.id] = n++;

    UF global(n);
    for (int v = 0; v < NUM_VIEWS; v++) {
        UF uf(n);
        touched_[v].assign((size_t)n, 0);
        auto both = [&](int a, int b) { uf.unite(a, b); global.unite(a, b); };
        SpatialHash pins(100);
        for (auto& p : doc.parts) {
            if (!p.inView(v)) continue;
            int base = partBase_[p.id];
            for (auto& bus : p.def->buses)
                for (size_t i = 1; i < bus.size(); i++) both(base + bus[0], base + bus[i]);
            for (size_t i = 0; i < p.def->pins.size(); i++) pins.add(p.pinPos((int)i, v), base + (int)i);
        }
        // pins touching pins of other parts (parts plugged into breadboards/headers)
        for (auto& p : doc.parts) {
            if (!p.inView(v)) continue;
            int base = partBase_[p.id];
            int cnt = (int)p.def->pins.size();
            for (int i = 0; i < cnt; i++) {
                int me = base + i;
                pins.query(p.pinPos(i, v), kPinSnap, [&](int other) {
                    if (other >= base && other < base + cnt) return;
                    both(me, other);
                    touched_[v][(size_t)me] = touched_[v][(size_t)other] = 1;
                });
            }
        }
        // named nets (ground / power / net labels) are connected within the view too
        std::map<std::string, int> namedInView;
        for (auto& p : doc.parts) {
            if (!p.inView(v) || p.def->netProp.empty() || p.def->pins.empty()) continue;
            std::string name = p.prop(p.def->netProp);
            int nd = partBase_[p.id];
            auto it = namedInView.find(name);
            if (it == namedInView.end()) namedInView[name] = nd;
            else both(it->second, nd);
        }
        // wires
        SpatialHash ends(100);
        for (auto& w : doc.wires) {
            if (w.view != v) continue;
            int wn = wireNode_[w.id];
            for (int e = 0; e < 2; e++) {
                Vec2 pt = e == 0 ? w.pts.front() : w.pts.back();
                ends.add(pt, wn);
                if (w.ends[e].attached()) {
                    int pn = node(w.ends[e].part, w.ends[e].pin);
                    if (pn >= 0) { both(wn, pn); touched_[v][(size_t)pn] = 1; }
                } else {
                    pins.query(pt, kPinSnap, [&](int pn) { both(wn, pn); touched_[v][(size_t)pn] = 1; });
                }
            }
        }
        for (auto& w : doc.wires) {
            if (w.view != v) continue;
            int wn = wireNode_[w.id];
            for (int e = 0; e < 2; e++)
                ends.query(e == 0 ? w.pts.front() : w.pts.back(), 3, [&](int other) { both(wn, other); });
        }
        viewRoot_[v].resize((size_t)n);
        for (int i = 0; i < n; i++) viewRoot_[v][(size_t)i] = uf.find(i);
    }
    // named nets (ground symbols, power labels, net labels)
    std::map<std::string, int> named;
    for (auto& p : doc.parts) {
        if (!p.def || p.def->netProp.empty() || p.def->pins.empty()) continue;
        std::string name = p.prop(p.def->netProp);
        int nd = partBase_[p.id];
        auto it = named.find(name);
        if (it == named.end()) named[name] = nd;
        else global.unite(it->second, nd);
    }

    // compact net ids
    std::unordered_map<int, int> rootToNet;
    globalNet_.assign((size_t)n, -1);
    netPins_.clear();
    for (auto& p : doc.parts) {
        if (!p.def) continue;
        int base = partBase_[p.id];
        for (size_t i = 0; i < p.def->pins.size(); i++) {
            int r = global.find(base + (int)i);
            auto it = rootToNet.find(r);
            int net;
            if (it == rootToNet.end()) { net = (int)netPins_.size(); rootToNet[r] = net; netPins_.emplace_back(); }
            else net = it->second;
            globalNet_[(size_t)(base + (int)i)] = net;
            netPins_[(size_t)net].push_back({p.id, (int)i});
        }
    }
    for (auto& w : doc.wires) {
        int wn = wireNode_[w.id];
        auto it = rootToNet.find(global.find(wn));
        globalNet_[(size_t)wn] = it == rootToNet.end() ? -1 : it->second;
    }
    numNets_ = (int)netPins_.size();
    netNames_.assign((size_t)numNets_, "");
    for (auto& kv : named) {
        int net = globalNet_[(size_t)kv.second];
        if (net >= 0 && netNames_[(size_t)net].empty()) netNames_[(size_t)net] = kv.first;
    }
    int k = 1;
    for (int i = 0; i < numNets_; i++) if (netNames_[(size_t)i].empty()) netNames_[(size_t)i] = "N" + std::to_string(k++);
}

int Connectivity::netOf(int partId, int pin) const {
    int nd = node(partId, pin);
    return nd < 0 || nd >= (int)globalNet_.size() ? -1 : globalNet_[(size_t)nd];
}

int Connectivity::wireNet(int wireId) const {
    auto it = wireNode_.find(wireId);
    return it == wireNode_.end() ? -1 : globalNet_[(size_t)it->second];
}

int Connectivity::viewGroup(int view, int partId, int pin) const {
    int nd = node(partId, pin);
    return nd < 0 || nd >= (int)viewRoot_[view].size() ? -1 : viewRoot_[view][(size_t)nd];
}

bool Connectivity::pinConnectedInView(int view, int partId, int pin) const {
    int nd = node(partId, pin);
    return nd >= 0 && nd < (int)touched_[view].size() && touched_[view][(size_t)nd];
}

std::vector<RatLine> Connectivity::ratsnest(const Document& doc, int view, RoutingStatus* status) const {
    std::vector<RatLine> out;
    RoutingStatus st;
    std::unordered_map<int, const PartInst*> byId;
    for (auto& p : doc.parts) byId[p.id] = &p;
    for (int net = 0; net < numNets_; net++) {
        // Group the net's pins (in this view) by their in-view connectivity.
        std::map<int, std::vector<PinRef>> groups;
        int count = 0;
        for (auto& pr : netPins_[(size_t)net]) {
            auto it = byId.find(pr.part);
            if (it == byId.end() || !it->second->inView(view)) continue;
            // Breadboards only matter in breadboard view, and their unused holes never need routing.
            if (it->second->def->isBreadboard) continue;
            groups[viewGroup(view, pr.part, pr.pin)].push_back(pr);
            count++;
        }
        if (count < 2) continue;
        st.nets++;
        if (groups.size() <= 1) { st.routedNets++; continue; }
        // Prim's MST over groups using closest pin pairs.
        std::vector<std::vector<PinRef>> g;
        for (auto& kv : groups) g.push_back(kv.second);
        std::vector<bool> in(g.size(), false);
        in[0] = true;
        for (size_t added = 1; added < g.size(); added++) {
            float best = 1e30f;
            RatLine bl;
            size_t bj = 0;
            for (size_t i = 0; i < g.size(); i++) {
                if (!in[i]) continue;
                for (size_t jj = 0; jj < g.size(); jj++) {
                    if (in[jj]) continue;
                    for (auto& a : g[i])
                        for (auto& b : g[jj]) {
                            Vec2 pa = byId[a.part]->pinPos(a.pin, view), pb = byId[b.part]->pinPos(b.pin, view);
                            float dd = dist(pa, pb);
                            if (dd < best) { best = dd; bl = {pa, pb, a, b, net}; bj = jj; }
                        }
                }
            }
            in[bj] = true;
            out.push_back(bl);
            st.connectionsToRoute++;
        }
    }
    if (status) *status = st;
    return out;
}
