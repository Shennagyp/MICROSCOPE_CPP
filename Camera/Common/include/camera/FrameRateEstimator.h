#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <optional>
#include <vector>

namespace camera {

// Smoothed preview/receive-rate estimate from monotonic host timestamps.
// This is not configured camera FPS or device-reported frame-rate telemetry.
class FrameRateEstimator final {
public:
    explicit FrameRateEstimator(std::size_t sampleWindow = 30)
        : sampleWindow_(std::max<std::size_t>(1, sampleWindow)) {
        intervals_.reserve(sampleWindow_);
    }

    [[nodiscard]] std::size_t sampleWindow() const noexcept {
        return sampleWindow_;
    }

    // Records a timestamp in monotonic seconds. A rate is available after the
    // first valid interval. Invalid or non-increasing timestamps are ignored
    // without corrupting the previous valid baseline.
    std::optional<double> record(double timestampSeconds) {
        if (!std::isfinite(timestampSeconds))
            return std::nullopt;

        if (!previousTimestamp_) {
            previousTimestamp_ = timestampSeconds;
            return std::nullopt;
        }
        if (timestampSeconds <= *previousTimestamp_)
            return std::nullopt;

        intervals_.push_back(timestampSeconds - *previousTimestamp_);
        if (intervals_.size() > sampleWindow_) {
            const auto excess = intervals_.size() - sampleWindow_;
            intervals_.erase(
                intervals_.begin(),
                intervals_.begin() + static_cast<std::ptrdiff_t>(excess));
        }
        previousTimestamp_ = timestampSeconds;

        const double meanInterval =
            std::accumulate(intervals_.begin(), intervals_.end(), 0.0) /
            static_cast<double>(intervals_.size());
        if (!std::isfinite(meanInterval) || meanInterval <= 0.0)
            return std::nullopt;

        return 1.0 / meanInterval;
    }

    void reset() noexcept {
        intervals_.clear();
        previousTimestamp_.reset();
    }

private:
    std::size_t sampleWindow_;
    std::vector<double> intervals_;
    std::optional<double> previousTimestamp_;
};

} // namespace camera
