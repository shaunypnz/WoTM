#include "ps2/vu_ops.hpp"
#include "vu_jit.hpp"

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

// Synthetic instructions only. Generate both compiler inputs from the same
// microprogram; the tests supply independent expected results for each opcode.
int main(int argc, char **argv) {
    if (argc != 2) return 1;
    namespace fs = std::filesystem;
    const fs::path out = argv[1];
    fs::create_directories(out / "input");
    std::vector<std::uint8_t> code(16384);
    const auto emit = [&](unsigned pc, std::uint32_t lo, bool end = false) {
        const std::uint32_t hi = 0x000002FFu | (end ? 0x40000000u : 0u); // NOP
        std::memcpy(code.data() + pc, &lo, 4);
        std::memcpy(code.data() + pc + 4, &hi, 4);
    };
    const auto flag = [](unsigned op, unsigned it, unsigned is) {
        return (op << 25) | (it << 16) | (is << 11);
    };
    const auto immediate = [](unsigned op, unsigned it) {
        return (op << 25) | (it << 16) | (1u << 21) | 0x24u; // 0x824
    };
    emit(0, flag(0x1B, 2, 1)); // FMOR
    emit(8, immediate(0x17, 3)); // FSOR
    emit(16, flag(0x18, 4, 1)); // FMEQ stays a predicate
    emit(24, immediate(0x14, 5)); // FSEQ stays a predicate
    emit(32, flag(0x1A, 6, 1)); // FMAND
    emit(40, immediate(0x16, 7)); // FSAND
    emit(48, flag(0x1B, 0, 1)); // VI0 must remain zero
    emit(56, immediate(0x17, 0));
    emit(64, 0x13u << 25); // FCOR stays a predicate, unlike FMOR/FSOR
    emit(72, 0x8000033Cu, true); // lower NOP, E bit
    emit(80, 0x8000033Cu);
    const auto key = ps2::vu::program_key(code.data(), ps2::vu::reachable(code.data(), 16383, 0), 0);
    char name[64];
    std::snprintf(name, sizeof name, "%016llX_0000.bin", static_cast<unsigned long long>(key));
    for (const fs::path &p : {out / "input" / name, out / "program.bin"})
        std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char *>(code.data()), code.size());
    std::vector<std::uint8_t> wasm;
    if (!ps2::vu::jit::translate(code.data(), 0, {4096}, wasm)) return 1;
    std::ofstream(out / "program.wasm", std::ios::binary).write(reinterpret_cast<const char *>(wasm.data()), wasm.size());
    std::ofstream(out / "layout.json") << "{\"vi\":" << offsetof(ps2::Vu0Regs, vi)
        << ",\"mac\":" << offsetof(ps2::Vu0Regs, mac) << ",\"status\":" << offsetof(ps2::Vu0Regs, status)
        << ",\"clip\":" << offsetof(ps2::Vu0Regs, clip) << "}";
}
