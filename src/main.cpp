// designElectronics — entry point. Uses SDL3's main callbacks so the same code
// runs on desktop, iOS and Android.
//
// Command line:
//   designElectronics [file.dez]                open a project
//   designElectronics --selftest                run headless model/tool tests
//   designElectronics --export-examples DIR     write the built-in examples as .dez files
//   designElectronics --export-logo DIR         write logo.svg and app icon PNGs
//   designElectronics --mcp                     run as an MCP server on stdin/stdout (for AI assistants)
//   designElectronics --screenshot OUT.png [--example N] [--view bb|sch|pcb] [--sim]
//                     [--autoroute] [--size WxH] [--drawer parts|inspector] [--select N]
#define SDL_MAIN_USE_CALLBACKS 1
#include <cmath>
#include <cctype>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstring>

#include "app.h"
#include "history.h"
#include "logo.h"
#include "mcp.h"
#include "video.h"

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
    int add(const std::string& type, Props props = {}) {
        count++;
        std::string code;
        if (props.count("code")) { code = props["code"]; props.erase("code"); }
        PartInst& p = d.addPart(type, props, {count * 6000.f, count * 3000.f});
        p.code = code;
        return p.id;
    }
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

// Parts library data (parts/*.txt) and surface-mount pads end to end.
void partDataTests(int& failures) {
    printf("Parts library data\n");
    auto problems = checkPartData();
    for (size_t i = 0; i < problems.size() && i < 40; i++) printf("    %s\n", problems[i].c_str());
    check(problems.empty(), "every data part fits its packages, with no overlapping pads", failures);
    {   // an SMD chip and an SMD resistor on a board: route, DRC, Gerber paste layer
        TB t;
        int board = t.add("pcb_board", {{"width", "40"}, {"height", "30"}});
        int u = t.add("lm358", {{"package", "SOIC-8"}});
        int r = t.add("lm358", {{"package", "TSSOP-8"}});
        t.wire(u, "1OUT", r, "2IN+");
        t.wire(u, "VCC", r, "VCC");
        PartInst* b = t.d.part(board);
        b->place[PCB].pos = {0, 0};
        t.d.part(u)->place[PCB].pos = {400, 500};
        t.d.part(r)->place[PCB].pos = {1100, 600};
        Connectivity c;
        c.build(t.d);
        RouteResult rr = autoroute(t.d, c, true);
        c.build(t.d);
        RoutingStatus st;
        c.ratsnest(t.d, PCB, &st);
        bool topOnly = true;
        for (auto& w : t.d.wires) if (w.view == PCB && w.autorouted && w.layer != TOP) {
            for (auto& e : w.ends) if (e.attached() && t.d.part(e.part)->type != "via") topOnly = false;
        }
        check(st.connectionsToRoute == 0 && topOnly && rr.routed >= 2, ("SMD pads are routed on the top layer (" + rr.message + ")").c_str(), failures);
        int clr = 0;
        for (auto& is : runDrc(t.d, c))
            if (is.message.rfind("Too close", 0) == 0 || is.message.find("surface-mount") != std::string::npos) { clr++; printf("    drc: %s\n", is.message.c_str()); }
        check(clr == 0, "no DRC errors around fine-pitch SMD pads", failures);
        const char* tmp = SDL_GetPrefPath("designElectronics", "selftest");
        std::string zip = std::string(tmp ? tmp : "") + "gerber_smd.zip", msg;
        check(exportGerberZip(t.d, c, zip, &msg), "Gerber export with SMD pads", failures);
        Document d2;
        std::string err;
        check(d2.deserialize(t.d.serialize(true), &err) && d2.part(u)->def->pins[0].smd(), "SMD parts save and load", failures);
    }
}

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
    {   // LM393 night light: LDR divider vs. pot threshold; open-collector output sinks an LED
        float lit[2];
        for (int dark = 0; dark < 2; dark++) {
            TB t;
            int bat = t.add("battery", {{"voltage", "5V"}});
            int ldr = t.add("photocell", {{"light", dark ? "5" : "100"}});
            int rf = t.add("resistor", {{"resistance", "10kΩ"}});
            int pot = t.add("potentiometer");
            int u = t.add("lm393");
            int pu = t.add("resistor", {{"resistance", "1kΩ"}});
            int led = t.add("led", {{"color", "Yellow"}});
            t.wire(bat, "plus", ldr, "pin0"); t.wire(ldr, "pin1", rf, "pin0"); t.wire(rf, "pin1", bat, "minus");
            t.wire(bat, "plus", pot, "leg1"); t.wire(pot, "leg2", bat, "minus");
            t.wire(u, "VCC", bat, "plus"); t.wire(u, "GND", bat, "minus");
            t.wire(ldr, "pin1", u, "1IN+"); t.wire(pot, "wiper", u, "1IN-");
            t.wire(bat, "plus", pu, "pin0"); t.wire(pu, "pin1", led, "anode"); t.wire(led, "cathode", u, "1OUT");
            Connectivity cn; cn.build(t.d);
            Simulator sim;
            SimResult r;
            for (int k = 0; k < 6; k++) r = sim.step(t.d, cn, {}, 0.05);  // keeps working while the simulation runs
            lit[dark] = r.brightness[led];
        }
        printf("  LM393 night light: bright %.2f, dark %.2f\n", lit[0], lit[1]);
        check(lit[0] < 0.05f && lit[1] > 0.1f, "comparator switches an LED on in the dark", failures);
    }
    {   // programs: an ESP32 sketch blinks an LED and prints over Serial
        TB t;
        int esp = t.add("esp32_devkit", {{"code",
            "const int LED = 2;\n"
            "void setup() { pinMode(LED, OUTPUT); Serial.begin(115200); Serial.println(\"hello\"); }\n"
            "void loop() { digitalWrite(LED, HIGH); delay(200); digitalWrite(LED, LOW); delay(200); }\n"}});
        int r = t.add("resistor", {{"resistance", "220Ω"}}), led = t.add("led");
        t.wire(esp, "GPIO2", r, "pin0"); t.wire(r, "pin1", led, "anode"); t.wire(led, "cathode", esp, "GND");
        Connectivity cn; cn.build(t.d);
        Simulator sim;
        std::string serial;
        float on = 0, off = 0;
        for (int i = 0; i < 40; i++) {
            SimResult s = sim.step(t.d, cn, {}, 0.01);
            for (auto& kv : s.serial) serial += kv.second;
            if (i == 10) on = s.brightness[led];   // ~100 ms: LED on
            if (i == 30) off = s.brightness[led];  // ~300 ms: LED off
        }
        printf("  ESP32 sketch: LED %.2f then %.2f, serial \"%s\"\n", on, off, serial.substr(0, serial.find('\n')).c_str());
        check(on > 0.2f && off < 0.05f && serial == "hello\n", "an ESP32 sketch blinks an LED and prints to Serial", failures);
    }
    {   // programs: an Uno reads a pushbutton (INPUT_PULLUP) and lights an LED while it's held
        TB t;
        int uno = t.add("arduino_uno", {{"code",
            "void setup() { pinMode(2, INPUT_PULLUP); pinMode(13, OUTPUT); }\n"
            "void loop() { if (digitalRead(2) == LOW) digitalWrite(13, HIGH); else digitalWrite(13, LOW); }\n"}});
        int btn = t.add("pushbutton");
        int r = t.add("resistor", {{"resistance", "220Ω"}}), led = t.add("led");
        t.wire(uno, "D2", btn, "pin1"); t.wire(btn, "pin2", uno, "GND");
        t.wire(uno, "D13", r, "pin0"); t.wire(r, "pin1", led, "anode"); t.wire(led, "cathode", uno, "GND");
        Connectivity cn; cn.build(t.d);
        Simulator sim;
        SimResult a, b;
        for (int i = 0; i < 5; i++) a = sim.step(t.d, cn, {}, 0.01);
        for (int i = 0; i < 5; i++) b = sim.step(t.d, cn, {btn}, 0.01);
        printf("  Uno button sketch: released %.2f, pressed %.2f\n", a.brightness[led], b.brightness[led]);
        check(a.brightness[led] < 0.05f && b.brightness[led] > 0.3f, "a sketch reads a button through INPUT_PULLUP", failures);
    }
    {   // programs: shiftOut drives a 74HC595 (pattern 0b10100101 → QA, QC, QF, QH lit)
        TB t;
        int uno = t.add("arduino_uno", {{"code",
            "int latch = 8, clk = 12, data = 11;\n"
            "void setup() { pinMode(latch, OUTPUT); pinMode(clk, OUTPUT); pinMode(data, OUTPUT); }\n"
            "void loop() { digitalWrite(latch, LOW); shiftOut(data, clk, MSBFIRST, 0xA5); digitalWrite(latch, HIGH); delay(500); }\n"}});
        int sr = t.add("74hc595");
        t.wire(uno, "D11", sr, "SER"); t.wire(uno, "D12", sr, "SRCLK"); t.wire(uno, "D8", sr, "RCLK");
        t.wire(uno, "5V", sr, "VCC"); t.wire(uno, "5V", sr, "~SRCLR"); t.wire(uno, "GND", sr, "GND"); t.wire(uno, "GND", sr, "~OE");
        const char* q[] = {"QA", "QB", "QC", "QD", "QE", "QF", "QG", "QH"};
        int leds[8];
        for (int i = 0; i < 8; i++) {
            int r = t.add("resistor", {{"resistance", "330Ω"}});
            leds[i] = t.add("led");
            t.wire(sr, q[i], r, "pin0"); t.wire(r, "pin1", leds[i], "anode"); t.wire(leds[i], "cathode", uno, "GND");
        }
        Connectivity cn; cn.build(t.d);
        Simulator sim;
        SimResult rr;
        for (int i = 0; i < 20; i++) rr = sim.step(t.d, cn, {}, 0.01);
        std::string pattern;
        for (int i = 0; i < 8; i++) pattern += rr.brightness[leds[i]] > 0.2f ? '1' : '0';
        printf("  74HC595 via shiftOut(0xA5): QA..QH = %s (expected 10100101)\n", pattern.c_str());
        check(pattern == "10100101", "shiftOut from a sketch loads a 74HC595", failures);
    }
    {   // programs: read a MAX6675 by bit-banging and show it on an SSD1306 (Adafruit API)
        TB t;
        int esp = t.add("esp32_devkit", {{"code",
            "#include <Adafruit_SSD1306.h>\n"
            "Adafruit_SSD1306 display(128, 64, &Wire, -1);\n"
            "float readTC() { uint16_t v = 0; digitalWrite(5, LOW);\n"
            "  for (int i = 15; i >= 0; i--) { digitalWrite(18, HIGH); if (digitalRead(19)) v |= (1 << i); digitalWrite(18, LOW); }\n"
            "  digitalWrite(5, HIGH); return (v >> 3) * 0.25; }\n"
            "void setup() { Serial.begin(115200); pinMode(18, OUTPUT); pinMode(5, OUTPUT); pinMode(19, INPUT); digitalWrite(5, HIGH);\n"
            "  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) Serial.println(\"no oled\"); }\n"
            "void loop() { float c = readTC(); Serial.println(c, 2); display.clearDisplay(); display.setCursor(0, 0);\n"
            "  display.print(c, 1); display.print(\" C\"); display.drawRect(0, 20, 50, 10, SSD1306_WHITE); display.display(); delay(200); }\n"}});
        int tc = t.add("max6675", {{"temperature", "123.25"}});
        int ol = t.add("ssd1306");
        t.wire(esp, "GPIO18", tc, "SCK"); t.wire(esp, "GPIO5", tc, "CS"); t.wire(esp, "GPIO19", tc, "SO");
        t.wire(esp, "3V3", tc, "VCC"); t.wire(esp, "GND", tc, "GND");
        t.wire(esp, "GPIO21", ol, "SDA"); t.wire(esp, "GPIO22", ol, "SCL"); t.wire(esp, "3V3", ol, "VCC"); t.wire(esp, "GND", ol, "GND");
        Connectivity cn; cn.build(t.d);
        Simulator sim;
        std::string serial;
        SimResult r;
        for (int i = 0; i < 30; i++) { r = sim.step(t.d, cn, {}, 0.01); for (auto& kv : r.serial) serial += kv.second; }
        std::string shown = r.oled.count(ol) && !r.oled[ol].text.empty() ? r.oled[ol].text[0].text : "";
        size_t lit = 0;
        if (r.oled.count(ol)) for (auto px : r.oled[ol].px) lit += px;
        printf("  MAX6675 -> OLED: serial \"%s\", OLED \"%s\", %zu px, %zu warnings\n", serial.substr(0, serial.find('\n')).c_str(), shown.c_str(), lit,
               r.warnings.size());
        check(serial.rfind("123.25\n", 0) == 0 && shown == "123.3 C" && lit == 2 * 50 + 2 * 8 && r.warnings.empty(),
              "a sketch reads a MAX6675 and draws on an SSD1306 OLED", failures);
    }
    {   // programs: compile errors are reported with their line
        TB t;
        int uno = t.add("arduino_uno", {{"code", "void setup() {\n  pinMode(13, OUTPUT)\n}\nvoid loop() {}\n"}});
        Connectivity cn; cn.build(t.d);
        SimResult r = simulate(t.d, cn, {}, 0.01);
        printf("  broken sketch: line %d: %s\n", r.code[uno].errorLine, r.code[uno].error.c_str());
        check(r.code[uno].errorLine == 3 && !r.code[uno].error.empty(), "sketch errors point at the right line", failures);
    }
    {   // CD4017 clocked by an ESP32 square wave: outputs advance
        TB t;
        int esp = t.add("esp32_devkit", {{"outputs", "GPIO4=BLINK:50, GPIO5=LOW"}});
        int c = t.add("cd4017");
        t.wire(esp, "3V3", c, "VDD"); t.wire(esp, "GND", c, "VSS"); t.wire(esp, "GPIO4", c, "CLK");
        t.wire(esp, "GPIO5", c, "~CLKINH"); t.wire(esp, "GPIO5", c, "RST");
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
        t.wire(esp, "GPIO26", dec, "~LT"); t.wire(esp, "GPIO26", dec, "~BL"); t.wire(esp, "GPIO27", dec, "LE");
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

// ---- version history (local) ----
void historyTests(int& failures) {
    printf("Version history\n");
    char* base = SDL_GetPrefPath("designElectronics", "designElectronics-selftest");
    std::string root = std::string(base ? base : "./") + "history-test";
    SDL_free(base);
    SDL_RemovePath((root + "/x").c_str());
    history::setRoot(root);
    Document d;
    examples()[0].build(d);  // LED with battery
    d.syncWireEnds();
    history::ensureUid(d);
    d.uid += std::to_string(SDL_GetTicks());  // fresh history for every run
    LocalVersion v1 = history::record(d, d.serialize(true), "");
    LocalVersion same = history::record(d, d.serialize(true), "");
    PartInst* r = nullptr;
    for (auto& p : d.parts) if (p.type == "resistor") r = &p;
    std::string before = r->prop("resistance");
    r->props["resistance"] = "330Ω";
    r->refresh();
    d.addPart("led", {{"color", "Green"}}, {0, 0});
    LocalVersion v2 = history::record(d, d.serialize(true), "Brighter");
    auto list = history::list(d.uid);
    printf("  v%d: %s\n  v%d: %s (%s)\n", v1.n, v1.summary.c_str(), v2.n, v2.summary.c_str(), v2.message.c_str());
    check(v1.n == 1 && same.n == 0 && v2.n == 2, "every save is a version; unchanged saves are skipped", failures);
    check(v2.summary.find("330Ω") != std::string::npos && v2.summary.find("Added") != std::string::npos, "change summary names what changed", failures);
    check(list.size() == 2 && list[0].n == 2, "versions are listed newest first", failures);
    Document old;
    std::string err;
    bool ok = old.deserialize(history::read(d.uid, list[1]), &err);
    std::string oldR;
    for (auto& p : old.parts) if (p.type == "resistor") oldR = p.prop("resistance");
    check(ok && oldR == before && old.parts.size() + 1 == d.parts.size(), "an old version reads back exactly", failures);
    std::string bin = std::string("de\x00\xff\x10 Ω") + "end";
    check(github::base64Decode(github::base64Encode(bin)) == bin && github::base64Encode("Man") == "TWFu" && github::base64Encode("Ma") == "TWE=",
          "base64 round trip", failures);
    GitHubSettings gs;
    gs.login = "octocat";
    GitLink l = github::defaultLink(gs, d);
    printf("  default GitHub location: %s / %s\n", l.repo.c_str(), l.path.c_str());
    check(l.repo == "octocat/electronics-designs" && l.path.rfind("led-with-battery-", 0) == 0, "each design gets its own folder", failures);
    history::setRoot("");
}

// ---- video writers: valid MP4 (H.264 SPS not byte-swapped) and GIF ----
void videoTests(int& failures) {
    printf("Video export\n");
    char* base = SDL_GetPrefPath("designElectronics", "designElectronics-selftest");
    std::string dir = base ? base : "./";
    SDL_free(base);
    const int W = 270, H = 480;  // not multiples of 16, like 1080×1920
    std::vector<uint8_t> px((size_t)W * H * 4);
    std::string err;
    auto mp4 = openMp4(dir + "t.mp4", W, H, 30, &err);
    auto gif = openGif(dir + "t.gif", W, H, 15, &err);
    bool ok = mp4 && gif;
    for (int f = 0; ok && f < 20; f++) {
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                uint8_t* p = &px[((size_t)y * W + x) * 4];
                bool led = (x - W / 2) * (x - W / 2) + (y - H / 2) * (y - H / 2) < 900 && (f / 5) % 2 == 0;
                p[0] = led ? 230 : 245; p[1] = led ? 40 : 245; p[2] = led ? 40 : 240; p[3] = 255;
            }
        ok = mp4->addFrame(px.data(), W * 4) && gif->addFrame(px.data(), W * 4);
    }
    ok = ok && mp4->finish() && gif->finish();
    std::string m = readTextFile(dir + "t.mp4"), gf = readTextFile(dir + "t.gif");
    bool mp4ok = m.size() > 1000 && m.find("ftyp") != std::string::npos && m.find("avcC") != std::string::npos &&
                 m.find(std::string("\x67\x42", 2)) != std::string::npos;  // SPS: NAL type 7, Baseline profile
    bool gifok = gf.size() > 100 && gf.compare(0, 6, "GIF89a") == 0 && (unsigned char)gf.back() == 0x3B;
    printf("  MP4 %zu bytes, GIF %zu bytes\n", m.size(), gf.size());
    check(ok && mp4ok, "MP4 export is valid H.264 (header not byte-swapped)", failures);
    check(ok && gifok, "GIF export is a valid animation", failures);
    SDL_RemovePath((dir + "t.mp4").c_str());
    SDL_RemovePath((dir + "t.gif").c_str());
}

