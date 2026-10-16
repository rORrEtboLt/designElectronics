// designElectronics — the browser build and the embeddable viewer.
//
// The same app compiles to WebAssembly (Emscripten). On the web site it runs as
// a full editor (/app/) and as a read-only viewer for <iframe> embeds (/embed/).
// A design travels inside the link itself: its compact JSON is deflated and
// base64url-encoded into "#d=…", so embedding needs no server or upload.
#include <cstring>

#include "app.h"
#include "logo.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace {

constexpr const char* kWebBase = "https://rorretbolt.github.io/designElectronics/";

// ---------------------------------------------------------------------------
// Minimal raw-DEFLATE encoder (RFC 1951): LZ77 with a hash chain and the fixed
// Huffman code. Plenty for JSON (typically 5–8× smaller); browsers decode it
// with DecompressionStream("deflate-raw").
// ---------------------------------------------------------------------------
class BitWriter {
public:
    std::string out;
    void bits(uint32_t v, int n) {  // LSB first
        acc_ |= v << cnt_;
        cnt_ += n;
        while (cnt_ >= 8) { out += (char)(acc_ & 0xFF); acc_ >>= 8; cnt_ -= 8; }
    }
    void huff(uint32_t code, int len) {  // Huffman codes are sent MSB first
        uint32_t r = 0;
        for (int i = 0; i < len; i++) r |= ((code >> i) & 1u) << (len - 1 - i);
        bits(r, len);
    }
    void flush() { if (cnt_) { out += (char)(acc_ & 0xFF); acc_ = 0; cnt_ = 0; } }

private:
    uint32_t acc_ = 0;
    int cnt_ = 0;
};

void putLiteral(BitWriter& w, int c) {
    if (c < 144) w.huff(0x30 + c, 8);
    else if (c < 256) w.huff(0x190 + (c - 144), 9);
    else if (c < 280) w.huff(c - 256, 7);
    else w.huff(0xC0 + (c - 280), 8);
}

