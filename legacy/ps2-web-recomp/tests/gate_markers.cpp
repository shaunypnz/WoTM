#include "gs/gs.hpp"
#include "ps2/system.hpp"

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

static std::uint64_t xyz2(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    return x | (static_cast<std::uint64_t>(y) << 16) | (static_cast<std::uint64_t>(z) << 32);
}

int main() {
    namespace fs = std::filesystem;
    using namespace ps2;
    const fs::path dir = fs::temp_directory_path() / ("ps2-marker-test-" + std::to_string(getpid()));
    fs::create_directories(dir);
    const fs::path err = dir / "stderr.txt";
    const int saved_err = dup(2);
    require(std::freopen(err.c_str(), "w", stderr) != nullptr, "stderr must redirect to the capture file");
    g_options.max_frames = 60;
    // One triangle at frame 30, so the same run shows the marker with no geometry and with geometry.
    g_options.on_vblank = [] {
        if (frame_count() != 30) return;
        gs::write(0x00, 8);                        // PRIM: flat-shaded triangle
        gs::write(0x04, xyz2(100, 100, 100));
        gs::write(0x04, xyz2(200, 100, 100));
        gs::write(0x04, xyz2(150, 200, 100));
    };
    // No guest code: leave the synthetic main thread idle and advance events.
    boot(0x100000, {});
    g_cpu.pc = kIdle;
    run();
    std::fflush(stderr);
    require(dup2(saved_err, 2) == 2, "stderr must be restored after the capture");
    close(saved_err);
    const std::string log = read_file(err);
    fs::remove_all(dir);
    require(log.find("[frame] 25 prims 0\n") != std::string::npos, "the frame marker must report prims 0 when nothing is drawn");
    require(log.find("[frame] 50 prims 3\n") != std::string::npos,
            "the frame marker must count the primitives the triangle drew (each vertex after the first closes one)");
    require(log.find("[ee] stopped after 60 frames") != std::string::npos, "the clean-exit marker the gate reads");
    std::puts("PASS: the frame marker counts drawn primitives and still shows the empty case");
}
