// designElectronics — Export ▸ Video or GIF: record the circuit (optionally
// simulating) in Instagram / YouTube formats. Frames are rendered off-screen at
// the target size with a fixed simulation step, so the timing is real.
#include <cctype>
#include <cstdio>
#include <algorithm>
#include <cmath>

#include "app.h"
#include "logo.h"
#include "video.h"

namespace {

struct Preset {
    const char* name;
    const char* hint;
    int w, h;
};
const Preset kPresets[] = {
    {"Reels / Shorts", "9:16 · 1080×1920", 1080, 1920},
    {"Instagram post", "4:5 · 1080×1350", 1080, 1350},
    {"Square", "1:1 · 1080×1080", 1080, 1080},
    {"YouTube", "16:9 · 1920×1080", 1920, 1080},
};

}  // namespace

struct App::VideoJob {
    std::unique_ptr<VideoWriter> writer;
    SDL_Texture* target = nullptr;
    Simulator sim;
    SimResult result;
    std::string path, displayPath;
    int w = 0, h = 0, fps = 30, frames = 0, frame = 0;
    int viewMode = 0;  // 0..2 a view, 3 = tour of all three
    bool simulate = true, watermark = true, gif = false;
    bool cancel = false;
};

App::~App() = default;  // here, where VideoJob is complete

void App::openVideoSheet() {
    videoTitle = doc.title;
    ui.openSheet("video", "Export video or GIF", 680, 620, [this](Rect body) {
        float s = ui.s;
        auto& th = ui.th;
        float y = body.y;
        if (videoJob) {  // exporting: progress
            float pct = videoJob->frames ? (float)videoJob->frame / (float)videoJob->frames : 0;
            g.text({body.x, y + 14 * s}, "Recording " + std::string(videoJob->gif ? "GIF" : "MP4") + "…", 16 * s, th.text, 0);
            y += 40 * s;
            Rect bar(body.x, y, body.w, 12 * s);
            g.fillRoundRect(bar, 6 * s, th.border);
            g.fillRoundRect(Rect(bar.x, bar.y, std::max(12 * s, bar.w * pct), bar.h), 6 * s, th.accent);
            y += 26 * s;
            char b[96];
            snprintf(b, sizeof b, "Frame %d of %d", videoJob->frame, videoJob->frames);
            g.text({body.x, y + 8 * s}, b, 13 * s, th.textDim, 0);
            if (ui.button("vid-cancel", Rect(body.r() - 110 * s, body.b() - 38 * s, 110 * s, 38 * s), "Cancel")) videoJob->cancel = true;
            return;
        }
        auto heading = [&](const std::string& t) {
            g.text({body.x, y + 8 * s}, t, 12.5f * s, th.textDim, 0);
            y += 22 * s;
        };
        heading("FORMAT");
        float x = body.x;
        for (int i = 0; i < 4; i++) {
            const Preset& p = kPresets[i];
            float w = (body.w - 3 * 8 * s) / 4;
            Rect r(x, y, w, 58 * s);
            bool sel = videoPreset == i;
            if (ui.button("vid-p" + std::to_string(i), r, "", BTN_NORMAL, sel)) videoPreset = i;
            if (sel) g.strokeRoundRect(r, 6 * s, 2 * s, th.accent);
            // little aspect-ratio glyph
            float gh = 24 * s, gw = gh * (float)p.w / (float)p.h;
            if (gw > 36 * s) { gw = 36 * s; gh = gw * (float)p.h / (float)p.w; }
            g.strokeRect(Rect(r.x + 10 * s, r.center().y - gh / 2, gw, gh), 1.5f * s, sel ? th.accent : th.textDim);
            std::string nm = p.name;
            while (nm.size() > 4 && g.textWidth(nm, 12.5f * s) > r.w - 60 * s) { nm.resize(nm.size() - 4); nm += "…"; }
            g.text({r.x + 54 * s, r.y + 20 * s}, nm, 12.5f * s, th.text, 0);
            g.text({r.x + 54 * s, r.y + 39 * s}, p.hint, 11 * s, th.textDim, 0);
            x += w + 8 * s;
        }
        y += 70 * s;
        heading("FILE");
        if (ui.button("vid-mp4", Rect(body.x, y, 120 * s, 32 * s), "MP4 video", BTN_CHIP, !videoGif)) videoGif = false;
        if (ui.button("vid-gif", Rect(body.x + 128 * s, y, 120 * s, 32 * s), "Animated GIF", BTN_CHIP, videoGif)) videoGif = true;
        g.text({body.x + 262 * s, y + 16 * s}, videoGif ? "half size, 15 fps — for chats, READMEs and blogs" : "30 fps H.264 — upload anywhere", 12 * s, th.textDim, 0);
        y += 44 * s;
        heading("SHOW");
        const char* views[] = {"Breadboard", "Schematic", "PCB", "Tour of all three"};
        x = body.x;
        for (int i = 0; i < 4; i++) {
            float w = g.textWidth(views[i], 13 * s) + 26 * s;
            if (ui.button("vid-v" + std::to_string(i), Rect(x, y, w, 32 * s), views[i], BTN_CHIP, videoView == i)) videoView = i;
            x += w + 6 * s;
        }
        y += 44 * s;
        heading("LENGTH");
        const int secs[] = {5, 10, 15, 30};
        x = body.x;
        for (int i = 0; i < 4; i++) {
            std::string l = std::to_string(secs[i]) + " s";
            if (ui.button("vid-d" + std::to_string(i), Rect(x, y, 64 * s, 32 * s), l, BTN_CHIP, videoSeconds == secs[i])) videoSeconds = secs[i];
            x += 70 * s;
        }
        ui.checkbox("vid-sim", Rect(x + 16 * s, y, 220 * s, 32 * s), "Run the simulation", videoSimulate);
        y += 44 * s;
        heading("NAME");
        ui.textField("vid-title", Rect(body.x, y, body.w, 34 * s), videoTitle, "Used for the file name", true);
        y += 44 * s;
        ui.checkbox("vid-wm", Rect(body.x, y, body.w, 30 * s), "Add a small designElectronics logo and link in the corner", videoWatermark);
        if (ui.button("vid-go", Rect(body.r() - 160 * s, body.b() - 38 * s, 160 * s, 38 * s), "Export…", BTN_PRIMARY)) {
            std::string base;
            for (char c : (videoTitle.empty() ? doc.title : videoTitle)) base += std::isalnum((unsigned char)c) ? c : '_';
            static const SDL_DialogFileFilter mp4f[] = {{"MP4 video", "mp4"}};
            static const SDL_DialogFileFilter giff[] = {{"GIF animation", "gif"}};
            bool gif = videoGif;
#ifdef __EMSCRIPTEN__
            (void)mp4f; (void)giff;
            SDL_CreateDirectory("/tmp");
            startVideo("/tmp/" + base + (gif ? ".gif" : ".mp4"));  // downloaded when finished
            return;
#endif
            showSaveDialog(base + (gif ? ".gif" : ".mp4"), gif ? giff : mp4f, 1, [this, gif](const std::string& p0) {
                std::string p = p0;
                std::string ext = gif ? ".gif" : ".mp4";
                if (p.rfind("content://", 0) != 0 && (p.size() < 4 || p.substr(p.size() - 4) != ext)) p += ext;
                startVideo(p);
            });
        }
    });
}

