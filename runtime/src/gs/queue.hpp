#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace ps2::gs::queue {

enum Type : std::uint32_t { kWrap, kGif, kGifReset, kReset, kRegisterReset, kVblank, kPresent, kDownload, kVif1, kVif1Write, kVuReset, kState, kVuFence, kDownloadAsync };

enum class Role { Emulation, Vu, Render };

struct Header {
    std::uint32_t type, bytes, arg0, arg1;
};

class Ring {
public:
    void start(std::size_t capacity);
    bool active() const { return capacity_ != 0; }
    void post(Type type, std::uint32_t arg0, std::uint32_t arg1, const void *data, std::uint32_t bytes);
    bool peek(Header &header, const std::uint8_t *&payload);
    void pop(const Header &header);
    void wait_for_work(int timeout_ms);
    void wake();
    // PS2_THREADS census: nanoseconds this ring's producer spent waiting for room (its consumer is the slow
    // one) and its consumer spent with nothing to do (it is the slow one). Only counted while counted.
    std::uint64_t producer_stall_ns() const { return producer_stall_ns_.load(std::memory_order_relaxed); }
    std::uint64_t consumer_idle_ns() const { return consumer_idle_ns_.load(std::memory_order_relaxed); }

private:
    std::unique_ptr<std::uint8_t[]> buffer_;
    std::uint64_t capacity_ = 0;
    alignas(64) std::atomic<std::uint64_t> head_{0};
    alignas(64) std::atomic<std::uint64_t> tail_{0};
    alignas(64) std::atomic<std::uint32_t> posted_{0};
    alignas(64) std::atomic<std::uint32_t> freed_{0};
    std::atomic<std::uint32_t> consumer_waiting_{0}, producer_waiting_{0};
    alignas(64) std::atomic<std::uint64_t> producer_stall_ns_{0}, consumer_idle_ns_{0};
};

Ring &render();
Ring &vu();
Role &role();
Ring &outbox();

bool active();
bool vu_active();
void post(Type type, std::uint32_t arg0, std::uint32_t arg1, const void *data, std::uint32_t bytes);

// The PS2_THREADS census is the only reason these timings are taken: off, no clock is read here.
bool stats_on();

void wait(std::atomic<std::uint32_t> &value, std::uint32_t seen);
void notify(std::atomic<std::uint32_t> &value);

}
