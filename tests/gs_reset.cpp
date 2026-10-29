#include "gs/gs.hpp"
#include "gs/queue.hpp"
#include "hw.hpp"

#include <cstdio>
#include <cstdlib>

static void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main() {
    using namespace ps2;
    gs::init();
    gs::write_pixel(gs::PSMCT32, 0, 1, 0, 0, 0x12345678);
    gs::write(0x22, 3);
    gs::write(0x4C, 0x1000046);
    gs::write_privileged(0, 0x8066);
    gs::write_privileged(0x20, 3);
    gs::g_state.count = 2;
    gs::write_privileged(0x1000, 1);
    require(gs::g_state.scanmsk == 3, "interrupt acknowledgement must not reset drawing state");
    gs::write_privileged(0x1000, 0x200);
    require(gs::g_state.scanmsk == 0 && gs::g_state.ctx[0].frame == 0 && gs::g_state.count == 0,
            "CSR RESET must clear the scan mask and drawing environment");
    require(gs::display_regs().pmode == 0 && gs::display_regs().smode2 == 0,
            "CSR RESET must clear privileged display state");
    require(gs::read_pixel(gs::PSMCT32, 0, 1, 0, 0) == 0x12345678,
            "register reset must preserve local video memory");
    hw::gs_set_crt(1, 2, 0);
    require(gs::display_regs().smode2 == 1, "SetGsCrt must select interlaced field mode");
    hw::gs_set_crt(1, 2, 1);
    require(gs::display_regs().smode2 == 3, "SetGsCrt must select interlaced frame mode");
    gs::write(0x22, 2);
    gs::start_thread();
    gs::write_privileged(0x1000, 0x200);
    require(gs::g_state.scanmsk == 2, "queued reset must respect render command ordering");
    gs::render_pending([](const gs::DisplayRegs &) {}, 1000);
    require(gs::g_state.scanmsk == 0, "render worker must consume the register reset");
    std::puts("PASS: GS reset clears stale movie state, preserves VRAM and obeys render ordering");
}
