// designElectronics — video (MP4/H.264) and animated GIF writers for exporting
// a running circuit in social-media formats.
#pragma once
#include <cstdint>
#include <memory>
#include <string>

class VideoWriter {
public:
    virtual ~VideoWriter() = default;
    // One frame, RGBA8 top-down, `stride` bytes per row.
    virtual bool addFrame(const uint8_t* rgba, int stride) = 0;
    virtual bool finish() = 0;
    const std::string& error() const { return err_; }

protected:
    std::string err_;
};

// MP4 with H.264 video (minih264 + minimp4, both public domain). Width/height must be even.
std::unique_ptr<VideoWriter> openMp4(const std::string& path, int width, int height, int fps, std::string* err);

// Animated GIF (looping), 256-colour palette built from the first frames.
std::unique_ptr<VideoWriter> openGif(const std::string& path, int width, int height, int fps, std::string* err);
