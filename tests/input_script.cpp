#include "kernel.hpp"
#include "iop/sio2.hpp"
#include "ps2/system.hpp"
#include <cstdio>
#include <cstdlib>

static unsigned poll_buttons(unsigned port) {
    using namespace ps2::iop::sio2;
    write(0x1F808268u, 0xCu);
    write(0x1F808200u, port | (3u << 8) | (9u << 18));
    write(0x1F808260u, 1); write(0x1F808260u, 0x42); write(0x1F808260u, 0);
    unsigned reply[9];
    for (auto &v : reply) v = read(0x1F808264u);
    if (reply[1] != 0x41 || reply[2] != 0x5A) {
        std::fprintf(stderr, "FAIL: scripted controller port %u is disconnected\n", port + 1);
        std::exit(1);
    }
    return (~(reply[3] | (reply[4] << 8))) & 0xffff;
}

int main() {
    using namespace ps2;
    Press legacy{}, second{}, invalid{};
    if (!parse_press("1:start:2", legacy) || legacy.port != 0 ||
        !parse_press("2@5:cross+lleft:2", second) || second.port != 1 ||
        second.buttons != 0x4000 || second.stick != 4 ||
        parse_press("3@5:cross", invalid) || parse_press("0@5:cross", invalid)) return 1;
    g_options.max_frames = 8;
    g_options.presses = {legacy, second};
    g_options.axis_presses = {{2, 2, 2, 0}, {5, 2, 3, 255}};
    g_options.on_vblank = [] {
        const auto frame = frame_count();
        const unsigned want1 = frame >= 1 && frame < 3 ? 8 : 0;
        const unsigned want2 = frame >= 5 && frame < 7 ? 0x4000 : 0;
        if (poll_buttons(0) != want1 || poll_buttons(1) != want2) {
            std::fprintf(stderr, "FAIL: independent controller data at frame %llu\n", static_cast<unsigned long long>(frame));
            std::exit(1);
        }
        const bool expired = frame_count() >= 7;
        if (g_options.presses.empty() != expired || g_options.axis_presses.empty() != expired) {
            std::fprintf(stderr, "FAIL: incorrect input script expiry at frame %llu\n",
                         static_cast<unsigned long long>(frame_count()));
            std::exit(1);
        }
    };
    // No guest code: leave the synthetic main thread idle and advance events.
    boot(0x100000, {});
    g_cpu.pc = kIdle;
    run();
    std::puts("PASS: scripts survive gaps and release manual input after the final hold");
}
