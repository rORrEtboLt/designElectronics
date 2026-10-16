// designElectronics — the Program panel: edit a board's Arduino sketch, run it
// in the simulator (executing line highlighted), read its Serial output, and
// upload it to a real board.
#include <cctype>
#include <cstring>
#include <algorithm>
#include <cmath>

#include "app.h"

namespace {

// Arduino pin expression for a header pin name: GPIO2 → 2, D13 → 13, GP15 → 15, C13 → PC13.
std::string arduinoPinFor(const std::string& type, const std::string& name) {
    std::string n = name.substr(0, name.find('/'));
    auto digitsAfter = [&](size_t k) { return k < n.size() && std::all_of(n.begin() + (long)k, n.end(), ::isdigit) ? n.substr(k) : std::string(); };
    if (type == "nodemcu_esp8266" || type == "wemos_d1_mini") return n;  // D0…D8 are constants there
    if (type.rfind("stm32", 0) == 0 && n.size() >= 2 && std::isalpha((unsigned char)n[0]) && !digitsAfter(1).empty()) return "P" + n;
    for (const char* pre : {"GPIO", "GP", "IO", "D"})
        if (n.rfind(pre, 0) == 0 && !digitsAfter(std::strlen(pre)).empty()) return digitsAfter(std::strlen(pre));
    if (!n.empty() && std::all_of(n.begin(), n.end(), ::isdigit)) return n;
    if (n.size() >= 2 && n[0] == 'A' && std::isdigit((unsigned char)n[1])) return n;
    return "LED_BUILTIN";
}

}  // namespace

std::string App::defaultSketch(const PartInst& p) {
    // start from the board's current Pin outputs ("GPIO2=BLINK:500") when there are any
    std::string outputs = p.prop("outputs"), pin = "LED_BUILTIN";
    int period = 500;
    size_t eq = outputs.find('=');
    if (eq != std::string::npos) {
        std::string name = outputs.substr(0, eq);
        name.erase(0, name.find_first_not_of(' '));
        name.erase(name.find_last_not_of(' ') + 1);
        pin = arduinoPinFor(p.type, name);
        size_t b = outputs.find("BLINK:", eq);
        if (b != std::string::npos) period = std::max(10, std::atoi(outputs.c_str() + b + 6));
    }
    bool esp = p.type.find("esp") != std::string::npos || p.type == "nano_esp32";
    std::string baud = esp ? "115200" : "9600";
    char buf[1200];
    snprintf(buf, sizeof buf,
             "// %s: this sketch runs in the simulator.\n"
             "// Press Run to start it; Upload sends it to a real board.\n"
             "\n"
             "const int LED_PIN = %s;\n"
             "\n"
             "void setup() {\n"
             "  pinMode(LED_PIN, OUTPUT);\n"
             "  Serial.begin(%s);\n"
             "  Serial.println(\"Hello from designElectronics!\");\n"
             "}\n"
             "\n"
             "void loop() {\n"
             "  digitalWrite(LED_PIN, HIGH);\n"
             "  delay(%d);\n"
             "  digitalWrite(LED_PIN, LOW);\n"
             "  delay(%d);\n"
             "}\n",
             p.def->title.c_str(), pin.c_str(), baud.c_str(), period, period);
    return buf;
}

void App::openCode(int partId) {
    PartInst* p = doc.part(partId);
    if (!p || (p->def->sim != "board" && p->def->sim != "mcu")) { ui.toast("Select a microcontroller board to program it."); return; }
    if (p->code.empty()) {
        checkpoint();
        p->code = defaultSketch(*p);
        changed();
    }
    if (codePart != partId) codeState = CodeEditorState();
    codePart = partId;
    codeOpen = true;
    codeCheckRev = -1;
    drawerParts = drawerInspector = drawerLayers = false;
}

