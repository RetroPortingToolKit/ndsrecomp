#pragma once
// Headless presented-frame capture for the interactive frontend. No window is
// shown, no audio device is opened and no desktop input is read: SDL's dummy
// drivers run the real frontend loop (adaptive widescreen compositor, title
// patches, audio mixer) and the finished screens are composed into the
// requested canvas and written as PNG.
//   --capture-out DIR        DIR/frame_%06d.png (composed canvas)
//   --capture-size WxH       canvas size (default 1920x1080)
//   --capture-layout L       stacked (as the window presents it, default) | top-large
//   --capture-from/--capture-to N, --capture-every K   inclusive DS-frame range
//   --capture-wav FILE       mixed stereo output at the DS mixer rate (32728 Hz)
//   --input-script FILE      "<frame> TOKEN[+TOKEN...]", '-' releases. Tokens: A B X Y L R
//                            START SELECT UP DOWN LEFT RIGHT, TOUCH:x,y, AIM:dx,dy (MPH aim)
//   --exit-at-frame N        stop after N frames
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct NdsCaptureOptions {
    bool enabled = false;
    std::string out_dir, wav_path, input_path, layout = "stacked";
    int width = 1920, height = 1080;
    long from = 1, to = -1, every = 1, exit_at = 0;
};

class NdsCapture {
public:
    struct Input {
        uint16_t keys = 0;  // active-high, frontend key_bit() layout
        bool touch = false;
        int tx = 0, ty = 0;
        int aim_x = 0, aim_y = 0;
    };
    NdsCaptureOptions opt;

    void open(int wav_rate) {
        std::filesystem::create_directories(opt.out_dir);
        if (!opt.wav_path.empty()) {
            wav_ = std::fopen(opt.wav_path.c_str(), "wb");
            if (!wav_) throw std::runtime_error("cannot open --capture-wav");
            rate_ = wav_rate;
            header(0);
        }
        if (!opt.input_path.empty()) load_script();
    }
    bool wanted(long frame) const {
        if (frame < opt.from || (opt.to >= 0 && frame > opt.to)) return false;
        return (frame - std::max(1l, opt.from)) % opt.every == 0;
    }
    Input input_for(long frame) const {
        Input held;
        for (const auto& s : steps_) { if (s.first > frame) break; held = s.second; }
        return held;
    }
    void audio(long frame, const int16_t* samples, size_t frames) {
        if (!wav_ || !wanted(frame)) return;
        std::fwrite(samples, 4, frames, wav_);
        wav_frames_ += frames;
    }
    // ARGB8888 top (tw x 192) and bottom (bw x 192) screens -> canvas -> PNG.
    void frame(long number, const uint32_t* top, int tw, const uint32_t* bottom, int bw) {
        if (!wanted(number)) return;
        const int W = opt.width, H = opt.height;
        canvas_.assign(size_t(W) * H, 0xFF141414u);
        if (opt.layout == "top-large") {
            // Top screen fills the canvas height; the touch screen sits inset at the bottom-right.
            blit(top, tw, 0, 0, std::min(W, int(double(H) * tw / 192.0)), H, true);
            const int bw_px = W / 4, bh_px = bw_px * 192 / std::max(bw, 1);
            blit(bottom, bw, W - bw_px - W / 64, H - bh_px - H / 36, bw_px, bh_px, false);
        } else {
            const int lw = std::max(tw, bw);
            const double s = std::min(double(W) / lw, double(H) / 384.0);
            const int y0 = int((H - 384 * s) / 2.0);
            const int tdw = int(tw * s), bdw = int(bw * s), dh = int(192 * s);
            blit(top, tw, (W - tdw) / 2, y0, tdw, dh, false);
            blit(bottom, bw, (W - bdw) / 2, y0 + dh, bdw, dh, false);
        }
        char name[64];
        std::snprintf(name, sizeof name, "frame_%06ld.png", number);
        png((std::filesystem::path(opt.out_dir) / name).string());
        ++written_;
    }
    void finish(long frames) {
        if (!opt.enabled) return;
        if (wav_) { header(wav_frames_ * 4); std::fclose(wav_); wav_ = nullptr; }
        std::fprintf(stderr, "capture: %llu frames written, %llu audio samples (%d Hz), present size %dx%d (%ld simulated)\n",
            (unsigned long long)written_, (unsigned long long)wav_frames_, rate_, opt.width, opt.height, frames);
    }

private:
    std::vector<std::pair<long, Input>> steps_;
    std::FILE* wav_ = nullptr;
    uint64_t wav_frames_ = 0, written_ = 0;
    int rate_ = 32728;
    std::vector<uint32_t> canvas_;
    std::vector<uint8_t> z_;

