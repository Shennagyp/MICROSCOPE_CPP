#pragma once

#include <atomic>
#include <cstdint>

namespace amscope::detail {

// One acquisition's permission to publish frame callbacks into the current
// camera session. A failed Stop can reactivate this same acquisition at the
// new session generation; a successful Stop/Close leaves it stale.
class AcquisitionFrameGate final {
public:
    explicit AcquisitionFrameGate(std::uint64_t generation) noexcept
        : generation_(generation) {}

    [[nodiscard]] bool accepts(std::uint64_t activeSessionGeneration) const noexcept {
        return generation_.load(std::memory_order_acquire) ==
               activeSessionGeneration;
    }

    [[nodiscard]] std::uint64_t generation() const noexcept {
        return generation_.load(std::memory_order_acquire);
    }

    void activateFor(std::uint64_t generation) noexcept {
        generation_.store(generation, std::memory_order_release);
    }

    // Invalidate only the still-active generation. A concurrent Stop/Close or
    // newer session changes the expected value, making a stale callback a no-op.
    [[nodiscard]] bool invalidateIfCurrent(
        std::uint64_t expectedGeneration) noexcept {
        if (expectedGeneration == 0) return false;
        return generation_.compare_exchange_strong(
            expectedGeneration, 0,
            std::memory_order_acq_rel,
            std::memory_order_acquire);
    }

private:
    std::atomic<std::uint64_t> generation_;
};

} // namespace amscope::detail
