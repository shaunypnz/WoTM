#include "hw.hpp"

#include "dmac.hpp"
#include "gs/gif.hpp"
#include "gs/gs.hpp"
#include "iop/sif.hpp"
#include "ipu/ipu.hpp"
#include "vif.hpp"
#include "vu_micro.hpp"
#include "kernel.hpp"
#include "ps2/memory.hpp"
#include "ps2/system.hpp"

#include "ps2/state.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <vector>

namespace ps2::hw {

// Defined further down; the poll census, the VBlank wait skip and the fast poll path all read through them.
std::uint32_t read_hw32(std::uint32_t a);
std::uint32_t intc_stat_read();

namespace {

constexpr std::uint32_t kIntcStat = 0x1000F000u, kIntcMask = 0x1000F010u;
constexpr std::uint32_t kDmacStat = 0x1000E010u;
// INTC_STAT bit 2 is the VBlank start, and the bit a PS2 game's frame wait spins on: graphdev.c's VSync()
// clears it and re-reads the register until it comes back.
constexpr std::uint32_t kVblankStartBit = 1u << 2;

// g_intc_latched is what the guest reads back: the kernel acknowledging an interrupt before
// running the handlers must not hide it from code that polls INTC_STAT, the way sceGsSyncV
// waits for VBlank start while a VBlank handler is installed. Only a guest write clears it.
std::uint32_t g_intc_stat = 0, g_intc_mask = 0, g_intc_latched = 0;
std::uint32_t g_dmac_stat = 0;
std::map<std::uint32_t, std::uint64_t> g_regs;
std::set<std::uint32_t> g_reported;
std::uint32_t g_crt[3] = {};

void report(const char *what, std::uint32_t a, std::uint64_t v) {
    if (g_options.trace_io || g_reported.insert(a).second)
        std::fprintf(stderr, "[io] %s 0x%08X = 0x%llX (pc 0x%08X)\n", what, a, static_cast<unsigned long long>(v), g_cpu.pc);
}

struct Timer {
    std::uint32_t mode = 0, comp = 0, hold = 0;
    std::uint64_t base = 0;
    std::uint32_t frozen = 0;
};

Timer g_timers[4];

std::uint64_t timer_divider(const Timer &t) {
    switch (t.mode & 3u) {
    case 0: return 2;
    case 1: return 32;
    case 2: return 512;
    default: return hblank_cycles();
    }
}

std::uint32_t timer_count(int n) {
    const Timer &t = g_timers[n];
    if (!(t.mode & 0x80u)) return t.frozen;
    return static_cast<std::uint32_t>(((now() - t.base) / timer_divider(t)) & 0xFFFFu);
}

std::uint32_t timer_read(int n) {
    const std::uint64_t due = scheduled_at(kEventTimer0 + n);
    if (due > now()) return timer_count(n);
    const Timer &t = g_timers[n];
    return static_cast<std::uint32_t>(((due - 1 - t.base) / timer_divider(t)) & 0xFFFFu);
}

void timer_set_count(int n, std::uint32_t value) {
    Timer &t = g_timers[n];
    t.frozen = value & 0xFFFFu;
    t.base = now() - static_cast<std::uint64_t>(value & 0xFFFFu) * timer_divider(t);
}

void timer_schedule(int n);

void timer_event(int n, std::uint64_t at) {
    Timer &t = g_timers[n];
    if (!(t.mode & 0x80u)) return;
    const std::uint32_t count = static_cast<std::uint32_t>(((at - t.base) / timer_divider(t)) & 0xFFFFu);
    if (count == (t.comp & 0xFFFFu)) {
        t.mode |= 0x400u;
        if (t.mode & 0x100u) intc_raise(9u + static_cast<unsigned>(n));
        if (t.mode & 0x40u) t.base = at;
    }
    if (count == 0 && !(t.mode & 0x40u && t.comp == 0)) {
        t.mode |= 0x800u;
        if (t.mode & 0x200u) intc_raise(9u + static_cast<unsigned>(n));
    }
    timer_schedule(n);
}

void timer_schedule(int n) {
    Timer &t = g_timers[n];
    if (!(t.mode & 0x80u) || !(t.mode & 0x300u)) {
        schedule(kEventTimer0 + n, ~0ull, [] {});
        return;
    }
    const std::uint32_t count = timer_count(n);
    std::uint32_t to_compare = ((t.comp & 0xFFFFu) - count) & 0xFFFFu;
    if (!to_compare) to_compare = 0x10000u;
    std::uint32_t to_overflow = 0x10000u - count;
    std::uint32_t ticks = to_compare;
    if ((t.mode & 0x200u) && to_overflow < ticks) ticks = to_overflow;
    if (!(t.mode & 0x100u)) ticks = to_overflow;
    const std::uint64_t div = timer_divider(t);
    const std::uint64_t at = t.base + ((now() - t.base) / div + ticks) * div;
    schedule(kEventTimer0 + n, at, [n, at] { timer_event(n, at); });
}

int timer_index(std::uint32_t a) { return static_cast<int>((a >> 11) & 3u); }

bool is_timer(std::uint32_t a) { return a >= 0x10000000u && a < 0x10002000u && (a & 0x7FFu) < 0x40u; }

// PS2_POLL=PATH: where the EE waits. Every hardware-register read is counted per address together with the
// guest PCs it came from, and the table goes to PATH when the run ends. Reads far above changes means the
// guest spins on that register, and the PCs say which guest function spins - the alternative is a trace of
// every poll, which is far too big to read. PS2_POLL_FROM=<frame> and PS2_POLL_FRAMES=<count> limit the
// census to a window: one table lookup per poll over a whole fight costs enough host time to change the
// frame rate the census exists to explain.
struct PollSite {
    std::uint32_t pc;
    std::uint64_t hits;
};

struct PollStat {
    std::uint64_t reads = 0, changes = 0;
    std::uint32_t last = 0;
    bool seen = false;
    std::vector<PollSite> sites;
};

std::map<std::uint32_t, PollStat> g_poll;

// Who answers a poll of a hardware register: the census while PS2_POLL is on for this frame, the VBlank wait
// skip when the frame is waiting for the next VBlank, nobody otherwise. A hook replaces the read rather than
// wrapping it, so the census counts the same read a normal run answers - and the host cost of the counting is
// the cost of one pointer test on the reads it is not counting.
using PollHook = std::uint32_t (*)(std::uint32_t);
PollHook g_poll_hook = nullptr;
std::uint32_t poll_note(std::uint32_t a);
std::uint32_t wait_ld32(std::uint32_t a);

// Decided once a frame in poll_window(): a hardware read happens a few hundred thousand times a frame in the
// VBlank wait, so the census must cost one pointer test when it is not running.
void poll_window() {
    static const bool wanted = std::getenv("PS2_POLL") != nullptr;
    static const std::uint64_t from = [] { const char *v = std::getenv("PS2_POLL_FROM"); return v ? std::strtoull(v, nullptr, 10) : 0ull; }();
    static const std::uint64_t span = [] { const char *v = std::getenv("PS2_POLL_FRAMES"); return v ? std::strtoull(v, nullptr, 10) : ~0ull; }();
    const std::uint64_t frame = frame_count();
    if (wanted && frame >= from && frame - from < span) g_poll_hook = poll_note;
    else if (g_poll_hook == poll_note) g_poll_hook = nullptr;
}

std::uint32_t poll_note(std::uint32_t a) {
    PollStat &p = g_poll[a];
    const std::uint32_t v = read_hw32(a);
    if (p.seen) p.changes += p.last != v;
    p.last = v;
    p.seen = true;
    ++p.reads;
    const std::uint32_t pc = g_cpu.pc & ~kUnwind;
    for (PollSite &s : p.sites)
        if (s.pc == pc) { ++s.hits; return v; }
    if (p.sites.size() == 8) {
        // Eight slots cannot hold every caller, and the callers that arrive first are not the ones that poll
        // most: age the table so a hot guest PC is never locked out of it. Counts are then approximate for
        // everything that was aged out; the reads total is exact.
        for (PollSite &s : p.sites) --s.hits;
        p.sites.erase(std::remove_if(p.sites.begin(), p.sites.end(), [](const PollSite &s) { return s.hits == 0; }), p.sites.end());
    }
    p.sites.push_back({pc, 1});
    return v;
}

// The VBlank wait skip is on by default (PS2_SKIP_VBLANK_WAIT=0 turns it off), but never while a movie plays: with it
// armed during the PS2 edition's intro movies they stopped reaching the screen (3600 frames presented 180 times, 6
// uploads instead of ~1700, on the RX 570). A frame only skips once the IPU has decoded nothing for kMovieQuiet
// frames. In a fight the wait is a few hundred thousand INTC_STAT reads a frame, and skipping it took the EE thread
// from 14.4-16.4 to 12.6-13.0 host ms per frame on the RX 570 machine (threaded, 2x).
bool g_wait_skip = true;
constexpr std::uint32_t kMovieQuiet = 120;
std::uint32_t g_movie_quiet = kMovieQuiet;
std::uint64_t g_last_macroblocks = 0;
// Armed at the start of a frame's emulation, off again after the first skip: at most one wait per guest frame
// is skipped, because a second wait waits for something the first jump cannot know about.
bool g_wait_armed = false;
std::uint64_t g_wait_skips = 0, g_wait_skipped_cycles = 0;

// A fight runs graphdev.c's VSync() - `REG_INTC_STAT = 4; while (!(REG_INTC_STAT & 4));` - a few hundred
// thousand times a frame. Every iteration is one hardware read through the decode path, and the loop ends on
// the first read after the emulated VBlank comes due. Nothing about the frame depends on the iterations: the
// value they return is decided by the clock, and the clock only moves because they run. So when the register
// still says no VBlank, move the clock to the scheduled VBlank and read again - the instant the last
// iteration would have seen, reached without running the loop.
std::uint32_t wait_ld32(std::uint32_t a) {
    if (g_wait_armed && a == kIntcStat && !(intc_stat_read() & kVblankStartBit)) {
        // The first read of a frame that is still waiting decides that frame: after a jump the wait is over,
        // and a wait that cannot be jumped is not asked again. Everything else the frame polls, including the
        // other 369 000 reads of this register, then costs one false test.
        g_wait_armed = false;
        const std::uint64_t at = scheduled_at(kEventVblank);
        // Only a VBlank that has not come due yet can end this wait, and only guest code can be waiting: the
        // host's own page (syscalls, the handler return trampoline) and a pending interrupt unwind are the
        // emulator moving the clock, not a loop waiting for it.
        if (at > now() && !(g_cpu.pc & kUnwind) && g_cpu.pc >= 0x10000u) {
            ++g_wait_skips;
            g_wait_skipped_cycles += at - now();
            advance_clock(at);
        }
    }
    return read_hw32(a);
}

}

// The poll census and the VBlank wait skip both answer polls, for one frame at a time: the census while
// PS2_POLL covers this frame, otherwise the skip when it is switched on. Decided once a frame in
// frame_begins(), so a poll that is not being counted costs one pointer test.
void frame_begins() {
    poll_window();
    const std::uint64_t macroblocks = ipu::macroblocks();
    if (macroblocks != g_last_macroblocks) {
        g_last_macroblocks = macroblocks;
        g_movie_quiet = 0;
    } else if (g_movie_quiet < kMovieQuiet) {
        ++g_movie_quiet;
    }
    g_wait_armed = g_wait_skip && g_movie_quiet >= kMovieQuiet;
    if (g_wait_skip && !g_poll_hook) g_poll_hook = wait_ld32;
}

void set_wait_skip(bool on) { g_wait_skip = on; }

void reset() {
    g_intc_stat = g_intc_mask = g_intc_latched = g_dmac_stat = 0;
    static const bool skip_wanted = [] { const char *v = std::getenv("PS2_SKIP_VBLANK_WAIT"); return !v || *v != '0'; }();
    g_wait_skip = skip_wanted;
    g_movie_quiet = kMovieQuiet;
    g_last_macroblocks = ipu::macroblocks();
    g_wait_armed = false;
    g_poll_hook = nullptr;
    gs::reset();
    gif::reset();
    vif::reset();
    vu::reset();
    dmac::reset();
    ipu::reset();
    g_regs.clear();
    for (Timer &t : g_timers) t = Timer{};
    g_timers[3].mode = 0x183u;
}

void vblank_start() {
    intc_raise(2);
    gs::vblank(true);
    schedule(kEventVblankEnd, now() + hblank_cycles() * 22u, [] { intc_raise(3); });
}

std::uint32_t intc_pending() { return g_intc_stat & g_intc_mask; }

void intc_raise(unsigned cause) {
    g_intc_stat |= 1u << cause;
    g_intc_latched |= 1u << cause;
    if (g_intc_mask & (1u << cause)) kernel::wake_idle();
}

void intc_ack(unsigned cause) { g_intc_stat &= ~(1u << cause); }

bool intc_set_mask(unsigned cause, bool enabled) {
    const std::uint32_t bit = 1u << cause;
    const bool was = g_intc_mask & bit;
    if (enabled) g_intc_mask |= bit;
    else g_intc_mask &= ~bit;
    return was != enabled;
}

std::uint32_t dmac_pending() {
    const std::uint32_t cis = g_dmac_stat & 0x3FFu, cim = (g_dmac_stat >> 16) & 0x3FFu;
    return cis & cim;
}

void dmac_ack(unsigned channel) { g_dmac_stat &= ~(1u << channel); }

bool dmac_set_mask(unsigned channel, bool enabled) {
    const std::uint32_t bit = 0x10000u << channel;
    const bool was = g_dmac_stat & bit;
    if (enabled) g_dmac_stat |= bit;
    else g_dmac_stat &= ~bit;
    return was != enabled;
}

std::uint64_t gs_imr() { return gs::read_privileged(0x12001010u); }
std::uint64_t gs_csr() { return gs::read_privileged(0x12001000u); }
void gs_set_imr(std::uint64_t value) { gs::write_privileged(0x12001010u, value); }

void gs_set_crt(std::uint32_t interlace, std::uint32_t mode, std::uint32_t field) {
    g_crt[0] = interlace;
    g_crt[1] = mode;
    g_crt[2] = field;
    // The real syscall programs SMODE2; recording its arguments alone leaves
    // the previous movie's frame/field display mode active after a reset.
    gs::write_privileged(0x20, (interlace & 1u) | ((field & 1u) << 1));
    std::fprintf(stderr, "[gs] SetGsCrt interlace %u mode 0x%X field %u\n", interlace, mode, field);
    if (!g_options.rate_forced) g_options.pal = mode == 3u;
}

void dmac_raise(unsigned channel) {
    g_dmac_stat |= 1u << channel;
    if (g_dmac_stat & (0x10000u << channel)) kernel::wake_idle();
}

std::uint32_t read32(std::uint32_t a) { return g_poll_hook ? g_poll_hook(a) : read_hw32(a); }

std::uint32_t intc_stat_read() { return g_intc_stat | (kernel::interruptible() ? g_intc_latched : 0u); }

std::uint32_t read_hw32(std::uint32_t a) {
    // Ordered by what a fight actually reads rather than by register order: the VBlank wait spins on INTC_STAT
    // and the drawing wait on the VIF1 status register, and everything past them is reached through more
    // range checks.
    if (a == kIntcStat) return intc_stat_read();
    if (a >= 0x10003C00u && a < 0x10004000u) return vif::read(1, a & 0x3FFu);
    if (a == kIntcMask) return g_intc_mask;
    if (a == kDmacStat) return g_dmac_stat;
    if (a >= 0x12000000u && a < 0x12002000u) return static_cast<std::uint32_t>(gs::read_privileged(a & ~7u) >> ((a & 4u) * 8));
    if (dmac::owns(a)) return dmac::read(a);
    if (a >= 0x10003000u && a < 0x10003800u) return gif::read(a);
    if (a >= 0x10003800u && a < 0x10003C00u) return vif::read(0, a & 0x3FFu);
    if (a >= 0x10002000u && a < 0x10002040u) return ipu::read32(a);
    if (is_timer(a)) {
        const int n = timer_index(a);
        switch (a & 0x30u) {
        case 0x00: return timer_read(n);
        case 0x10: return g_timers[n].mode;
        case 0x20: return g_timers[n].comp;
        default: return g_timers[n].hold;
        }
    }
    if (a >= 0x1000F200u && a < 0x1000F240u) return sif::get_reg(((a >> 4) & 7u) + 1u);
    if (a == 0x1000F130u) return 0;
    if (a == 0x1000F520u) return static_cast<std::uint32_t>(g_regs[0x1000F590u]);
    auto it = g_regs.find(a);
    const std::uint64_t v = it == g_regs.end() ? 0 : it->second;
    report("read", a, v);
    return static_cast<std::uint32_t>(v);
}

void write32(std::uint32_t a, std::uint32_t v) {
    if (a == kIntcStat) { g_intc_stat &= ~v; g_intc_latched &= ~v; return; }
    if (a == kIntcMask) { g_intc_mask ^= v; return; }
    if (a == kDmacStat) {
        g_dmac_stat &= ~(v & 0xFFFFu);
        g_dmac_stat ^= v & 0xFFFF0000u;
        return;
    }
    if (a >= 0x12000000u && a < 0x12002000u) {
        if (!(a & 4u)) gs::write_privileged(a, v);
        return;
    }
    if (dmac::owns(a)) { dmac::write(a, v); return; }
    if (a >= 0x10003000u && a < 0x10003800u) { gif::write(a, v); return; }
    if (a >= 0x10003800u && a < 0x10003C00u) { vif::write(0, a & 0x3FFu, v); return; }
    if (a >= 0x10003C00u && a < 0x10004000u) { vif::write(1, a & 0x3FFu, v); return; }
    if ((a >= 0x10002000u && a < 0x10002040u) || (a >= 0x10007010u && a < 0x10007020u)) { ipu::write32(a, v); return; }
    if (is_timer(a)) {
        const int n = timer_index(a);
        Timer &t = g_timers[n];
        switch (a & 0x30u) {
        case 0x00: timer_set_count(n, v); break;
        case 0x10: {
            const std::uint32_t count = timer_count(n);
            t.mode = (t.mode & 0xC00u & ~(v & 0xC00u)) | (v & 0x3FFu);
            timer_set_count(n, count);
            break;
        }
        case 0x20: t.comp = v & 0xFFFFu; break;
        default: t.hold = v & 0xFFFFu; break;
        }
        timer_schedule(n);
        return;
    }
    if (a >= 0x1000F200u && a < 0x1000F240u) { sif::set_reg(((a >> 4) & 7u) + 1u, v); return; }
    report("write", a, v);
    g_regs[a] = v;
}

std::uint64_t read64(std::uint32_t a) {
    if (a >= 0x12000000u && a < 0x12002000u) return gs::read_privileged(a);
    return read32(a) | (static_cast<std::uint64_t>(read32(a + 4)) << 32);
}

void write64(std::uint32_t a, std::uint64_t v) {
    if (a >= 0x12000000u && a < 0x12002000u) {
        gs::write_privileged(a, v);
        return;
    }
    write32(a, static_cast<std::uint32_t>(v));
    write32(a + 4, static_cast<std::uint32_t>(v >> 32));
}

void read128(std::uint32_t a, Reg128 &out) {
    std::memset(&out, 0, sizeof out);
    if (a >= 0x10005000u && a < 0x10006000u) {
        gs::read_image(reinterpret_cast<std::uint8_t *>(&out), 1);
        return;
    }
    if (a >= 0x10007000u && a < 0x10007010u) {
        ipu::read_fifo(reinterpret_cast<std::uint8_t *>(&out));
        return;
    }
    report("read128", a, 0);
}

void write128(std::uint32_t a, const Reg128 &v) {
    const auto *bytes = reinterpret_cast<const std::uint8_t *>(&v);
    if (a >= 0x10004000u && a < 0x10005000u) { vif::transfer(0, bytes, 1); return; }
    if (a >= 0x10005000u && a < 0x10006000u) { vif::transfer(1, bytes, 1); return; }
    if (a >= 0x10006000u && a < 0x10007000u) { gif::transfer(3, bytes, 1); return; }
    if (a >= 0x10007010u && a < 0x10007020u) { ipu::write_fifo(bytes); return; }
    report("write128", a, v.d[0]);
}

}

