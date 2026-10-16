// designElectronics — MP4 (H.264) and GIF writers (see video.h).
#include "video.h"

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#define H264E_MAX_THREADS 0
#define H264E_SVC_API 0
extern "C" {
#include "../third_party/minih264/minih264e.h"
#include "../third_party/minimp4/minimp4.h"
}
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace {

// ===========================================================================
// MP4 / H.264
// ===========================================================================
class Mp4Writer : public VideoWriter {
public:
    bool open(const std::string& path, int w, int h, int fps) {
        w_ = w;
        h_ = h;
        fps_ = fps;
        file_ = std::fopen(path.c_str(), "wb");
        if (!file_) { err_ = "Couldn't create " + path; return false; }
        // encoder works on whole macroblocks; the stream is cropped back to w × h
        ew_ = (w + 15) & ~15;
        eh_ = (h + 15) & ~15;
        H264E_create_param_t cp;
        std::memset(&cp, 0, sizeof cp);
        cp.width = w;
        cp.height = h;
        cp.gop = fps * 2;                     // key frame every 2 s
        cp.vbv_size_bytes = 0;
        cp.const_input_flag = 1;
        cp.temporal_denoise_flag = 0;
        int persist = 0, scratch = 0;
        if (H264E_sizeof(&cp, &persist, &scratch) != H264E_STATUS_SUCCESS) { err_ = "Unsupported video size"; return false; }
        enc_ = (H264E_persist_t*)std::malloc((size_t)persist);
        scratch_ = (H264E_scratch_t*)std::malloc((size_t)scratch);
        int ist = enc_ && scratch_ ? H264E_init(enc_, &cp) : -1;
        if (ist != H264E_STATUS_SUCCESS) { err_ = "Couldn't start the video encoder (code " + std::to_string(ist) + ")"; return false; }
        mux_ = MP4E_open(0, 0, this, &Mp4Writer::write);
        if (!mux_ || mp4_h26x_write_init(&mp4_, mux_, w, h, 0) != MP4E_STATUS_OK) { err_ = "Couldn't start the MP4 writer"; return false; }
        y_.assign((size_t)ew_ * eh_, 0);
        u_.assign((size_t)(ew_ / 2) * (eh_ / 2), 128);
        v_.assign((size_t)(ew_ / 2) * (eh_ / 2), 128);
        return true;
    }

    ~Mp4Writer() override {
        if (file_) std::fclose(file_);
        std::free(enc_);
        std::free(scratch_);
    }

    bool addFrame(const uint8_t* rgba, int stride) override {
        // RGB → YUV 4:2:0 (BT.601, studio range), edge pixels repeated into the padding
        for (int y = 0; y < eh_; y++) {
            const uint8_t* row = rgba + (size_t)std::min(y, h_ - 1) * (size_t)stride;
            uint8_t* Y = &y_[(size_t)y * (size_t)ew_];
            for (int x = 0; x < ew_; x++) {
                const uint8_t* p = row + (size_t)std::min(x, w_ - 1) * 4;
                Y[x] = (uint8_t)((66 * p[0] + 129 * p[1] + 25 * p[2] + 128) / 256 + 16);
            }
        }
        for (int y = 0; y < eh_ / 2; y++) {
            for (int x = 0; x < ew_ / 2; x++) {
                int r = 0, g = 0, b = 0;
                for (int dy = 0; dy < 2; dy++)
                    for (int dx = 0; dx < 2; dx++) {
                        const uint8_t* p = rgba + (size_t)std::min(2 * y + dy, h_ - 1) * (size_t)stride + (size_t)std::min(2 * x + dx, w_ - 1) * 4;
                        r += p[0]; g += p[1]; b += p[2];
                    }
                r /= 4; g /= 4; b /= 4;
                u_[(size_t)y * (size_t)(ew_ / 2) + (size_t)x] = (uint8_t)std::clamp((-38 * r - 74 * g + 112 * b + 128) / 256 + 128, 0, 255);
                v_[(size_t)y * (size_t)(ew_ / 2) + (size_t)x] = (uint8_t)std::clamp((112 * r - 94 * g - 18 * b + 128) / 256 + 128, 0, 255);
            }
        }
        H264E_io_yuv_t yuv;
        yuv.yuv[0] = y_.data(); yuv.stride[0] = ew_;
        yuv.yuv[1] = u_.data(); yuv.stride[1] = ew_ / 2;
        yuv.yuv[2] = v_.data(); yuv.stride[2] = ew_ / 2;
        H264E_run_param_t rp;
        std::memset(&rp, 0, sizeof rp);
        rp.encode_speed = 4;
        rp.qp_min = 16;
        rp.qp_max = 34;
        rp.desired_frame_bytes = (int)((long)w_ * h_ / 8);  // generous: diagrams stay crisp
        uint8_t* coded = nullptr;
        int size = 0;
        int st = H264E_encode(enc_, scratch_, &rp, &yuv, &coded, &size);
        if (st != H264E_STATUS_SUCCESS) { err_ = "Video encoding failed (code " + std::to_string(st) + ")"; return false; }
        int ms = mp4_h26x_write_nal(&mp4_, coded, size, (unsigned)(90000 / fps_));
        if (ms != MP4E_STATUS_OK) { err_ = "Writing the MP4 failed (code " + std::to_string(ms) + ", " + std::to_string(size) + " bytes)"; return false; }
        return !ioError_;
    }

    bool finish() override {
        if (mux_) {
            mp4_h26x_write_close(&mp4_);
            MP4E_close(mux_);
            mux_ = nullptr;
        }
        if (file_) { std::fclose(file_); file_ = nullptr; }
        if (ioError_) err_ = "Writing the file failed (disk full?)";
        return !ioError_;
    }

private:
    static int write(int64_t offset, const void* buffer, size_t size, void* token) {
        auto* self = (Mp4Writer*)token;
        if (std::fseek(self->file_, (long)offset, SEEK_SET) != 0 || std::fwrite(buffer, 1, size, self->file_) != size) {
            self->ioError_ = true;
            return 1;
        }
        return 0;
    }
    FILE* file_ = nullptr;
    int w_ = 0, h_ = 0, ew_ = 0, eh_ = 0, fps_ = 30;
    bool ioError_ = false;
    H264E_persist_t* enc_ = nullptr;
    H264E_scratch_t* scratch_ = nullptr;
    MP4E_mux_t* mux_ = nullptr;
    mp4_h26x_writer_t mp4_;
    std::vector<uint8_t> y_, u_, v_;
};

// ===========================================================================
// Animated GIF
// ===========================================================================
class GifWriter : public VideoWriter {
public:
    bool open(const std::string& path, int w, int h, int fps) {
        w_ = w;
        h_ = h;
        delay_ = std::max(2, (int)std::lround(100.0 / fps));
        f_ = std::fopen(path.c_str(), "wb");
        if (!f_) { err_ = "Couldn't create " + path; return false; }
        return true;
    }
    ~GifWriter() override { if (f_) std::fclose(f_); }

    bool addFrame(const uint8_t* rgba, int stride) override {
        // frames are collected at reduced precision until the palette is built from the first ones
        std::vector<uint8_t> px((size_t)w_ * h_ * 3);
        for (int y = 0; y < h_; y++)
            for (int x = 0; x < w_; x++) {
                const uint8_t* p = rgba + (size_t)y * (size_t)stride + (size_t)x * 4;
                uint8_t* q = &px[((size_t)y * w_ + x) * 3];
                q[0] = p[0]; q[1] = p[1]; q[2] = p[2];
            }
        if (palette_.empty()) {
            pending_.push_back(std::move(px));
            if (pending_.size() >= 6) flushPending();
            return true;
        }
        writeFrame(px);
        return !ioErr_;
    }

    bool finish() override {
        if (palette_.empty()) flushPending();
        if (f_) {
            std::fputc(0x3B, f_);  // trailer
            std::fclose(f_);
            f_ = nullptr;
        }
        return !ioErr_;
    }

private:
    FILE* f_ = nullptr;
    int w_ = 0, h_ = 0, delay_ = 7;
    bool ioErr_ = false;
    std::vector<std::vector<uint8_t>> pending_;
    std::vector<uint8_t> palette_;  // 256 × RGB
    std::vector<uint8_t> prevIdx_;  // indices of the previous frame
    std::map<uint32_t, uint8_t> cache_;

    void put(const void* d, size_t n) { if (std::fwrite(d, 1, n, f_) != n) ioErr_ = true; }
    void put8(int v) { uint8_t b = (uint8_t)v; put(&b, 1); }
    void put16(int v) { put8(v & 0xFF); put8((v >> 8) & 0xFF); }

    // Median-cut palette from the collected frames (diagrams have few colours, so this is exact for most).
    void buildPalette() {
        std::map<uint32_t, uint32_t> hist;
        for (auto& fr : pending_)
            for (size_t i = 0; i < fr.size(); i += 3 * 3) hist[(uint32_t)fr[i] << 16 | (uint32_t)fr[i + 1] << 8 | fr[i + 2]]++;
        // the most common colours (backgrounds, board fills, wires) get exact entries;
        // median cut shares the rest among the anti-aliased in-between shades
        std::vector<std::pair<uint32_t, uint32_t>> byCount(hist.begin(), hist.end());
        std::sort(byCount.begin(), byCount.end(), [](auto& a, auto& b) { return a.second > b.second; });
        size_t exact = std::min<size_t>(byCount.size() <= 256 ? byCount.size() : 96, byCount.size());
        struct Box { std::vector<std::pair<uint32_t, uint32_t>> c; };
        std::vector<Box> boxes;
        for (size_t i = 0; i < exact; i++) boxes.push_back(Box{{byCount[i]}});
        if (exact < byCount.size()) {
            boxes.emplace_back();
            for (size_t i = exact; i < byCount.size(); i++) boxes.back().c.push_back(byCount[i]);
        }
        while (boxes.size() < 256) {
            // split the box with the widest channel range
            int bi = -1, bch = 0, brange = 0;
            for (size_t i = 0; i < boxes.size(); i++) {
                if (boxes[i].c.size() < 2) continue;
                for (int ch = 0; ch < 3; ch++) {
                    int lo = 255, hi = 0;
                    for (auto& e : boxes[i].c) { int v = (int)(e.first >> (16 - 8 * ch)) & 0xFF; lo = std::min(lo, v); hi = std::max(hi, v); }
                    if (hi - lo > brange) { brange = hi - lo; bi = (int)i; bch = ch; }
                }
            }
            if (bi < 0) break;
            auto& c = boxes[(size_t)bi].c;
            std::sort(c.begin(), c.end(), [&](auto& a, auto& b) { return ((a.first >> (16 - 8 * bch)) & 0xFF) < ((b.first >> (16 - 8 * bch)) & 0xFF); });
            uint64_t total = 0, acc = 0;
            for (auto& e : c) total += e.second;
            size_t cut = 0;
            for (; cut < c.size() - 1; cut++) { acc += c[cut].second; if (acc * 2 >= total) break; }
            Box nb;
            nb.c.assign(c.begin() + (long)cut + 1, c.end());
            c.resize(cut + 1);
            boxes.push_back(std::move(nb));
        }
        palette_.assign(256 * 3, 0);
        for (size_t i = 0; i < boxes.size(); i++) {
            uint64_t r = 0, g = 0, b = 0, n = 0;
            for (auto& e : boxes[i].c) { r += (e.first >> 16 & 0xFF) * e.second; g += (e.first >> 8 & 0xFF) * e.second; b += (e.first & 0xFF) * e.second; n += e.second; }
            if (!n) continue;
            palette_[i * 3] = (uint8_t)(r / n); palette_[i * 3 + 1] = (uint8_t)(g / n); palette_[i * 3 + 2] = (uint8_t)(b / n);
        }
    }

    uint8_t nearest(uint32_t rgb) {
        auto it = cache_.find(rgb);
        if (it != cache_.end()) return it->second;
        int r = (int)(rgb >> 16 & 0xFF), g = (int)(rgb >> 8 & 0xFF), b = (int)(rgb & 0xFF);
        int best = 0, bd = 1 << 30;
        for (int i = 0; i < 256; i++) {
            int dr = r - palette_[(size_t)i * 3], dg = g - palette_[(size_t)i * 3 + 1], db = b - palette_[(size_t)i * 3 + 2];
            int d = 2 * dr * dr + 4 * dg * dg + 3 * db * db;
            if (d < bd) { bd = d; best = i; }
        }
        if (cache_.size() < 200000) cache_[rgb] = (uint8_t)best;
        return (uint8_t)best;
    }

    void flushPending() {
        if (pending_.empty()) { pending_.push_back(std::vector<uint8_t>((size_t)w_ * h_ * 3, 255)); }
        buildPalette();
        // header + global palette + looping
        put("GIF89a", 6);
        put16(w_); put16(h_);
        put8(0xF7);  // global colour table, 8 bits
        put8(0); put8(0);
        put(palette_.data(), palette_.size());
        put("\x21\xFF\x0BNETSCAPE2.0\x03\x01\x00\x00\x00", 19);
        auto frames = std::move(pending_);
        pending_.clear();
        for (auto& fr : frames) writeFrame(fr);
    }

    void writeFrame(const std::vector<uint8_t>& px) {
        std::vector<uint8_t> idx((size_t)w_ * h_);
        for (size_t i = 0; i < idx.size(); i++) idx[i] = nearest((uint32_t)px[i * 3] << 16 | (uint32_t)px[i * 3 + 1] << 8 | px[i * 3 + 2]);
        // only the rectangle that changed since the previous frame is stored
        int x0 = 0, y0 = 0, x1 = w_ - 1, y1 = h_ - 1;
        if (!prevIdx_.empty()) {
            x0 = w_; y0 = h_; x1 = -1; y1 = -1;
            for (int y = 0; y < h_; y++)
                for (int x = 0; x < w_; x++)
                    if (idx[(size_t)y * w_ + x] != prevIdx_[(size_t)y * w_ + x]) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
            if (x1 < 0) { x0 = y0 = 0; x1 = y1 = 0; }  // identical frame: 1 px
        }
        int rw = x1 - x0 + 1, rh = y1 - y0 + 1;
        // graphic control extension: delay, keep the previous frame underneath
        put8(0x21); put8(0xF9); put8(4); put8(0x04); put16(delay_); put8(0); put8(0);
        put8(0x2C); put16(x0); put16(y0); put16(rw); put16(rh); put8(0);
        std::vector<uint8_t> sub((size_t)rw * rh);
        for (int y = 0; y < rh; y++) std::memcpy(&sub[(size_t)y * rw], &idx[(size_t)(y0 + y) * w_ + x0], (size_t)rw);
        lzw(sub);
        prevIdx_ = std::move(idx);
    }

    // GIF LZW with 8-bit minimum code size
    void lzw(const std::vector<uint8_t>& data) {
        const int minBits = 8, clear = 256, eoi = 257;
        put8(minBits);
        std::vector<uint8_t> block;
        uint32_t bitBuf = 0;
        int bitCnt = 0, codeSize = minBits + 1, next = eoi + 1;
        auto emit = [&](int code) {
            bitBuf |= (uint32_t)code << bitCnt;
            bitCnt += codeSize;
            while (bitCnt >= 8) {
                block.push_back((uint8_t)(bitBuf & 0xFF));
                bitBuf >>= 8;
                bitCnt -= 8;
                if (block.size() == 255) { put8(255); put(block.data(), 255); block.clear(); }
            }
        };
        std::vector<int> dict((size_t)4096 * 256, -1);  // (prefix code, byte) → code
        emit(clear);
        int prefix = data.empty() ? -1 : data[0];
        for (size_t i = 1; i < data.size(); i++) {
            int c = data[i];
            int& slot = dict[(size_t)prefix * 256 + (size_t)c];
            if (slot >= 0) { prefix = slot; continue; }
            emit(prefix);
            if (next < 4096) {
                slot = next++;
                if (next > (1 << codeSize) && codeSize < 12) codeSize++;
            } else {  // table full: start over
                emit(clear);
                std::fill(dict.begin(), dict.end(), -1);
                codeSize = minBits + 1;
                next = eoi + 1;
            }
            prefix = c;
        }
        if (prefix >= 0) emit(prefix);
        emit(eoi);
        if (bitCnt > 0) block.push_back((uint8_t)(bitBuf & 0xFF));
        if (!block.empty()) { put8((int)block.size()); put(block.data(), block.size()); }
        put8(0);
    }
};

}  // namespace

std::unique_ptr<VideoWriter> openMp4(const std::string& path, int width, int height, int fps, std::string* err) {
    auto w = std::make_unique<Mp4Writer>();
    if (!w->open(path, width & ~1, height & ~1, fps)) { if (err) *err = w->error(); return nullptr; }
    return w;
}

std::unique_ptr<VideoWriter> openGif(const std::string& path, int width, int height, int fps, std::string* err) {
    auto w = std::make_unique<GifWriter>();
    if (!w->open(path, width, height, fps)) { if (err) *err = w->error(); return nullptr; }
    return w;
}
