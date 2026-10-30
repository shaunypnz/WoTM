#include "gs/gs.hpp"
#include "gs/gpu/gpu.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

static void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main() {
    using namespace ps2;
    setenv("PS2_GS", "both", 1);
    gs::init();
    require(gs::gpu::mode() == gs::gpu::Mode::mirror, "test requires the GPU and CPU raster paths");
    gs::write(0x1A, 1);
    gs::write(0x46, 1);
    gs::write(0x40, 1ull << 16);
    gs::write(0x4E, 1ull << 32);
    gs::write(0x4C, (1u << 16) | (gs::PSMCT24 << 24));
    gs::write(0x47, 1u << 14);
    gs::write(0x00, 0);
    gs::write(0x01, 0x80332211);
    gs::write(0x05, 0);
    require(gs::read_pixel(gs::PSMCT24, 0, 1, 0, 0) == 0x332211,
            "CPU: 24-bit framebuffer must ignore destination-alpha test");
    std::vector<std::uint32_t> pixels;
    std::uint32_t width = 0, height = 0;
    require(gs::gpu::display({0, 1, gs::PSMCT24, 0, 0, 1, 1, false}, pixels, width, height), "GPU readback");
    require((pixels[0] & 0xFFFFFF) == 0x332211,
            "GPU: 24-bit framebuffer must ignore destination-alpha test");
    // The same test must still reject a destination with alpha bit 1 in C32.
    gs::write(0x4C, 1u << 16);
    gs::write(0x47, 0);
    gs::write(0x00, 0);
    gs::write(0x01, 0x80665544);
    gs::write(0x05, 16);
    gs::write(0x47, 1u << 14);
    gs::write(0x00, 0);
    gs::write(0x01, 0x80998877);
    gs::write(0x05, 16);
    require(gs::read_pixel(gs::PSMCT32, 0, 1, 1, 0) == 0x80665544,
            "CPU: 32-bit destination alpha must remain active");
    require(gs::gpu::display({0, 1, gs::PSMCT32, 1, 0, 1, 1, false}, pixels, width, height), "GPU C32 readback");
    require((pixels[0] & 0xFFFFFF) == 0x665544, "GPU: 32-bit destination alpha must remain active");
    std::puts("PASS: C24 bypasses destination-alpha testing; C32 retains it on CPU and GPU");
}