void putMatch(BitWriter& w, int len, int dist) {
    static const int lbase[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const int lext[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const int dbase[] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const int dext[] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    int li = 28;
    while (li > 0 && lbase[li] > len) li--;
    putLiteral(w, 257 + li);
    if (lext[li]) w.bits((uint32_t)(len - lbase[li]), lext[li]);
    int di = 29;
    while (di > 0 && dbase[di] > dist) di--;
    w.huff((uint32_t)di, 5);
    if (dext[di]) w.bits((uint32_t)(dist - dbase[di]), dext[di]);
}

std::string deflateRaw(const std::string& in) {
    BitWriter w;
    w.bits(1, 1);  // final block
    w.bits(1, 2);  // fixed Huffman
    const int kWindow = 32768, kHash = 1 << 15, kMaxChain = 64;
    std::vector<int> head(kHash, -1), prev(in.size(), -1);
    auto hash = [&](size_t i) { return ((uint8_t)in[i] << 10 ^ (uint8_t)in[i + 1] << 5 ^ (uint8_t)in[i + 2]) & (kHash - 1); };
    size_t i = 0, n = in.size();
    auto insert = [&](size_t k) { if (k + 2 < n) { int h = hash(k); prev[k] = head[h]; head[h] = (int)k; } };
    while (i < n) {
        int bestLen = 0, bestDist = 0;
        if (i + 2 < n) {
            int cand = head[hash(i)];
            for (int chain = 0; cand >= 0 && chain < kMaxChain && (int)i - cand <= kWindow; chain++, cand = prev[(size_t)cand]) {
                int l = 0;
                while (l < 258 && i + (size_t)l < n && in[(size_t)cand + (size_t)l] == in[i + (size_t)l]) l++;
                if (l > bestLen) { bestLen = l; bestDist = (int)i - cand; if (l == 258) break; }
            }
        }
        if (bestLen >= 3) {
            putMatch(w, bestLen, bestDist);
            for (int k = 0; k < bestLen; k++) insert(i + (size_t)k);
            i += (size_t)bestLen;
        } else {
            putLiteral(w, (uint8_t)in[i]);
            insert(i);
            i++;
        }
    }
    putLiteral(w, 256);  // end of block
    w.flush();
    return w.out;
}

std::string base64url(const std::string& data) {
    std::string b = github::base64Encode(data);
    for (auto& c : b) c = c == '+' ? '-' : c == '/' ? '_' : c;
    while (!b.empty() && b.back() == '=') b.pop_back();
    return b;
}

App* g_webApp = nullptr;

}  // namespace

// Pack the design into a link fragment: "#d=<deflate+base64url of the compact JSON>".
std::string App::shareFragment() {
    doc.syncWireEnds();
    return "d=" + base64url(deflateRaw(doc.compactJson()));
}

// ---------------------------------------------------------------------------
// Viewer bar (embed mode)
// ---------------------------------------------------------------------------
void App::drawEmbedBar() {
    float s = ui.s;
    auto& th = ui.th;
    Rect r = rTop;
    g.fillRect(r, th.panel2);
    g.fillRect({r.x, r.b() - std::max(1.f, s), r.w, std::max(1.f, s)}, th.border);
    float x = r.x + 8 * s, h = 32 * s, y = r.center().y - h / 2;
    bool narrow = r.w / s < 640;
    // logo (links to the project site)
    Rect logo(x, y, h, h);
    drawLogo(g, logo.inset(2 * s));
    if (ui.button("emb-logo", logo, "", BTN_FLAT)) SDL_OpenURL(kWebBase);
    ui.tooltip(logo, "Made with designElectronics");
    x += h + 8 * s;
    // right side, right to left: open in editor, simulate, fit
    float rx = r.r() - 8 * s;
    std::string openLabel = narrow ? "Open" : "Open in editor";
    float ow = g.textWidth(openLabel, 13.5f * s) + 24 * s;
    rx -= ow;
    if (ui.button("emb-open", Rect(rx, y, ow, h), openLabel, BTN_NORMAL)) SDL_OpenURL(editorUrl.empty() ? kWebBase : editorUrl.c_str());
    ui.tooltip(Rect(rx, y, ow, h), "Open this design in the full editor");
    rx -= 6 * s;
    float sw = narrow ? h : g.textWidth("Simulate", 13.5f * s) + 30 * s;
    rx -= sw;
    Rect simR(rx, y, sw, h);
    if (ui.button("emb-sim", simR, "", simOn ? BTN_PRIMARY : BTN_NORMAL)) toggleSim();
    uint32_t simCol = simOn ? rgba(255, 255, 255) : th.text;
    if (narrow) ui.drawIcon(simOn ? "stop" : "play", simR.inset(7 * s), simCol);
    else g.text(simR.center(), simOn ? "Stop" : "Simulate", 13.5f * s, simCol, 1);
    ui.tooltip(simR, simOn ? "Stop the simulation" : "Run the simulation");
    rx -= 6 * s + h;
    if (ui.iconButton("emb-fit", Rect(rx, y, h, h), "fit", "", false, true, "Fit in window")) fitView(view, rCanvas);
    rx -= 10 * s;
    // view tabs: shrink (and drop the title) until they fit
    const char* full[] = {"Breadboard", "Schematic", "PCB"};
    const char* mid[] = {"Bread", "Schem", "PCB"};
    const char* tiny[] = {"BB", "SCH", "PCB"};
    float avail = rx - x;
    float tabW = 96 * s;
    const char** labels = full;
    bool showTitle = avail >= 3 * tabW + 150 * s;
    if (!showTitle && avail < 3 * tabW) { tabW = 60 * s; labels = mid; }
    if (avail < 3 * tabW) { tabW = std::max(36 * s, avail / 3); labels = tiny; }
    float tx = showTitle ? std::clamp(r.center().x - 1.5f * tabW, x + 150 * s, rx - 3 * tabW) : x;
    if (showTitle) {
        std::string t = doc.title;
        float maxW = tx - x - 12 * s;
        while (t.size() > 3 && g.textWidth(t, 14 * s) > maxW) { t.resize(t.size() - 4); t += "…"; }
        g.text({x, r.center().y}, t, 14 * s, th.text, 0);
    }
    Rect seg(tx, y, 3 * tabW, h);
    g.fillRoundRect(seg, 8 * s, th.border);
    for (int v = 0; v < 3; v++) {
        Rect b(tx + (float)v * tabW + 2 * s, y + 2 * s, tabW - 4 * s, h - 4 * s);
        if (ui.button(std::string("emb-v") + std::to_string(v), b, labels[v], BTN_TAB, view == v)) setView(v);
    }
}

void App::handleViewerKeys() {
    for (auto k : in.keys) {
        switch (k) {
            case SDLK_1: setView(BB); break;
            case SDLK_2: setView(SCH); break;
            case SDLK_3: setView(PCB); break;
            case SDLK_EQUALS: case SDLK_PLUS: case SDLK_KP_PLUS: zoomAt(1.25f, rCanvas.center()); break;
            case SDLK_MINUS: case SDLK_KP_MINUS: zoomAt(0.8f, rCanvas.center()); break;
            case SDLK_0: fitView(view, rCanvas); break;
            case SDLK_SPACE: toggleSim(); break;
            default: break;
        }
    }
}

// ---------------------------------------------------------------------------
// Share ▸ Embed in a website
// ---------------------------------------------------------------------------
void App::openEmbedSheet() {
    static int embedView = -1;
    static bool embedSim = false;
    static std::string embedW = "800", embedH = "500";
    embedView = view;
    ui.openSheet("embed", "Embed in a website", 640, 560, [this](Rect body) {
        float s = ui.s;
        auto& th = ui.th;
        float y = body.y;
        std::string frag = shareFragment();
        const char* vk[] = {"breadboard", "schematic", "pcb"};
        std::string opts = frag + "&view=" + vk[embedView] + (embedSim ? "&sim=1" : "");
        std::string embedUrl = std::string(kWebBase) + "embed/#" + opts;
        std::string editorLink = std::string(kWebBase) + "app/#" + frag;
        std::string code = "<iframe src=\"" + embedUrl + "\" width=\"" + embedW + "\" height=\"" + embedH +
                           "\" style=\"border:1px solid #ddd;border-radius:8px\" loading=\"lazy\" allowfullscreen "
                           "title=\"" + doc.title + " — designElectronics\"></iframe>";

        float h = ui.wrappedText(Rect(body.x, y, body.w, 0),
                                 "Paste this into any blog or web page (WordPress, Ghost, Notion, Medium-style editors with HTML embeds…). "
                                 "Readers can switch views, pan, zoom and run the simulation. The whole design is packed into the link — nothing is uploaded.",
                                 th.textDim, 13.5f * s);
        y += h + 12 * s;
        // options
        g.text({body.x, y + 15 * s}, "Start in", 13 * s, th.textDim, 0);
        const char* names[] = {"Breadboard", "Schematic", "PCB"};
        float bx = body.x + 70 * s;
        for (int v = 0; v < 3; v++) {
            float bw = g.textWidth(names[v], 13 * s) + 24 * s;
            if (ui.button(std::string("emb-sv") + std::to_string(v), Rect(bx, y, bw, 30 * s), names[v], BTN_CHIP, embedView == v)) embedView = v;
            bx += bw + 6 * s;
        }
        ui.checkbox("emb-sim-start", Rect(bx + 10 * s, y, body.r() - bx - 10 * s, 30 * s), "Simulating", embedSim);
        y += 42 * s;
        g.text({body.x, y + 15 * s}, "Size", 13 * s, th.textDim, 0);
        ui.textField("emb-w", Rect(body.x + 70 * s, y, 80 * s, 30 * s), embedW, "800", true);
        g.text({body.x + 158 * s, y + 15 * s}, "×", 14 * s, th.textDim, 0);
        ui.textField("emb-h", Rect(body.x + 176 * s, y, 80 * s, 30 * s), embedH, "500", true);
        g.text({body.x + 266 * s, y + 15 * s}, "pixels", 13 * s, th.textDim, 0);
        y += 44 * s;
        // the code
        Rect box(body.x, y, body.w, std::max(80 * s, body.b() - y - 110 * s));
        g.fillRoundRect(box, 8 * s, rgba(15, 23, 41));
        std::string shown = code.size() > 900 ? code.substr(0, 420) + " … " + code.substr(code.size() - 220) : code;
        float oy = ui.beginScroll("emb-code", box.inset(10 * s), ui.wrappedText(Rect(0, 0, box.w - 20 * s, 0), shown, 0, 12 * s, false));
        ui.wrappedText(Rect(box.x + 10 * s, box.y + 10 * s + oy, box.w - 20 * s, 0), shown, rgba(220, 230, 255), 12 * s);
        ui.endScroll();
        y = box.b() + 8 * s;
        char info[160];
        snprintf(info, sizeof info, "Link size: %.1f KB. Very large designs can use a hosted file instead: …/embed/#src=https://…/design.dez",
                 (double)embedUrl.size() / 1024.0);
        g.text({body.x, y + 8 * s}, info, 12 * s, embedUrl.size() > 60000 ? th.danger : th.textDim, 0);
        // actions
        float by = body.b() - 38 * s;
        float x = body.r();
        auto btn = [&](const std::string& id, const std::string& label, bool primary, std::function<void()> fn) {
            float w = g.textWidth(label, 14 * s) + 30 * s;
            x -= w;
            if (ui.button(id, Rect(x, by, w, 38 * s), label, primary ? BTN_PRIMARY : BTN_NORMAL)) fn();
            x -= 8 * s;
        };
        btn("emb-copy", "Copy embed code", true, [this, code]() {
            SDL_SetClipboardText(code.c_str());
            ui.toast("Embed code copied — paste it into your page's HTML.");
        });
        btn("emb-preview", "Preview", false, [embedUrl]() { SDL_OpenURL(embedUrl.c_str()); });
        btn("emb-link", "Copy editor link", false, [this, editorLink]() {
            SDL_SetClipboardText(editorLink.c_str());
            ui.toast("Link copied: opens this design in the web editor.");
        });
    });
}

// ---------------------------------------------------------------------------
// Browser glue: downloads instead of save dialogs, <input type=file> for open
// ---------------------------------------------------------------------------
#ifdef __EMSCRIPTEN__
EM_JS(void, de_js_download, (const char* path, const char* name), {
    const data = FS.readFile(UTF8ToString(path));
    const blob = new Blob([data], { type: 'application/octet-stream' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = UTF8ToString(name);
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 10000);
});

EM_JS(void, de_js_pick_file, (const char* accept), {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = UTF8ToString(accept);
    input.onchange = async () => {
        const f = input.files && input.files[0];
        if (!f) return;
        const buf = new Uint8Array(await f.arrayBuffer());
        const path = '/tmp/' + f.name.split('/').join('_').split(String.fromCharCode(92)).join('_');
        try { FS.mkdir('/tmp'); } catch (e) {}
        FS.writeFile(path, buf);
        Module.ccall('de_web_opened', null, ['string'], [path]);
    };
    input.click();
});

extern "C" EMSCRIPTEN_KEEPALIVE void de_web_opened(const char* path) {
    if (!g_webApp) return;
    std::string p = path;
    App* app = g_webApp;
    app->runOnMain([app, p]() { app->confirmDiscard([app, p]() { app->loadFile(p); }); });
}
#endif

void webRegisterApp(App* app) { g_webApp = app; }

// Save `name` through `write(path)` and hand it to the browser as a download.
void webSaveAndDownload(const std::string& name, const std::function<void(const std::string&)>& write) {
#ifdef __EMSCRIPTEN__
    std::string path = "/tmp/" + name;
    SDL_CreateDirectory("/tmp");
    write(path);
    de_js_download(path.c_str(), name.c_str());
#else
    (void)name; (void)write;
#endif
}

void webPickFile(const char* accept) {
#ifdef __EMSCRIPTEN__
    de_js_pick_file(accept);
#else
    (void)accept;
#endif
}