    static uint16_t key(const std::string& n) {
        if (n == "A") return 1u << 0;      if (n == "B") return 1u << 1;
        if (n == "SELECT") return 1u << 2; if (n == "START") return 1u << 3;
        if (n == "RIGHT") return 1u << 4;  if (n == "LEFT") return 1u << 5;
        if (n == "UP") return 1u << 6;     if (n == "DOWN") return 1u << 7;
        if (n == "R") return 1u << 8;      if (n == "L") return 1u << 9;
        if (n == "X") return 1u << 10;     if (n == "Y") return 1u << 11;
        if (n == "-" || n.empty()) return 0;
        throw std::runtime_error("unknown button " + n + " in --input-script");
    }
    void load_script() {
        std::ifstream in(opt.input_path);
        if (!in) throw std::runtime_error("cannot read --input-script");
        std::string line;
        while (std::getline(in, line)) {
            auto hash = line.find('#'); if (hash != std::string::npos) line.resize(hash);
            std::istringstream s(line); long f; std::string b;
            if (!(s >> f)) continue;
            s >> b;
            Input held; std::istringstream t(b); std::string tok;
            while (std::getline(t, tok, '+')) {
                if (tok.rfind("TOUCH:", 0) == 0) { held.touch = std::sscanf(tok.c_str() + 6, "%d,%d", &held.tx, &held.ty) == 2; }
                else if (tok.rfind("AIM:", 0) == 0) { std::sscanf(tok.c_str() + 4, "%d,%d", &held.aim_x, &held.aim_y); }
                else held.keys |= key(tok);
            }
            steps_.emplace_back(f, held);
        }
        std::stable_sort(steps_.begin(), steps_.end(), [](auto& a, auto& b) { return a.first < b.first; });
    }
    // Bilinear blit of a w x 192 ARGB screen into the canvas rectangle.
    void blit(const uint32_t* src, int sw, int dx, int dy, int dw, int dh, bool) {
        if (dw <= 0 || dh <= 0 || !src) return;
        for (int y = 0; y < dh; ++y) {
            const int cy = dy + y; if (cy < 0 || cy >= opt.height) continue;
            const double fy = (y + 0.5) * 192.0 / dh - 0.5;
            const int y0 = std::clamp(int(std::floor(fy)), 0, 191), y1 = std::min(y0 + 1, 191);
            const double wy = std::clamp(fy - y0, 0.0, 1.0);
            for (int x = 0; x < dw; ++x) {
                const int cx = dx + x; if (cx < 0 || cx >= opt.width) continue;
                const double fx = (x + 0.5) * sw / double(dw) - 0.5;
                const int x0 = std::clamp(int(std::floor(fx)), 0, sw - 1), x1 = std::min(x0 + 1, sw - 1);
                const double wx = std::clamp(fx - x0, 0.0, 1.0);
                uint32_t out = 0xFF000000u;
                for (int sh = 0; sh <= 16; sh += 8) {
                    auto c = [&](int xx, int yy) { return double((src[size_t(yy) * sw + xx] >> sh) & 255); };
                    const double v = (c(x0, y0) * (1 - wx) + c(x1, y0) * wx) * (1 - wy) + (c(x0, y1) * (1 - wx) + c(x1, y1) * wx) * wy;
                    out |= uint32_t(v + 0.5) << sh;
                }
                canvas_[size_t(cy) * opt.width + cx] = out;
            }
        }
    }
    void header(uint64_t data) {
        uint8_t h[44]; std::memcpy(h, "RIFF", 4); uint32_t v = uint32_t(36 + data); std::memcpy(h + 4, &v, 4);
        std::memcpy(h + 8, "WAVEfmt ", 8); uint32_t len = 16, rate = uint32_t(rate_), bps = rate * 4; uint16_t pcm = 1, ch = 2, bits = 16, align = 4;
        std::memcpy(h + 16, &len, 4); std::memcpy(h + 20, &pcm, 2); std::memcpy(h + 22, &ch, 2); std::memcpy(h + 24, &rate, 4);
        std::memcpy(h + 28, &bps, 4); std::memcpy(h + 32, &align, 2); std::memcpy(h + 34, &bits, 2); std::memcpy(h + 36, "data", 4);
        uint32_t d = uint32_t(data); std::memcpy(h + 40, &d, 4);
        std::fseek(wav_, 0, SEEK_SET); std::fwrite(h, 1, 44, wav_); std::fseek(wav_, 0, SEEK_END);
    }
    static void be32(uint8_t* p, uint32_t v) { p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v); }
    static uint32_t crc(uint32_t c, const uint8_t* p, size_t n) {
        static uint32_t table[256]; static bool init = false;
        if (!init) { for (uint32_t i = 0; i < 256; ++i) { uint32_t x = i; for (int k = 0; k < 8; ++k) x = x & 1 ? 0xEDB88320u ^ (x >> 1) : x >> 1; table[i] = x; } init = true; }
        c = ~c; while (n--) c = table[(c ^ *p++) & 255] ^ (c >> 8); return ~c;
    }
    static void chunk(std::FILE* f, const char* type, const uint8_t* d, size_t n) {
        uint8_t head[8]; be32(head, uint32_t(n)); std::memcpy(head + 4, type, 4); std::fwrite(head, 1, 8, f);
        uint32_t c = crc(0, reinterpret_cast<const uint8_t*>(type), 4);
        if (n) { std::fwrite(d, 1, n, f); c = crc(c, d, n); }
        uint8_t tail[4]; be32(tail, c); std::fwrite(tail, 1, 4, f);
    }
    // RGB24 PNG with stored deflate blocks (no zlib); an intermediate that
    // tools/frames_to_raw.py packs into a lossless clip.
    void png(const std::string& path) {
        std::FILE* f = std::fopen(path.c_str(), "wb"); if (!f) throw std::runtime_error("cannot write capture frame");
        static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10}; std::fwrite(sig, 1, 8, f);
        const int w = opt.width, h = opt.height;
        uint8_t ihdr[13]; be32(ihdr, uint32_t(w)); be32(ihdr + 4, uint32_t(h)); ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
        chunk(f, "IHDR", ihdr, 13);
        const size_t row = size_t(w) * 3 + 1, raw = row * size_t(h), blocks = (raw + 65534) / 65535;
        z_.resize(2 + raw + blocks * 5 + 4); size_t zo = 0; z_[zo++] = 0x78; z_[zo++] = 0x01;
        uint32_t a = 1, b = 0; size_t done = 0, in_block = 0;
        for (int y = 0; y < h; ++y) for (size_t x = 0; x <= size_t(w) * 3; ++x) {
            if (!in_block) { size_t take = std::min<size_t>(raw - done, 65535);
                z_[zo] = done + take == raw; z_[zo + 1] = uint8_t(take); z_[zo + 2] = uint8_t(take >> 8); z_[zo + 3] = uint8_t(~take); z_[zo + 4] = uint8_t((~take) >> 8); zo += 5; in_block = take; }
            uint8_t v = 0;
            if (x) { size_t px = (x - 1) / 3, ch = (x - 1) % 3; v = uint8_t(canvas_[size_t(y) * w + px] >> (16 - 8 * ch)); }
            z_[zo++] = v; a = (a + v) % 65521u; b = (b + a) % 65521u; --in_block; ++done;
        }
        be32(&z_[zo], (b << 16) | a); zo += 4;
        chunk(f, "IDAT", z_.data(), zo); chunk(f, "IEND", nullptr, 0); std::fclose(f);
    }
};
