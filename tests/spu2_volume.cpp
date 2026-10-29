#include "iop/iop.hpp"
#include "iop/spu2.hpp"

#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

// Voice volume and ADSR are independent gains, including when VOL's sweep bit is clear.
// BVOL (DMA input) is signed Q15; unlike fixed voice/master volume it has no sweep bit.
// Reference: PCSX2 pcsx2/SPU2/Mixer.cpp MixVoice / spu2Mix and RegTable.cpp (0x76c).

static void require(bool ok, const std::string &message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message.c_str()); std::exit(1); }
}

namespace {

constexpr std::uint32_t kBlock = 0x4000;      // ADPCM data above the SPU2's live mixer/input rings
constexpr std::uint16_t kVol = 0x3479;        // fixed volume, the value the game writes
constexpr std::uint32_t kVoiceReg = 0x0;      // core 0, voice 0: VOL_L
constexpr std::uint32_t kAddrReg = 0x1C0;     // core 0, voice 0: SSA / LSA / NA

// AutoDMA packs each 256-frame block as 256 left samples followed by 256 right samples.
void write_stream(std::uint32_t madr, std::uint32_t frames) {
    for (std::uint32_t i = 0; i < frames; ++i) {
        const std::int16_t v = static_cast<std::int16_t>((i / 54) % 2 ? 30000 : -30000);
        const auto block = madr + (i / 256) * 1024;
        std::memcpy(ps2::iop::ram_ptr(block + (i % 256) * 2), &v, 2);
        const std::int16_t right = -v;
        std::memcpy(ps2::iop::ram_ptr(block + 512 + (i % 256) * 2), &right, 2);
    }
}

long peak(const std::vector<std::int16_t> &samples, unsigned channel) {
    long result = 0;
    for (std::size_t i = channel; i < samples.size(); i += 2)
        result = std::max(result, std::labs(samples[i]));
    return result;
}

}

