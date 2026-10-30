// The guest seeds its fight RNG from the drive clock (InitBeforeDbLoad calls mathfRandInit with the result of
// sceCdReadClock), so the clock it reads has to belong to the emulated machine. Reading this host's wall clock
// made the seed - and with it the fight, its length and which boot reaches a result - depend on the time of day
// (docs/FULL_SINGLE_PLAYER.md). A PS2 drive RTC holds its factory date until someone sets it, then counts
// emulated seconds, and that is what the HLEs must hand over.
#include "iop/kernel.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>

static void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static int bcd_to_int(std::uint8_t byte) { return (byte >> 4) * 10 + (byte & 0x0F); }

// seconds into the emulated day, which is what the guest turns into its RNG seed (hour * 60 + minute)
static long seconds_of_day(const std::uint8_t *clock) {
    return bcd_to_int(clock[3]) * 3600L + bcd_to_int(clock[2]) * 60L + bcd_to_int(clock[1]);
}

static bool valid_bcd(const std::uint8_t *clock) {
    for (int i = 0; i < 8; ++i)
        if ((clock[i] >> 4) > 9 || (clock[i] & 0x0F) > 9) return false;
    return true;
}

constexpr std::uint64_t kOneSecond = 36864000u;  // usec_to_cycles(1000000): the IOP clock ticks 36.864 MHz

int main() {
    std::uint8_t boot[8], minute[8], hour[8], again[8], live[8];
    ps2::iop::kernel::cd_clock_at(0, boot);
    ps2::iop::kernel::cd_clock_at(60 * kOneSecond, minute);
    ps2::iop::kernel::cd_clock_at(3661 * kOneSecond, hour);  // 01:01:01
    ps2::iop::kernel::cd_clock_at(0, again);
    ps2::iop::kernel::cd_clock(live);  // whatever this machine has actually run

    for (const std::uint8_t *clock : {boot, minute, hour, again, live})
        require(valid_bcd(clock), "every clock byte is BCD");
    require(boot[5] == 0x01 && boot[6] == 0x01 && boot[7] == 0x00,
            "an unconfigured drive RTC reads its factory date, not this host's date");
    require(seconds_of_day(boot) == 0, "the clock starts at midnight of that date");
    require(seconds_of_day(minute) == 60, "the clock counts emulated seconds");
    require(seconds_of_day(hour) == 3661, "and keeps counting: one hour, one minute, one second");
    for (int i = 0; i < 8; ++i) require(again[i] == boot[i], "one emulated instant reads one clock");

    // the probe has to see the bad case: a clock taken from this host's wall clock moves with the date, and
    // two reads of an idle machine drift apart. Neither is allowed to reach the guest.
    const std::time_t host = std::time(nullptr);
    const std::tm *tm = std::gmtime(&host);
    require(boot[7] != static_cast<std::uint8_t>(tm->tm_year % 100) || boot[6] != tm->tm_mon + 1 ||
                boot[5] != tm->tm_mday,
            "the RTC date is the drive's own, never this host's date");
    std::uint8_t later[8];
    ps2::iop::kernel::cd_clock_at(0, later);
    require(seconds_of_day(later) == seconds_of_day(boot), "an idle machine does not advance the RTC");

    std::printf("PASS: the drive clock is the emulated RTC (boot %02X-%02X-%02X %02X:%02X:%02X, live %02X:%02X:%02X)\n",
                boot[7], boot[6], boot[5], boot[3], boot[2], boot[1], live[3], live[2], live[1]);
}
