#include "camera/FrameRateEstimator.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void requireNear(double actual, double expected, double tolerance,
                 const char* message) {
    require(std::abs(actual - expected) <= tolerance, message);
}

} // namespace

int main() {
    {
        camera::FrameRateEstimator estimator;
        require(!estimator.record(10.0),
                "the first timestamp establishes a baseline only");
        const auto firstRate = estimator.record(10.04);
        require(firstRate.has_value(), "the second timestamp produces a rate");
        requireNear(*firstRate, 25.0, 0.0001,
                    "two frames 40 ms apart produce 25 FPS");
        const auto secondRate = estimator.record(10.08);
        require(secondRate.has_value(), "a later valid timestamp produces a rate");
        requireNear(*secondRate, 25.0, 0.0001,
                    "a stable interval preserves the estimate");
    }

    {
        camera::FrameRateEstimator estimator;
        require(!estimator.record(1.0), "baseline timestamp is accepted");
        require(!estimator.record(1.0),
                "a non-increasing timestamp is ignored");
        require(!estimator.record(std::numeric_limits<double>::quiet_NaN()),
                "a NaN timestamp is ignored");
        require(!estimator.record(std::numeric_limits<double>::infinity()),
                "an infinite timestamp is ignored");
        const auto firstValid = estimator.record(1.5);
        require(firstValid.has_value(),
                "invalid timestamps do not corrupt the previous baseline");
        requireNear(*firstValid, 2.0, 0.0001,
                    "the valid interval is measured from the previous valid timestamp");
        const auto nextValid = estimator.record(2.0);
        require(nextValid.has_value(), "subsequent valid interval is accepted");
        requireNear(*nextValid, 2.0, 0.0001,
                    "the moving average uses valid intervals only");
    }

    {
        camera::FrameRateEstimator estimator(2);
        require(!estimator.record(0.0), "moving-window baseline accepted");
        (void)estimator.record(1.0);
        (void)estimator.record(2.0);
        const auto rate = estimator.record(2.5);
        require(rate.has_value(), "moving window emits a rate");
        requireNear(*rate, 1.0 / 0.75, 0.0001,
                    "only the two most recent intervals contribute");
    }

    {
        camera::FrameRateEstimator estimator;
        (void)estimator.record(5.0);
        (void)estimator.record(5.5);
        estimator.reset();
        require(!estimator.record(100.0),
                "reset clears the previous timestamp");
        const auto rate = estimator.record(100.25);
        require(rate.has_value(), "a fresh interval works after reset");
        requireNear(*rate, 4.0, 0.0001,
                    "reset also clears accumulated intervals");
    }

    {
        camera::FrameRateEstimator estimator(0);
        require(estimator.sampleWindow() == 1,
                "a zero sample window is normalized to one");
        (void)estimator.record(0.0);
        const auto first = estimator.record(1.0);
        require(first.has_value(), "one-sample window produces a rate");
        requireNear(*first, 1.0, 0.0001,
                    "single interval rate is correct");
        const auto second = estimator.record(3.0);
        require(second.has_value(), "single-sample window continues updating");
        requireNear(*second, 0.5, 0.0001,
                    "single-sample window replaces its prior interval");
    }

    std::cout << "All camera frame-rate estimator tests passed.\n";
    return EXIT_SUCCESS;
}
