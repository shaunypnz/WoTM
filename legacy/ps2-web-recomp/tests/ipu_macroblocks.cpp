#include "ipu/ipu.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

static void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static std::string read_file(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The IPU reads its bitstream MSB first, so a bit writer fills each qword from the top bit down.
struct BitWriter {
    std::uint8_t *qword;
    unsigned at = 0;
    void put(std::uint32_t value, unsigned bits) {
        for (unsigned i = 0; i < bits; ++i)
            if (value & (1u << (bits - 1 - i)))
                qword[(at + i) / 8] = static_cast<std::uint8_t>(qword[(at + i) / 8] | (0x80u >> ((at + i) % 8)));
        at += bits;
    }
};

int main() {
    namespace fs = std::filesystem;
    using namespace ps2;
    const fs::path dir = fs::temp_directory_path() / ("ps2-ipu-test-" + std::to_string(getpid()));
    fs::create_directories(dir);
    const fs::path err = dir / "stderr.txt";
    const int saved_err = dup(2);
    require(std::freopen(err.c_str(), "w", stderr) != nullptr, "stderr must redirect to the capture file");

    ipu::reset();
    // BCLR moves the bit position and decodes nothing: the marker must stay silent, so a run whose IPU never
    // decoded a macroblock cannot read as one that did.
    ipu::write32(0x10002000, 0x00000000);
    std::fflush(stderr);
    require(read_file(err).find("[ipu] macroblocks decoded") == std::string::npos,
            "a command that decodes nothing must not report decoded macroblocks");

    // One intra macroblock: six blocks, each a DC size of 0, one (run 0, level 1) coefficient and an EOB.
    // BDEC forces all six blocks, so the coded-block pattern is not read from the stream. The luma DC-size code
    // "00" carries a 1-bit differential, the chroma one carries none, so a luma block costs a bit more.
    std::uint8_t qword[16] = {};
    BitWriter bits{qword, 0};
    for (int block = 0; block < 6; ++block) {
        bits.put(0x0, 2);   // DC size code "00": a 1-bit differential for luma, none for chroma
        if (block < 4) bits.put(0x0, 1);
        bits.put(0x3, 2);   // DCT table 0: run 0, level 1
        bits.put(0x0, 1);   // coefficient sign
        bits.put(0x2, 2);   // end of block
    }
    ipu::write_fifo(qword);
    ipu::write32(0x10002000, 0x2C000000);  // BDEC: intra, DC reset, qsc 0
    std::fflush(stderr);
    const std::string log = read_file(err);
    require(log.find("[ipu] macroblocks decoded 1\n") != std::string::npos,
            "the first decoded macroblock must print the gate marker with no trace flag");

    // The decoded macroblock is 16x16 luma plus two 8x8 chroma planes: 768 bytes of 16-bit samples reach the
    // output FIFO, so the marker reports pixels the game can read back, not just a command it wrote.
    std::uint8_t out[16] = {};
    bool any = false;
    for (int i = 0; i < 48; ++i) {
        ipu::read_fifo(out);
        for (std::uint8_t b : out) any |= b != 0;
    }
    require(any, "the decoded macroblock must reach the IPU output FIFO");

    require(dup2(saved_err, 2) == 2, "stderr must be restored after the capture");
    close(saved_err);
    fs::remove_all(dir);
    std::puts("PASS: the IPU marker counts decoded macroblocks and stays silent when none decoded");
}
