#include "hw.hpp"
#include "ps2/memory.hpp"
#include "ps2/system.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>

// The fight's frame wait, measured the way the game runs it. graphdev.c's sceGsSyncV calls VSync(), which is
// `REG_INTC_STAT = 4; do { u = REG_INTC_STAT; } while (!(u & 4));` - a few hundred thousand hardware reads a
// frame in a fight, each one a host call into the register decode path, and the value it waits for is decided
// only by the emulated clock reaching the scheduled VBlank. hw.cpp answers that wait by moving the clock
// instead of running the loop. This test counts what the wait costs with the skip on and with it off, so a
// change that removes the skip - or never arms it - fails here instead of turning back into frames.
//
// The loop is imitated in host code because this runtime has no EE interpreter: the game's own loop is
// generated C++. What stays faithful is the per-iteration effect on the emulated clock (the loop body's
// cycles, spent through the same budget the guest runs on) and the register access itself, which goes through
// ps2::ld32 exactly as the generated code does.

static void require(bool ok, const char *what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}

namespace {

using namespace ps2;

constexpr std::uint32_t kIntcStat = 0x1000F000u;
constexpr std::uint32_t kVblankStart = 1u << 2;
constexpr std::uint64_t kFrameNtsc = kEeClock / 60u;
// One VSync() loop body: the load, the mask, the branch. The exact guest cycle count does not matter - the
// number under test is how many iterations the wait takes, not how long each one pretends to be.
constexpr std::int64_t kLoopCycles = 6;

struct Result {
    std::uint64_t reads = 0;
    double ms = 0.0;
};

Result g_skip_on{}, g_skip_off{};

// Inside a VBlank action no next VBlank is scheduled yet - vblank() schedules it after its own work - so the
// frame wait's target is scheduled here with the runtime's own action for it.
void await_next_vblank() {
    const std::uint64_t at = now() + kFrameNtsc;
    schedule(hw::kEventVblank, at, ps2::event_action(hw::kEventVblank, at));
}

// Run the wait until it sees the VBlank, and say what it cost.
void wait_once(Result &out) {
    // The guest that waits here is in graphdev.c's VSync(), and the skip only trusts a wait that looks like
    // guest code: hold the PC the real loop holds for the length of the measurement.
    const std::uint32_t saved_pc = g_cpu.pc;
    g_cpu.pc = 0x246980;
    hw::write32(kIntcStat, kVblankStart);   // REG_INTC_STAT = 4: the guest acknowledges VBlank before waiting
    await_next_vblank();
    const auto start = std::chrono::steady_clock::now();
    std::uint64_t reads = 0;
    std::uint32_t value = 0;
    for (; reads < 20u << 20; ++reads) {
        value = ld32(kIntcStat);
        if (value & kVblankStart) break;
        if (g_cpu.budget < kLoopCycles) preempt(g_cpu);   // hand out the next slice, running what comes due
        g_cpu.budget -= static_cast<std::int32_t>(kLoopCycles);   // the loop body spends them: the clock moves
    }
    g_cpu.pc = saved_pc;
    out.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    out.reads = reads + 1;
    std::fprintf(stderr, "[test] wait cost %llu reads, %.3f ms (frame %llu)\n", static_cast<unsigned long long>(out.reads),
                 out.ms, static_cast<unsigned long long>(frame_count()));
    require(value & kVblankStart, "the wait must end on a VBlank, whatever it cost");
}

}

int main() {
    using namespace ps2;
    // The skip is on by default (PS2_SKIP_VBLANK_WAIT=0 turns it off); start this boot with it off so the unskipped wait is measured first, and
    // switch it on between frames, which is where a frame's poll hook is chosen.
    setenv("PS2_SKIP_VBLANK_WAIT", "0", 1);
    g_options.pal = false;
    g_options.on_vblank = [] {
        const std::uint64_t n = frame_count();
        if (n == 2) wait_once(g_skip_off);        // one poll per loop iteration of clock
        if (n == 3) hw::set_wait_skip(true);
        if (n == 5) wait_once(g_skip_on);         // the clock jumps to the VBlank the loop is waiting for
    };
    boot(0x100000, {});
    g_cpu.pc = kIdle;                             // no guest code here: the events move the clock
    g_options.max_frames = 8;
    run();

    std::fprintf(stderr, "[test] without the skip: %llu reads, %.1f ms\nwith the skip: %llu reads, %.3f ms\n",
                 static_cast<unsigned long long>(g_skip_off.reads), g_skip_off.ms,
                 static_cast<unsigned long long>(g_skip_on.reads), g_skip_on.ms);
    require(g_skip_off.reads > 100000, "the unskipped wait must crawl: one poll per loop iteration of clock");
    require(g_skip_on.reads <= 4, "the skipped wait must end on the first polls, not on a hundred thousand");
    require(g_skip_on.ms * 100.0 < g_skip_off.ms, "the skip must remove most of the wait's host time");
    // A fight waits like this once a frame, and the frame budget is 16.7 ms of host time.
    require(g_skip_on.ms < 1.0, "one frame's VBlank wait must cost far under the frame budget");
    std::puts("PASS: the VBlank wait costs a clock jump with the skip and a hundred thousand polls without it");
}
