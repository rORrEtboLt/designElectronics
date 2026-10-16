// Transient circuit simulator.
//
// Modified nodal analysis with Norton-equivalent sources (so a plain, possibly
// non-symmetric, conductance matrix suffices), backward-Euler capacitors and
// piecewise-linear / behavioural device models whose discrete states (diode
// on/off, op-amp saturation, logic levels, 555 latch, flip-flops, counters…)
// are iterated to consistency every time step. Part models and their pin
// mapping come from PartDef::sim / PartDef::simParams, so custom parts can be
// simulated too (see docs/FILE_FORMAT.md).
#include <array>
#include <cmath>
#include <cstdio>
#include <unordered_map>

#include "tools.h"

namespace {

// ---------------------------------------------------------------------------
// Linear system
// ---------------------------------------------------------------------------
struct Stamp {
    std::vector<double> G, I;
    int n = 0;
    void init(int nn) { n = nn; G.assign((size_t)n * n, 0); I.assign((size_t)n, 0); }
    double& g(int r, int c) { return G[(size_t)r * n + c]; }
    // Node -1 is ground; node -2 is a floating (unconnected) pin and any element touching it is ignored.
    static bool fl(int n) { return n == -2; }
    void cond(int a, int b, double gg) {
        if (fl(a) || fl(b)) return;
        if (a >= 0) g(a, a) += gg;
        if (b >= 0) g(b, b) += gg;
        if (a >= 0 && b >= 0) { g(a, b) -= gg; g(b, a) -= gg; }
    }
    // current source pushing i amps out of a, into b
    void cur(int a, int b, double i) {
        if (fl(a) || fl(b)) return;
        if (a >= 0) I[(size_t)a] -= i;
        if (b >= 0) I[(size_t)b] += i;
    }
    // Norton source: V(plus) - V(minus) = v through r
    void vsrc(int minus, int plus, double v, double r) { cond(minus, plus, 1 / r); cur(minus, plus, v / r); }
    // Controlled output: drives `out` towards  sum(c_k * V(n_k)) + k0  through 1/go (current from true ground)
    void lin(int out, const std::vector<std::pair<int, double>>& terms, double k0, double go) {
        if (out < 0) return;
        g(out, out) += go;
        for (auto& t : terms) if (t.first >= 0) g(out, t.first) -= go * t.second;
        I[(size_t)out] += go * k0;
    }
    // VCCS: current gm*(V(cp)-V(cn)) + i0 flowing from node a to node b through the device
    void vccs(int a, int b, int cp, int cn, double gm, double i0) {
        if (fl(a) || fl(b)) return;
        if (a >= 0) { if (cp >= 0) g(a, cp) += gm; if (cn >= 0) g(a, cn) -= gm; I[(size_t)a] -= i0; }
        if (b >= 0) { if (cp >= 0) g(b, cp) -= gm; if (cn >= 0) g(b, cn) += gm; I[(size_t)b] += i0; }
    }
    bool solve(std::vector<double>& x) {
        std::vector<double> A = G, b = I;
        int N = n;
        for (int c = 0; c < N; c++) {
            int piv = c;
            double best = std::fabs(A[(size_t)c * N + c]);
            for (int r = c + 1; r < N; r++) {
                double v = std::fabs(A[(size_t)r * N + c]);
                if (v > best) { best = v; piv = r; }
            }
            if (best < 1e-18) return false;
            if (piv != c) {
                for (int k = 0; k < N; k++) std::swap(A[(size_t)c * N + k], A[(size_t)piv * N + k]);
                std::swap(b[(size_t)c], b[(size_t)piv]);
            }
            double d = A[(size_t)c * N + c];
            for (int r = c + 1; r < N; r++) {
                double f = A[(size_t)r * N + c] / d;
                if (f == 0) continue;
                double* rr = &A[(size_t)r * N];
                const double* cr = &A[(size_t)c * N];
                for (int k = c; k < N; k++) rr[k] -= f * cr[k];
                b[(size_t)r] -= f * b[(size_t)c];
            }
        }
        x.assign((size_t)N, 0);
        for (int r = N - 1; r >= 0; r--) {
            double s = b[(size_t)r];
            for (int k = r + 1; k < N; k++) s -= A[(size_t)r * N + k] * x[(size_t)k];
            x[(size_t)r] = s / A[(size_t)r * N + r];
        }
        return true;
    }
};

// ---------------------------------------------------------------------------
// simParams parsing:  "KEY=val;KEY2=a,b;ITEM args;ITEM args"
// ---------------------------------------------------------------------------
struct Item {
    std::string kind, args;
};
struct Params {
    std::map<std::string, std::string> kv;
    std::vector<Item> items;
    std::set<std::string> flags;
    std::string get(const char* k, const char* def = "") const { auto it = kv.find(k); return it == kv.end() ? def : it->second; }
    double num(const char* k, double def) const { auto it = kv.find(k); return it == kv.end() ? def : std::atof(it->second.c_str()); }
};

std::string trim(std::string s) {
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { out.push_back(trim(cur)); cur.clear(); }
        else cur += c;
    }
    if (!trim(cur).empty()) out.push_back(trim(cur));
    return out;
}

Params parseParams(const std::string& s) {
    Params p;
    for (auto& part : split(s, ';')) {
        if (part.empty()) continue;
        size_t sp = part.find(' ');
        size_t eq = part.find('=');
        if (sp != std::string::npos && (eq == std::string::npos || sp < eq)) p.items.push_back({part.substr(0, sp), trim(part.substr(sp + 1))});
        else if (eq != std::string::npos) p.kv[trim(part.substr(0, eq))] = trim(part.substr(eq + 1));
        else p.flags.insert(part);
    }
    return p;
}

const Params& paramsFor(const PartDef* d) {
    static std::unordered_map<const PartDef*, Params> cache;
    auto it = cache.find(d);
    if (it != cache.end()) return it->second;
    if (cache.size() > 2000) cache.clear();
    return cache[d] = parseParams(d->simParams);
}

double ledVf(const std::string& color) {
    if (color == "Red") return 1.8;
    if (color == "Orange") return 2.0;
    if (color == "Yellow") return 2.1;
    if (color == "Green") return 2.2;
    return 3.0;
}

double powerRating(const std::string& s) {
    if (s.find("1/8") != std::string::npos) return 0.125;
    if (s.find("1/4") != std::string::npos) return 0.25;
    if (s.find("1/2") != std::string::npos) return 0.5;
    double v = std::atof(s.c_str());
    return v > 0 ? v : 0.25;
}

// 7-segment patterns for 0-9 (bits a..g)
const int kSeg[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

}  // namespace

// ---------------------------------------------------------------------------
// Simulator
// ---------------------------------------------------------------------------

struct Simulator::Impl {
    struct PState {
        std::vector<double> d;
        std::vector<int> b;
        double& D(size_t i) { if (d.size() <= i) d.resize(i + 1, 0); return d[i]; }
        int& B(size_t i) { if (b.size() <= i) b.resize(i + 1, 0); return b[i]; }
    };
    std::map<int, PState> st;
};

