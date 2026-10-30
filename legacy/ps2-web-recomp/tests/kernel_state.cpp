// The EE kernel state must carry kernel values and nothing else. Thread and Alarm have padding holes (the bytes
// a compiler inserts to align order and context), no code ever writes them, and they hold whatever the heap or
// stack held before. Written as whole structs those bytes land in the state: two boots of one input stop
// matching byte for byte and a saved state is not reproducible - which is what the fight-length comparison
// depends on. So the section is pinned to one field per line (the record below), and threads and alarms are
// re-created on heap chunks poisoned with 0xAA to show no leftovers reach the stream either.
#include "kernel.hpp"
#include "hw.hpp"
#include "ps2/memory.hpp"
#include "ps2/state.hpp"
#include "ps2/system.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
constexpr unsigned char kPoison = 0xAA;
constexpr std::size_t kContextBytes = 34 * 16 + 36 * 4;  // ThreadContext: 32 r + hi + lo, then sa/pc, f[32], facc, fcr31

void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

void syscall(unsigned n, unsigned a0 = 0, unsigned a1 = 0, unsigned a2 = 0) {
    auto &c = ps2::g_cpu;
    c.r[3].w[0] = n;
    c.r[4].w[0] = a0;
    c.r[5].w[0] = a1;
    c.r[6].w[0] = a2;
    ps2::kernel::handle_syscall(c, c.pc);
}

// Free blocks filled with a byte no kernel value in this test uses, so the next kernel allocation lands on it.
void poison_allocator() {
    for (int size = 32; size <= 1024; size += 16)
        std::vector<unsigned char> block(size, kPoison);
}

void put(std::vector<std::uint8_t> &out, std::uint32_t v) {
    for (int byte = 0; byte < 4; ++byte) out.push_back(static_cast<std::uint8_t>(v >> (byte * 8)));
}

// The wire record for one thread: map key, the 15 4-byte fields in the order kernel.cpp declares them, the 8-byte
// order, then the context with no holes between them. A whole-struct write shifts this by 12 bytes.
std::vector<std::uint8_t> thread_record(std::uint32_t id, std::uint32_t status, std::uint32_t entry,
                                        std::uint32_t gp) {
    std::vector<std::uint8_t> out;
    for (std::uint32_t v : {id, id, status, 0u, 0u, entry, 0u, 0u, gp, 0u, 0u, 0u, 0u, 0u, 0u, 0u}) put(out, v);
    out.insert(out.end(), 8, 0);            // order, still 0 before the first reschedule
    out.insert(out.end(), kContextBytes, 0);  // context, zeroed by StartThread-free creation
    return out;
}

std::vector<std::uint8_t> save() {
    ps2::state::Writer w;
    ps2::kernel::state(w);
    return w.out;
}
}

int main() {
    using namespace ps2;
    constexpr unsigned descriptor = 0x2000;
    st32(descriptor + 4, 0x110000);   // entry
    st32(descriptor + 8, 0x200000);   // stack
    st32(descriptor + 12, 0x4000);    // stack size
    st32(descriptor + 16, 0x300000);  // gp
    st32(descriptor + 20, 1);         // priority

    hw::reset();
    kernel::reset();
    kernel::start_main_thread(0x100000, 0x300000, 0, 0);

    // One main thread, no other kernel state: the whole section is the header, one record and the scalars.
    const auto single = save();
    const auto record = thread_record(1, 1, 0x100000, 0x300000);
    const std::size_t tail = 4 * 4 + 4 + 4 + 8 + 1 + 4 + 4 + 4 + 4 + 4 + 4;  // empty maps/vecs, then the kernel scalars
    require(record.size() == 4 * 16 + 8 + kContextBytes, "the thread record is the field list, no holes");
    require(single.size() == 8 + 4 + record.size() + tail, "the EE kernel section is as long as its fields");
    require(std::search(single.begin(), single.end(), record.begin(), record.end()) != single.end(),
            "the thread record appears field by field, not as a padded struct");

    for (int round = 0; round < 24; ++round) {
        poison_allocator();
        syscall(0x20, descriptor);  // CreateThread
        const int worker = static_cast<int>(g_cpu.r[2].sw[0]);
        require(worker > 1, "worker created on a recycled chunk");
        syscall(0x22, static_cast<unsigned>(worker), 0);  // StartThread fills in its context
        syscall(0x18, 0x10u, 0x120000u, 0x33u);           // SetAlarm
        const int alarm = static_cast<int>(g_cpu.r[2].sw[0]);
        require(alarm >= 0, "alarm set");
        if (round % 2) {  // leave half of them live, recycle the rest so their chunks carry the poison forward
            syscall(0x21, static_cast<unsigned>(worker));  // DeleteThread
            syscall(0x19, static_cast<unsigned>(alarm));   // ReleaseAlarm
        }
    }

    const auto first = save();
    require(!first.empty(), "the EE kernel section is not empty");
    require(std::find(first.begin(), first.end(), kPoison) == first.end(),
            "the EE kernel state holds no heap leftovers");

    const auto second = save();
    require(first == second, "the EE kernel state repeats across two saves");

    state::Reader reader(first.data(), first.size());
    reader.set_version(4);  // machine_state puts the format version in its header; a section test has to say it
    kernel::state(reader);
    const auto third = save();
    require(first == third, "the EE kernel state is unchanged by a save and a load");

    std::printf("PASS: the EE kernel state holds only kernel values (%zu bytes, record %zu, poison-free)\n",
                first.size(), record.size());
}
