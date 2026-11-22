#include "AmcamEventQueue.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

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

    queue.reset();
    const auto timeoutStart = std::chrono::steady_clock::now();
    require(queue.waitNextUntil(
                timeoutStart + std::chrono::milliseconds(2), event) ==
                AmcamEventQueue::WaitResult::Timeout,
            "timed wait returns Timeout when no event or wake signal arrives");

    queue.signal();
    require(queue.waitNextUntil(
                std::chrono::steady_clock::now() + std::chrono::seconds(1), event) ==
                AmcamEventQueue::WaitResult::Signalled,
            "signal wakes the timed waiter so watchdog inputs can be recalculated");

    queue.signal();
    require(queue.push(0x0004) == AmcamEventQueue::PushResult::Queued,
            "SDK event is accepted while a reconfiguration wake is pending");
    require(queue.waitNextUntil(
                std::chrono::steady_clock::now() + std::chrono::seconds(1), event) ==
                AmcamEventQueue::WaitResult::Event && event == 0x0004,
            "queued SDK events take priority over a reconfiguration wake");
    require(queue.waitNextUntil(
                std::chrono::steady_clock::now() + std::chrono::seconds(1), event) ==
                AmcamEventQueue::WaitResult::Signalled,
            "reconfiguration wake remains observable after queued SDK events");
    queue.stop();
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

    queue.reset();
    constexpr std::size_t producerCount = 4;
    constexpr std::size_t eventsPerProducer = 32;
    std::vector<std::thread> producers;
    producers.reserve(producerCount);
    for (std::size_t producer = 0; producer < producerCount; ++producer) {
        producers.emplace_back([&, producer]() {
            for (std::size_t i = 0; i < eventsPerProducer; ++i) {
                const auto value = static_cast<std::uint32_t>(
                    producer * eventsPerProducer + i);
                if (queue.push(value) != AmcamEventQueue::PushResult::Queued)
                    std::abort();
            }
        });
    }
    for (auto& producer : producers) producer.join();

    std::vector<std::uint32_t> received;
    received.reserve(producerCount * eventsPerProducer);
    for (std::size_t i = 0; i < producerCount * eventsPerProducer; ++i) {
        require(queue.waitNext(event) == AmcamEventQueue::WaitResult::Event,
                "concurrent producer event remains readable");
        received.push_back(event);
    }
    std::sort(received.begin(), received.end());
    for (std::size_t i = 0; i < received.size(); ++i) {
        require(received[i] == i,
                "concurrent producers do not lose or duplicate queued events");
    }

    std::cout << "All Amcam event-queue tests passed.\n";
    return EXIT_SUCCESS;
}