Simulator::Simulator() : impl_(new Impl) {}
Simulator::~Simulator() { delete impl_; }
void Simulator::reset() { impl_->st.clear(); time = 0; }

SimResult Simulator::step(const Document& doc, const Connectivity& conn, const std::set<int>& pressed, double seconds) {
    SimResult res;
    res.ran = true;
    int nets = conn.numNets();
    res.netVoltage.assign((size_t)nets, 0);

    // ---- reference net ----
    int gnd = -1;
    for (int n = 0; n < nets && gnd < 0; n++) if (conn.netName(n) == "GND") gnd = n;
    std::vector<const PartInst*> parts;
    for (auto& p : doc.parts) if (p.def && !p.def->sim.empty()) parts.push_back(&p);
    for (auto* p : parts) {
        if (gnd >= 0) break;
        if (p->def->sim == "battery") gnd = conn.netOf(p->id, p->def->pinIndex("minus"));
    }
    for (auto* p : parts) {
        if (gnd >= 0) break;
        if (p->def->sim == "board") {
            for (size_t i = 0; i < p->def->pins.size(); i++) if (p->def->pins[i].name == "GND") { gnd = conn.netOf(p->id, (int)i); break; }
        }
    }

    // ---- nodes: nets touched by ≥2 simulated pins (others float harmlessly) ----
    std::map<int, int> pinsPerNet;
    for (auto* p : parts)
        for (size_t i = 0; i < p->def->pins.size(); i++) pinsPerNet[conn.netOf(p->id, (int)i)]++;
    std::unordered_map<int, int> nodeOf;
    for (auto& kv : pinsPerNet)
        if (kv.first >= 0 && kv.first != gnd && kv.second >= 2) { int id = (int)nodeOf.size(); nodeOf[kv.first] = id; }
    const int FLOAT = -2;
    auto nodeOfPin = [&](const PartInst& p, int pin) -> int {
        if (pin < 0 || pin >= (int)p.def->pins.size()) return FLOAT;
        int net = conn.netOf(p.id, pin);
        if (net == gnd) return -1;
        auto it = nodeOf.find(net);
        return it == nodeOf.end() ? FLOAT : it->second;
    };
    auto pinIdx = [](const PartInst& p, const std::string& tok) -> int {
        if (tok.empty()) return -1;
        bool digits = true;
        for (char c : tok) if (!std::isdigit((unsigned char)c)) digits = false;
        if (digits) { int k = std::atoi(tok.c_str()) - 1; return k >= 0 && k < (int)p.def->pins.size() ? k : -1; }
        for (size_t i = 0; i < p.def->pins.size(); i++) if (p.def->pins[i].name == tok) return (int)i;
        int k = p.def->pinIndex(tok);
        if (k >= 0) return k;
        // "GPIO2" also matches a pin named "GPIO2/SDA"
        for (size_t i = 0; i < p.def->pins.size(); i++) {
            const std::string& n = p.def->pins[i].name;
            if (n.size() > tok.size() && n.compare(0, tok.size(), tok) == 0 && n[tok.size()] == '/') return (int)i;
        }
        return -1;
    };
    auto N = [&](const PartInst& p, const std::string& tok) { return nodeOfPin(p, pinIdx(p, tok)); };
    auto Nid = [&](const PartInst& p, const char* id) { return nodeOfPin(p, p.def->pinIndex(id)); };
    auto toks = [](const std::string& s) { return split(s, ','); };

    int n = (int)nodeOf.size();
    std::vector<std::string> unsupported;
    for (auto& p : doc.parts) {
        if (!p.def || !p.def->sim.empty()) continue;
        if (p.def->isBreadboard || p.def->isBoard || !p.def->netProp.empty() || p.type == "via" || p.type == "hole" ||
            p.type.rfind("header", 0) == 0 || p.type == "crystal")
            continue;
        unsupported.push_back(p.label);
    }
    if (n == 0) {
        res.message = parts.empty() ? "Nothing to simulate yet — add a battery or a board and some parts." : "Simulating (nothing is connected yet)";
        time += seconds;
        return res;
    }

    auto& ST = impl_->st;
    bool dynamic = false;
    for (auto* p : parts) {
        const std::string& m = p->def->sim;
        if (m == "capacitor" || m == "555" || m == "logic" || m == "relay") dynamic = true;
    }
    double dt = 1e-3;
    int steps = dynamic ? (int)std::clamp(std::ceil(seconds / dt), 1.0, 60.0) : 1;
    if (!dynamic) dt = std::max(seconds, 1e-3);

    std::vector<double> x((size_t)n, 0);
    auto V = [&](int node) { return node >= 0 ? x[(size_t)node] : 0.0; };
    Stamp S;
    std::map<int, double> ledAccum;
    std::map<int, std::vector<double>> segAccum;
    std::map<int, std::array<double, 3>> rgbAccum;
    double totalT = 0;
    bool solveFailed = false;
    bool anySource = false;
    int finalIters = 0;
    char buf[200];

    for (int stepI = 0; stepI < steps; stepI++) {
        double t = time + dt * (stepI + 1);
        // iterate: stamp → solve → update discrete states, until consistent
        for (int pass = 0; pass < 2; pass++) {  // pass 1 re-runs after clocked (edge) updates
            int iter;
            for (iter = 0; iter < 60; iter++) {
                S.init(n);
                for (int i = 0; i < n; i++) S.cond(i, -1, 1e-9);
                anySource = false;
                // -------------------- STAMP --------------------
                for (auto* pp : parts) {
                    const PartInst& p = *pp;
                    const PartDef& d = *p.def;
                    const std::string& m = d.sim;
                    const Params& P = paramsFor(&d);
                    auto& ps = ST[p.id];
                    if (m == "resistor") S.cond(Nid(p, "pin0"), Nid(p, "pin1"), 1 / std::max(1e-3, parseSI(p.prop("resistance"), 220)));
                    else if (m == "potentiometer") {
                        double r = std::max(1.0, parseSI(p.prop("resistance"), 10000));
                        double w = std::clamp(std::atof(p.prop("wiper").c_str()) / 100.0, 0.0, 1.0);
                        S.cond(Nid(p, "leg1"), Nid(p, "wiper"), 1 / std::max(0.5, r * w));
                        S.cond(Nid(p, "wiper"), Nid(p, "leg2"), 1 / std::max(0.5, r * (1 - w)));
                    } else if (m == "photocell") {
                        double light = std::clamp(std::atof(p.prop("light").c_str()), 0.0, 100.0);
                        S.cond(Nid(p, "pin0"), Nid(p, "pin1"), 1 / (1e6 * std::pow(10.0, -3 * light / 100)));
                    } else if (m == "battery") {
                        std::string style = p.prop("style");
                        double rint = style == "Coin cell" ? 15 : style == "AA pack" ? 0.3 : 1.5;
                        S.vsrc(Nid(p, "minus"), Nid(p, "plus"), parseSI(p.prop("voltage"), 9), rint);
                        anySource = true;
                    } else if (m == "capacitor") {
                        double C = std::max(1e-15, parseSI(p.prop("capacitance"), 1e-7));
                        int a = nodeOfPin(p, 0), b = nodeOfPin(p, 1);
                        if (a == FLOAT || b == FLOAT) continue;
                        double g = C / dt;
                        S.cond(a, b, g);
                        S.cur(b, a, g * ps.D(0));
                    } else if (m == "inductor") S.cond(Nid(p, "pin0"), Nid(p, "pin1"), 1 / 0.05);
                    else if (m == "switch") S.cond(Nid(p, "common"), Nid(p, p.prop("position") != "right" ? "a" : "b"), 1 / 0.02);
                    else if (m == "pushbutton") { if (pressed.count(p.id) || p.prop("pressed") == "yes") S.cond(Nid(p, "pin1"), Nid(p, "pin2"), 1 / 0.02); }
                    else if (m == "motor") S.cond(Nid(p, "pin0"), Nid(p, "pin1"), 1 / 10.0);
                    else if (m == "buzzer") S.cond(Nid(p, "plus"), Nid(p, "minus"), 1 / 170.0);
                    else if (m == "multimeter") {
                        bool curMode = p.prop("mode").find("Current") != std::string::npos;
                        S.cond(Nid(p, "v"), Nid(p, "com"), curMode ? 1 / 0.01 : 1 / 1e7);
                    } else if (m == "led" || m == "diode") {
                        int a = Nid(p, "anode"), c = Nid(p, "cathode");
                        double vf = m == "led" ? ledVf(p.prop("color")) : 0.7, ron = m == "led" ? 15 : 0.2;
                        if (ps.B(0)) { S.cond(a, c, 1 / ron); S.cur(c, a, vf / ron); }
                    } else if (m == "npn" || m == "pnp") {
                        bool npn = m == "npn";
                        double vf = P.num("VF", 0.65), beta = P.num("BETA", 150), ron = 50;
                        int b = Nid(p, "B"), c = Nid(p, "C"), e = Nid(p, "E");
                        if (ps.B(0)) {
                            int ja = npn ? b : e, jc = npn ? e : b;
                            S.cond(ja, jc, 1 / ron);
                            S.cur(jc, ja, vf / ron);
                            double gm = beta / ron;
                            if (ps.B(1)) S.cond(c, e, 1 / 1.0);          // saturated
                            else if (npn) S.vccs(c, e, b, e, gm, -gm * vf);  // Ic = gm (Vbe - vf), C→E
                            else S.vccs(e, c, e, b, gm, -gm * vf);         // PNP: E→C
                        }
                    } else if (m == "nmos") {
                        if (ps.B(0)) S.cond(Nid(p, "D"), Nid(p, "S"), 1 / 0.05);
                    } else if (m == "board" || m == "mcu") {
                        // ground pins
                        std::vector<int> gnds;
                        if (m == "board") {
                            for (size_t i = 0; i < d.pins.size(); i++) if (d.pins[i].name == P.get("GND", "GND")) gnds.push_back(nodeOfPin(p, (int)i));
                        } else {
                            for (auto& t : toks(P.get("GND"))) gnds.push_back(N(p, t));
                        }
                        int g0 = gnds.empty() ? -1 : gnds[0];
                        for (size_t k = 1; k < gnds.size(); k++) S.cond(g0, gnds[k], 1 / 0.01);
                        double vio = P.num("IO", 5), rout = P.num("ROUT", 30);
                        int vcc = -1;
                        bool powered = true;
                        if (m == "board") {
                            for (auto& it : P.items) {
                                if (it.kind != "RAIL") continue;
                                size_t eq = it.args.rfind('=');
                                if (eq == std::string::npos) continue;
                                std::string pname = it.args.substr(0, eq);
                                double v = std::atof(it.args.substr(eq + 1).c_str());
                                for (size_t i = 0; i < d.pins.size(); i++)
                                    if (d.pins[i].name == pname) S.vsrc(g0, nodeOfPin(p, (int)i), v, 0.1);
                            }
                            anySource = true;
                        } else {
                            auto vccs = toks(P.get("VCC"));
                            vcc = vccs.empty() ? -1 : N(p, vccs[0]);
                            for (size_t k = 1; k < vccs.size(); k++) S.cond(vcc, N(p, vccs[k]), 1 / 0.01);
                            powered = vcc != FLOAT && V(vcc) - V(g0) > 1.8;
                            S.cond(vcc, g0, 1 / 2000.0);  // supply current of the chip
                        }
                        if (!powered) continue;
                        for (auto& o : split(p.prop("outputs"), ',')) {
                            size_t eq = o.find('=');
                            if (eq == std::string::npos) continue;
                            std::string pin = trim(o.substr(0, eq)), val = trim(o.substr(eq + 1));
                            for (auto& ch : val) ch = (char)std::toupper((unsigned char)ch);
                            int node = N(p, pin);
                            if (node == FLOAT) continue;
                            double level = 0;
                            if (val == "HIGH" || val == "1") level = 1;
                            else if (val.rfind("BLINK", 0) == 0) {
                                double ms = std::max(1.0, std::atof(val.c_str() + 6));
                                level = ((long)(t * 1000 / ms)) % 2 == 0 ? 1 : 0;
                            } else if (val.rfind("PWM", 0) == 0) level = std::clamp(std::atof(val.c_str() + 4) / 100.0, 0.0, 1.0);
                            if (m == "board") S.vsrc(g0, node, level * vio, rout);
                            else if (level >= 1) S.cond(node, vcc, 1 / rout);
                            else if (level <= 0) S.cond(node, g0, 1 / rout);
                            else { S.cond(node, vcc, level / rout); S.cond(node, g0, (1 - level) / rout); }
                        }
                    } else if (m == "reg") {
                        int in = N(p, P.get("IN")), out = N(p, P.get("OUT"));
                        bool adj = !P.get("ADJ").empty();
                        int ref = adj ? N(p, P.get("ADJ")) : N(p, P.get("GND"));
                        double v = adj ? P.num("VREF", 1.25) : (P.kv.count("VPROP") ? parseSI(p.prop(P.get("VPROP")), 5) : P.num("V", 5));
                        double drop = P.num("DROP", 2);
                        int mode = ps.B(0);  // 0 off, 1 regulating, 2 dropout
                        if (mode == 1) S.vsrc(ref, out, v, 0.05);
                        else if (mode == 2 && !P.flags.count("BOOST")) S.vsrc(out, in, drop, 0.3);
                        S.cond(in, adj ? -1 : ref, 1 / 1e5);
                    } else if (m == "555") {
                        int k = 0;
                        for (auto& it : P.items) {
                            if (it.kind != "U") continue;
                            std::map<std::string, std::string> kv;
                            for (auto& a : toks(it.args)) { size_t e = a.find('='); if (e != std::string::npos) kv[a.substr(0, e)] = a.substr(e + 1); }
                            int g0 = N(p, kv["GND"]), vcc = N(p, kv["VCC"]), ctrl = N(p, kv["CTRL"]), out = N(p, kv["OUT"]), dis = N(p, kv["DIS"]);
                            S.cond(vcc, g0, 1 / 10000.0);
                            S.cond(vcc, ctrl, 1 / 5000.0);
                            S.cond(ctrl, g0, 1 / 10000.0);
                            bool powered = V(vcc) - V(g0) > 3.0;
                            if (powered) {
                                if (ps.B((size_t)k)) S.vsrc(vcc, out, -1.2, 10);
                                else { S.vsrc(g0, out, 0.1, 10); S.cond(dis, g0, 1 / 15.0); }
                            }
                            k++;
                        }
                    } else if (m == "opamp" || m == "comparator" || m == "amp386") {
                        int vcc = N(p, P.get(m == "amp386" ? "VS" : "VCC")), g0 = N(p, P.get("GND"));
                        double pw = V(vcc) - V(g0);
                        S.cond(vcc, g0, 1 / 20000.0);
                        if (pw < 1.5) continue;
                        double go = 1 / 30.0;
                        if (m == "amp386") {
                            int ip = N(p, P.get("IN+")), im = N(p, P.get("IN-")), out = N(p, P.get("OUT"));
                            double G = P.num("GAIN", 20);
                            S.cond(ip, g0, 1 / 50000.0);
                            S.cond(im, g0, 1 / 50000.0);
                            int mode = ps.B(0);
                            if (mode == 0) S.lin(out, {{vcc, 0.5}, {g0, 0.5}, {ip, G}, {im, -G}}, 0, go);
                            else if (mode == 1) S.vsrc(vcc, out, -0.5, 1 / go);
                            else S.vsrc(g0, out, 0.5, 1 / go);
                            continue;
                        }
                        int k = 0;
                        for (auto& it : P.items) {
                            auto io = split(it.args, '>');
                            if (io.size() != 2) continue;
                            auto in = toks(io[0]);
                            if (in.size() != 2) continue;
                            int ip = N(p, in[0]), im = N(p, in[1]), out = N(p, io[1]);
                            if (it.kind == "CMP") {
                                if (ps.B((size_t)k)) S.cond(out, g0, 1 / 40.0);
                            } else {
                                int mode = ps.B((size_t)k);
                                double A = 1e5;
                                if (mode == 0) S.lin(out, {{g0, 1}, {ip, A}, {im, -A}}, 0, go);
                                else if (mode == 1) S.vsrc(vcc, out, -P.num("HI", 1.5), 1 / go);
                                else S.vsrc(g0, out, P.num("LO", 0.02), 1 / go);
                            }
                            k++;
                        }
                    } else if (m == "tl431") {
                        int ref = N(p, P.get("REF")), a = N(p, P.get("A")), kk = N(p, P.get("K"));
                        if (ps.B(0)) S.vccs(kk, a, ref, a, 50, -50 * 2.495);
                    } else if (m == "opto") {
                        for (auto& it : P.items) {
                            auto io = split(it.args, '>');
                            if (io.size() != 2) continue;
                            if (it.kind == "LED" && ps.B(0)) { int a = N(p, io[0]), c = N(p, io[1]); S.cond(a, c, 1 / 30.0); S.cur(c, a, 1.2 / 30.0); }
                            if (it.kind == "SW" && ps.B(1)) S.cond(N(p, io[0]), N(p, io[1]), 1 / 50.0);
                        }
                    } else if (m == "uln") {
                        int g0 = N(p, P.get("GND"));
                        int k = 0;
                        for (auto& it : P.items) {
                            auto io = split(it.args, '>');
                            if (io.size() != 2) continue;
                            int in = N(p, io[0]), out = N(p, io[1]);
                            S.cond(in, g0, 1 / 3000.0);
                            if (ps.B((size_t)k)) S.cond(out, g0, 1 / 2.0);
                            k++;
                        }
                    } else if (m == "l293") {
                        int vs = N(p, P.get("VS")), g0 = N(p, P.get("GND"));
                        if (V(vs) - V(g0) < 4.5) continue;
                        int k = 0;
                        for (auto& it : P.items) {
                            if (it.kind != "CH") continue;
                            size_t colon = it.args.find(':');
                            auto io = split(it.args.substr(colon + 1), '>');
                            if (io.size() != 2) continue;
                            int out = N(p, io[1]);
                            int lvl = ps.B((size_t)k);  // 0 hi-z, 1 high, 2 low
                            if (lvl == 1) S.vsrc(vs, out, -1.4, 2);
                            else if (lvl == 2) S.vsrc(g0, out, 0.2, 2);
                            k++;
                        }
                    } else if (m == "relay") {
                        auto coil = toks(P.items.empty() ? "" : P.items[0].args);
                        if (coil.size() == 2) S.cond(N(p, coil[0]), N(p, coil[1]), 1 / P.num("R", 70));
                        int com = N(p, P.get("COM"));
                        S.cond(com, N(p, ps.B(0) ? P.get("NO") : P.get("NC")), 1 / 0.05);
                    } else if (m == "seg7" || m == "rgbled" || m == "leds") {
                        int k = 0;
                        auto led = [&](int a, int c, double vf, double ron) {
                            if (ps.B((size_t)k)) { S.cond(a, c, 1 / ron); S.cur(c, a, vf / ron); }
                            k++;
                        };
                        bool ca = P.flags.count("CA") > 0;
                        if (m == "seg7") {
                            auto coms = toks(P.get("COM"));
                            int com = coms.empty() ? FLOAT : N(p, coms[0]);
                            double vf = ledVf(p.prop("color"));
                            for (auto& it : P.items)
                                if (it.kind == "SEG")
                                    for (auto& s : toks(it.args)) { int sg = N(p, s); led(ca ? com : sg, ca ? sg : com, vf, 20); }
                        } else if (m == "rgbled") {
                            int com = N(p, P.get("COM"));
                            const char* cs[3] = {"R", "G", "B"};
                            double vfs[3] = {1.9, 2.8, 2.9};
                            for (int c = 0; c < 3; c++) { int pn = N(p, P.get(cs[c])); led(ca ? com : pn, ca ? pn : com, vfs[c], 20); }
                        } else {
                            for (auto& it : P.items) {
                                auto parts2 = split(it.args, ':');
                                auto io = split(parts2[0], '>');
                                if (io.size() == 2) led(N(p, io[0]), N(p, io[1]), parts2.size() > 1 ? std::atof(parts2[1].c_str()) : 2, parts2.size() > 2 ? std::atof(parts2[2].c_str()) : 20);
                            }
                        }
                    } else if (m == "sensor") {
                        int vcc = N(p, P.get("VCC")), g0 = N(p, P.get("GND"));
                        double pw = V(vcc) - V(g0);
                        S.cond(vcc, g0, 1 / 20000.0);
                        if (pw < 2) continue;
                        for (auto& it : P.items) {
                            auto a = split(it.args, ':');
                            if (a.size() < 2) continue;
                            int out = N(p, a[0]);
                            std::string pv = p.prop(a[1]);
                            double val = std::atof(pv.c_str());
                            bool yes = pv == "yes" || pv == "1" || pv == "on";
                            if (it.kind == "LIN") S.vsrc(g0, out, std::atof(a.size() > 2 ? a[2].c_str() : "0") + std::atof(a.size() > 3 ? a[3].c_str() : "0.01") * val, 100);
                            else if (it.kind == "RATIO") { double k = std::clamp(val / 100, 0.0, 1.0); S.lin(out, {{vcc, k}, {g0, 1 - k}}, 0, 1 / 1000.0); }
                            else if (it.kind == "DIG") S.vsrc(g0, out, yes ? (a.size() > 2 ? std::atof(a[2].c_str()) : pw) : 0, 100);
                            else if (it.kind == "OC" && yes) S.cond(out, g0, 1 / 20.0);
                        }
                    } else if (m == "logic") {
                        int vcc = N(p, P.get("VCC")), g0 = N(p, P.get("GND"));
                        double pw = V(vcc) - V(g0);
                        S.cond(vcc, g0, 1 / 100000.0);
                        if (pw < 2.0) continue;
                        double rout = P.num("ROUT", 50);
                        int k = 0;
                        for (auto& it : P.items) {
                            auto io = split(it.args, '>');
                            size_t base = (size_t)k * 32;
                            if (it.kind == "MUX" || it.kind == "SW") {
                                // analog switches: state slot base+8.. holds which channel is closed (-1 none)
                                if (io.size() == 2) {
                                    auto ins = toks(io[0]);
                                    auto outs = toks(io[1]);
                                    if (it.kind == "SW" && outs.size() == 2 && ps.B(base + 8)) S.cond(N(p, outs[0]), N(p, outs[1]), 1 / 80.0);
                                    if (it.kind == "MUX" && ins.size() == 5) {
                                        int ch = ps.B(base + 8) - 1;
                                        if (ch >= 0 && ch < (int)outs.size()) S.cond(N(p, ins[4]), N(p, outs[(size_t)ch]), 1 / 80.0);
                                    }
                                }
                            } else if (io.size() == 2) {
                                auto outs = toks(io[1]);
                                for (size_t o = 0; o < outs.size(); o++) {
                                    int lvl = ps.B(base + 8 + o);  // 0 = hi-z, 1 = high, 2 = low
                                    int on = N(p, outs[o]);
                                    if (lvl == 1) S.cond(on, vcc, 1 / rout);
                                    else if (lvl == 2) S.cond(on, g0, 1 / rout);
                                }
                            }
                            k++;
                        }
                    }
                }
                if (!S.solve(x)) { solveFailed = true; break; }

                // -------------------- UPDATE discrete states --------------------
                bool changed = false;
                auto setB = [&](int& slot, int v) { if (slot != v) { slot = v; changed = true; } };
                for (auto* pp : parts) {
                    const PartInst& p = *pp;
                    const PartDef& d = *p.def;
                    const std::string& m = d.sim;
                    const Params& P = paramsFor(&d);
                    auto& ps = ST[p.id];
                    auto junction = [&](int& slot, int a, int c, double vf, double ron) {
                        double vd = V(a) - V(c);
                        if (a == FLOAT || c == FLOAT) { setB(slot, 0); return; }
                        if (!slot && vd > vf + 1e-6) setB(slot, 1);
                        else if (slot && (vd - vf) / ron < -1e-9) setB(slot, 0);
                    };
                    if (m == "led") junction(ps.B(0), Nid(p, "anode"), Nid(p, "cathode"), ledVf(p.prop("color")), 15);
                    else if (m == "diode") junction(ps.B(0), Nid(p, "anode"), Nid(p, "cathode"), 0.7, 0.2);
                    else if (m == "npn" || m == "pnp") {
                        bool npn = m == "npn";
                        double vf = P.num("VF", 0.65), beta = P.num("BETA", 150), ron = 50;
                        int b = Nid(p, "B"), c = Nid(p, "C"), e = Nid(p, "E");
                        junction(ps.B(0), npn ? b : e, npn ? e : b, vf, ron);
                        if (ps.B(0)) {
                            double vce = npn ? V(c) - V(e) : V(e) - V(c);
                            double ib = ((npn ? V(b) - V(e) : V(e) - V(b)) - vf) / ron;
                            if (!ps.B(1) && vce < 0.15) setB(ps.B(1), 1);
                            else if (ps.B(1) && vce / 1.0 > beta * ib * 1.01) setB(ps.B(1), 0);
                        } else setB(ps.B(1), 0);
                    } else if (m == "nmos") {
                        setB(ps.B(0), V(Nid(p, "G")) - V(Nid(p, "S")) > P.num("VTH", 2.0) ? 1 : 0);
                    } else if (m == "reg") {
                        int in = N(p, P.get("IN")), out = N(p, P.get("OUT"));
                        bool adj = !P.get("ADJ").empty();
                        int ref = adj ? N(p, P.get("ADJ")) : N(p, P.get("GND"));
                        double v = adj ? P.num("VREF", 1.25) : (P.kv.count("VPROP") ? parseSI(p.prop(P.get("VPROP")), 5) : P.num("V", 5));
                        double drop = P.num("DROP", 2);
                        double vin = V(in) - (adj ? V(out) - v : V(ref));
                        int mode = ps.B(0);
                        int want = mode;
                        if (P.flags.count("BOOST")) want = vin > 1.8 ? 1 : 0;
                        else if (vin < 1.0 && V(in) - V(out) < 0.5) want = 0;
                        else if (mode == 1 && V(in) - V(out) < drop * 0.98) want = 2;
                        else if (mode != 1 && V(in) - (adj ? V(out) : V(ref)) > (adj ? drop : v + drop)) want = 1;
                        else if (mode == 0 && V(in) > 1.0) want = 2;
                        setB(ps.B(0), want);
                    } else if (m == "555") {
                        int k = 0;
                        for (auto& it : P.items) {
                            if (it.kind != "U") continue;
                            std::map<std::string, std::string> kv;
                            for (auto& a : toks(it.args)) { size_t e = a.find('='); if (e != std::string::npos) kv[a.substr(0, e)] = a.substr(e + 1); }
                            int g0 = N(p, kv["GND"]), ctrl = N(p, kv["CTRL"]), trig = N(p, kv["TRIG"]), thr = N(p, kv["THR"]), rst = N(p, kv["RESET"]);
                            int vccN = N(p, kv["VCC"]);
                            // CTRL is usually left open: the internal divider still sets it to 2/3 VCC
                            double vc = ctrl == FLOAT ? (V(vccN) - V(g0)) * 2.0 / 3.0 : V(ctrl) - V(g0);
                            int q = ps.B((size_t)k);
                            if (rst != FLOAT && V(rst) - V(g0) < 0.7) q = 0;
                            else if (trig != FLOAT && V(trig) - V(g0) < vc / 2) q = 1;
                            else if (thr != FLOAT && V(thr) - V(g0) > vc) q = 0;
                            setB(ps.B((size_t)k), q);
                            k++;
                        }
                    } else if (m == "opamp" || m == "comparator" || m == "amp386") {
                        int vcc = N(p, P.get(m == "amp386" ? "VS" : "VCC")), g0 = N(p, P.get("GND"));
                        if (V(vcc) - V(g0) < 1.5) continue;
                        if (m == "amp386") {
                            int ip = N(p, P.get("IN+")), im = N(p, P.get("IN-"));
                            double ideal = 0.5 * (V(vcc) + V(g0)) + P.num("GAIN", 20) * (V(ip) - V(im));
                            double hi = V(vcc) - 0.5, lo = V(g0) + 0.5;
                            int mode = ps.B(0), want = mode;
                            double vd = V(ip) - V(im);
                            if (mode == 0) want = ideal > hi ? 1 : ideal < lo ? 2 : 0;
                            else if (mode == 1 && ideal < hi) want = 0;
                            else if (mode == 2 && ideal > lo) want = 0;
                            (void)vd;
                            setB(ps.B(0), want);
                            continue;
                        }
                        int k = 0;
                        for (auto& it : P.items) {
                            auto io = split(it.args, '>');
                            if (io.size() != 2) continue;
                            auto in = toks(io[0]);
                            if (in.size() != 2) continue;
                            double vd = V(N(p, in[0])) - V(N(p, in[1]));
                            if (it.kind == "CMP") setB(ps.B((size_t)k), vd < 0 ? 1 : 0);
                            else {
                                double ideal = V(g0) + 1e5 * vd;
                                double hi = V(vcc) - P.num("HI", 1.5), lo = V(g0) + P.num("LO", 0.02);
                                int mode = ps.B((size_t)k), want = mode;
                                // leave saturation only when the inputs ask to move back into range
                                if (mode == 0) want = ideal > hi ? 1 : ideal < lo ? 2 : 0;
                                else if (mode == 1 && vd < 0) want = 0;
                                else if (mode == 2 && vd > 0) want = 0;
                                setB(ps.B((size_t)k), want);
                            }
                            k++;
                        }
                    } else if (m == "tl431") {
                        double v = V(N(p, P.get("REF"))) - V(N(p, P.get("A")));
                        setB(ps.B(0), v > 2.495 ? 1 : 0);
                    } else if (m == "opto") {
                        for (auto& it : P.items) {
                            auto io = split(it.args, '>');
                            if (io.size() != 2 || it.kind != "LED") continue;
                            int a = N(p, io[0]), c = N(p, io[1]);
                            junction(ps.B(0), a, c, 1.2, 30);
                            double i = ps.B(0) ? (V(a) - V(c) - 1.2) / 30 : 0;
                            setB(ps.B(1), i > 0.001 ? 1 : 0);
                        }
                    } else if (m == "uln") {
                        int g0 = N(p, P.get("GND"));
                        int k = 0;
                        for (auto& it : P.items) {
                            auto io = split(it.args, '>');
                            if (io.size() != 2) continue;
                            setB(ps.B((size_t)k), V(N(p, io[0])) - V(g0) > 1.4 ? 1 : 0);
                            k++;
                        }
                    } else if (m == "l293") {
                        int g0 = N(p, P.get("GND"));
                        int k = 0;
                        for (auto& it : P.items) {
                            if (it.kind != "CH") continue;
                            size_t colon = it.args.find(':');
                            int en = N(p, it.args.substr(0, colon));
                            auto io = split(it.args.substr(colon + 1), '>');
                            if (io.size() != 2) continue;
                            bool enabled = V(en) - V(g0) > 2.0;
                            bool high = V(N(p, io[0])) - V(g0) > 2.0;
                            setB(ps.B((size_t)k), !enabled ? 0 : high ? 1 : 2);
                            k++;
                        }
                    } else if (m == "relay") {
                        auto coil = toks(P.items.empty() ? "" : P.items[0].args);
                        if (coil.size() != 2) continue;
                        double i = std::fabs(V(N(p, coil[0])) - V(N(p, coil[1]))) / P.num("R", 70);
                        double pull = P.num("I", 0.04);
                        if (!ps.B(0) && i > pull) setB(ps.B(0), 1);
                        else if (ps.B(0) && i < pull * 0.5) setB(ps.B(0), 0);
                    } else if (m == "seg7" || m == "rgbled" || m == "leds") {
                        int k = 0;
                        bool ca = P.flags.count("CA") > 0;
                        auto led = [&](int a, int c, double vf, double ron) { junction(ps.B((size_t)k), a, c, vf, ron); k++; };
                        if (m == "seg7") {
                            auto coms = toks(P.get("COM"));
                            int com = coms.empty() ? FLOAT : N(p, coms[0]);
                            double vf = ledVf(p.prop("color"));
                            for (auto& it : P.items)
                                if (it.kind == "SEG")
                                    for (auto& s : toks(it.args)) { int sg = N(p, s); led(ca ? com : sg, ca ? sg : com, vf, 20); }
                        } else if (m == "rgbled") {
                            int com = N(p, P.get("COM"));
                            const char* cs[3] = {"R", "G", "B"};
                            double vfs[3] = {1.9, 2.8, 2.9};
                            for (int c = 0; c < 3; c++) { int pn = N(p, P.get(cs[c])); led(ca ? com : pn, ca ? pn : com, vfs[c], 20); }
                        } else {
                            for (auto& it : P.items) {
                                auto parts2 = split(it.args, ':');
                                auto io = split(parts2[0], '>');
                                if (io.size() == 2) led(N(p, io[0]), N(p, io[1]), parts2.size() > 1 ? std::atof(parts2[1].c_str()) : 2, parts2.size() > 2 ? std::atof(parts2[2].c_str()) : 20);
                            }
                        }
                    } else if (m == "logic") {
                        int vcc = N(p, P.get("VCC")), g0 = N(p, P.get("GND"));
                        double pw = V(vcc) - V(g0);
                        bool powered = pw >= 2.0;
                        int k = 0;
                        for (auto& it : P.items) {
                            size_t base = (size_t)k * 32;
                            k++;
                            auto io = split(it.args, '>');
                            if (io.size() != 2) continue;
                            auto ins = toks(io[0]);
                            auto outs = toks(io[1]);
                            auto level = [&](size_t i, bool schmitt) -> bool {
                                double v = (V(N(p, ins[i])) - V(g0)) / std::max(pw, 1e-6);
                                if (!schmitt) return v > 0.5;
                                int& mem = ps.B(base + 4 + i);
                                if (v > 0.6) mem = 1;
                                else if (v < 0.4) mem = 0;
                                return mem != 0;
                            };
                            std::vector<int> lv(outs.size(), 0);  // 0 hi-z, 1 high, 2 low
                            auto L = [](bool b) { return b ? 1 : 2; };
                            const std::string& K = it.kind;
                            if (!powered) {
                                // all outputs float
                            } else if (K == "AND" || K == "NAND" || K == "OR" || K == "NOR" || K == "XOR" || K == "XNOR" || K == "SNAND") {
                                bool sch = K == "SNAND";
                                bool r = (K == "OR" || K == "NOR") ? false : (K == "XOR" || K == "XNOR") ? false : true;
                                for (size_t i = 0; i < ins.size(); i++) {
                                    bool v = level(i, sch);
                                    if (K == "OR" || K == "NOR") r = r || v;
                                    else if (K == "XOR" || K == "XNOR") r = r != v;
                                    else r = r && v;
                                }
                                if (K == "NAND" || K == "NOR" || K == "XNOR" || K == "SNAND") r = !r;
                                if (!lv.empty()) lv[0] = L(r);
                            } else if (K == "NOT" || K == "SNOT" || K == "BUF") {
                                bool v = level(0, K == "SNOT");
                                if (!lv.empty()) lv[0] = L(K == "BUF" ? v : !v);
                            } else if (K == "TRI") {
                                if (ins.size() == 2 && !lv.empty()) lv[0] = level(1, false) ? 0 : L(level(0, false));
                            } else if (K == "DEC138" && ins.size() == 6) {
                                int sel = (level(0, false) ? 1 : 0) | (level(1, false) ? 2 : 0) | (level(2, false) ? 4 : 0);
                                bool en = level(3, false) && !level(4, false) && !level(5, false);
                                for (size_t o = 0; o < lv.size(); o++) lv[o] = L(!(en && (int)o == sel));
                            } else if (K == "BCD7" && ins.size() == 7) {
                                int val = (level(0, false) ? 1 : 0) | (level(1, false) ? 2 : 0) | (level(2, false) ? 4 : 0) | (level(3, false) ? 8 : 0);
                                bool lt = !level(4, false), bl = !level(5, false), le = level(6, false);
                                int& latched = ps.B(base + 1);
                                if (!le) latched = val;
                                int segs = lt ? 0x7F : bl ? 0 : (latched <= 9 ? kSeg[latched] : 0);
                                for (size_t o = 0; o < lv.size() && o < 7; o++) lv[o] = L((segs >> o) & 1);
                            } else if (K == "MUX" && ins.size() == 5) {
                                int sel = (level(0, false) ? 1 : 0) | (level(1, false) ? 2 : 0) | (level(2, false) ? 4 : 0);
                                setB(ps.B(base + 8), level(3, false) ? 0 : sel + 1);
                                continue;
                            } else if (K == "SW" && ins.size() == 1) {
                                setB(ps.B(base + 8), level(0, false) ? 1 : 0);
                                continue;
                            } else if ((K == "DFF" || K == "DFFH") && ins.size() == 4) {
                                int& q = ps.B(base + 1);
                                bool set = K == "DFF" ? !level(2, false) : level(2, false);
                                bool clr = K == "DFF" ? !level(3, false) : level(3, false);
                                if (set) q = 1;
                                if (clr) q = 0;
                                if (lv.size() >= 2) { lv[0] = L(set && clr ? true : q != 0); lv[1] = L(set && clr ? true : q == 0); }
                            } else if (K == "CNT" && ins.size() == 2) {
                                int& c = ps.B(base + 1);
                                if (level(1, false)) c = 0;
                                for (size_t o = 0; o < lv.size(); o++) lv[o] = L((c >> o) & 1);
                            } else if (K == "SR595" && ins.size() == 5) {
                                int& shiftR = ps.B(base + 1);
                                int& latch = ps.B(base + 2);
                                if (!level(3, false)) shiftR = 0;
                                bool oe = level(4, false);
                                for (size_t o = 0; o < 8 && o < lv.size(); o++) lv[o] = oe ? 0 : L((latch >> o) & 1);
                                if (lv.size() > 8) lv[8] = L((shiftR >> 7) & 1);
                            } else if (K == "J4017" && ins.size() == 3) {
                                int& c = ps.B(base + 1);
                                if (level(2, false)) c = 0;
                                for (size_t o = 0; o < 10 && o < lv.size(); o++) lv[o] = L(c == (int)o);
                                if (lv.size() > 10) lv[10] = L(c < 5);
                            }
                            for (size_t o = 0; o < lv.size(); o++) setB(ps.B(base + 8 + o), lv[o]);
                        }
                    }
                }
                if (!changed) break;
            }
            finalIters = iter;
            if (solveFailed) break;

            // -------------------- clocked (edge) updates --------------------
            if (pass == 1) break;
            bool edges = false;
            for (auto* pp : parts) {
                const PartInst& p = *pp;
                if (p.def->sim != "logic") continue;
                const Params& P = paramsFor(p.def.get());
                auto& ps = ST[p.id];
                int vcc = N(p, P.get("VCC")), g0 = N(p, P.get("GND"));
                double pw = V(vcc) - V(g0);
                if (pw < 2.0) continue;
                int k = 0;
                for (auto& it : P.items) {
                    size_t base = (size_t)k * 32;
                    k++;
                    auto io = split(it.args, '>');
                    if (io.size() != 2) continue;
                    auto ins = toks(io[0]);
                    auto lvl = [&](size_t i) { return i < ins.size() && (V(N(p, ins[i])) - V(g0)) > 0.5 * pw; };
                    const std::string& K = it.kind;
                    auto edge = [&](size_t clkIn, size_t slot, bool rising) {
                        bool now = lvl(clkIn);
                        int& prev = ps.B(base + slot);
                        bool e = rising ? (now && !prev) : (!now && prev);
                        prev = now;
                        return e;
                    };
                    if (K == "DFF" || K == "DFFH") {
                        if (edge(1, 0, true)) { int nq = lvl(0) ? 1 : 0; if (ps.B(base + 1) != nq) { ps.B(base + 1) = nq; edges = true; } }
                    } else if (K == "CNT") {
                        if (edge(0, 0, false) && !lvl(1)) { ps.B(base + 1) = (ps.B(base + 1) + 1) & 15; edges = true; }
                    } else if (K == "SR595") {
                        if (edge(1, 0, true)) { ps.B(base + 1) = ((ps.B(base + 1) << 1) | (lvl(0) ? 1 : 0)) & 0xFF; edges = true; }
                        if (edge(2, 3, true)) { ps.B(base + 2) = ps.B(base + 1); edges = true; }
                    } else if (K == "J4017") {
                        if (edge(0, 0, true) && !lvl(1) && !lvl(2)) { ps.B(base + 1) = (ps.B(base + 1) + 1) % 10; edges = true; }
                    }
                }
            }
            if (!edges) break;
        }
        if (solveFailed) break;

        // -------------------- commit time step --------------------
        for (auto* pp : parts) {
            const PartInst& p = *pp;
            auto& ps = ST[p.id];
            if (p.def->sim == "capacitor") {
                int a = nodeOfPin(p, 0), b = nodeOfPin(p, 1);
                ps.D(0) = (a == FLOAT || b == FLOAT) ? 0 : V(a) - V(b);
            }
        }
        // accumulate brightness (time-weighted over the frame)
        for (auto* pp : parts) {
            const PartInst& p = *pp;
            auto& ps = ST[p.id];
            const std::string& m = p.def->sim;
            if (m == "led" && ps.B(0)) {
                double i = (V(Nid(p, "anode")) - V(Nid(p, "cathode")) - ledVf(p.prop("color"))) / 15;
                ledAccum[p.id] += std::max(0.0, i) * dt;
            } else if (m == "seg7" || m == "rgbled") {
                const Params& P = paramsFor(p.def.get());
                bool ca = P.flags.count("CA") > 0;
                std::vector<std::pair<int, int>> pairs;  // (a,c) per led index
                if (m == "seg7") {
                    auto coms = toks(P.get("COM"));
                    int com = coms.empty() ? FLOAT : N(p, coms[0]);
                    for (auto& it : P.items) if (it.kind == "SEG") for (auto& s : toks(it.args)) { int sg = N(p, s); pairs.push_back({ca ? com : sg, ca ? sg : com}); }
                } else {
                    int com = N(p, P.get("COM"));
                    for (const char* c : {"R", "G", "B"}) { int pn = N(p, P.get(c)); pairs.push_back({ca ? com : pn, ca ? pn : com}); }
                }
                double vf7 = ledVf(p.prop("color"));
                double vfs[3] = {1.9, 2.8, 2.9};
                auto& acc = segAccum[p.id];
                acc.resize(pairs.size(), 0);
                for (size_t k = 0; k < pairs.size(); k++) {
                    if (!ps.B(k)) continue;
                    double vf = m == "seg7" ? vf7 : vfs[k];
                    double i = (V(pairs[k].first) - V(pairs[k].second) - vf) / 20;
                    acc[k] += std::max(0.0, i) * dt;
                }
            }
        }
        totalT += dt;
    }
    time += dt * steps;

    if (solveFailed) {
        res.message = "The circuit could not be solved — check for a short circuit between power rails.";
        return res;
    }
    res.message = !anySource ? "No power source: add a battery or a microcontroller board." : (finalIters >= 59 ? "Simulating (unstable — oscillating states)" : "Simulating");
    for (auto& kv : nodeOf) res.netVoltage[(size_t)kv.first] = x[(size_t)kv.second];
    res.time = time;

    // -------------------- results --------------------
    for (auto& kv : ledAccum) {
        double iavg = kv.second / std::max(totalT, 1e-9);
        res.brightness[kv.first] = (float)std::clamp(iavg / 0.015, 0.0, 2.0);
        const PartInst* p = doc.part(kv.first);
        if (p && iavg > 0.035) {
            res.damaged.insert(p->id);
            snprintf(buf, sizeof buf, "%s: %.0f mA is too much for an LED (max ~20 mA). Add or increase a series resistor.", p->label.c_str(), iavg * 1000);
            res.warnings.push_back(buf);
        }
    }
    for (auto& kv : segAccum) {
        const PartInst* p = doc.part(kv.first);
        if (!p) continue;
        std::vector<float> seg;
        for (double a : kv.second) seg.push_back((float)std::clamp(a / std::max(totalT, 1e-9) / 0.010, 0.0, 1.5));
        if (p->def->sim == "rgbled" && seg.size() == 3) {
            float r = std::min(1.f, seg[0]), gg = std::min(1.f, seg[1]), bb = std::min(1.f, seg[2]);
            float mx = std::max({r, gg, bb});
            if (mx > 0.02f) {
                res.glow[p->id] = rgba((int)(255 * r / mx), (int)(255 * gg / mx), (int)(255 * bb / mx));
                res.brightness[p->id] = mx;
            }
        }
        res.segments[p->id] = seg;
    }
    for (auto* pp : parts) {
        const PartInst& p = *pp;
        const std::string& m = p.def->sim;
        auto& ps = ST[p.id];
        if (m == "resistor") {
            double r = std::max(1e-3, parseSI(p.prop("resistance"), 220));
            double v = V(Nid(p, "pin0")) - V(Nid(p, "pin1"));
            double pw = v * v / r;
            if (pw > powerRating(p.prop("power")) * 1.05) {
                res.damaged.insert(p.id);
                snprintf(buf, sizeof buf, "%s dissipates %.2f W, more than its %s rating.", p.label.c_str(), pw, p.prop("power").c_str());
                res.warnings.push_back(buf);
            }
        } else if (m == "battery") {
            std::string style = p.prop("style");
            double rint = style == "Coin cell" ? 15 : style == "AA pack" ? 0.3 : 1.5;
            double i = (parseSI(p.prop("voltage"), 9) - (V(Nid(p, "plus")) - V(Nid(p, "minus")))) / rint;
            if (i > 1.0 && style != "Coin cell") {
                res.damaged.insert(p.id);
                snprintf(buf, sizeof buf, "%s is short-circuited (%.1f A)!", p.label.c_str(), i);
                res.warnings.push_back(buf);
            }
        } else if (m == "multimeter") {
            bool cur = p.prop("mode").find("Current") != std::string::npos;
            double v = V(Nid(p, "v")) - V(Nid(p, "com"));
            if (cur) {
                double i = v / 0.01;
                if (std::fabs(i) < 1) snprintf(buf, sizeof buf, "%.1fmA", i * 1000);
                else snprintf(buf, sizeof buf, "%.2fA", i);
            } else snprintf(buf, sizeof buf, "%.2fV", v);
            res.meterText[p.id] = buf;
        } else if (m == "motor") {
            res.activity[p.id] = (float)std::clamp(std::fabs(V(Nid(p, "pin0")) - V(Nid(p, "pin1"))) / 10.0 / 0.3, 0.0, 1.0);
        } else if (m == "buzzer") {
            res.activity[p.id] = (float)std::clamp((V(Nid(p, "plus")) - V(Nid(p, "minus"))) / 3.0, 0.0, 1.0);
        } else if (m == "relay") {
            if (ps.B(0)) res.activity[p.id] = 1;
        } else if (m == "board") {
            const Params& P = paramsFor(p.def.get());
            for (auto& it : P.items) {
                if (it.kind != "RAIL") continue;
                size_t eq = it.args.rfind('=');
                std::string pname = it.args.substr(0, eq);
                double v = std::atof(it.args.substr(eq + 1).c_str());
                for (size_t i = 0; i < p.def->pins.size(); i++) {
                    if (p.def->pins[i].name != pname) continue;
                    int node = nodeOfPin(p, (int)i);
                    double drop = v - V(node);
                    if (node != FLOAT && drop > 0.3) {
                        res.damaged.insert(p.id);
                        snprintf(buf, sizeof buf, "%s: the %s pin is overloaded (%.1f A) — short circuit?", p.label.c_str(), pname.c_str(), drop / 0.1);
                        res.warnings.push_back(buf);
                    }
                }
            }
        }
    }
    if (!unsupported.empty()) {
        std::string s = "Not simulated (pinout only): ";
        for (size_t i = 0; i < unsupported.size() && i < 6; i++) s += (i ? ", " : "") + unsupported[i];
        if (unsupported.size() > 6) s += "…";
        res.warnings.push_back(s);
    }
    return res;
}

SimResult simulate(const Document& doc, const Connectivity& conn, const std::set<int>& pressedButtons, double seconds) {
    Simulator s;
    SimResult r;
    double t = 0;
    while (t < seconds - 1e-9) {
        double chunk = std::min(0.05, seconds - t);
        r = s.step(doc, conn, pressedButtons, chunk);
        t += chunk;
    }
    return r;
}
