#pragma once

#include <cmath>
#include <cstdint>
#include <optional>

namespace camera {

// A measured rate over an explicit interval. This is not a configured target
// rate and is not a host-side arrival estimator.
struct FrameRateMeasurement {
    std::uint64_t framesInInterval = 0;
    std::uint64_t intervalTicks = 0;
    std::uint64_t ticksPerSecond = 0;
    std::optional<std::uint64_t> cumulativeFrames;

    [[nodiscard]] std::optional<double> framesPerSecond() const noexcept {
        if (intervalTicks == 0 || ticksPerSecond == 0)
            return std::nullopt;

        const long double rate =
            static_cast<long double>(framesInInterval) *
            static_cast<long double>(ticksPerSecond) /
            static_cast<long double>(intervalTicks);
        const double result = static_cast<double>(rate);
        if (!std::isfinite(result) || result < 0.0)
            return std::nullopt;
        return result;
    }
};

} // namespace camera