int main() {
    using namespace ps2;
    std::vector<std::int16_t> got;
    iop::spu2::set_sink([&got](const std::int16_t *samples, std::size_t frames) {
        got.insert(got.end(), samples, samples + frames * 2);
    });
    iop::spu2::reset();

    // An ADPCM block of 28 samples per word group, filter 0 and shift 0, so the decoded sample is the nibble
    // shifted up: 0x7 and 0x8 alternate into a square wave of about +-30000.
    std::uint16_t *ram = iop::spu2::ram();
    for (std::uint32_t b = 0; b < 64; ++b) {
        const std::uint32_t at = kBlock + b * 8;
        ram[at] = b == 63 ? 0x0300 : 0x0000;                 // shift/filter 0; last block loops
        for (unsigned w = 0; w < 7; ++w) ram[at + 1 + w] = 0x8787;
    }
    iop::spu2::write(kAddrReg, kBlock >> 16, 2);            // SSA high
    iop::spu2::write(kAddrReg + 2, kBlock & 0xFFFF, 2);     // SSA low
    iop::spu2::write(kAddrReg + 4, kBlock >> 16, 2);        // LSAX high
    iop::spu2::write(kAddrReg + 6, kBlock & 0xFFFF, 2);     // LSAX low
    iop::spu2::write(kVoiceReg + 6, 0x80FF, 2);             // ADSR1: exponential attack, rate 0
    iop::spu2::write(kVoiceReg + 8, 0x9FC0, 2);             // ADSR2: as the game sets it
    iop::spu2::write(kVoiceReg + 4, 0x1000, 2);             // unity pitch, exercise both sample polarities
    iop::spu2::write(kVoiceReg, kVol, 2);                   // VOL_L
    iop::spu2::write(kVoiceReg + 2, kVol, 2);               // VOL_R
    iop::spu2::write(0x1A0, 1, 2);                          // KEYON voice 0

    // run() takes EE cycles and charges the IOP clock a eighth of them: 320M is about a second of sound.
    for (int i = 0; i < 40; ++i) iop::run(8'000'000);
    require(got.size() > 4000, "the voice must reach the audio sink (got " + std::to_string(got.size()) + " halves)");
    long peak_voice = 0;
    for (std::size_t i = 2000; i < got.size(); i += 2) peak_voice = std::max(peak_voice, std::labs(got[i]));
    // With ADSR near full scale, the decoded peak is multiplied by the fixed volume.
    const long want = 32768L * (kVol << 1) / 32768;
    require(peak_voice >= want * 4 / 5, "a fixed-volume voice must reach its volume times its sample (peak " +
                                            std::to_string(peak_voice) + ", want about " + std::to_string(want) + ")");

    // Holding the attack at rate 127 leaves ADSR at zero for this short interval.
    // A fixed-volume voice must therefore be silent, not bypass its envelope.
    iop::spu2::write(kVoiceReg + 6, 0x7FFF, 2);
    iop::spu2::write(0x1A0, 1, 2);
    got.clear();
    iop::run(800'000);
    require(iop::spu2::read(kVoiceReg + 0xA, 2) == 0, "slow attack fixture must have a zero envelope");
    require(peak(got, 0) == 0 && peak(got, 1) == 0, "fixed voice volume must respect a zero ADSR envelope");

    // Restart at full level, then release with the slowest linear rate. While
    // the release is still active, force the envelope to 1/4 and check both ears.
    iop::spu2::write(kVoiceReg + 6, 0x80FF, 2);
    iop::spu2::write(kVoiceReg + 8, 0x9FDF, 2);
    iop::spu2::write(0x1A0, 1, 2);
    iop::run(800'000);
    iop::spu2::write(0x1A4, 1, 2);
    iop::spu2::write(kVoiceReg + 0xA, 0x2000, 2);
    got.clear();
    iop::run(800'000);
    const long quarter = want / 4;
    require(peak(got, 0) >= quarter - 4 && peak(got, 0) <= quarter + 4 &&
            peak(got, 1) >= quarter - 4 && peak(got, 1) <= quarter + 4,
            "fixed-volume release must follow ADSR in both channels (peak " + std::to_string(peak(got, 0)) + ")");

    // The DMA input path is the other half of the mix: it must still come through at its volume.
    // Check full, half, muted and negative gain on both cores, with distinct stereo channels.
    for (unsigned core = 0; core < 2; ++core) {
        for (const std::uint16_t gain : {0x7FFF, 0x3FFF, 0x0000, 0xC000}) {
            got.clear();
            iop::spu2::reset();
            iop::spu2::write(0x76C + core * 0x28, gain, 2);         // BVOL_L
            iop::spu2::write(0x76E + core * 0x28, gain, 2);         // BVOL_R
            iop::spu2::write(0x1B0 + core * 0x400, 1u << core, 2); // ADMA input on
            constexpr std::uint32_t kMadr = 0x100000;
            write_stream(kMadr, 4096);
            iop::spu2::dma(core, kMadr, 4096, true, -1);            // 4096 words = 4096 stereo frames
            for (int i = 0; i < 40; ++i) iop::run(8'000'000);
            require(got.size() >= 4096 * 2, "DMA output must reach the sink");
            const auto expected_sample = [core, gain](long source) {
                auto value = source * static_cast<std::int16_t>(gain) >> 15;
                if (core == 0) {
                    value = value * 32766 >> 15; // core-0 fixed master 0x3fff
                    value = value * 32767 >> 15; // core-1 external gain 0x7fff
                }
                return value * 32766 >> 15;     // final core-1 fixed master 0x3fff
            };
            const long expected_peak = std::max(std::labs(expected_sample(30000)), std::labs(expected_sample(-30000)));
            for (unsigned ch = 0; ch < 2; ++ch)
                require(std::labs(peak(got, ch) - expected_peak) <= 4,
                        "DMA signed Q15 gain, core " + std::to_string(core) + " channel " + std::to_string(ch) +
                        " (peak " + std::to_string(peak(got, ch)) + ", want " + std::to_string(expected_peak) + ")");
            if (gain == 0x7FFF) require(peak(got, 0) > 20000, "full-volume DMA must exceed 20000");
            for (std::size_t i = 0; i < 4096; ++i) {
                const long source = (i / 54) % 2 ? 30000 : -30000;
                const long expected = expected_sample(source);
                require(std::labs(got[i * 2] - expected) <= 4 && std::labs(got[i * 2 + 1] + expected) <= 4,
                        "DMA must preserve stereo packing and gain polarity at frame " + std::to_string(i));
            }
        }
    }

    std::puts("PASS: fixed voice volume respects ADSR, and both DMA cores preserve stereo and signed Q15 gains");
}