// ---- layers: hidden / locked survive save + load ----
void layerTests(int& failures) {
    printf("Layers\n");
    Document d;
    examples()[0].build(d);
    PartInst* led = nullptr;
    for (auto& p : d.parts) if (p.type == "led") led = &p;
    led->hidden = 1u << BB;
    led->locked = true;
    Document e;
    std::string err;
    bool ok = e.deserialize(d.serialize(true), &err);
    const PartInst* l2 = nullptr;
    for (auto& p : e.parts) if (p.type == "led") l2 = &p;
    check(ok && l2 && l2->hiddenIn(BB) && !l2->hiddenIn(SCH) && l2->locked, "hidden (per view) and locked are saved and loaded", failures);
    Connectivity c1, c2;
    c1.build(d);
    examples()[0].build(e = Document());
    c2.build(e);
    SimResult r = simulate(d, c1, {});
    check(c1.numNets() == c2.numNets() && !r.brightness.empty() && r.brightness.begin()->second > 0.3f, "a hidden part still connects and simulates", failures);
}

// ---- GitHub sync against a mock API (tests/github_mock_test.py sets DE_GITHUB_API) ----
int githubSelftest() {
    int failures = 0;
    GitHubSettings s;
    s.token = "test-token";
    std::string err, login;
    check(github::whoAmI(s.token, login, err), ("who am I: " + (err.empty() ? login : err)).c_str(), failures);
    s.login = login;
    Document d;
    examples()[0].build(d);
    history::ensureUid(d);
    GitLink link = github::defaultLink(s, d);
    GitCommit c1, c2;
    check(github::commitFile(s, link, "version one", "First", c1, err), ("first commit " + err).c_str(), failures);
    check(github::commitFile(s, link, "version two Ω", "Second", c2, err), ("second commit " + err).c_str(), failures);
    std::vector<GitCommit> list;
    bool listed = github::listCommits(s, link, list, err);
    printf("  listed %zu commit(s)%s%s\n", list.size(), list.empty() ? "" : ", newest: ", list.empty() ? err.c_str() : list[0].message.c_str());
    check(listed && list.size() == 2 && list[0].message == "Second", "history lists both commits, newest first", failures);
    std::string body;
    check(github::fetchFile(s, link, c1.sha, body, err) && body == "version one", "an old version downloads exactly", failures);
    GitLink other = link;
    other.repo = "someone-else/designs";
    check(!github::commitFile(s, other, "x", "x", c1, err) && err.find("only create repositories under your own account") != std::string::npos,
          "won't create repos for other owners", failures);
    s.token = "bad";
    check(!github::whoAmI(s.token, login, err) && err.find("401") != std::string::npos, "bad token gives a clear error", failures);
    printf(failures ? "%d FAILURE(S)\n" : "GitHub sync tests passed.\n", failures);
    return failures ? 1 : 0;
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
    partDataTests(failures);
    simTests(failures);
    historyTests(failures);
    layerTests(failures);
    videoTests(failures);
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

// Writes the logo as SVG plus PNGs at the sizes a macOS .iconset needs.
int exportLogo(const std::string& dir) {
    if (!writeTextFile(dir + "/logo.svg", logoSvg())) { printf("could not write %s/logo.svg\n", dir.c_str()); return 1; }
    for (int size : {16, 32, 64, 128, 256, 512, 1024}) {
        SDL_Surface* s = makeLogoSurface(size);
        std::string path = dir + "/icon_" + std::to_string(size) + ".png";
        bool ok = s && SDL_SavePNG(s, path.c_str());
        SDL_DestroySurface(s);
        if (!ok) { printf("could not write %s\n", path.c_str()); return 1; }
    }
    printf("wrote logo.svg and icon_*.png to %s\n", dir.c_str());
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
    std::string sheet;  // history | commit | account
    bool openCode = false;  // open the program panel of the first board
    std::string ai;         // --ai "prompt": run the AI tab with this message, capture when it finishes
    bool aiStarted = false;
    // scripted pointer events: frame -> (type, x, y) in window points
    struct Ev { int frame; char type; float x, y; };
    std::vector<Ev> events;
};

// --record-ui out.mp4: records the app window itself (with --ai: the agent building the circuit,
// time-lapsed by --speed, then the finished circuit simulating at normal speed).
struct UiRecorder {
    std::string path;
    double speed = 4;          // time-lapse while the agent works
    double curSpeed = 1;
    int fps = 30, outW = 0, outH = 0;
    std::unique_ptr<VideoWriter> vw;
    std::vector<uint8_t> buf;
    std::string err;
    double nextT = 0;
    long frames = 0;
    void frame(SDL_Surface* s) {
        double now = (double)SDL_GetTicksNS() / 1e9;
        if (vw && now < nextT) return;
        SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
        if (!c) return;
        if (!vw) {
            outW = std::min(1920, c->w) & ~1;
            outH = (int)std::lround((double)outW * c->h / c->w) & ~1;
            vw = openMp4(path, outW, outH, fps, &err);
            if (!vw) { SDL_DestroySurface(c); return; }
            nextT = now;
        }
        // box-filtered downscale to the output size
        buf.assign((size_t)outW * outH * 4, 0);
        double rx = (double)c->w / outW, ry = (double)c->h / outH;
        const uint8_t* src = (const uint8_t*)c->pixels;
        for (int y = 0; y < outH; y++) {
            int y0 = (int)(y * ry), y1 = std::max(y0 + 1, std::min(c->h, (int)((y + 1) * ry)));
            for (int x = 0; x < outW; x++) {
                int x0 = (int)(x * rx), x1 = std::max(x0 + 1, std::min(c->w, (int)((x + 1) * rx)));
                unsigned acc[4] = {0, 0, 0, 0}, n = 0;
                for (int yy = y0; yy < y1; yy++)
                    for (int xx = x0; xx < x1; xx++, n++) {
                        const uint8_t* p = src + (size_t)yy * c->pitch + (size_t)xx * 4;
                        for (int k = 0; k < 4; k++) acc[k] += p[k];
                    }
                uint8_t* o = &buf[((size_t)y * outW + x) * 4];
                for (int k = 0; k < 4; k++) o[k] = (uint8_t)(acc[k] / n);
                o[3] = 255;
            }
        }
        SDL_DestroySurface(c);
        // keep the video's clock: repeat the frame for slots we fell behind on (at most a few)
        int copies = 0;
        do {
            vw->addFrame(buf.data(), outW * 4);
            frames++;
            nextT += curSpeed / fps;
        } while (nextT <= now && ++copies < 4);
        if (nextT < now) nextT = now;
    }
};
UiRecorder g_ui;
std::string g_agentCmd;
std::string g_animate, g_zoomTo;
// --animate "max6675.temperature=24:85": sets that property on every part of the type, eased over `seconds`
std::function<void(double)> makeAnimation(App* app, const std::string& spec, double seconds) {
    size_t dot = spec.find('.'), eq = spec.find('='), col = spec.find(':');
    if (dot == std::string::npos || eq == std::string::npos || col == std::string::npos) return nullptr;
    std::string type = spec.substr(0, dot), prop = spec.substr(dot + 1, eq - dot - 1);
    double a = std::atof(spec.substr(eq + 1, col - eq - 1).c_str()), b = std::atof(spec.substr(col + 1).c_str());
    return [=](double t) {
        double u = std::clamp(t / std::max(0.1, seconds * 0.8), 0.0, 1.0);
        u = u * (2 - u);  // ease out
        char v[32];
        snprintf(v, sizeof v, "%.2f", a + (b - a) * u);
        for (auto& p : app->doc.parts) if (p.type == type) p.props[prop] = v;
    };
}
struct UiScript {
    int phase = 0;
    double t0 = 0;
    size_t typed = 0;
} g_uis;

// Ease the canvas camera toward framing `target` (world units), or the whole design when target is empty.
void glideCamera(App* app, Rect target, float rate) {
    Camera& c = app->cam[app->view];
    Camera saved = c;
    if (target.w <= 0) {
        app->fitView(app->view, app->rCanvas);
    } else {
        Rect vp = app->rCanvas;
        c.zoom = std::min(vp.w / target.w, vp.h / target.h);
        c.pan = vp.center() - target.center() * c.zoom;
    }
    Camera goal = c;
    c = saved;
    if (!saved.valid) { c = goal; return; }
    // interpolate in log-zoom so the motion feels even
    Vec2 ctrNow = (app->rCanvas.center() - c.pan) / c.zoom, ctrGoal = (app->rCanvas.center() - goal.pan) / goal.zoom;
    float z = std::exp(std::log(c.zoom) + (std::log(goal.zoom) - std::log(c.zoom)) * rate);
    Vec2 ctr = ctrNow + (ctrGoal - ctrNow) * rate;
    c.zoom = z;
    c.pan = app->rCanvas.center() - ctr * z;
}

App* g_app = nullptr;
bool g_embed = false;
// --record OUT.mp4|OUT.gif [--preset 0-3] [--seconds N] [--record-view 0-3]: export a video and quit
std::string g_record;
int g_recordPreset = 0, g_recordSeconds = 3, g_recordView = -1;
bool g_recordStarted = false;
std::string g_editorUrl;
int g_loadExample = -1;
Screenshot g_shot;
int g_frame = 0;

}  // namespace

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
    std::vector<char*> rest;
    rest.push_back(argv[0]);
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--share-fragment") {  // print the "#d=…" link fragment for a .dez (or an example index)
            std::string arg = next();
            App tmp;
            if (!arg.empty() && std::isdigit((unsigned char)arg[0])) examples()[(size_t)std::atoi(arg.c_str()) % examples().size()].build(tmp.doc);
            else if (!tmp.doc.deserialize(readTextFile(arg), nullptr)) return SDL_APP_FAILURE;
            if (i + 1 < argc && std::string(argv[i + 1]) == "--json") printf("%s\n", tmp.doc.compactJson().c_str());
            else printf("%s\n", tmp.shareFragment().c_str());
            return SDL_APP_SUCCESS;
        }
        if (a == "--export-logo") return exportLogo(next()) == 0 ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
        if (a == "--github-selftest") return githubSelftest() == 0 ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
        if (a == "--mcp") {
            int port = 0;
            std::string token;
            for (int k = i + 1; k + 1 < argc; k++) {
                if (std::string(argv[k]) == "--connect") port = std::atoi(argv[k + 1]);
                if (std::string(argv[k]) == "--token") token = argv[k + 1];
            }
            return runMcpServer(port, token) == 0 ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
        }
        if (a == "--selftest") return selftest() == 0 ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
        if (a == "--export-examples") return exportExamples(next()) == 0 ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
        if (a == "--screenshot") g_shot.out = next();
        else if (a == "--example") g_shot.example = std::atoi(next().c_str());
        else if (a == "--view") { std::string v = next(); g_shot.view = (v == "sch" || v == "schematic") ? SCH : v == "pcb" ? PCB : BB; }
        else if (a == "--sim") g_shot.sim = true;
        else if (a == "--autoroute") g_shot.route = true;
        else if (a == "--frames") g_shot.frames = std::atoi(next().c_str());
        else if (a == "--drawer") g_shot.drawer = next();
        else if (a == "--select") g_shot.select = std::atoi(next().c_str());
        else if (a == "--menu") g_shot.menu = next();
        else if (a == "--sheet") g_shot.sheet = next();
        else if (a == "--open-code") g_shot.openCode = true;
        else if (a == "--ai") g_shot.ai = next();
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
        else if (a == "--embed") g_embed = true;
        else if (a == "--record") g_record = next();
        else if (a == "--record-ui") g_ui.path = next();
        else if (a == "--animate") g_animate = next();
        else if (a == "--zoom-to") g_zoomTo = next();  // "max6675,ssd1306": move in on these parts halfway through  // "type.prop=from:to" eased over the recording
        else if (a == "--speed") g_ui.speed = std::atof(next().c_str());
        else if (a == "--agent-cmd") g_agentCmd = next();  // testing: a custom command instead of Claude Code
        else if (a == "--preset") g_recordPreset = std::atoi(next().c_str());
        else if (a == "--seconds") g_recordSeconds = std::atoi(next().c_str());
        else if (a == "--record-view") g_recordView = std::atoi(next().c_str());
        else if (a == "--editor-url") g_editorUrl = next();
        else if (a == "--load-example") g_loadExample = std::atoi(next().c_str());
        else if (a == "--size") { std::string s = next(); sscanf(s.c_str(), "%dx%d", &g_shot.w, &g_shot.h); }
        else if (a.rfind("-psn", 0) == 0) continue;  // macOS Finder launch argument
        else rest.push_back(argv[i]);
    }
    g_app = new App();
    *appstate = g_app;
    g_app->embedMode = g_app->readOnly = g_embed;
    g_app->editorUrl = g_editorUrl;
    g_app->scratchSession = !g_shot.out.empty() || !g_record.empty();
    if (!g_app->init((int)rest.size(), rest.data())) return SDL_APP_FAILURE;
    if (g_loadExample >= 0 && g_loadExample < (int)examples().size()) {
        g_app->doc.clear();
        examples()[(size_t)g_loadExample].build(g_app->doc);
        g_app->doc.syncWireEnds();
        g_app->doc.touch();
        g_app->savedRevision = g_app->doc.revision;
    }
