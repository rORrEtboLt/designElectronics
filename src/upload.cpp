// designElectronics — upload a board's sketch to real hardware with arduino-cli
// (the command-line tool behind Arduino IDE 2, which also ships inside the IDE).
// Desktop only: compiling needs the real toolchains for each board family.
#include <algorithm>
#include <memory>
#include <mutex>

#include "app.h"

namespace {

struct BoardTarget {
    const char* type;
    const char* fqbn;
    const char* indexUrl;  // extra Boards Manager URL ("" = built into arduino-cli)
};

const BoardTarget kTargets[] = {
    {"arduino_uno", "arduino:avr:uno", ""},
    {"arduino_nano", "arduino:avr:nano", ""},
    {"arduino_mega2560", "arduino:avr:mega", ""},
    {"arduino_leonardo", "arduino:avr:leonardo", ""},
    {"arduino_micro", "arduino:avr:micro", ""},
    {"arduino_pro_mini", "arduino:avr:pro", ""},
    {"arduino_pro_micro", "arduino:avr:leonardo", ""},
    {"arduino_nano_every", "arduino:megaavr:nona4809", ""},
    {"arduino_uno_r4_minima", "arduino:renesas_uno:minima", ""},
    {"arduino_uno_r4_wifi", "arduino:renesas_uno:unor4wifi", ""},
    {"arduino_due", "arduino:sam:arduino_due_x", ""},
    {"nano_33_iot", "arduino:samd:nano_33_iot", ""},
    {"mkr_wifi_1010", "arduino:samd:mkrwifi1010", ""},
    {"nano_33_ble", "arduino:mbed_nano:nano33ble", ""},
    {"nano_rp2040", "arduino:mbed_nano:nanorp2040connect", ""},
    {"nano_esp32", "arduino:esp32:nano_nora", ""},
    {"esp32_devkit", "esp32:esp32:esp32", "https://espressif.github.io/arduino-esp32/package_esp32_index.json"},
    {"esp32_devkit30", "esp32:esp32:esp32", "https://espressif.github.io/arduino-esp32/package_esp32_index.json"},
    {"esp32s3_devkit", "esp32:esp32:esp32s3", "https://espressif.github.io/arduino-esp32/package_esp32_index.json"},
    {"esp32c3_supermini", "esp32:esp32:esp32c3", "https://espressif.github.io/arduino-esp32/package_esp32_index.json"},
    {"esp32_cam", "esp32:esp32:esp32cam", "https://espressif.github.io/arduino-esp32/package_esp32_index.json"},
    {"nodemcu_esp8266", "esp8266:esp8266:nodemcuv2", "https://arduino.esp8266.com/stable/package_esp8266com_index.json"},
    {"wemos_d1_mini", "esp8266:esp8266:d1_mini", "https://arduino.esp8266.com/stable/package_esp8266com_index.json"},
    {"rpi_pico", "rp2040:rp2040:rpipico", "https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json"},
    {"rpi_pico2", "rp2040:rp2040:rpipico2", "https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json"},
    {"stm32_bluepill", "STMicroelectronics:stm32:GenF1:pnum=BLUEPILL_F103C8", "https://github.com/stm32duino/BoardManagerFiles/raw/main/package_stmicroelectronics_index.json"},
    {"stm32_blackpill", "STMicroelectronics:stm32:GenF4:pnum=BLACKPILL_F411CE", "https://github.com/stm32duino/BoardManagerFiles/raw/main/package_stmicroelectronics_index.json"},
    {"teensy40", "teensy:avr:teensy40", "https://www.pjrc.com/teensy/package_teensy_index.json"},
    {"xiao", "Seeeduino:samd:seeed_XIAO_m0", "https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json"},
    {"feather", "adafruit:samd:adafruit_feather_m0", "https://adafruit.github.io/arduino-board-index/package_adafruit_index.json"},
};

[[maybe_unused]] const BoardTarget* targetFor(const std::string& type) {
    for (auto& t : kTargets) if (type == t.type) return &t;
    return nullptr;
}

std::string coreOf(const std::string& fqbn) {
    size_t a = fqbn.find(':');
    size_t b = a == std::string::npos ? a : fqbn.find(':', a + 1);
    return b == std::string::npos ? fqbn : fqbn.substr(0, b);
}

std::string homeDir() {
    const char* h = SDL_getenv("HOME");
    if (!h) h = SDL_getenv("USERPROFILE");
    return h ? h : "";
}

bool fileExists(const std::string& p) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(p.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
}

// Where arduino-cli might be: PATH (via the shell-less lookup below), Homebrew, and Arduino IDE 2's bundled copy.
[[maybe_unused]] std::string findArduinoCli() {
#if defined(_WIN32)
    const char* exe = "arduino-cli.exe";
    std::vector<std::string> dirs;
    if (const char* lad = SDL_getenv("LOCALAPPDATA")) dirs.push_back(std::string(lad) + "\\Programs\\Arduino IDE\\resources\\app\\lib\\backend\\resources");
    if (const char* pf = SDL_getenv("ProgramFiles")) dirs.push_back(std::string(pf) + "\\Arduino IDE\\resources\\app\\lib\\backend\\resources");
    if (const char* pf = SDL_getenv("ProgramFiles")) dirs.push_back(std::string(pf) + "\\Arduino CLI");
    char sep = ';';
#else
    const char* exe = "arduino-cli";
    std::string home = homeDir();
    std::vector<std::string> dirs = {"/opt/homebrew/bin", "/usr/local/bin", "/usr/bin", home + "/bin", home + "/.local/bin",
                                     "/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources",
                                     home + "/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources",
                                     "/opt/arduino-ide/resources/app/lib/backend/resources", "/snap/bin"};
    char sep = ':';
#endif
    if (const char* path = SDL_getenv("PATH")) {
        std::string ps = path;
        size_t a = 0;
        while (a <= ps.size()) {
            size_t e = ps.find(sep, a);
            if (e == std::string::npos) e = ps.size();
            if (e > a) dirs.insert(dirs.begin(), ps.substr(a, e - a));
            a = e + 1;
        }
    }
    for (auto& d : dirs) {
        std::string p = d + (d.back() == '/' || d.back() == '\\' ? "" : "/") + exe;
        if (fileExists(p)) return p;
    }
    return "";
}

}  // namespace