namespace ps2::hw {

std::function<void()> event_action(int id, std::uint64_t at) {
    if (id == kEventVblankEnd) return [] { intc_raise(3); };
    if (id >= kEventTimer0 && id <= kEventTimer3) return [n = id - kEventTimer0, at] { timer_event(n, at); };
    return [] {};
}

// The census table, biggest poll first: address, reads, value changes, and the guest PCs behind them.
void poll_report() {
    const char *path = std::getenv("PS2_POLL");
    if (!path || g_poll.empty()) return;    std::FILE *f = std::fopen(path, "w");
    if (!f) return;
    std::vector<std::pair<std::uint32_t, PollStat>> rows(g_poll.begin(), g_poll.end());
    std::sort(rows.begin(), rows.end(), [](const auto &x, const auto &y) { return x.second.reads > y.second.reads; });
    for (const auto &[address, p] : rows) {
        std::fprintf(f, "0x%08X reads %llu changes %llu", address, static_cast<unsigned long long>(p.reads),
                     static_cast<unsigned long long>(p.changes));
        for (const PollSite &s : p.sites) std::fprintf(f, " pc 0x%08X:%llu", s.pc, static_cast<unsigned long long>(s.hits));
        std::fprintf(f, "\n");
    }
    std::fclose(f);
}

// Whether the VBlank wait skip was live in this run - a run that shows zero skips never reached the wait, so
// its frame times say nothing about the skip.
void wait_report() {
    std::fprintf(stderr, "[io] vblank waits skipped %llu (%llu guest cycles)\n", static_cast<unsigned long long>(g_wait_skips),
                 static_cast<unsigned long long>(g_wait_skipped_cycles));
}

void state(ps2::state::Stream &s) {
    s.tag("eehw");
    s.pod(g_intc_stat);
    s.pod(g_intc_mask);
    s.pod(g_dmac_stat);
    s.map(g_regs);
    s.pod(g_crt);
    s.pod(g_timers);
}

}

// memory.hpp's lane for the register area: a guest 32-bit load of 0x10000000-0x1000FFFF reaches here with one
// range test already done, instead of walking the general load path to the same register file. One call
// answers it - the hook that owns polls this frame, or the register files themselves.
namespace ps2 {

std::uint32_t poll_ld32(std::uint32_t a) { return hw::g_poll_hook ? hw::g_poll_hook(a) : hw::read_hw32(a); }

}
