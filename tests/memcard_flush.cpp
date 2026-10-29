#include "iop/sio2.hpp"
#include "ps2/system.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

static void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main() {
    namespace fs = std::filesystem;
    using namespace ps2;
    const fs::path dir = fs::temp_directory_path() / ("ps2-mc-test-" + std::to_string(getpid()));
    require(!fs::exists(dir), "test directory must be new");
    g_options.memcard_path = (dir / "card.ps2").string();
    int saved = 0;
    g_options.on_memcard_saved = [&] { ++saved; };
    iop::sio2::reset();
    // Send a one-byte write using the public SIO2 memory-card protocol.
    iop::sio2::write(0x1F808268, 0xC);
    iop::sio2::write(0x1F808200, 2 | (4 << 8) | (8 << 18));
    for (unsigned byte : {0x81u, 0x42u, 1u, 0x5Au}) iop::sio2::write(0x1F808260, byte);
    iop::sio2::flush_cards();
    require(saved == 0, "normal writes should retain the debounce delay");
    iop::sio2::flush_cards(true);
    require(saved == 0, "failed write must not report a saved card");
    fs::create_directory(dir);
    iop::sio2::flush_cards(true);
    require(saved == 1 && fs::file_size(g_options.memcard_path) == 528u * 0x4000u,
            "forced flush must retry pending data after an I/O failure");
    char byte = 0;
    std::ifstream(g_options.memcard_path, std::ios::binary).read(&byte, 1);
    require(byte == 0x5A, "persist the data written by the guest");
    require(!fs::exists(g_options.memcard_path + ".tmp"), "successful save must leave no staging file");
    iop::sio2::flush_cards(true);
    require(saved == 1, "clean cards should not be saved again");
    fs::remove_all(dir);
    std::puts("PASS: card flush preserves pending data on failure and persists it without waiting at shutdown");
}
