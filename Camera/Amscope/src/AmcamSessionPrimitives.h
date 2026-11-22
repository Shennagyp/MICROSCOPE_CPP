#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace amscope::detail {

// Tracks SDK callbacks that have entered the application callback context.
// The SDK Stop/Close boundary must prevent new callback entries before wait()
// is used; wait() then drains callbacks already in flight.
class AmcamCallbackTracker final {
public:
    class Lease final {
    public:
        Lease() noexcept = default;
        explicit Lease(AmcamCallbackTracker* owner) noexcept : owner_(owner) {}
        ~Lease() { reset(); }

        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;

        Lease(Lease&& other) noexcept : owner_(other.owner_) {
            other.owner_ = nullptr;
        }

        Lease& operator=(Lease&& other) noexcept {
            if (this != &other) {
                reset();
                owner_ = other.owner_;
                other.owner_ = nullptr;
            }
            return *this;
        }

        void reset() noexcept {
            if (!owner_) return;
            owner_->leave();
            owner_ = nullptr;
        }

    private:
        AmcamCallbackTracker* owner_ = nullptr;
    };

    [[nodiscard]] Lease enter() noexcept {
        active_.fetch_add(1, std::memory_order_acq_rel);
        return Lease(this);
    }

    void wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this]() {
            return active_.load(std::memory_order_acquire) == 0;
        });
    }

    [[nodiscard]] std::uint32_t activeCount() const noexcept {
        return active_.load(std::memory_order_acquire);
    }

private:
    void leave() noexcept {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            active_.fetch_sub(1, std::memory_order_acq_rel);
        }
        condition_.notify_all();
    }

    std::atomic<std::uint32_t> active_{0};
    mutable std::mutex mutex_;
    std::condition_variable condition_;
};

// Every successful native Stop creates a cache-flush obligation. A failed
// flush must leave that obligation pending for the next Start attempt.
// Generation tracking also preserves a newer obligation raised while an older
// flush attempt is executing.
class AmcamFrameCacheFlushObligation final {
public:
    void markPending() noexcept {
        generation_.fetch_add(1, std::memory_order_acq_rel);
    }

    void reset() noexcept {
        completed_.store(generation_.load(std::memory_order_acquire),
                         std::memory_order_release);
    }

    [[nodiscard]] bool pending() const noexcept {
        return generation_.load(std::memory_order_acquire) !=
               completed_.load(std::memory_order_acquire);
    }

    template <typename Flush>
    bool retry(Flush&& flush) {
        const auto target = generation_.load(std::memory_order_acquire);
        if (target == completed_.load(std::memory_order_acquire))
            return true;

        if (!std::forward<Flush>(flush)())
            return false;

        auto completed = completed_.load(std::memory_order_acquire);
        while (completed < target &&
               !completed_.compare_exchange_weak(
                   completed, target,
                   std::memory_order_acq_rel,
                   std::memory_order_acquire)) {
        }
        return true;
    }

private:
    std::atomic<std::uint64_t> generation_{0};
    std::atomic<std::uint64_t> completed_{0};
};

// A slot is reusable only when the pool owns its sole shared_ptr reference.
// Consumers may retain leases; the acquisition worker then uses another free
// slot or the existing discard buffer rather than mutating retained pixels.
template <typename T>
[[nodiscard]] std::shared_ptr<T> tryAcquireUnleasedFrameSlot(
    const std::vector<std::shared_ptr<T>>& slots) noexcept {
    for (const auto& slot : slots) {
        if (slot.use_count() == 1)
            return slot;
    }
    return {};
}

} // namespace amscope::detail