void App::startVideo(const std::string& path) {
    const Preset& pr = kPresets[std::clamp(videoPreset, 0, 3)];
    auto job = std::make_shared<VideoJob>();
    job->gif = videoGif;
    job->w = videoGif ? pr.w / 2 : pr.w;
    job->h = videoGif ? pr.h / 2 : pr.h;
    job->fps = videoGif ? 15 : 30;
    job->frames = videoSeconds * job->fps;
    job->viewMode = videoView;
    job->simulate = videoSimulate;
    job->watermark = videoWatermark;
    job->path = path;
    std::string err;
    job->writer = videoGif ? openGif(path, job->w, job->h, job->fps, &err) : openMp4(path, job->w, job->h, job->fps, &err);
    if (!job->writer) { ui.dialog("Couldn't start the export", {err}, {}); return; }
    job->target = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, job->w, job->h);
    if (!job->target) { ui.dialog("Couldn't start the export", {SDL_GetError()}, {}); return; }
    ensureConn();
    videoJob = std::move(job);
    if (!ui.sheetOpen("video")) openVideoSheet();
}

// Render one frame of the video into the off-screen target and hand it to the encoder.
bool App::renderVideoFrame() {
    VideoJob& J = *videoJob;
    int v = J.viewMode < 3 ? J.viewMode : std::min(2, J.frame * 3 / std::max(1, J.frames));
    double t = (double)J.frame / J.fps;
    if (videoAnimate) videoAnimate(t);
    if (J.simulate) {
        J.result = J.sim.step(doc, conn, {}, 1.0 / J.fps);
        (void)t;
    }
    // swap in the video's view of the world
    SimResult savedSim = std::move(sim);
    bool savedSimOn = simOn;
    float savedS = ui.s;
    Camera savedCam = cam[v];
    Rect savedCanvas = rCanvas;
    int savedView = view;
    if (J.simulate) { sim = J.result; simOn = true; } else { sim = SimResult(); simOn = false; }
    float W = (float)J.w, H = (float)J.h;
    float u = std::min(W, H) / 1080.f;  // layout unit
    ui.s = 2.0f * u;
    view = v;
    SDL_SetRenderTarget(ren, J.target);
    SDL_SetRenderDrawColor(ren, 240, 241, 244, 255);
    SDL_RenderClear(ren);
    // the circuit fills the frame; the watermark is a small faded corner mark
    Rect vp(0, 0, W, H);
    g.fillRect(vp, v == PCB ? rgba(60, 64, 72) : rgba(246, 246, 244));
    rCanvas = vp;
    fitView(v, vp);
    if (videoZoom) {
        Rect focus;
        float k = videoZoom(t, focus);
        if (k > 0 && focus.w > 0) {
            Camera& c = cam[v];
            float z1 = std::min(vp.w / focus.w, vp.h / focus.h);
            Vec2 c0 = (vp.center() - c.pan) / c.zoom;
            float z = std::exp(std::log(c.zoom) + (std::log(z1) - std::log(c.zoom)) * k);
            Vec2 ctr = c0 + (focus.center() - c0) * k;
            c.zoom = z;
            c.pan = vp.center() - ctr * z;
        }
    }
    drawCanvas(v, cam[v], vp, true);
    if (J.watermark) {
        float m = 28 * u, lg = 40 * u, px = 22 * u;
        const char* link = "rorretbolt.github.io/designElectronics";
        float x = W - m - g.textWidth(link, px);
        uint32_t ink = v == PCB ? rgba(255, 255, 255, 150) : rgba(30, 40, 60, 130);
        drawLogo(g, Rect(x - lg - 10 * u, H - m - lg, lg, lg), 0.55f);
        g.text({x, H - m - lg / 2}, link, px, ink, 0);
    }
    g.flush();
    SDL_Surface* shot = SDL_RenderReadPixels(ren, nullptr);
    SDL_SetRenderTarget(ren, nullptr);
    // restore
    sim = std::move(savedSim);
    simOn = savedSimOn;
    ui.s = savedS;
    cam[v] = savedCam;
    rCanvas = savedCanvas;
    view = savedView;
    if (!shot) return false;
    SDL_Surface* rgba32 = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(shot);
    if (!rgba32) return false;
    bool ok = J.writer->addFrame((const uint8_t*)rgba32->pixels, rgba32->pitch);
    SDL_DestroySurface(rgba32);
    return ok;
}

