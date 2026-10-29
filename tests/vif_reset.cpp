#include "vif.hpp"
#include "hw.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

static void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main() {
    using namespace ps2;
    vif::reset();
    for (int unit = 0; unit < 2; ++unit) {
        for (unsigned i = 0; i < 4; ++i) {
            vif::write(unit, 0x100 + i * 16, i == 2 ? 4096 : 0);
            vif::write(unit, 0x140 + i * 16, 0x800000 + i);
        }
        // Leave an incomplete V4-32 transfer in the FIFO, then reset it.
        const std::uint32_t incomplete[] = {0x6C010000, 1, 2, 3};
        vif::transfer(unit, reinterpret_cast<const std::uint8_t *>(incomplete), 1);
        vif::write(unit, 0x10, 1);
        for (unsigned i = 0; i < 4; ++i) {
            require(vif::read(unit, 0x100 + i * 16) == (i == 2 ? 4096u : 0u), "FBRST must retain ROW");
            require(vif::read(unit, 0x140 + i * 16) == 0x800000 + i, "FBRST must retain COL");
        }
        // The first unpack after reset must get texture Q from ROW2, and
        // must not be consumed as the tail of the discarded transfer.
        const std::uint32_t unpack[] = {
            0x01000101, 0x20000000, 0x50505050, 0x75014000,
            0x00200010, 0, 0, 0,
        };
        vif::transfer(unit, reinterpret_cast<const std::uint8_t *>(unpack), 2);
        std::uint32_t out[4];
        std::memcpy(out, unit ? g_vu1_mem : g_vu0_mem, sizeof out);
        require(out[0] == 16 && out[1] == 32 && out[2] == 4096 && out[3] == 0,
                "masked unpack after reset must retain Q and discard the old FIFO");
    }
    vif::reset();
    require(vif::read(0, 0x120) == 0 && vif::read(1, 0x120) == 0,
            "full machine reset must still clear unpack registers");
    std::puts("PASS: VIF software reset preserves ROW/COL and discards pending transfers");
}