// ---------------------------------------------------------------------------
// Background command runner (SDL processes; output streamed into the log)
// ---------------------------------------------------------------------------
struct App::UploadJob {
    std::mutex m;
    std::string log;
    bool running = false;
    int exitCode = 0;
    std::string what;  // "list", "upload", "install"
    std::string output;  // full output of the last command (for parsing)
};

void App::runCli(const std::vector<std::string>& args, const std::string& what, std::function<void(int, const std::string&)> done) {
#if defined(__EMSCRIPTEN__) || defined(SDL_PLATFORM_IOS) || defined(SDL_PLATFORM_ANDROID)
    (void)args; (void)what;
    done(-1, "Uploading needs the desktop app.");
#else
    if (!uploadJob) uploadJob = std::make_shared<UploadJob>();
    auto job = uploadJob;
    {
        std::lock_guard<std::mutex> lk(job->m);
        if (job->running) return;
        job->running = true;
        job->what = what;
        std::string cmd;
        for (auto& a : args) cmd += (cmd.empty() ? "" : " ") + (a.find(' ') != std::string::npos ? "\"" + a + "\"" : a);
        job->log += "$ " + cmd + "\n";
    }
    struct Ctx { std::vector<std::string> args; std::shared_ptr<UploadJob> job; std::function<void(int, const std::string&)> done; App* app; };
    auto* ctx = new Ctx{args, job, std::move(done), this};
    SDL_Thread* t = SDL_CreateThread([](void* ud) -> int {
        auto* c = (Ctx*)ud;
        std::vector<const char*> argv;
        for (auto& a : c->args) argv.push_back(a.c_str());
        argv.push_back(nullptr);
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void*)argv.data());
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
        SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
        SDL_Process* proc = SDL_CreateProcessWithProperties(props);
        SDL_DestroyProperties(props);
        int code = -1;
        std::string all;
        if (!proc) {
            all = std::string("Could not start ") + c->args[0] + ": " + SDL_GetError() + "\n";
        } else {
            SDL_IOStream* out = SDL_GetProcessOutput(proc);
            char buf[1024];
            for (;;) {
                size_t n = out ? SDL_ReadIO(out, buf, sizeof buf) : 0;
                if (n == 0) {
                    if (out && SDL_GetIOStatus(out) == SDL_IO_STATUS_NOT_READY) { SDL_Delay(20); continue; }
                    break;
                }
                std::string chunk(buf, n);
                all += chunk;
                std::lock_guard<std::mutex> lk(c->job->m);
                c->job->log += chunk;
                if (c->job->log.size() > 60000) c->job->log.erase(0, c->job->log.size() - 50000);
            }
            SDL_WaitProcess(proc, true, &code);
            SDL_DestroyProcess(proc);
        }
        {
            std::lock_guard<std::mutex> lk(c->job->m);
            if (!proc) c->job->log += all;
            c->job->running = false;
            c->job->exitCode = code;
            c->job->output = all;
        }
        auto done = c->done;
        c->app->runOnMain([done, code, all]() { done(code, all); });
        delete c;
        return 0;
    }, "arduino-cli", ctx);
    if (t) SDL_DetachThread(t);
    else {
        std::lock_guard<std::mutex> lk(job->m);
        job->running = false;
        delete ctx;
    }
