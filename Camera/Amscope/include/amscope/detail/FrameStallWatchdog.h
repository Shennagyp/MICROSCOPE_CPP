#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>

namespace amscope::detail {

inline constexpr std::chrono::milliseconds kMinimumFrameStallTimeout{5000};
inline constexpr std::uint64_t kExposureTimeoutMultiplier = 3;
inline constexpr std::uint64_t kExposureTimeoutGraceMilliseconds = 1000;

// Exposure is reported in microseconds. Round up to whole milliseconds so
// the health deadline never underestimates a fractional-millisecond exposure.
inline std::chrono::milliseconds frameStallTimeout(
    std::uint32_t exposureMicroseconds) noexcept {
    const std::uint64_t exposureMilliseconds =
        (static_cast<std::uint64_t>(exposureMicroseconds) + 999U) / 1000U;
    const std::uint64_t exposureBasedMilliseconds =
        exposureMilliseconds * kExposureTimeoutMultiplier +
        kExposureTimeoutGraceMilliseconds;
    const std::uint64_t minimumMilliseconds =
        static_cast<std::uint64_t>(kMinimumFrameStallTimeout.count());
    const std::uint64_t timeoutMilliseconds =
        std::max(minimumMilliseconds, exposureBasedMilliseconds);
    using Rep = std::chrono::milliseconds::rep;
    const auto maximum = static_cast<std::uint64_t>(
        std::numeric_limits<Rep>::max());
    return std::chrono::milliseconds(static_cast<Rep>(
        std::min(timeoutMilliseconds, maximum)));
}

inline bool frameStalled(
    std::chrono::steady_clock::time_point now,
    std::chrono::steady_clock::time_point lastFrameAt,
    std::uint32_t exposureMicroseconds) noexcept {
    return now >= lastFrameAt &&
           now - lastFrameAt >= frameStallTimeout(exposureMicroseconds);
}


inline std::chrono::milliseconds heartbeatStallTimeout(
    std::uint32_t heartbeatIntervalMilliseconds) noexcept {
    const std::uint64_t heartbeatBasedMilliseconds =
        static_cast<std::uint64_t>(heartbeatIntervalMilliseconds) *
            kExposureTimeoutMultiplier +
        kExposureTimeoutGraceMilliseconds;
    const std::uint64_t minimumMilliseconds =
        static_cast<std::uint64_t>(kMinimumFrameStallTimeout.count());
    const std::uint64_t timeoutMilliseconds =
        std::max(minimumMilliseconds, heartbeatBasedMilliseconds);
    using Rep = std::chrono::milliseconds::rep;
    const auto maximum = static_cast<std::uint64_t>(
        std::numeric_limits<Rep>::max());
    return std::chrono::milliseconds(static_cast<Rep>(
        std::min(timeoutMilliseconds, maximum)));
}

inline bool heartbeatStalled(
    std::chrono::steady_clock::time_point now,
    std::chrono::steady_clock::time_point lastHeartbeatAt,
    std::uint32_t heartbeatIntervalMilliseconds) noexcept {
    return now >= lastHeartbeatAt &&
           now - lastHeartbeatAt >=
               heartbeatStallTimeout(heartbeatIntervalMilliseconds);
}

} // namespace amscope::detail