// Called every app frame while exporting: renders a slice of frames, keeping the UI responsive.
void App::stepVideoExport() {
    if (!videoJob) return;
    VideoJob& J = *videoJob;
    Uint64 start = SDL_GetTicks();
    bool failed = false;
    while (!J.cancel && J.frame < J.frames && SDL_GetTicks() - start < 45) {
        if (!renderVideoFrame()) { failed = true; break; }
        J.frame++;
    }
    if (failed || J.cancel || J.frame >= J.frames) {
        bool ok = !failed && !J.cancel && J.writer->finish();
        std::string err = J.writer->error();
        std::string path = J.path;
        bool gif = J.gif;
        bool cancelled = J.cancel;
        SDL_DestroyTexture(J.target);
        videoJob.reset();
        ui.closeSheet();
        if (cancelled) { SDL_RemovePath(path.c_str()); ui.toast("Export cancelled"); return; }
        if (!ok) {
            lastVideoError = err.empty() ? "Couldn't render the video." : err;
            ui.dialog("Export failed", {lastVideoError}, {});
            return;
        }
        lastVideoError.clear();
#ifdef __EMSCRIPTEN__
        webSaveAndDownload(path.substr(path.find_last_of('/') + 1), [](const std::string&) {});  // file is already written
#endif
        ui.toast(std::string(gif ? "GIF" : "Video") + " saved: " + path, 5);
    }
}
