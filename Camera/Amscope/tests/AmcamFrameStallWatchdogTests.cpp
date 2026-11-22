#include "amscope/detail/FrameStallWatchdog.h"

#include <chrono>
#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}
} // namespace

int main() {
    using namespace std::chrono_literals;
    using Clock = std::chrono::steady_clock;
    using amscope::detail::frameStallTimeout;
    using amscope::detail::frameStalled;

    require(frameStallTimeout(0) == 5000ms,
            "unknown exposure uses the five-second minimum");
    require(frameStallTimeout(7000) == 5000ms,
            "short exposure remains bounded by the five-second minimum");
    require(frameStallTimeout(2'000'000) == 7000ms,
            "long exposure uses three exposure periods plus one second");
    require(frameStallTimeout(7'000'001) == 22'003ms,
            "fractional milliseconds are rounded up before applying the multiplier");

    const auto origin = Clock::time_point{};
    require(!frameStalled(origin + 6999ms, origin, 2'000'000),
            "frame stream is healthy immediately before its deadline");
    require(frameStalled(origin + 7000ms, origin, 2'000'000),
            "frame stream is stalled at its deadline");
    require(!frameStalled(origin - 1ms, origin, 2'000'000),
            "a clock value before the last frame cannot be treated as stalled");


    using amscope::detail::heartbeatStallTimeout;
    using amscope::detail::heartbeatStalled;

    require(heartbeatStallTimeout(0) == 5000ms,
            "disabled or unknown heartbeat interval keeps a bounded five-second deadline");
    require(heartbeatStallTimeout(1000) == 5000ms,
            "one-second heartbeat keeps the five-second minimum");
    require(heartbeatStallTimeout(2000) == 7000ms,
            "two-second heartbeat uses three intervals plus one second");
    require(heartbeatStallTimeout(10000) == 31000ms,
            "maximum heartbeat interval receives a proportional watchdog deadline");

    require(!heartbeatStalled(origin + 6999ms, origin, 2000),
            "heartbeat stream is healthy before its deadline");
    require(heartbeatStalled(origin + 7000ms, origin, 2000),
            "heartbeat stream is stalled at its deadline");
    require(!heartbeatStalled(origin - 1ms, origin, 2000),
            "a clock value before the last heartbeat cannot be treated as stalled");

    std::cout << "All frame-stall watchdog tests passed.\n";
    return EXIT_SUCCESS;
}
