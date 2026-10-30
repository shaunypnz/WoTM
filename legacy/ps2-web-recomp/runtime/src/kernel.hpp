#pragma once

#include "ps2/cpu.hpp"
#include "ps2/system.hpp"

#include <cstdint>
#include <functional>

namespace ps2::state {
class Stream;
}

namespace ps2::kernel {

void state(ps2::state::Stream &s);
std::function<void()> alarm_action(int id);

void reset();
void start_main_thread(std::uint32_t entry, std::uint32_t gp, std::uint32_t argc, std::uint32_t argv);
void handle_syscall(Cpu &c, std::uint32_t pc);
void service(Cpu &c);
bool must_unwind();
bool in_handler();
int current_thread();
void on_vblank_start();
void on_hblank();
void wake_idle();
void dump();

// Whether the guest currently allows interrupts, and whether a handler is running: the INTC_STAT read needs
// both, and the guest reads INTC_STAT a few hundred thousand times a frame waiting for VBlank. Out of line
// that is a call per poll, so both live here.
extern bool g_in_handler;
inline bool interrupts_enabled() { const std::uint32_t status = g_cpu.cop0[12]; return (status & 0x10001u) == 0x10001u && !(status & 6u); }
inline bool interruptible() { return interrupts_enabled() && !g_in_handler; }

}