#ifdef __EMSCRIPTEN__
    // a design opened from a link isn't a file on this device: Save downloads it under its title
    g_app->filePath.clear();
    g_app->savedRevision = g_app->doc.revision;
#endif
    // start view / simulation (web links: #view=pcb&sim=1)
    if (g_shot.out.empty()) {
        if (g_shot.view >= 0) g_app->view = g_shot.view;
        if (g_shot.sim) g_app->simOn = true;
    }
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
        // screenshots are for sharing: no local file path or "Opened…" toast in them
        g_app->filePath.clear();
        g_app->ui.toast("", 0);
        if (g_shot.openCode)
            for (auto& p : g_app->doc.parts)
                if (p.def->sim == "board" || p.def->sim == "mcu") { g_app->openCode(p.id); g_app->ui.toast("", 0); break; }
        if (g_shot.sheet == "history") g_app->openHistory();
        else if (g_shot.sheet == "commit") g_app->openCommitSheet();
        else if (g_shot.sheet == "account") g_app->openGitHubAccount();
        else if (g_shot.sheet == "embed") g_app->openEmbedSheet();
        else if (g_shot.sheet == "video") g_app->openVideoSheet();
        else if (g_shot.sheet == "upload")
            for (auto& p : g_app->doc.parts)
                if (p.def->sim == "board") { g_app->openUploadSheet(p.id); break; }
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    return ((App*)appstate)->event(*event);
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    App* app = (App*)appstate;
    if (!g_record.empty()) {
        if (!g_recordStarted) {
            g_recordStarted = true;
            app->videoPreset = g_recordPreset;
            app->videoSeconds = g_recordSeconds;
            app->videoView = g_recordView >= 0 ? g_recordView : app->view;
            app->videoGif = g_record.size() > 4 && g_record.substr(g_record.size() - 4) == ".gif";
            app->videoTitle = app->doc.title;
            if (!g_animate.empty()) app->videoAnimate = makeAnimation(app, g_animate, g_recordSeconds);
            if (!g_zoomTo.empty()) {
                std::string types = "," + g_zoomTo + ",";
                double secs = g_recordSeconds;
                app->videoZoom = [app, types, secs](double t, Rect& focus) -> float {
                    bool any = false;
                    for (auto& p : app->doc.parts)
                        if (types.find("," + p.type + ",") != std::string::npos) {
                            Rect r = p.worldBounds(app->videoView < 3 ? app->videoView : BB);
                            focus = any ? focus.united(r) : r;
                            any = true;
                        }
                    if (!any) return 0;
                    focus = Rect(focus.x - 150, focus.y - 150, focus.w + 300, focus.h + 300);
                    double u = std::clamp((t - secs * 0.3) / 1.6, 0.0, 1.0);  // glide in from 30% of the clip
                    return (float)(u * u * (3 - 2 * u));
                };
            }
            app->startVideo(g_record);
            if (!app->videoJob) { printf("could not start recording\n"); return SDL_APP_FAILURE; }
        } else if (!app->videoJob) {
            if (!app->lastVideoError.empty()) { printf("recording failed: %s\n", app->lastVideoError.c_str()); return SDL_APP_FAILURE; }
            printf("recorded %s\n", g_record.c_str());
            return SDL_APP_SUCCESS;
        }
    }
    if (!g_shot.out.empty()) {
        g_frame++;
        if (g_frame == 3) {
            if (g_shot.drawer == "parts") app->drawerParts = true;
            if (g_shot.drawer == "inspector") app->drawerInspector = true;
            if (g_shot.drawer == "layers") app->drawerLayers = true;
            if (g_shot.drawer == "layers-tab") app->rightTab = 1;
            if (g_shot.drawer == "ai-tab") app->rightTab = 2;
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
        bool waitAI = false;
        if (!g_ui.path.empty()) {
            // scripted recording: hold, type the prompt, time-lapse the agent, then run the result
            double now = (double)SDL_GetTicksNS() / 1e9;
            auto& S = g_uis;
            if (!app->frameTap) {
                app->frameTap = [](SDL_Surface* s) { g_ui.frame(s); };
                app->rightTab = 2;
                app->detectAgents();
                if (!g_agentCmd.empty()) { app->agentKind = "custom"; app->agentCustomCommand = g_agentCmd; }
                S.t0 = now;
            }
            bool running = false, detected = false;
            std::map<std::string, std::string> paths;
            app->agentMessages(running, detected, paths);
            double t = now - S.t0;
            static int lastPhase = -1;
            static bool lastRunning = false;
            if (S.phase != lastPhase || running != lastRunning) {
                printf("[rec %.1fs] phase %d running %d\n", (double)SDL_GetTicks() / 1000.0, S.phase, (int)running);
                fflush(stdout);
                lastPhase = S.phase;
                lastRunning = running;
            }
            if (S.phase == 0 && t > 1.5 && detected) { S.phase = 1; S.t0 = now; }
            else if (S.phase == 1) {  // type the prompt
                size_t want = std::min(g_shot.ai.size(), (size_t)(t / 0.03));
                while (S.typed < want) {  // whole UTF-8 characters
                    S.typed++;
                    while (S.typed < g_shot.ai.size() && ((unsigned char)g_shot.ai[S.typed] & 0xC0) == 0x80) S.typed++;
                }
                app->agentInput = g_shot.ai.substr(0, S.typed);
                if (S.typed >= g_shot.ai.size() && t > g_shot.ai.size() * 0.03 + 0.8) {
                    app->agentInput.clear();
                    app->startAgent(g_shot.ai);
                    g_ui.curSpeed = g_ui.speed;
                    S.phase = 2;
                    S.t0 = now;
                }
            } else if (S.phase == 2 && !running && t > 2) {
                g_ui.curSpeed = 1;
                app->selParts.clear();  // no selection outline in the finished shot
                app->selWires.clear();
                S.phase = 3;
                S.t0 = now;
            } else if (S.phase == 3 && t > 2.5) {  // run it
                app->simOn = true;
                S.phase = 4;
                S.t0 = now;
            } else if (S.phase == 4) {
                // warm the thermocouple up so the graph moves
                for (auto& p : app->doc.parts)
                    if (p.type == "max6675") {
                        char b[16];
                        snprintf(b, sizeof b, "%.2f", 24 + 60 * std::min(1.0, t / 13) * (2 - std::min(1.0, t / 13)));
                        p.props["temperature"] = b;
                    }
                // then move in on the modules so the OLED reads on a phone
                if (t > 3) {
                    Rect focus;
                    bool any = false;
                    for (auto& p : app->doc.parts)
                        if (p.type == "max6675" || p.type == "ssd1306" || p.type == "sh1106") {
                            Rect r = p.worldBounds(app->view);
                            focus = any ? focus.united(r) : r;
                            any = true;
                        }
                    if (any) glideCamera(app, Rect(focus.x - 250, focus.y - 250, focus.w + 500, focus.h + 500), 0.05f);
                }
                if (t > 16) S.phase = 5;
            }
            if (S.phase == 2) glideCamera(app, Rect(), 0.08f);  // keep the growing design in frame
            waitAI = S.phase < 5 && g_frame < 60 * 600;
            if (!waitAI && g_ui.vw) {
                app->frameTap = nullptr;
                bool ok = g_ui.vw->finish();
                printf("%s %s (%ld frames, %dx%d)\n", ok ? "recorded" : "FAILED", g_ui.path.c_str(), g_ui.frames, g_ui.outW, g_ui.outH);
                bool r2 = false, d2 = false;
                for (auto& m : app->agentMessages(r2, d2, paths)) if (m.role != App::ChatMsg::R_USER) printf("  [%d] %s\n", (int)m.role, m.text.substr(0, 200).c_str());
                g_ui.vw.reset();
                std::string dez = g_ui.path.substr(0, g_ui.path.find_last_of('.')) + ".dez";
                writeTextFile(dez, app->doc.toJson(false).dump(1));
                printf("design saved to %s\n", dez.c_str());
            }
        } else if (!g_shot.ai.empty()) {
            app->rightTab = 2;
            bool running = false, detected = false;
            std::map<std::string, std::string> paths;
            auto msgs = app->agentMessages(running, detected, paths);
            if (!g_shot.aiStarted) {
                if (!app->agent) app->detectAgents();
                if (detected) {
                    g_shot.aiStarted = true;
                    app->startAgent(g_shot.ai);
                }
                waitAI = true;
            } else {
                waitAI = running;
                if (!running) for (auto& m : msgs) if (m.role != App::ChatMsg::R_USER) printf("  [%d] %s\n", (int)m.role, m.text.substr(0, 160).c_str());
            }
            if (g_frame > 60 * 240) waitAI = false;  // give up after ~4 minutes
        }
        if (g_frame >= g_shot.frames && !waitAI) {
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
