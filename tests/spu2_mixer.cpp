#include "iop/iop.hpp"
#include "iop/spu2.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
std::vector<std::int16_t> output;

void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

void reset_fixture() {
    ps2::iop::spu2::reset();
    output.clear();
    for (unsigned core = 0; core < 2; ++core) {
        const auto base = core * 0x400;
        ps2::iop::spu2::write(base + 0x188, 0xFFFFFF, 4);
        ps2::iop::spu2::write(base + 0x190, 0xFFFFFF, 4);
        ps2::iop::spu2::write(base + 0x198, core ? 0x0CCC : 0x0CC0, 2);
    }
}

void voice(unsigned core, unsigned index, std::uint16_t waveform = 0x7777) {
    using namespace ps2::iop::spu2;
    constexpr unsigned block = 0x4000;
    ram()[block] = 0x0300; // constant positive ADPCM waveform, loop end
    for (unsigned word = 1; word < 8; ++word) ram()[block + word] = waveform;
    const auto base = core * 0x400;
    const auto reg = base + index * 16;
    write(base + 0x1C0 + index * 12, 0, 2);
    write(base + 0x1C2 + index * 12, block, 2);
    write(base + 0x1C4 + index * 12, 0, 2);
    write(base + 0x1C6 + index * 12, block, 2);
    write(reg, 0x3FFF, 2); write(reg + 2, 0x3FFF, 2);
    write(reg + 4, 0x1000, 2);
    write(reg + 6, 0x00FF, 2); write(reg + 8, 0x1FC0, 2);
    write(base + 0x1A0 + (index / 16) * 2, 1u << (index % 16), 2);
}

long peak(unsigned channel) {
    long result = 0;
    require(output.size() >= 128, "mixer must deliver audio");
    for (unsigned i = 64 + channel; i < output.size(); i += 2)
        result = std::max(result, std::labs(output[i]));
    return result;
}

void run() { ps2::iop::run(800'000); }
}

int main() {
    using namespace ps2::iop::spu2;
    set_sink([](const std::int16_t *samples, std::size_t frames) {
        output.insert(output.end(), samples, samples + frames * 2);
    });

    reset_fixture(); voice(0, 0);
    write(0x788, 0, 2); write(0x78A, 0, 2); // final core-1 master mute
    run();
    require(peak(0) == 0 && peak(1) == 0, "core 0 must pass through the final core-1 master volume");

    reset_fixture(); voice(0, 0);
    write(0x760, 0x2000, 2); write(0x762, 0x2000, 2);
    write(0x790, 0x4000, 2); write(0x792, 0x4000, 2); // core-1 AVOL, half gain
    write(0x788, 0x2000, 2); write(0x78A, 0x2000, 2);
    run();
    require(peak(0) >= 3575 && peak(0) <= 3584 && peak(1) == peak(0),
            "core-0 master, core-1 external gain and core-1 master must all apply");

    for (unsigned core = 0; core < 2; ++core) {
        for (const std::uint16_t waveform : {0x7777, 0x8888}) {
            reset_fixture(); voice(core, 0, waveform); voice(core, 1, waveform);
            write(0x760 + core * 0x28, 0x2000, 2);
            write(0x762 + core * 0x28, 0x2000, 2);
            run();
            require(peak(0) >= 16370 && peak(0) <= 16384 && peak(1) == peak(0),
                    "overloaded voice mix must saturate before master attenuation");
        }

        reset_fixture(); voice(core, 16);
        write(core * 0x400 + 0x18A, 0, 2); // disable high voices on the left only
        run();
        require(peak(0) == 0 && peak(1) > 28000, "VMIX must gate each voice and channel independently");

        reset_fixture(); voice(core, 0);
        write(core * 0x400 + 0x198, 0, 2);
        run();
        require(peak(0) == 0 && peak(1) == 0, "MMIX must mute a core's dry voice output");

        for (unsigned channel = 0; channel < 2; ++channel) {
            reset_fixture();
            write(core * 0x400 + 0x198, channel ? 0x040 : 0x080, 2);
            write(core * 0x400 + 0x1B0, 1u << core, 2);
            constexpr unsigned address = 0x100000;
            for (unsigned i = 0; i < 256; ++i) {
                const std::int16_t left = 6000, right = 12000;
                std::memcpy(ps2::iop::ram_ptr(address + i * 2), &left, 2);
                std::memcpy(ps2::iop::ram_ptr(address + 512 + i * 2), &right, 2);
            }
            dma(core, address, 256, true, -1);
            run();
            require(peak(channel) > 5900 && peak(1 - channel) == 0,
                    "MMIX must gate left and right DMA input independently");
        }
    }

    reset_fixture(); voice(0, 0);
    write(0x598, 0x0CC0, 2); // leave core-1 input/voices enabled, disable external input
    run();
    require(peak(0) == 0 && peak(1) == 0, "MMIX external-input gates must control core-0 routing");
    std::puts("PASS: SPU2 voices saturate before gain and follow VMIX/MMIX and the core-0 to core-1 path");
}