#endif
}

void App::openUploadSheet(int partId) {
    PartInst* p = doc.part(partId);
    if (!p) return;
#if defined(__EMSCRIPTEN__) || defined(SDL_PLATFORM_IOS) || defined(SDL_PLATFORM_ANDROID)
    ui.dialog("Upload to a board", {"Uploading to a real board needs the desktop app (macOS, Windows or Linux) with Arduino IDE 2 or arduino-cli installed.",
                                    "", "You can export the sketch (.ino) and open it in the Arduino IDE."}, {});
    return;
#else
    uploadPart = partId;
    arduinoCli = findArduinoCli();
    const BoardTarget* t = targetFor(p->type);
    uploadFqbn = t ? t->fqbn : "";
    uploadPorts.clear();
    uploadStatus.clear();
    if (!uploadJob) uploadJob = std::make_shared<UploadJob>();
    if (!arduinoCli.empty()) refreshPorts();
    ui.openSheet("upload", "Upload to a board", 680, 560, [this](Rect body) {
        float s = ui.s;
        auto& th = ui.th;
        PartInst* p = doc.part(uploadPart);
        if (!p) { ui.closeSheet(); return; }
        float y = body.y;
        if (arduinoCli.empty()) {
            float h = ui.wrappedText(Rect(body.x, y, body.w, 0),
                                     "To upload, designElectronics uses arduino-cli, the tool behind the Arduino IDE. Install either one:\n\n"
                                     "• Arduino IDE 2 (it includes arduino-cli), or\n"
                                     "• arduino-cli on its own: macOS  brew install arduino-cli · Windows  winget install ArduinoSA.CLI · Linux  see the install page\n\n"
                                     "Then open this window again. You can also export the sketch and open it in the Arduino IDE.",
                                     th.text, 14 * s);
            y += h + 16 * s;
            if (ui.button("up-ide", Rect(body.x, y, 200 * s, 36 * s), "Get the Arduino IDE", BTN_PRIMARY)) SDL_OpenURL("https://www.arduino.cc/en/software");
            if (ui.button("up-cli", Rect(body.x + 210 * s, y, 200 * s, 36 * s), "arduino-cli install page", BTN_NORMAL))
                SDL_OpenURL("https://arduino.github.io/arduino-cli/latest/installation/");
            if (ui.button("up-again", Rect(body.x + 420 * s, y, 120 * s, 36 * s), "Check again", BTN_NORMAL)) {
                arduinoCli = findArduinoCli();
                if (!arduinoCli.empty()) refreshPorts();
            }
            if (ui.button("up-export", Rect(body.r() - 150 * s, body.b() - 38 * s, 150 * s, 38 * s), "Export .ino", BTN_NORMAL)) exportSketch(uploadPart);
            return;
        }
        bool busy;
        std::string log;
        {
            std::lock_guard<std::mutex> lk(uploadJob->m);
            busy = uploadJob->running;
            log = uploadJob->log;
        }
        const float lw = 90 * s, rowH = 34 * s;
        g.text({body.x, y + rowH / 2}, "Board", 13 * s, th.textDim, 0);
        ui.textField("up-fqbn", Rect(body.x + lw, y, body.w - lw, rowH), uploadFqbn, "e.g. arduino:avr:uno", true);
        y += rowH + 8 * s;
        g.text({body.x, y + rowH / 2}, "Port", 13 * s, th.textDim, 0);
        std::vector<std::string> opts = uploadPorts;
        if (opts.empty()) opts.push_back("");
        if (uploadPort.empty() && !uploadPorts.empty()) uploadPort = uploadPorts[0];
        std::string shown = uploadPort;
        if (ui.comboField("up-port", Rect(body.x + lw, y, body.w - lw - 100 * s, rowH), shown, opts, true)) uploadPort = shown;
        if (ui.button("up-refresh", Rect(body.r() - 92 * s, y, 92 * s, rowH), "Refresh", BTN_NORMAL, false, !busy)) refreshPorts();
        y += rowH + 4 * s;
        g.text({body.x + lw, y + 9 * s}, uploadPorts.empty() ? "No boards found — plug one in over USB and press Refresh." : uploadStatus, 12 * s,
               uploadPorts.empty() ? th.danger : th.textDim, 0);
        y += 24 * s;
        // log
        Rect logR(body.x, y, body.w, body.b() - y - 50 * s);
        g.fillRoundRect(logR, 6 * s, rgba(22, 27, 34));
        std::vector<std::string> lines;
        size_t a = 0;
        while (a < log.size()) {
            size_t e = log.find('\n', a);
            if (e == std::string::npos) e = log.size();
            lines.push_back(log.substr(a, e - a));
            a = e + 1;
        }
        float lh = 16 * s;
        size_t fit = (size_t)std::max(1.f, (logR.h - 12 * s) / lh);
        size_t first = lines.size() > fit ? lines.size() - fit : 0;
        for (size_t i = first; i < lines.size(); i++) {
            std::string l = lines[i];
            while (l.size() > 4 && g.textWidth(l, 11.5f * s, FONT_MONO) > logR.w - 20 * s) l.resize(l.size() - 4);
            g.text({logR.x + 10 * s, logR.y + 6 * s + (float)(i - first) * lh + lh / 2}, l, 11.5f * s,
                   l.rfind("$ ", 0) == 0 ? rgba(130, 170, 255) : rgba(200, 210, 225), 0, FONT_MONO);
        }
        if (lines.empty()) g.text({logR.x + 10 * s, logR.y + 18 * s}, "Output from the compiler and uploader appears here.", 12 * s, rgba(120, 135, 155), 0);
        // actions
        float by = body.b() - 38 * s, x = body.r();
        auto btn = [&](const std::string& id, const std::string& label, bool primary, bool enabled, std::function<void()> fn) {
            float w = g.textWidth(label, 14 * s) + 30 * s;
            x -= w;
            if (ui.button(id, Rect(x, by, w, 38 * s), label, primary ? BTN_PRIMARY : BTN_NORMAL, false, enabled)) fn();
            x -= 8 * s;
        };
        btn("up-go", busy ? "Working…" : "Compile & Upload", true, !busy && !uploadFqbn.empty() && !uploadPort.empty(), [this]() { startUpload(); });
        btn("up-install", "Install board support", false, !busy && !uploadFqbn.empty(), [this]() { installCore(); });
        btn("up-verify", "Compile only", false, !busy && !uploadFqbn.empty(), [this]() { startUpload(true); });
        g.text({body.x, by + 19 * s}, uploadResult, 13 * s, uploadResultOk ? th.ok : th.danger, 0);
    });
#endif
}

