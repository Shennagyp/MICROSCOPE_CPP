#pragma once

#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace amscope::detail {

// Bounded, allocation-free event handoff from the vendor callback to the
// acquisition worker. The vendor callback never pulls a frame or touches UI
// code. Overflow is explicit: the acquisition fails instead of silently
// losing callback events or growing an unbounded queue.
class AmcamEventQueue final {
public:
    static constexpr std::size_t kCapacity = 256;

    enum class PushResult : std::uint8_t {
        Queued,
        Ignored,
        Overflow
    };

    enum class WaitResult : std::uint8_t {
        Event,
        Stopped,
        Overflow
    };

    PushResult push(std::uint32_t event) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_) return PushResult::Ignored;
        if (overflow_) return PushResult::Overflow;

        if (count_ == events_.size()) {
            overflow_ = true;
            condition_.notify_one();
            return PushResult::Overflow;
        }

        const std::size_t tail = (head_ + count_) % events_.size();
        events_[tail] = event;
        ++count_;
        condition_.notify_one();
        return PushResult::Queued;
    }

    WaitResult waitNext(std::uint32_t& event) {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this]() {
            return stopped_ || overflow_ || count_ != 0;
        });

        if (stopped_) return WaitResult::Stopped;
        if (overflow_) return WaitResult::Overflow;

        event = events_[head_];
        head_ = (head_ + 1) % events_.size();
        --count_;
        return WaitResult::Event;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        head_ = 0;
        count_ = 0;
        overflow_ = false;
        stopped_ = false;
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
        }
        condition_.notify_all();
    }

    // Discard only events that were queued before a successful SDK Stop.
    // Keep the queue stopped until the next explicit reset/start.
    void clearPending() {
        std::lock_guard<std::mutex> lock(mutex_);
        head_ = 0;
        count_ = 0;
        overflow_ = false;
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    std::array<std::uint32_t, kCapacity> events_{};
    std::size_t head_ = 0;
    std::size_t count_ = 0;
    bool overflow_ = false;
    bool stopped_ = true;
};

} // namespace amscope::detail
