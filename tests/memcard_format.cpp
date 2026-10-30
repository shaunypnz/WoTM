#include "iop/sio2.hpp"
#include "ps2/system.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <vector>

static void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static std::uint32_t u32(const std::vector<std::uint8_t> &card, std::size_t page, std::size_t at) {
    std::uint32_t v;
    std::memcpy(&v, &card[page * 528 + at], 4);
    return v;
}

// Reads `bytes` from `page` through the SIO2 memory-card protocol (set page 0x23, read 0x43), as the IOP does.
static std::vector<std::uint8_t> read_page(std::uint32_t page, std::size_t bytes) {
    using namespace ps2::iop::sio2;
    const auto transfer = [](std::initializer_list<unsigned> in, std::size_t out) {
        write(0x1F808268, 0xC);
        write(0x1F808200, 2 | (static_cast<std::uint32_t>(in.size()) << 8) | (static_cast<std::uint32_t>(out) << 18));
        for (unsigned b : in) write(0x1F808260, b);
        std::vector<std::uint8_t> got(out);
        for (auto &b : got) b = static_cast<std::uint8_t>(read(0x1F808264));
        return got;
    };
    transfer({0x81, 0x23, page & 0xFF, (page >> 8) & 0xFF, 0, 0, 0, 0, 0}, 9);
    std::vector<std::uint8_t> got = transfer({0x81, 0x43, static_cast<unsigned>(bytes)}, bytes + 6);
    return {got.begin() + 4, got.begin() + 4 + static_cast<std::ptrdiff_t>(bytes)};
}

int main() {
    namespace fs = std::filesystem;
    using namespace ps2;
    std::vector<std::uint8_t> card;
    iop::sio2::format_card(card);
    require(card.size() == 528u * 0x4000u, "a formatted card is 16384 pages of 528 bytes");
    require(std::memcmp(card.data(), "Sony PS2 Memory Card Format 1.2.0.0", 35) == 0, "superblock magic and version");
    require(u32(card, 0, 0x30) == 8192 && u32(card, 0, 0x34) == 41 && u32(card, 0, 0x38) == 8135 && u32(card, 0, 0x50) == 8,
            "clusters, allocatable range and indirect FAT cluster as the PS2 formatter writes them");
    require(u32(card, 16, 0) == 9 && u32(card, 16, 31 * 4) == 40 && u32(card, 16, 32 * 4) == 0xFFFFFFFFu,
            "the indirect FAT lists the 32 FAT clusters 9-40");
    require(u32(card, 18, 0) == 0xFFFFFFFFu && u32(card, 18, 4) == 0x7FFFFFFFu, "FAT: root directory chain ends, the rest is free");
    require(card[82 * 528 + 0x40] == '.' && card[82 * 528 + 0x41] == 0 && card[83 * 528 + 0x40] == '.' && card[83 * 528 + 0x41] == '.',
            "root directory holds . and ..");

    // ECC: an all-zero chunk is 77 7F 7F (what real cards carry for empty data); every page's spare matches its data
    // and a single flipped bit changes it.
    const std::uint8_t zero[128] = {};
    const auto z = iop::sio2::chunk_ecc(zero);
    require(z[0] == 0x77 && z[1] == 0x7F && z[2] == 0x7F, "ECC of an empty chunk");
    for (std::uint32_t page : {0u, 16u, 18u, 82u, 83u, 1000u, 16383u})
        for (int chunk = 0; chunk < 4; ++chunk) {
            const auto ecc = iop::sio2::chunk_ecc(&card[page * 528 + chunk * 128]);
            require(std::memcmp(&card[page * 528 + 512 + chunk * 3], ecc.data(), 3) == 0, "spare bytes hold each chunk's ECC");
        }
    std::uint8_t one[128] = {};
    one[77] = 0x10;
    require(iop::sio2::chunk_ecc(one) != z, "a flipped bit changes the ECC");

    const fs::path dir = fs::temp_directory_path() / ("ps2-mcfmt-test-" + std::to_string(getpid()));
    fs::create_directory(dir);
    // A missing card file goes in formatted; the guest reads the superblock through SIO2.
    g_options.memcard_path = (dir / "new.ps2").string();
    iop::sio2::reset();
    const std::vector<std::uint8_t> super = read_page(0, 128);
    require(std::memcmp(super.data(), "Sony PS2 Memory Card Format ", 28) == 0, "a new card reads back formatted through SIO2");
    fs::remove_all(dir);
    std::puts("PASS: blank cards go in formatted (superblock, FAT, root directory, ECC) and read back through SIO2");
}