void App::refreshPorts() {
    runCli({arduinoCli, "board", "list", "--format", "json"}, "list", [this](int code, const std::string& out) {
        uploadPorts.clear();
        uploadStatus.clear();
        if (code != 0) { uploadStatus = "Couldn't list ports (see the log)."; return; }
        Json j = Json::parse(out);
        const Json& arr = j.isArray() ? j : j["detected_ports"];
        std::string detected;
        for (auto& e : arr.items()) {
            const Json& port = e["port"];
            std::string addr = port["address"].str();
            if (addr.empty()) continue;
            std::string proto = port["protocol"].str();
            if (proto != "serial" && !proto.empty()) continue;
            uploadPorts.push_back(addr);
            const Json& boards = e["matching_boards"];
            if (boards.size() && detected.empty()) {
                detected = boards[(size_t)0]["name"].str() + " on " + addr;
                if (uploadPort.empty()) uploadPort = addr;
            }
        }
        uploadStatus = detected.empty() ? std::to_string(uploadPorts.size()) + " serial port(s) found" : "Detected " + detected;
        if (!uploadPorts.empty() && std::find(uploadPorts.begin(), uploadPorts.end(), uploadPort) == uploadPorts.end()) uploadPort = uploadPorts[0];
    });
}

void App::installCore() {
    std::string core = coreOf(uploadFqbn);
    std::string url;
    for (auto& t : kTargets) if (coreOf(t.fqbn) == core && *t.indexUrl) url = t.indexUrl;
    std::vector<std::string> upd = {arduinoCli, "core", "update-index"};
    std::vector<std::string> inst = {arduinoCli, "core", "install", core};
    if (!url.empty()) { upd.push_back("--additional-urls"); upd.push_back(url); inst.push_back("--additional-urls"); inst.push_back(url); }
    uploadResult = "Installing " + core + " (this can take a few minutes)…";
    uploadResultOk = true;
    runCli(upd, "install", [this, inst](int code, const std::string&) {
        if (code != 0) { uploadResult = "Couldn't update the board index (see the log)."; uploadResultOk = false; return; }
        runCli(inst, "install", [this](int code2, const std::string&) {
            uploadResultOk = code2 == 0;
            uploadResult = code2 == 0 ? "Board support installed." : "Install failed (see the log).";
        });
    });
}

