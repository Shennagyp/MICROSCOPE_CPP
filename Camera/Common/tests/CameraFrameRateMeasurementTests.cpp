#include "camera/CameraFrameRate.h"

#include <cmath>
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
    using camera::FrameRateMeasurement;

    const FrameRateMeasurement measured{91, 1000, 1000, 1322};
    const auto rate = measured.framesPerSecond();
    require(rate && std::abs(*rate - 91.0) < 1e-12,
            "rate is derived from frame count and explicit interval timebase");
    require(measured.cumulativeFrames && *measured.cumulativeFrames == 1322,
            "cumulative count remains distinct from interval frame count");

    const FrameRateMeasurement unavailable{10, 0, 1000, std::nullopt};
    require(!unavailable.framesPerSecond(),
            "zero interval does not produce an invented rate");

    const FrameRateMeasurement unknownTimebase{10, 1000, 0, std::nullopt};
    require(!unknownTimebase.framesPerSecond(),
            "unknown timebase does not produce an assumed unit");

    const FrameRateMeasurement zeroFrames{0, 1000, 1000, 0};
    const auto zeroRate = zeroFrames.framesPerSecond();
    require(zeroRate && *zeroRate == 0.0,
            "a valid zero-frame measurement remains a measured zero rate");
    require(zeroFrames.cumulativeFrames && *zeroFrames.cumulativeFrames == 0,
            "a present zero cumulative count remains distinct from unavailable");

    std::cout << "All common camera frame-rate measurement tests passed.\n";
    return EXIT_SUCCESS;
}
