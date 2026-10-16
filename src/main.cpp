// designElectronics — entry point. Uses SDL3's main callbacks so the same code
// runs on desktop, iOS and Android.
//
// Command line:
//   designElectronics [file.dez]                open a project
//   designElectronics --selftest                run headless model/tool tests
//   designElectronics --export-examples DIR     write the built-in examples as .dez files
//   designElectronics --screenshot OUT.png [--example N] [--view bb|sch|pcb] [--sim]
//                     [--autoroute] [--size WxH] [--drawer parts|inspector] [--select N]
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstring>

#include "app.h"

namespace {

int check(bool ok, const char* what, int& failures) {
    printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
    if (!ok) failures++;
    return ok;
}


// ---- circuit-building helpers for simulation tests ----
struct TB {
    Document d;
    int count = 0;
    int add(const std::string& type, Props props = {}) { count++; return d.addPart(type, props, {count * 6000.f, count * 3000.f}).id; }
    int pin(int part, const std::string& name) {
        const PartInst* p = d.part(part);
        for (size_t i = 0; i < p->def->pins.size(); i++) if (p->def->pins[i].name == name || p->def->pins[i].id == name) return (int)i;
        printf("  !! no pin %s on %s\n", name.c_str(), p->type.c_str());
        return -1;
    }
    void wire(int a, const std::string& pa, int b, const std::string& pb) {
        Wire w;
        w.id = d.newId();
        w.view = SCH;
        w.ends[0] = {a, pin(a, pa)};
        w.ends[1] = {b, pin(b, pb)};
        w.pts = {d.part(a)->pinPos(w.ends[0].pin, SCH), d.part(b)->pinPos(w.ends[1].pin, SCH)};
        d.wires.push_back(w);
    }
    int net(Connectivity& c, int part, const std::string& p) { return c.netOf(part, pin(part, p)); }
};

int simTests(int& failures) {
    printf("Simulation models\n");
    {   // 555 astable blinker: f = 1.44 / ((R1 + 2 R2) C)
        TB t;
        int bat = t.add("battery", {{"voltage", "9V"}});
        int u = t.add("timer555");
        int r1 = t.add("resistor", {{"resistance", "1kΩ"}}), r2 = t.add("resistor", {{"resistance", "10kΩ"}}), r3 = t.add("resistor", {{"resistance", "470Ω"}});
        int c = t.add("capacitor_electrolytic", {{"capacitance", "10µF"}});
        int led = t.add("led");
        t.wire(bat, "plus", u, "VCC"); t.wire(bat, "plus", u, "RESET"); t.wire(bat, "minus", u, "GND");
        t.wire(bat, "plus", r1, "pin0"); t.wire(r1, "pin1", u, "DIS"); t.wire(u, "DIS", r2, "pin0"); t.wire(r2, "pin1", u, "THR");
        t.wire(u, "THR", u, "TRIG"); t.wire(u, "TRIG", c, "plus"); t.wire(c, "minus", bat, "minus");
        t.wire(u, "OUT", r3, "pin0"); t.wire(r3, "pin1", led, "anode"); t.wire(led, "cathode", bat, "minus");
        Connectivity cn; cn.build(t.d);
        Simulator sim;
        int transitions = 0; bool prev = false;
        for (int i = 0; i < 200; i++) {
            SimResult r = sim.step(t.d, cn, {}, 0.01);
            bool on = r.brightness.count(led) && r.brightness[led] > 0.5f;
            if (i > 0 && on != prev) transitions++;
            prev = on;
        }
        double f = transitions / 2.0 / 2.0;
        printf("  555 astable: %.1f Hz (expected ~6.9 Hz)\n", f);
        check(f > 4.5 && f < 9.5, "555 astable blinks an LED at the right frequency", failures);
    }
    {   // ESP32 driving 74HC08 AND gates
        TB t;
        int esp = t.add("esp32_devkit", {{"outputs", "GPIO4=HIGH, GPIO5=LOW"}});
        int g = t.add("74hc08");
        int ra = t.add("resistor", {{"resistance", "220Ω"}}), rb = t.add("resistor", {{"resistance", "220Ω"}});
        int la = t.add("led"), lb = t.add("led", {{"color", "Green"}});
        t.wire(esp, "3V3", g, "VCC"); t.wire(esp, "GND", g, "GND");
        t.wire(esp, "GPIO4", g, "1A"); t.wire(esp, "GPIO4", g, "1B"); t.wire(esp, "GPIO4", g, "2A"); t.wire(esp, "GPIO5", g, "2B");
        t.wire(g, "1Y", ra, "pin0"); t.wire(ra, "pin1", la, "anode"); t.wire(la, "cathode", esp, "GND");
        t.wire(g, "2Y", rb, "pin0"); t.wire(rb, "pin1", lb, "anode"); t.wire(lb, "cathode", esp, "GND");
        Connectivity cn; cn.build(t.d);
        SimResult r = simulate(t.d, cn, {}, 0.05);
        float a = r.brightness.count(la) ? r.brightness[la] : 0, b = r.brightness.count(lb) ? r.brightness[lb] : 0;
        printf("  74HC08: 1&1 -> LED %.2f, 1&0 -> LED %.2f\n", a, b);
        check(a > 0.3f && b < 0.05f, "logic gates follow their truth table", failures);
    }
    {   // LM358 non-inverting amplifier, gain 2
        TB t;
        int bat = t.add("battery", {{"voltage", "9V"}});
        int op = t.add("lm358");
        int ra = t.add("resistor", {{"resistance", "10kΩ"}}), rb = t.add("resistor", {{"resistance", "1kΩ"}});
        int rf = t.add("resistor", {{"resistance", "10kΩ"}}), rg = t.add("resistor", {{"resistance", "10kΩ"}});
        int mm = t.add("multimeter");
        t.wire(bat, "plus", op, "VCC"); t.wire(bat, "minus", op, "GND");
        t.wire(bat, "plus", ra, "pin0"); t.wire(ra, "pin1", op, "1IN+"); t.wire(op, "1IN+", rb, "pin0"); t.wire(rb, "pin1", bat, "minus");
        t.wire(op, "1OUT", rf, "pin0"); t.wire(rf, "pin1", op, "1IN-"); t.wire(op, "1IN-", rg, "pin0"); t.wire(rg, "pin1", bat, "minus");
        t.wire(mm, "V+", op, "1OUT"); t.wire(mm, "COM", bat, "minus");
        Connectivity cn; cn.build(t.d);
        SimResult r = simulate(t.d, cn, {}, 0.05);
        double v = std::atof(r.meterText[mm].c_str());
        printf("  LM358 gain-2 amplifier: in 0.82 V -> out %s (expected ~1.64 V)\n", r.meterText[mm].c_str());
        check(std::fabs(v - 1.636) < 0.05, "op-amp amplifies with the right gain", failures);
    }
    {   // ESP32 blink on GPIO2
        TB t;
        int esp = t.add("esp32_devkit", {{"outputs", "GPIO2=BLINK:500"}});
        int r = t.add("resistor", {{"resistance", "220Ω"}}), led = t.add("led");
        t.wire(esp, "GPIO2", r, "pin0"); t.wire(r, "pin1", led, "anode"); t.wire(led, "cathode", esp, "GND");
        Connectivity cn; cn.build(t.d);
        Simulator sim;
        float b1 = 0, b2 = 0;
        for (int i = 0; i < 30; i++) { SimResult s = sim.step(t.d, cn, {}, 0.01); if (i == 20) b1 = s.brightness[led]; }
        for (int i = 0; i < 50; i++) { SimResult s = sim.step(t.d, cn, {}, 0.01); if (i == 40) b2 = s.brightness[led]; }
        printf("  ESP32 GPIO2 blink: t=0.2s %.2f, t=0.7s %.2f\n", b1, b2);
        check(b1 > 0.2f && b2 < 0.05f, "ESP32 pin BLINK output toggles", failures);
    }
    {   // Raspberry Pi GPIO17 and Arduino Mega D53 drive LEDs; Due/Pi pins are 3.3 V, Mega 5 V
        TB t;
        int pi = t.add("rpi4", {{"outputs", "GPIO17=HIGH, GPIO2=HIGH"}});
        int mega = t.add("arduino_mega2560", {{"outputs", "D53=HIGH"}});
        int r1 = t.add("resistor", {{"resistance", "220Ω"}}), l1 = t.add("led");
        int r2 = t.add("resistor", {{"resistance", "220Ω"}}), l2 = t.add("led");
        int m1 = t.add("multimeter"), m2 = t.add("multimeter"), m3 = t.add("multimeter");
        t.wire(pi, "GPIO17", r1, "pin0"); t.wire(r1, "pin1", l1, "anode"); t.wire(l1, "cathode", pi, "GND");
        t.wire(mega, "D53", r2, "pin0"); t.wire(r2, "pin1", l2, "anode"); t.wire(l2, "cathode", mega, "GND");
        t.wire(pi, "GND", mega, "GND");
        t.wire(m1, "V+", pi, "GPIO2/SDA"); t.wire(m1, "COM", pi, "GND");
        t.wire(m2, "V+", mega, "5V"); t.wire(m2, "COM", mega, "GND");
        t.wire(m3, "V+", pi, "3V3"); t.wire(m3, "COM", pi, "GND");
        Connectivity cn; cn.build(t.d);
        SimResult r = simulate(t.d, cn, {}, 0.05);
        double vSda = std::atof(r.meterText[m1].c_str()), v5 = std::atof(r.meterText[m2].c_str()), v33 = std::atof(r.meterText[m3].c_str());
        printf("  Pi GPIO17 LED %.2f, Mega D53 LED %.2f, Pi GPIO2 %s, Mega 5V %s, Pi 3V3 %s\n", r.brightness[l1], r.brightness[l2],
               r.meterText[m1].c_str(), r.meterText[m2].c_str(), r.meterText[m3].c_str());
        check(r.brightness[l1] > 0.2f && r.brightness[l2] > 0.2f, "Raspberry Pi and Arduino Mega pins drive LEDs", failures);
        check(std::fabs(vSda - 3.3) < 0.1 && std::fabs(v5 - 5) < 0.1 && std::fabs(v33 - 3.3) < 0.1, "board rails and GPIO levels are right", failures);
    }
    {   // CD4017 clocked by an ESP32 square wave: outputs advance
        TB t;
        int esp = t.add("esp32_devkit", {{"outputs", "GPIO4=BLINK:50, GPIO5=LOW"}});
        int c = t.add("cd4017");
        t.wire(esp, "3V3", c, "VDD"); t.wire(esp, "GND", c, "VSS"); t.wire(esp, "GPIO4", c, "CLK");
        t.wire(esp, "GPIO5", c, "INH"); t.wire(esp, "GPIO5", c, "RST");
        int ld = t.add("led");
        int rr = t.add("resistor", {{"resistance", "100Ω"}});
        t.wire(c, "Q3", rr, "pin0"); t.wire(rr, "pin1", ld, "anode"); t.wire(ld, "cathode", esp, "GND");
        Connectivity cn; cn.build(t.d);
        Simulator sim;
        int on = 0;
        for (int i = 0; i < 200; i++) { SimResult s = sim.step(t.d, cn, {}, 0.01); if (s.brightness[ld] > 0.05f) on++; }
        printf("  CD4017: Q3 lit in %d of 200 samples (expected ~20)\n", on);
        check(on > 8 && on < 40, "CD4017 counter advances on clock edges", failures);
    }
    {   // Relay switching an LED
        TB t;
        int bat = t.add("battery", {{"voltage", "5V"}});
        int k = t.add("relay_module");
        int r = t.add("resistor", {{"resistance", "220Ω"}}), led = t.add("led");
        t.wire(bat, "plus", k, "COIL+"); t.wire(k, "COIL-", bat, "minus");
        t.wire(bat, "plus", k, "COM"); t.wire(k, "NO", r, "pin0"); t.wire(r, "pin1", led, "anode"); t.wire(led, "cathode", bat, "minus");
        Connectivity cn; cn.build(t.d);
        SimResult s = simulate(t.d, cn, {}, 0.05);
        printf("  relay: %s, coil+ %.2f V, coil- %.2f V, NO %.2f V, LED %.2f\n", s.message.c_str(), s.netVoltage[(size_t)t.net(cn, k, "COIL+")],
               s.netVoltage[(size_t)t.net(cn, k, "COIL-")], s.netVoltage[(size_t)t.net(cn, k, "NO")], s.brightness[led]);
        check(s.brightness[led] > 0.3f, "relay coil closes the NO contact", failures);
    }
    {   // CD4511 + 7-segment showing "2" from an ESP32
        TB t;
        int esp = t.add("esp32_devkit", {{"outputs", "GPIO12=LOW, GPIO13=HIGH, GPIO14=LOW, GPIO27=LOW, GPIO26=HIGH"}});
        int dec = t.add("cd4511"), seg = t.add("seg7_cc");
        t.wire(esp, "3V3", dec, "VDD"); t.wire(esp, "GND", dec, "VSS");
        t.wire(esp, "GPIO12", dec, "A"); t.wire(esp, "GPIO13", dec, "B"); t.wire(esp, "GPIO14", dec, "C"); t.wire(esp, "GPIO27", dec, "D");
        t.wire(esp, "GPIO26", dec, "LT"); t.wire(esp, "GPIO26", dec, "BL"); t.wire(esp, "GPIO27", dec, "LE");
        const char* segs[] = {"a", "b", "c", "d", "e", "f", "g"};
        for (auto* sg : segs) {
            int r = t.add("resistor", {{"resistance", "220Ω"}});
            t.wire(dec, sg, r, "pin0");
            t.wire(r, "pin1", seg, sg);
        }
        t.wire(seg, "COM", esp, "GND");
        Connectivity cn; cn.build(t.d);
        SimResult s = simulate(t.d, cn, {}, 0.05);
        std::string lit;
        for (int i = 0; i < 7; i++) if (s.segments[seg].size() > (size_t)i && s.segments[seg][(size_t)i] > 0.2f) lit += segs[i];
        printf("  CD4511 + 7-seg showing 2: lit segments \"%s\" (expected abdeg)\n", lit.c_str());
        check(lit == "abdeg", "BCD decoder drives the 7-segment display", failures);
    }
    return failures;
}

int selftest() {
    int failures = 0;
    printf("designElectronics %s self-test\n", kAppVersion);
    printf("Part library: %zu entries\n", partLibrary().size());
    for (auto& e : partLibrary()) {
        PartDefPtr d = getPartDef(e.type, e.props);
        if (!d) { printf("  [FAIL] part %s\n", e.type.c_str()); failures++; continue; }
        // every pin id must be unique
        std::set<std::string> ids;
        for (auto& p : d->pins) ids.insert(p.id);
        if (ids.size() != d->pins.size()) { printf("  [FAIL] duplicate pin ids in %s\n", e.type.c_str()); failures++; }
        // JSON round trip of the definition
        PartDef back;
        std::string err;
        if (!partDefFromJson(partDefToJson(*d), back, &err) || back.pins.size() != d->pins.size() || back.gfx[BB].size() != d->gfx[BB].size()) {
            printf("  [FAIL] part JSON round trip %s: %s\n", e.type.c_str(), err.c_str());
            failures++;
        }
    }
    check(parseSI("4.7kΩ") == 4700, "parseSI 4.7kΩ", failures);
    check(std::fabs(parseSI("100nF") - 1e-7) < 1e-12, "parseSI 100nF", failures);
    check(std::fabs(parseSI("100µF") - 1e-4) < 1e-9, "parseSI 100µF", failures);

    for (size_t i = 0; i < examples().size(); i++) {
        Document d;
        examples()[i].build(d);
        d.syncWireEnds();
        printf("Example %zu: %s (%zu parts, %zu wires)\n", i, examples()[i].title.c_str(), d.parts.size(), d.wires.size());
        Connectivity c;
        c.build(d);
        RoutingStatus st[3];
        for (int v = 0; v < 3; v++) c.ratsnest(d, v, &st[v]);
        printf("  nets=%d  BB: %d/%d routed  SCH: %d/%d  PCB: %d/%d\n", c.numNets(), st[0].routedNets, st[0].nets, st[1].routedNets, st[1].nets, st[2].routedNets, st[2].nets);
        check(st[BB].connectionsToRoute == 0, "breadboard view fully wired", failures);
        // file round trip
        std::string text = d.serialize(true);
        Document d2;
        std::string err;
        bool ok = d2.deserialize(text, &err);
        check(ok && d2.parts.size() == d.parts.size() && d2.wires.size() == d.wires.size(), "save/load round trip", failures);
        if (ok) {
            Connectivity c2;
            c2.build(d2);
            check(c2.numNets() == c.numNets(), "nets identical after reload", failures);
        }
        // simulation
        SimResult sr = simulate(d, c, {});
        printf("  sim: %s\n", sr.message.c_str());
        for (auto& kv : sr.brightness) printf("    LED part %d brightness %.2f\n", kv.first, kv.second);
        for (auto& kv : sr.meterText) printf("    meter part %d reads %s\n", kv.first, kv.second.c_str());
        for (auto& w : sr.warnings) printf("    warning: %s\n", w.c_str());
        if (i == 0 || i == 1) check(!sr.brightness.empty() && sr.brightness.begin()->second > 0.3f, "LED lights up in simulation", failures);
        if (i == 4 || i == 5 || i == 6) {
            Simulator s2;
            int changes = 0;
            float prev = -1;
            for (int k = 0; k < 150; k++) {
                SimResult r2 = s2.step(d, c, {}, 0.01);
                float b = r2.brightness.empty() ? 0 : r2.brightness.begin()->second;
                bool on = b > 0.1f;
                if (prev >= 0 && on != (prev > 0.1f)) changes++;
                prev = b;
            }
            printf("  LED changed state %d times in 1.5 s\n", changes);
            check(changes >= 2, "example LED blinks in simulation", failures);
        }
        if (i == 2) {
            bool off = sr.brightness.empty() || sr.brightness.begin()->second < 0.05f;
            check(off, "LED off with button released", failures);
            std::set<int> pressed;
            for (auto& p : d.parts) if (p.type == "pushbutton") pressed.insert(p.id);
            SimResult on = simulate(d, c, pressed);
            check(!on.brightness.empty() && on.brightness.begin()->second > 0.3f, "LED on with button pressed", failures);
        }
        if (i == 3) {
            bool good = !sr.meterText.empty() && sr.meterText.begin()->second.rfind("4.", 0) == 0;
            check(good, "multimeter reads ~4.5V across the divider", failures);
        }
        // autoroute + DRC
        RouteResult rr = autoroute(d, c, true);
        c.build(d);
        c.ratsnest(d, PCB, &st[2]);
        printf("  autoroute: %s\n", rr.message.c_str());
        check(st[2].connectionsToRoute == 0, "PCB fully routed", failures);
        auto issues = runDrc(d, c);
        int copperIssues = 0;
        for (auto& is : issues) if (is.message.rfind("Too close", 0) == 0) { copperIssues++; if (copperIssues <= 3) printf("    drc: %s\n", is.message.c_str()); }
        for (auto& is : issues) if (is.message.rfind("Too close", 0) != 0) printf("    drc: %s\n", is.message.c_str());
        printf("  DRC: %zu issue(s), %d clearance\n", issues.size(), copperIssues);
        check(copperIssues == 0, "no clearance violations after autoroute", failures);
        // exports
        std::string svg = exportSvg(d, c, BB);
        check(svg.find("<svg") != std::string::npos, "SVG export", failures);
        check(exportBomCsv(d).find("Quantity") == 0, "BOM export", failures);
        check(exportNetlistXml(d, c).find("<net ") != std::string::npos, "netlist export", failures);
        check(exportSpice(d, c).find(".end") != std::string::npos, "SPICE export", failures);
        const char* tmp = SDL_GetPrefPath("designElectronics", "selftest");
        std::string zip = std::string(tmp ? tmp : "") + "gerber_test.zip";
        std::string msg;
        check(exportGerberZip(d, c, zip, &msg), "Gerber zip export", failures);
    }
    simTests(failures);
    printf(failures ? "\n%d FAILURE(S)\n" : "\nAll tests passed.\n", failures);
    return failures ? 1 : 0;
}

int exportExamples(const std::string& dir) {
    for (size_t i = 0; i < examples().size(); i++) {
        Document d;
        examples()[i].build(d);
        d.syncWireEnds();
        std::string name = examples()[i].title;
        for (auto& c : name) if (c == ' ' || c == '+' || c == '/') c = '_';
        std::string path = dir + "/" + name + kFileExt;
        if (!writeTextFile(path, d.serialize(true))) { printf("could not write %s\n", path.c_str()); return 1; }
        printf("wrote %s\n", path.c_str());
    }
    return 0;
}

struct Screenshot {
    std::string out;
    int frames = 12;
    int example = -1;
    int view = -1;
    bool sim = false, route = false;
    int w = 0, h = 0;
    std::string drawer;
    int select = -1;
    std::string menu;
    // scripted pointer events: frame -> (type, x, y) in window points
    struct Ev { int frame; char type; float x, y; };
    std::vector<Ev> events;
};

App* g_app = nullptr;
Screenshot g_shot;
int g_frame = 0;

}  // namespace

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
    std::vector<char*> rest;
    rest.push_back(argv[0]);
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--selftest") return selftest() == 0 ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
        if (a == "--export-examples") return exportExamples(next()) == 0 ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
        if (a == "--screenshot") g_shot.out = next();
        else if (a == "--example") g_shot.example = std::atoi(next().c_str());
        else if (a == "--view") { std::string v = next(); g_shot.view = v == "sch" ? SCH : v == "pcb" ? PCB : BB; }
        else if (a == "--sim") g_shot.sim = true;
        else if (a == "--autoroute") g_shot.route = true;
        else if (a == "--frames") g_shot.frames = std::atoi(next().c_str());
        else if (a == "--drawer") g_shot.drawer = next();
        else if (a == "--select") g_shot.select = std::atoi(next().c_str());
        else if (a == "--menu") g_shot.menu = next();
        else if (a == "--events") {
            // "frame:d:x:y,frame:m:x:y,frame:u:x:y,frame:k:keycode:0"
            std::string spec = next() + ",";
            std::string cur;
            for (char ch : spec) {
                if (ch != ',') { cur += ch; continue; }
                Screenshot::Ev e{};
                char t = 0;
                if (sscanf(cur.c_str(), "%d:%c:%f:%f", &e.frame, &t, &e.x, &e.y) == 4) { e.type = t; g_shot.events.push_back(e); }
                cur.clear();
            }
        }
        else if (a == "--size") { std::string s = next(); sscanf(s.c_str(), "%dx%d", &g_shot.w, &g_shot.h); }
        else if (a.rfind("-psn", 0) == 0) continue;  // macOS Finder launch argument
        else rest.push_back(argv[i]);
    }
    g_app = new App();
    *appstate = g_app;
    if (!g_app->init((int)rest.size(), rest.data())) return SDL_APP_FAILURE;
    if (!g_shot.out.empty()) {
        if (g_shot.w > 0) SDL_SetWindowSize(g_app->win, g_shot.w, g_shot.h);
        if (g_shot.example >= 0) {
            g_app->doc.clear();
            examples()[(size_t)g_shot.example].build(g_app->doc);
            g_app->doc.syncWireEnds();
            g_app->doc.touch();
            g_app->savedRevision = g_app->doc.revision;
            for (auto& c : g_app->cam) c.valid = false;
        }
        if (g_shot.view >= 0) g_app->view = g_shot.view;
        if (g_shot.route) { g_app->ensureConn(); autoroute(g_app->doc, g_app->conn, true); g_app->doc.touch(); }
        if (g_shot.sim) g_app->simOn = true;
        if (g_shot.select >= 0 && g_shot.select < (int)g_app->doc.parts.size()) g_app->selParts = {g_app->doc.parts[(size_t)g_shot.select].id};
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    return ((App*)appstate)->event(*event);
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    App* app = (App*)appstate;
    if (!g_shot.out.empty()) {
        g_frame++;
        if (g_frame == 3) {
            if (g_shot.drawer == "parts") app->drawerParts = true;
            if (g_shot.drawer == "inspector") app->drawerInspector = true;
            if (!g_shot.menu.empty()) {
                std::vector<MenuItem> m = g_shot.menu == "file" ? app->fileMenu() : g_shot.menu == "view" ? app->viewMenu() : app->allMenus();
                app->ui.openMenu(Rect(60 * app->ui.s, 8 * app->ui.s, 40 * app->ui.s, 36 * app->ui.s), m);
            }
        }
        for (auto& ev : g_shot.events) {
            if (ev.frame != g_frame) continue;
            SDL_Event e{};
            SDL_WindowID wid = SDL_GetWindowID(app->win);
            if (ev.type == 'd' || ev.type == 'u') {
                e.type = ev.type == 'd' ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
                e.button.windowID = wid; e.button.button = SDL_BUTTON_LEFT; e.button.down = ev.type == 'd';
                e.button.clicks = 1; e.button.x = ev.x; e.button.y = ev.y;
            } else if (ev.type == 'm') {
                e.type = SDL_EVENT_MOUSE_MOTION;
                e.motion.windowID = wid; e.motion.x = ev.x; e.motion.y = ev.y;
                e.motion.state = SDL_BUTTON_LMASK;
            } else if (ev.type == 'k') {
                e.type = SDL_EVENT_KEY_DOWN;
                e.key.windowID = wid; e.key.key = (SDL_Keycode)ev.x; e.key.mod = (SDL_Keymod)ev.y; e.key.down = true;
            }
            app->event(e);
        }
        if (g_frame >= g_shot.frames) {
            // Render one more frame and capture it before presenting.
            app->capturePath = g_shot.out;
            app->iterate();
            bool ok = app->captureOk;
            printf("%s %s\n", ok ? "saved" : "FAILED", g_shot.out.c_str());
            app->ensureConn();
            RoutingStatus st;
            app->conn.ratsnest(app->doc, app->view, &st);
            printf("doc: %zu parts, %zu wires, %zu selected, undo=%zu, view nets %d/%d routed\n", app->doc.parts.size(), app->doc.wires.size(),
                   app->selParts.size() + app->selWires.size(), app->undoStack.size(), st.routedNets, st.nets);
            for (auto& w : app->doc.wires) printf("  wire %d view %d ends %d:%d -> %d:%d\n", w.id, w.view, w.ends[0].part, w.ends[0].pin, w.ends[1].part, w.ends[1].pin);
            app->savedRevision = app->doc.revision;  // don't prompt
            return SDL_APP_SUCCESS;
        }
    }
    return app->iterate();
}

void SDL_AppQuit(void* appstate, SDL_AppResult) {
    App* app = (App*)appstate;
    if (app) {
        if (g_shot.out.empty()) app->shutdown();
        delete app;
    }
    SDL_Quit();
}
