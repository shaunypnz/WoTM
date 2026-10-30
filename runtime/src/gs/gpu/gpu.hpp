#pragma once

#include "../gs.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ps2::gs::gpu {

enum class Mode { off, mirror, primary };

// Internal resolution multiplier cap (PS2_SCALE 1..4). VRAM and the frame buffers grow with its square.
constexpr std::uint32_t kMaxScale = 4;

// The window output's optional post pass (PS2_AA=fxaa, PS2_SHARPEN=0..1; F9 cycles presets in the window).
struct PostFilter {
    bool fxaa = false;
    float sharpen = 0.0f;
};

struct DisplaySource {
    std::uint32_t fbp, fbw, psm, dbx, dby, width, height;
    bool halved;
};

struct SurfaceTarget {
    enum Kind { X11, Wayland, Canvas } kind;
    void *display;
    std::uint64_t window;
    void *surface;
    const char *selector;
    std::uint32_t width, height;
};

Mode mode();
bool init(Mode m);
void draw(const State &s, const Vertex *v, int count);
void image_begin(const State &s);
void image_data(const std::uint8_t *data, std::size_t bytes);
void local_copy(const State &s);
void load_clut(std::uint64_t tex0, std::uint64_t texclut);
void flush();
void texflush();
void wait_idle();
void download(std::uint8_t *memory, std::uint32_t offset = 0, std::uint32_t bytes = kMemorySize);
bool download_async(std::uint8_t *memory, std::uint32_t offset, std::uint32_t bytes, std::uint32_t serial);
bool finish_download(std::uint32_t serial);
void read_state(std::vector<std::uint32_t> &vram, std::uint32_t &scale, std::vector<std::uint32_t> &clut);
void write_state(const std::vector<std::uint32_t> &vram, std::uint32_t scale, const std::vector<std::uint32_t> &clut);
bool display(const DisplaySource &src, std::vector<std::uint32_t> &rgba, std::uint32_t &width, std::uint32_t &height);
bool display_async(const DisplaySource &src, std::vector<std::uint32_t> &rgba, std::uint32_t &width, std::uint32_t &height);
bool attach_surface(const SurfaceTarget &target);
void resize_surface(std::uint32_t width, std::uint32_t height);
bool has_surface();
// F9: the next filter preset (none, FXAA, FXAA + sharpening, sharpening), applied at the next present. Any thread.
void cycle_filter();
std::string describe(const PostFilter &f);
bool present(const DisplaySource &src);
double busy_ms();
void stats();

}
