#include "AmcamEventQueue.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    using amscope::detail::AmcamEventQueue;

    AmcamEventQueue queue;
    std::uint32_t event = 0;

    require(queue.push(0x0004) == AmcamEventQueue::PushResult::Ignored,
            "new queue starts stopped");

    queue.reset();
    require(queue.push(0x0004) == AmcamEventQueue::PushResult::Queued,
            "image event accepted");
    require(queue.push(0x0081) == AmcamEventQueue::PushResult::Queued,
            "disconnect event accepted");
    require(queue.waitNext(event) == AmcamEventQueue::WaitResult::Event && event == 0x0004,
            "events preserve FIFO order");
    require(queue.waitNext(event) == AmcamEventQueue::WaitResult::Event && event == 0x0081,
            "second event is delivered after first");

    queue.stop();
    require(queue.waitNext(event) == AmcamEventQueue::WaitResult::Stopped,
            "stopped queue wakes and terminates the consumer");
    require(queue.push(0x0004) == AmcamEventQueue::PushResult::Ignored,
            "stopped queue rejects late SDK callbacks");

    queue.reset();
    for (std::size_t i = 0; i < AmcamEventQueue::kCapacity; ++i) {
        require(queue.push(static_cast<std::uint32_t>(i)) ==
                    AmcamEventQueue::PushResult::Queued,
                "queue accepts events up to its fixed capacity");
    }
    require(queue.push(0xffff) == AmcamEventQueue::PushResult::Overflow,
            "queue reports overflow rather than silently dropping an event");
    require(queue.waitNext(event) == AmcamEventQueue::WaitResult::Overflow,
            "consumer observes overflow as a terminal result");

    queue.reset();
    std::atomic<AmcamEventQueue::WaitResult> waitResult{
        AmcamEventQueue::WaitResult::Event};
    std::thread waiter([&]() {
        std::uint32_t ignored = 0;
        waitResult.store(queue.waitNext(ignored), std::memory_order_release);
    });
    queue.stop();
    waiter.join();
    require(waitResult.load(std::memory_order_acquire) ==
                AmcamEventQueue::WaitResult::Stopped,
            "stop wakes a consumer blocked in waitNext");

    std::cout << "All Amcam event-queue tests passed.\n";
    return EXIT_SUCCESS;
}
