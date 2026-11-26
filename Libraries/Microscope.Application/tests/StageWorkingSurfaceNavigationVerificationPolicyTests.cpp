#include "microscope/application/motion/StageWorkingSurfaceNavigationRuntime.h"

#include <cmath>
#include <iostream>
#include <limits>

namespace {
using namespace microscope::application::motion;

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

using Policy = StageWorkingSurfaceNavigationVerificationPolicy;
using Mode = StageWorkingSurfaceNavigationPositionVerificationMode;
using Error = StageWorkingSurfaceNavigationVerificationPolicyError;

template<class TResult>
void checkError(TResult result, Error expected, const char* message) {
    check(!result && result.error() == expected, message);
}

void testAcceptsValidBoundedAndMeasuredPolicies() {
    const auto bounded = Policy::create(2.0, 3.0, 0.0, 0.5,
        Mode::BoundedCommandEstimate);
    check(bool(bounded), "accepts positive target tolerances and non-negative home uncertainty");
    if (bounded) {
        check(bounded.value().xTargetToleranceMicrometers() == 2.0 &&
              bounded.value().yTargetToleranceMicrometers() == 3.0 &&
              bounded.value().xHomeUncertaintyMicrometers() == 0.0 &&
              bounded.value().yHomeUncertaintyMicrometers() == 0.5 &&
              bounded.value().mode() == Mode::BoundedCommandEstimate,
              "retains every bounded-estimate policy value without changing units or axes");
    }

    const auto measured = Policy::create(1.25, 1.75, 0.2, 0.3,
        Mode::DevicePositionObservations);
    check(bool(measured) && measured.value().mode() == Mode::DevicePositionObservations,
          "accepts the device-position-observation verification mode");
}

void testRejectsInvalidTargetTolerances() {
    checkError(Policy::create(0.0, 1.0, 0.0, 0.0, Mode::BoundedCommandEstimate),
               Error::InvalidXTolerance, "rejects a zero X target tolerance");
    checkError(Policy::create(-1.0, 1.0, 0.0, 0.0, Mode::BoundedCommandEstimate),
               Error::InvalidXTolerance, "rejects a negative X target tolerance");
    checkError(Policy::create(1.0, 0.0, 0.0, 0.0, Mode::BoundedCommandEstimate),
               Error::InvalidYTolerance, "rejects a zero Y target tolerance");
    checkError(Policy::create(1.0, -1.0, 0.0, 0.0, Mode::BoundedCommandEstimate),
               Error::InvalidYTolerance, "rejects a negative Y target tolerance");
}

void testRejectsNonFiniteTargetTolerances() {
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const auto infinity = std::numeric_limits<double>::infinity();
    checkError(Policy::create(nan, 1.0, 0.0, 0.0, Mode::BoundedCommandEstimate),
               Error::InvalidXTolerance, "rejects a NaN X tolerance");
    checkError(Policy::create(infinity, 1.0, 0.0, 0.0, Mode::BoundedCommandEstimate),
               Error::InvalidXTolerance, "rejects an infinite X tolerance");
    checkError(Policy::create(1.0, nan, 0.0, 0.0, Mode::BoundedCommandEstimate),
               Error::InvalidYTolerance, "rejects a NaN Y tolerance");
    checkError(Policy::create(1.0, infinity, 0.0, 0.0, Mode::BoundedCommandEstimate),
               Error::InvalidYTolerance, "rejects an infinite Y tolerance");
}

void testRejectsInvalidHomeUncertainty() {
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const auto infinity = std::numeric_limits<double>::infinity();
    checkError(Policy::create(1.0, 1.0, -0.01, 0.0, Mode::BoundedCommandEstimate),
               Error::InvalidXHomeUncertainty, "rejects negative X home-reference uncertainty");
    checkError(Policy::create(1.0, 1.0, nan, 0.0, Mode::BoundedCommandEstimate),
               Error::InvalidXHomeUncertainty, "rejects NaN X home-reference uncertainty");
    checkError(Policy::create(1.0, 1.0, 0.0, -0.01, Mode::BoundedCommandEstimate),
               Error::InvalidYHomeUncertainty, "rejects negative Y home-reference uncertainty");
    checkError(Policy::create(1.0, 1.0, 0.0, infinity, Mode::BoundedCommandEstimate),
               Error::InvalidYHomeUncertainty, "rejects infinite Y home-reference uncertainty");
}

void testRejectsUnknownVerificationMode() {
    const auto invalidMode = static_cast<Mode>(255);
    checkError(Policy::create(1.0, 1.0, 0.0, 0.0, invalidMode),
               Error::InvalidMode, "rejects an enum value not defined by the verification contract");
}
} // namespace

int main() {
    testAcceptsValidBoundedAndMeasuredPolicies();
    testRejectsInvalidTargetTolerances();
    testRejectsNonFiniteTargetTolerances();
    testRejectsInvalidHomeUncertainty();
    testRejectsUnknownVerificationMode();

    if (failures != 0) {
        std::cerr << failures << " stage verification policy test(s) failed\n";
        return 1;
    }
    std::cout << "All stage verification policy tests passed\n";
    return 0;
}
