#include "AmcamSessionPrimitives.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void testCallbackTracker() {
    using amscope::detail::AmcamCallbackTracker;

    AmcamCallbackTracker tracker;
    require(tracker.activeCount() == 0, "callback tracker starts idle");
    tracker.wait();

    auto lease = tracker.enter();
    require(tracker.activeCount() == 1, "enter registers one active callback");

    std::promise<void> waiterStarted;
    auto started = waiterStarted.get_future();
    auto waiter = std::async(std::launch::async, [&]() {
        waiterStarted.set_value();
        tracker.wait();
    });
    started.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto blockedStatus = waiter.wait_for(std::chrono::milliseconds(20));
    lease.reset();
    waiter.wait();
    require(blockedStatus == std::future_status::timeout,
            "wait blocks until an in-flight callback lease is released");
    require(tracker.activeCount() == 0, "callback lease release returns tracker to idle");

    constexpr int callbackCount = 6;
    std::atomic<int> entered{0};
    std::atomic<bool> releaseCallbacks{false};
    std::vector<std::thread> callbacks;
    callbacks.reserve(callbackCount);
    for (int i = 0; i < callbackCount; ++i) {
        callbacks.emplace_back([&]() {
            auto callback = tracker.enter();
            entered.fetch_add(1, std::memory_order_release);
            while (!releaseCallbacks.load(std::memory_order_acquire))
                std::this_thread::yield();
        });
    }

    while (entered.load(std::memory_order_acquire) != callbackCount)
        std::this_thread::yield();
    require(tracker.activeCount() == callbackCount,
            "concurrent callback entries are all tracked");

    std::promise<void> drainStarted;
    auto drainStartedFuture = drainStarted.get_future();
    auto drain = std::async(std::launch::async, [&]() {
        drainStarted.set_value();
        tracker.wait();
    });
    drainStartedFuture.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto concurrentBlockedStatus =
        drain.wait_for(std::chrono::milliseconds(20));
    releaseCallbacks.store(true, std::memory_order_release);
    for (auto& callback : callbacks) callback.join();
    const auto drainCompletionStatus =
        drain.wait_for(std::chrono::seconds(2));

    require(concurrentBlockedStatus == std::future_status::timeout,
            "drain waits for all concurrent callbacks");
    require(drainCompletionStatus == std::future_status::ready,
            "drain completes after all concurrent callbacks leave");
    require(tracker.activeCount() == 0,
            "all concurrent callback leases are released");

    try {
        auto callback = tracker.enter();
        throw std::runtime_error("exercise callback unwinding");
    } catch (const std::runtime_error&) {
    }
    require(tracker.activeCount() == 0,
            "callback lease releases during exception unwinding");

    {
        auto first = tracker.enter();
        auto moved = std::move(first);
        require(tracker.activeCount() == 1, "moving a lease does not double-count");
    }
    require(tracker.activeCount() == 0, "moved lease releases exactly once");
}

void testFrameCacheFlushObligation() {
    using amscope::detail::AmcamFrameCacheFlushObligation;

    AmcamFrameCacheFlushObligation obligation;
    int flushCalls = 0;

    require(!obligation.pending(), "cache-flush obligation starts clear");
    require(obligation.retry([&]() {
        ++flushCalls;
        return true;
    }), "retry with no obligation succeeds as a no-op");
    require(flushCalls == 0, "no SDK flush is attempted without an obligation");

    obligation.markPending();
    require(obligation.pending(), "successful Stop creates a flush obligation");
    require(!obligation.retry([&]() {
        ++flushCalls;
        return false;
    }), "failed SDK flush is reported");
    require(obligation.pending(), "failed SDK flush preserves the obligation");
    require(flushCalls == 1, "failed attempt invokes the SDK operation once");

    require(obligation.retry([&]() {
        ++flushCalls;
        return true;
    }), "later successful SDK flush clears the obligation");
    require(!obligation.pending(), "successful flush clears the obligation");
    require(flushCalls == 2, "successful retry invokes the SDK operation once");

    require(obligation.retry([&]() {
        ++flushCalls;
        return true;
    }), "repeated retry after success remains a no-op");
    require(flushCalls == 2, "completed obligation is not flushed twice");

    obligation.markPending();
    const bool firstFlushSucceeded = obligation.retry([&]() {
        ++flushCalls;
        obligation.markPending();
        return true;
    });
    require(firstFlushSucceeded, "the captured flush generation can complete");
    require(obligation.pending(),
            "a newer Stop obligation raised during flush is not lost");

    require(obligation.retry([&]() {
        ++flushCalls;
        return true;
    }), "newer obligation can be retried independently");
    require(!obligation.pending(), "newer obligation clears after successful retry");

    obligation.markPending();
    bool exceptionPropagated = false;
    try {
        (void)obligation.retry([]() -> bool {
            throw std::runtime_error("simulated SDK wrapper exception");
        });
    } catch (const std::runtime_error&) {
        exceptionPropagated = true;
    }
    require(exceptionPropagated, "flush callback exceptions are not swallowed");
    require(obligation.pending(), "exception leaves the flush obligation pending");

    obligation.reset();
    require(!obligation.pending(), "session reset clears an obsolete obligation");
}

void testRetainedFrameLeases() {
    using amscope::detail::tryAcquireUnleasedFrameSlot;

    std::vector<std::shared_ptr<int>> slots;
    slots.push_back(std::make_shared<int>(10));
    slots.push_back(std::make_shared<int>(20));
    slots.push_back(std::make_shared<int>(30));

    auto first = tryAcquireUnleasedFrameSlot(slots);
    auto second = tryAcquireUnleasedFrameSlot(slots);
    auto third = tryAcquireUnleasedFrameSlot(slots);
    require(first && second && third, "pool provides all initially free slots");
    require(first.get() != second.get() && second.get() != third.get() &&
            first.get() != third.get(), "each retained lease uses a distinct slot");
    require(!tryAcquireUnleasedFrameSlot(slots),
            "pool reports exhaustion when every slot is retained");

    const auto* releasedSlot = second.get();
    second.reset();
    auto reused = tryAcquireUnleasedFrameSlot(slots);
    require(reused && reused.get() == releasedSlot,
            "a released slot becomes reusable without replacing the pool");
    require(*first == 10 && *third == 30,
            "other retained leases remain valid while a free slot is reused");
}

} // namespace

int main() {
    testCallbackTracker();
    testFrameCacheFlushObligation();
    testRetainedFrameLeases();
    std::cout << "All Amcam session primitive tests passed.\n";
    return EXIT_SUCCESS;
}
