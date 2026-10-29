#include "vu_micro.hpp"
#include "hw.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

void register_generated_vu();

static void require(bool ok, const char *what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}

int main(int argc, char **argv) {
    using namespace ps2;
    // This test deliberately exercises both paths, regardless of diagnostic
    // overrides in the developer's shell.
    unsetenv("PS2_VU_EXACT");
    unsetenv("PS2_VU_INTERPRET");
    unsetenv("PS2_VU_JIT");
    require(argc == 2, "synthetic program path");
    std::ifstream input(argv[1], std::ios::binary);
    input.read(reinterpret_cast<char *>(g_vu1_code), sizeof g_vu1_code);
    require(input.gcount() == sizeof g_vu1_code, "complete synthetic program");
    std::memcpy(g_vu0_code, g_vu1_code, sizeof g_vu0_code);
    register_generated_vu();
    vu::code_changed(0);
    vu::code_changed(1);
    const auto interpreted = vu::interpreted_calls();
    for (int unit = 0; unit < 2; ++unit) {
        for (const std::uint32_t mac : {0u, 0x20u, 0x10u, 0xFFFFu, 0x12345678u}) {
            for (const std::uint32_t status : {0u, 0x20u, 0x824u, 0xFFFu, 0xABCDEu}) {
                for (const std::uint16_t operand : {0u, 0x10u, 0x20u, 0xFFFFu}) {
                    for (const std::uint32_t clip : {0u, 0xFFFFFFu}) {
                        auto &r = vu::regs(unit);
                        r = {};
                        r.vf[0].w[3] = 0x3F800000u;
                        r.mac = mac;
                        r.status = status;
                        r.clip = clip;
                        r.vi[1] = operand;
                        vu::run(unit, 0);
                        require(r.vi[2] == ((mac & 0xFFFFu) | operand), "FMOR preserves all flag and operand bits");
                        require(r.vi[3] == ((status & 0xFFFu) | 0x824u), "FSOR preserves bits and masks status");
                        require(r.vi[4] == ((mac & 0xFFFFu) == operand), "FMEQ remains boolean");
                        require(r.vi[5] == ((status & 0xFFFu) == 0x824u), "FSEQ remains boolean");
                        require(r.vi[6] == ((mac & 0xFFFFu) & operand), "FMAND remains bitwise");
                        require(r.vi[7] == ((status & 0xFFFu) & 0x824u), "FSAND remains bitwise");
                        require(r.vi[1] == (clip == 0xFFFFFFu), "FCOR remains boolean");
                        require(r.vi[0] == 0 && r.tpc == 88, "zero register and E delay slot");
                    }
                }
            }
        }
    }
    require(vu::interpreted_calls() == interpreted, "VU1 exercised generated C++, without interpreter fallback");
    std::puts("PASS: 200 flag cases each through interpreter and generated C++");
}