void App::startUpload(bool compileOnly) {
    PartInst* p = doc.part(uploadPart);
    if (!p) return;
    // write the sketch into its own folder (arduino-cli needs folder/folder.ino)
    char* pref = SDL_GetPrefPath("designElectronics", "designElectronics");
    std::string base = std::string(pref ? pref : "") + "sketches/";
    SDL_free(pref);
    std::string name;
    for (char c : doc.title + "_" + p->label) name += std::isalnum((unsigned char)c) ? c : '_';
    std::string dir = base + name;
    SDL_CreateDirectory(dir.c_str());
    writeTextFile(dir + "/" + name + ".ino", p->code);
    std::vector<std::string> args = {arduinoCli, "compile", "--fqbn", uploadFqbn};
    std::string url;
    for (auto& t : kTargets) if (coreOf(t.fqbn) == coreOf(uploadFqbn) && *t.indexUrl) url = t.indexUrl;
    if (!url.empty()) { args.push_back("--additional-urls"); args.push_back(url); }
    if (!compileOnly) { args.push_back("--upload"); args.push_back("-p"); args.push_back(uploadPort); }
    args.push_back(dir);
    uploadResult = compileOnly ? "Compiling…" : "Compiling and uploading…";
    uploadResultOk = true;
    runCli(args, "upload", [this, compileOnly](int code, const std::string& out) {
        uploadResultOk = code == 0;
        if (code == 0) uploadResult = compileOnly ? "Compiled successfully." : "Uploaded! The board is running your sketch.";
        else if (out.find("platform not installed") != std::string::npos || out.find("Platform '") != std::string::npos ||
                 (out.find("not found") != std::string::npos && out.find("Platform") != std::string::npos))
            uploadResult = "Board support isn't installed: click Install board support, then try again.";
        else uploadResult = compileOnly ? "Compile failed (see the log)." : "Upload failed (see the log).";
    });
}