void App::drawCodePanel(Rect r) {
    float s = ui.s;
    auto& th = ui.th;
    PartInst* p = doc.part(codePart);
    if (!p) { codeOpen = false; return; }
    g.fillRect(r, th.panel);
    g.fillRect({r.x, r.y, r.w, std::max(1.f, s)}, th.border);
    float pad = 8 * s;
    // ---- header ----
    Rect hdr(r.x + pad, r.y + 6 * s, r.w - 2 * pad, 32 * s);
    std::string title = "Program · " + p->label + " (" + p->def->title + ")";
    float rx = hdr.r();
    auto hbtn = [&](const std::string& id, const std::string& label, ButtonStyle st, const std::string& tip, bool enabled = true) {
        float w = g.textWidth(label, 13.5f * s) + 26 * s;
        rx -= w;
        bool c = ui.button(id, Rect(rx, hdr.y, w, hdr.h), label, st, false, enabled);
        ui.tooltip(Rect(rx, hdr.y, w, hdr.h), tip);
        rx -= 6 * s;
        return c;
    };
    if (ui.iconButton("code-close", Rect(rx - hdr.h, hdr.y, hdr.h, hdr.h), "close", "", false, true, "Close the program panel")) {
        codeOpen = false;
        return;
    }
    rx -= hdr.h + 8 * s;
    bool narrow = r.w / s < 700;
    if (hbtn("code-export", narrow ? ".ino" : "Export .ino", BTN_NORMAL, "Save the sketch as an Arduino .ino file")) exportSketch(codePart);
    if (hbtn("code-upload", narrow ? "Upload" : "Upload to board…", BTN_NORMAL, "Compile and upload to a real board over USB (needs arduino-cli)"))
        openUploadSheet(codePart);
    if (hbtn("code-run", simOn ? "■ Stop" : "▶ Run", simOn ? BTN_NORMAL : BTN_PRIMARY, simOn ? "Stop the simulation" : "Run the program in the simulator"))
        toggleSim();
    float titleMax = rx - hdr.x - 8 * s;
    while (title.size() > 4 && g.textWidth(title, 15 * s) > titleMax) { title.resize(title.size() - 4); title += "…"; }
    g.text({hdr.x + 2 * s, hdr.center().y}, title, 15 * s, th.text, 0);

    // ---- status: live syntax check while editing, run status while simulating ----
    int execLine = 0, errLine = 0;
    std::string errMsg;
    auto it = sim.code.find(codePart);
    if (simOn && it != sim.code.end()) {
        if (it->second.running) execLine = it->second.line;
        errLine = it->second.errorLine;
        errMsg = it->second.error;
    } else {
        if (codeCheckRev != doc.revision && SDL_GetTicks() - lastCodeEdit > 350) {
            codeCheckRev = doc.revision;
            codeCheckLine = 0;
            codeCheckMsg.clear();
            checkSketch(*p, codeCheckLine, codeCheckMsg);
        }
        errLine = codeCheckLine;
        errMsg = codeCheckMsg;
    }

    // ---- body: editor + serial monitor ----
    Rect body(r.x + pad, hdr.b() + 6 * s, r.w - 2 * pad, r.b() - hdr.b() - 6 * s - pad);
    Rect edR, serR;
    if (narrow) {
        float eh = std::floor(body.h * 0.62f);
        edR = Rect(body.x, body.y, body.w, eh);
        serR = Rect(body.x, body.y + eh + 6 * s, body.w, body.h - eh - 6 * s);
    } else {
        float ew = std::floor(body.w * 0.64f);
        edR = Rect(body.x, body.y, ew, body.h);
        serR = Rect(body.x + ew + 8 * s, body.y, body.w - ew - 8 * s, body.h);
    }
    std::string text = p->code;
    bool wasEditing = codeEditing;
    if (ui.codeEditor("code-" + std::to_string(codePart), edR, text, codeState, execLine, errLine, errMsg)) {
        if (!codeEditing) { beginEdit(); codeEditing = true; }
        if (PartInst* pp = doc.part(codePart)) pp->code = text;
        lastCodeEdit = SDL_GetTicks();
        changed();
    }
    if (wasEditing && !ui.editingText()) { endEdit(); codeEditing = false; }

    // serial monitor
    g.fillRoundRect(serR, 6 * s, rgba(22, 27, 34));
    Rect sh(serR.x + 10 * s, serR.y + 6 * s, serR.w - 20 * s, 24 * s);
    g.text({sh.x, sh.center().y}, "Serial Monitor", 13 * s, rgba(200, 210, 225), 0);
    Rect clr(sh.r() - 56 * s, sh.y, 56 * s, sh.h);
    bool clrHot = ui.hovered(clr);
    if (ui.button("ser-clear", clr, "", BTN_FLAT)) serialLog[codePart].clear();
    g.fillRoundRect(clr, 5 * s, clrHot ? rgba(60, 70, 85) : rgba(40, 48, 60));
    g.text(clr.center(), "Clear", 12.5f * s, rgba(210, 220, 235), 1);
    Rect logR(serR.x + 10 * s, sh.b() + 4 * s, serR.w - 20 * s, serR.h - (sh.b() - serR.y) - 52 * s);
    const std::string& log = serialLog[codePart];
    std::vector<std::string> lines;
    {
        size_t a = 0;
        while (a <= log.size()) {
            size_t e = log.find('\n', a);
            if (e == std::string::npos) e = log.size();
            lines.push_back(log.substr(a, e - a));
            a = e + 1;
        }
        if (!lines.empty() && lines.back().empty()) lines.pop_back();
    }
    float lh = 17 * s;
    size_t fit = (size_t)std::max(1.f, std::floor(logR.h / lh));
    size_t firstLine = lines.size() > fit ? lines.size() - fit : 0;  // newest output at the bottom
    g.setClip(&logR);
    for (size_t i = firstLine; i < lines.size(); i++)
        g.text({logR.x, logR.y + (float)(i - firstLine) * lh + lh / 2}, lines[i], 12.5f * s, rgba(170, 240, 190), 0, FONT_MONO);
    g.setClip(nullptr);
    if (log.empty())
        ui.wrappedText(logR, simOn ? "Waiting for Serial output…" : "Serial.print() output appears here when the program runs.", rgba(120, 135, 155), 12.5f * s);
    Rect inR(serR.x + 10 * s, serR.b() - 40 * s, serR.w - 90 * s, 30 * s);
    bool send = ui.textField("ser-in", inR, serialSend, "Send to the board…", true);
    (void)send;
    bool enter = in.key(SDLK_RETURN) || in.key(SDLK_KP_ENTER);
    if ((ui.button("ser-send", Rect(inR.r() + 6 * s, inR.y, 64 * s, inR.h), "Send", BTN_NORMAL, false, !serialSend.empty()) || (enter && !serialSend.empty())) && simOn) {
        simulator.serialInput(codePart, serialSend + "\n");
        serialSend.clear();
    }
}

void App::exportSketch(int partId) {
    PartInst* p = doc.part(partId);
    if (!p || p->code.empty()) return;
    std::string name;
    for (char c : doc.title + "_" + p->label) name += std::isalnum((unsigned char)c) ? c : '_';
    std::string code = p->code;
    static const SDL_DialogFileFilter f[] = {{"Arduino sketch", "ino"}};
    showSaveDialog(name + ".ino", f, 1, [this, code](const std::string& path0) {
        std::string path = path0;
        if (path.rfind("content://", 0) != 0 && (path.size() < 4 || path.substr(path.size() - 4) != ".ino")) path += ".ino";
        if (writeTextFile(path, code)) ui.toast("Exported " + path);
        else ui.dialog("Export failed", {path}, {});
    });
}
