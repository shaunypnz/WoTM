#include "kernel.hpp"
#include "hw.hpp"
#include "ps2/memory.hpp"
#include "ps2/system.hpp"

#include <cstdio>
#include <cstdlib>

namespace {
void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

void syscall(unsigned n, unsigned a0 = 0, unsigned a1 = 0) {
    auto &c = ps2::g_cpu;
    c.r[3].w[0] = n;
    c.r[4].w[0] = a0;
    c.r[5].w[0] = a1;
    ps2::kernel::handle_syscall(c, c.pc);
}
}

int main() {
    using namespace ps2;
    hw::reset();
    kernel::reset();
    kernel::start_main_thread(0x100000, 0, 0, 0);
    syscall(0x29, 1, 10); // Main thread yields to a higher priority worker.
    constexpr unsigned descriptor = 0x2000;
    st32(descriptor + 4, 0x110000);
    st32(descriptor + 8, 0x200000);
    st32(descriptor + 12, 0x4000);
    st32(descriptor + 20, 1);
    syscall(0x20, descriptor);
    const unsigned worker = g_cpu.r[2].w[0];
    require(worker > 1, "worker created");
    syscall(0x22, worker);
    require(kernel::current_thread() == static_cast<int>(worker), "worker scheduled");
    require(g_cpu.r[31].w[0] == kIdle, "worker has the thread-return sentinel");
    g_cpu.pc = g_cpu.r[31].w[0]; // Entry function returns normally.
    kernel::service(g_cpu);
    require(kernel::current_thread() == 1, "worker return resumes the ready main thread");
    require(g_cpu.pc == 0x100000, "main resumes its own context");
    for (int i = 0; i < 3; ++i) {
        kernel::wake_idle();
        kernel::service(g_cpu);
        require(kernel::current_thread() == 1, "returned worker remains dormant");
    }
    g_cpu.pc = kIdle; // Last runnable thread returns, leaving real idle.
    kernel::service(g_cpu);
    require(kernel::current_thread() == 0 && g_cpu.pc == kIdle, "last return enters idle");
    kernel::service(g_cpu);
    require(kernel::current_thread() == 0, "idle remains safe without a current thread");
    std::puts("PASS: thread return resumes peers and leaves the returned worker dormant");
}
