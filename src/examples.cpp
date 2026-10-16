// Built-in example projects (also handy as a smoke test of the whole model).
#include "tools.h"

namespace {

PartInst& place(Document& d, const std::string& type, const Props& props, Vec2 bb, Vec2 sch, Vec2 pcb, bool flipBB = false, int rotBB = 0) {
    PartInst& p = d.addPart(type, props, {0, 0});
    p.place[BB].pos = bb;
    p.place[BB].flip = flipBB;
    p.place[BB].rot = rotBB;
    p.place[SCH].pos = sch;
    p.place[PCB].pos = pcb;
    return p;
}

WireEnd pinAt(const Document& d, int view, Vec2 pos) {
    // Prefer real parts over breadboard holes so wires stay attached to the part.
    WireEnd hole;
    for (auto& p : d.parts) {
        if (!p.inView(view)) continue;
        for (size_t i = 0; i < p.def->pins.size(); i++) {
            if (dist(p.pinPos((int)i, view), pos) < 1) {
                if (!p.def->isBreadboard) return {p.id, (int)i};
                if (!hole.attached()) hole = {p.id, (int)i};
            }
        }
    }
    return hole;
}

void wire(Document& d, int view, std::vector<Vec2> pts, uint32_t color = rgba(65, 140, 225)) {
    Wire w;
    w.id = d.newId();
    w.view = view;
    w.pts = std::move(pts);
    w.color = color;
    w.ends[0] = pinAt(d, view, w.pts.front());
    w.ends[1] = pinAt(d, view, w.pts.back());
    d.wires.push_back(w);
}

const uint32_t RED = rgba(220, 50, 50), BLACK = rgba(40, 40, 40), GREEN = rgba(40, 170, 80), YELLOW = rgba(240, 200, 40);

// half+ breadboard hole helpers (breadboard placed at `o`)
Vec2 hole(Vec2 o, int col, char row) {
    static const char* top = "jihgf";
    static const char* bot = "edcba";
    for (int r = 0; r < 5; r++) {
        if (top[r] == row) return o + Vec2((col - 1) * 100.f, 300 + r * 100.f);
        if (bot[r] == row) return o + Vec2((col - 1) * 100.f, 1000 + r * 100.f);
    }
    return o;
}
// rails: '+' top = y 0, '-' top = y 100, bottom '+' 1600, bottom '-' 1700 (cols with (col-1)%6 != 0)
Vec2 rail(Vec2 o, int col, bool top, bool plus) {
    float y = top ? (plus ? 0.f : 100.f) : (plus ? 1600.f : 1700.f);
    return o + Vec2((col - 1) * 100.f, y);
}

void addBoard(Document& d, Vec2 at, const char* w, const char* h) {
    PartInst& b = d.addPart("pcb_board", {{"width", w}, {"height", h}}, {0, 0});
    b.place[PCB].pos = at;
}

void ledBattery(Document& d) {
    d.title = "LED with battery";
    d.notesText = "A 9V battery lights an LED through a 470Ω current-limiting resistor.";
    Vec2 o{0, 0};
    place(d, "breadboard", {{"size", "half+"}}, o, {0, 0}, {0, 0});
    // resistor: col 6 → col 10 on row e;  LED: anode col 10, cathode col 11 on row c
    place(d, "resistor", {{"resistance", "470Ω"}}, hole(o, 6, 'e'), {300, 0}, {500, 400});
    place(d, "led", {{"color", "Red"}}, hole(o, 11, 'c'), {900, 0}, {1200, 400}, true);
    place(d, "battery", {{"voltage", "9V"}, {"style", "9V block"}}, {3500, 1500}, {0, 200}, {300, 900});
    addBoard(d, {0, 0}, "45", "35");
    wire(d, BB, {hole(o, 6, 'a'), rail(o, 6, false, true)}, RED);
    wire(d, BB, {hole(o, 11, 'a'), rail(o, 11, false, false)}, BLACK);
    wire(d, BB, {{3600, 1500}, {3600, 1600}, rail(o, 27, false, true)}, RED);
    wire(d, BB, {{3500, 1500}, {3500, 1700}, rail(o, 26, false, false)}, BLACK);
    wire(d, SCH, {{0, 200}, {0, 0}, {300, 0}});
    wire(d, SCH, {{700, 0}, {900, 0}});
    wire(d, SCH, {{1200, 0}, {1300, 0}, {1300, 600}, {0, 600}});
    d.notes.push_back({d.newId(), BB, {0, -1100}, {2200, 600}, "Press Simulate to light the LED. Try a 47Ω resistor to see what happens!"});
}

void arduinoBlink(Document& d) {
    d.title = "Arduino LED on pin 13";
    d.notesText = "Classic Arduino blink wiring: D13 → 220Ω → LED → GND.";
    Vec2 o{3000, 0};
    place(d, "arduino_uno", {{"outputs", "D13=HIGH"}}, {0, 0}, {0, 0}, {0, 0});
    place(d, "breadboard", {{"size", "half+"}}, o, {0, 0}, {0, 0});
    place(d, "resistor", {{"resistance", "220Ω"}}, hole(o, 10, 'f'), {1500, 1400}, {800, 800});
    place(d, "led", {{"color", "Green"}}, hole(o, 15, 'g'), {2100, 1400}, {1300, 1000}, true);
    PartInst& g1 = d.addPart("ground", {}, {0, 0});
    g1.place[SCH].pos = {-200, 1400};
    PartInst& g2 = d.addPart("ground", {}, {0, 0});
    g2.place[SCH].pos = {2600, 1500};
    addBoard(d, {-100, -100}, "75", "58");
    PartInst* ard = nullptr;
    for (auto& p : d.parts) if (p.type == "arduino_uno") ard = &p;
    Vec2 d13 = ard->pinPos(ard->def->pinIndex("D13"), BB);
    Vec2 gnd = ard->pinPos(ard->def->pinIndex("GND"), BB);
    wire(d, BB, {d13, {d13.x, -250}, {hole(o, 10, 'j').x, -250}, hole(o, 10, 'j')}, GREEN);
    wire(d, BB, {gnd, {gnd.x, -400}, {rail(o, 2, true, false).x, -400}, rail(o, 2, true, false)}, BLACK);
    wire(d, BB, {hole(o, 15, 'j'), rail(o, 15, true, false)}, BLACK);
    wire(d, SCH, {{1200, 1400}, {1500, 1400}});
    wire(d, SCH, {{1900, 1400}, {2100, 1400}});
    wire(d, SCH, {{2400, 1400}, {2600, 1400}, {2600, 1500}});
    wire(d, SCH, {{0, 1400}, {-200, 1400}});
}

void pushbuttonLed(Document& d) {
    d.title = "Pushbutton LED";
    d.notesText = "Hold the button while simulating to light the LED.";
    Vec2 o{0, 0};
    place(d, "breadboard", {{"size", "half+"}}, o, {0, 0}, {0, 0});
    place(d, "battery", {{"voltage", "3V"}, {"style", "AA pack"}}, {3500, 1500}, {0, 300}, {300, 1000});
    // button straddles the gap: left legs col 8 (rows e/f), right legs col 10
    place(d, "pushbutton", {}, hole(o, 8, 'f'), {300, 0}, {700, 300});
    place(d, "resistor", {{"resistance", "100Ω"}}, hole(o, 10, 'b'), {900, 0}, {1200, 300});
    place(d, "led", {{"color", "Yellow"}}, hole(o, 15, 'c'), {1500, 0}, {1300, 800}, true);
    addBoard(d, {0, 0}, "50", "40");
    wire(d, BB, {hole(o, 8, 'a'), rail(o, 8, false, true)}, RED);
    wire(d, BB, {hole(o, 15, 'a'), rail(o, 15, false, false)}, BLACK);
    wire(d, BB, {{3600, 1500}, {3600, 1600}, rail(o, 27, false, true)}, RED);
    wire(d, BB, {{3500, 1500}, {3500, 1700}, rail(o, 26, false, false)}, BLACK);
    d.notes.push_back({d.newId(), BB, {0, -1000}, {2400, 500}, "Simulate, then press and hold the button."});
}

void voltageDivider(Document& d) {
    d.title = "Voltage divider + multimeter";
    d.notesText = "Two 10kΩ resistors halve 9V; the multimeter reads ~4.5V.";
    Vec2 o{0, 0};
    place(d, "breadboard", {{"size", "half+"}}, o, {0, 0}, {0, 0});
    place(d, "battery", {{"voltage", "9V"}}, {3500, 1500}, {0, 200}, {300, 800});
    place(d, "resistor", {{"resistance", "10kΩ"}}, hole(o, 5, 'c'), {300, 0}, {600, 300});
    place(d, "resistor", {{"resistance", "10kΩ"}}, hole(o, 9, 'b'), {900, 0}, {600, 700});
    place(d, "multimeter", {}, {1500, -300}, {1500, 300}, {0, 0});
    addBoard(d, {0, 0}, "40", "30");
    wire(d, BB, {{3600, 1500}, {3600, 1600}, rail(o, 27, false, true)}, RED);
    wire(d, BB, {{3500, 1500}, {3500, 1700}, rail(o, 26, false, false)}, BLACK);
    wire(d, BB, {hole(o, 5, 'a'), rail(o, 5, false, true)}, RED);
    wire(d, BB, {hole(o, 13, 'a'), rail(o, 14, false, false)}, BLACK);
    wire(d, BB, {rail(o, 2, true, false), {-300, 100}, {-300, 1700}, rail(o, 2, false, false)}, BLACK);
    wire(d, BB, {{1600, -300}, {1600, 850}, {800, 850}, hole(o, 9, 'e')}, YELLOW);
    wire(d, BB, {{1500, -300}, rail(o, 16, true, false)}, BLACK);
}

void blinker555(Document& d) {
    d.title = "555 LED blinker";
    d.notesText = "Classic astable 555: R1 = 1kΩ, R2 = 10kΩ, C = 10µF → about 7 blinks per second.";
    Vec2 o{0, 0};
    place(d, "breadboard", {{"size", "half+"}}, o, {0, 0}, {0, 0});
    place(d, "battery", {{"voltage", "9V"}}, {3500, 1500}, {0, 300}, {1500, 1300});
    // 555 straddles the gap: pins 1-4 on row e (cols 10-13), pins 8-5 on row f (cols 10-13)
    place(d, "timer555", {}, hole(o, 10, 'f'), {800, 0}, {700, 300});
    place(d, "resistor", {{"resistance", "1kΩ"}}, hole(o, 7, 'i'), {300, -300}, {400, 700});
    place(d, "resistor", {{"resistance", "10kΩ"}}, hole(o, 12, 'h'), {300, 300}, {1000, 700});
    PartInst& c = place(d, "capacitor_electrolytic", {{"capacitance", "10µF"}}, hole(o, 10, 'b'), {300, 800}, {1300, 400});
    c.place[BB].flip = true;
    c.place[BB].rot = 2;
    place(d, "resistor", {{"resistance", "470Ω"}}, hole(o, 12, 'c'), {1600, 300}, {400, 1000});
    place(d, "led", {{"color", "Red"}}, hole(o, 17, 'd'), {2100, 300}, {1000, 1000}, true);
    addBoard(d, {0, 0}, "50", "40");
    wire(d, BB, {{3600, 1500}, {3600, 1600}, rail(o, 27, false, true)}, RED);
    wire(d, BB, {{3500, 1500}, {3500, 1700}, rail(o, 26, false, false)}, BLACK);
    wire(d, BB, {rail(o, 2, true, true), {-300, 0}, {-300, 1600}, rail(o, 2, false, true)}, RED);
    wire(d, BB, {rail(o, 3, true, false), {-200, 100}, {-200, 1700}, rail(o, 3, false, false)}, BLACK);
    wire(d, BB, {hole(o, 10, 'j'), rail(o, 10, true, true)}, RED);        // VCC
    wire(d, BB, {hole(o, 10, 'a'), rail(o, 10, false, false)}, BLACK);    // GND
    wire(d, BB, {hole(o, 13, 'a'), rail(o, 14, false, true)}, RED);       // RESET → +
    wire(d, BB, {hole(o, 7, 'j'), rail(o, 8, true, true)}, RED);          // R1 → +
    wire(d, BB, {hole(o, 16, 'g'), hole(o, 11, 'g')}, YELLOW);             // R2 → DIS
    wire(d, BB, {hole(o, 12, 'j'), {1150, 250}, {1450, 250}, {1450, 1150}, hole(o, 11, 'd')}, GREEN);  // THR → TRIG
    wire(d, BB, {hole(o, 17, 'a'), rail(o, 17, false, false)}, BLACK);    // LED → GND
    d.notes.push_back({d.newId(), BB, {0, -1000}, {2600, 600}, "Press Simulate: the 555 charges the capacitor and blinks the LED. Try 100µF for a slow blink."});
}

void esp32Blink(Document& d) {
    d.title = "ESP32 blink";
    d.notesText = "ESP32 DevKitC drives GPIO2 (BLINK:500) through a 220Ω resistor to an LED.";
    Vec2 o{0, 0};
    place(d, "breadboard", {{"size", "half+"}}, o, {0, 0}, {0, 0});
    // the DevKitC is wider than a half breadboard, so it sits above it and connects with jumper wires
    place(d, "esp32_devkit", {{"outputs", "GPIO2=BLINK:500"}}, {100, -1700}, {0, 0}, {0, 0});
    place(d, "resistor", {{"resistance", "220Ω"}}, hole(o, 6, 'f'), {1500, 500}, {600, 1400});
    place(d, "led", {{"color", "Red"}}, hole(o, 11, 'g'), {2100, 500}, {1100, 1400}, true);
    addBoard(d, {-300, -300}, "75", "50");
    wire(d, BB, {{500, -700}, hole(o, 6, 'j')}, YELLOW);                    // GPIO2 → resistor
    wire(d, BB, {{1300, -700}, rail(o, 14, true, false)}, BLACK);           // GND → − rail
    wire(d, BB, {hole(o, 11, 'j'), rail(o, 11, true, false)}, BLACK);       // LED cathode → − rail
    d.notes.push_back({d.newId(), BB, {2200, -2400}, {2200, 700},
                       "Select the ESP32 and edit \"Pin outputs\" in the Inspector, e.g. GPIO2=BLINK:100 or GPIO2=PWM:20, then press Simulate."});
}

void piLed(Document& d) {
    d.title = "Raspberry Pi LED";
    d.notesText = "Raspberry Pi 4 GPIO17 (physical pin 11) drives an LED through a 330Ω resistor; pin 9 is ground.";
    Vec2 o{0, 0};
    place(d, "breadboard", {{"size", "half+"}}, o, {0, 0}, {0, 0});
    place(d, "rpi4", {{"outputs", "GPIO17=BLINK:500"}}, {-400, -2700}, {0, 0}, {0, 0});
    place(d, "resistor", {{"resistance", "330Ω"}}, hole(o, 6, 'f'), {1600, 500}, {600, 1400});
    place(d, "led", {{"color", "Red"}}, hole(o, 11, 'g'), {2200, 500}, {1100, 1400}, true);
    addBoard(d, {-100, -100}, "90", "60");  // a HAT-sized board over the Pi footprint
    // header pin 11 = GPIO17 and pin 9 = GND, both on the inner row (columns 6 and 5)
    wire(d, BB, {{-400 + 330 + 500, -2700 + 190}, {430, -200}, {500, -200}, hole(o, 6, 'j')}, YELLOW);
    wire(d, BB, {{-400 + 330 + 400, -2700 + 190}, {330, -150}, {300, -150}, rail(o, 4, true, false)}, BLACK);
    wire(d, BB, {hole(o, 11, 'j'), rail(o, 11, true, false)}, BLACK);
    d.notes.push_back({d.newId(), BB, {3200, -2700}, {2200, 800},
                       "Select the Pi and edit \"Pin outputs\" (e.g. GPIO17=HIGH or GPIO18=PWM:20), then press Simulate. Pi GPIO pins are 3.3 V only."});
}

}  // namespace

const std::vector<Example>& examples() {
    static std::vector<Example> ex = {
        {"LED with battery", ledBattery},
        {"Arduino LED on pin 13", arduinoBlink},
        {"Pushbutton LED", pushbuttonLed},
        {"Voltage divider + multimeter", voltageDivider},
        {"555 LED blinker", blinker555},
        {"ESP32 blink", esp32Blink},
        {"Raspberry Pi LED", piLed},
    };
    return ex;
}
