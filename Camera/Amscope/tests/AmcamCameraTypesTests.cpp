#include "amscope/CameraTypes.h"

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

static_assert(static_cast<std::int32_t>(amscope::AmcamAutoExposureMode::Off) == 0,
              "Off must match Amcam AutoExpoEnable value 0");
static_assert(static_cast<std::int32_t>(amscope::AmcamAutoExposureMode::Continuous) == 1,
              "Continuous must match Amcam AutoExpoEnable value 1");
static_assert(static_cast<std::int32_t>(amscope::AmcamAutoExposureMode::Once) == 2,
              "Once must match Amcam AutoExpoEnable value 2");
static_assert(!amscope::isAutomaticExposureMode(amscope::AmcamAutoExposureMode::Off),
              "Off is the only manual exposure mode");
static_assert(amscope::isAutomaticExposureMode(amscope::AmcamAutoExposureMode::Continuous),
              "Continuous is automatic");
static_assert(amscope::isAutomaticExposureMode(amscope::AmcamAutoExposureMode::Once),
              "Once is automatic while the one-shot operation is active");
static_assert(amscope::amcamAutoExposureModeSymbol(amscope::AmcamAutoExposureMode::Once) == "Once",
              "Once must remain the exact feature enum symbol");

constexpr bool parsesOnceToNativeMode() {
    auto mode = amscope::AmcamAutoExposureMode::Off;
    return amscope::parseAmcamAutoExposureMode("Once", mode) &&
           mode == amscope::AmcamAutoExposureMode::Once;
}
static_assert(parsesOnceToNativeMode(),
              "The public Once symbol must map to native SDK value 2");

int main() {
    using amscope::AmcamAutoExposureMode;

    amscope::Frame raw8;
    raw8.pixelFormat = amscope::PixelFormat::Raw8;
    raw8.pixelFormatCode = 0x42474752U; // RGGB FourCC
    raw8.bitsPerPixel = 8;
    require(raw8.pixelFormat != amscope::PixelFormat::RGB24,
            "RAW8 is not misidentified as RGB24");
    require(raw8.bitsPerPixel == 8 && raw8.pixelFormatCode != 0,
            "RAW8 retains sensor format identity");

    amscope::Frame raw16;
    raw16.pixelFormat = amscope::PixelFormat::Raw16;
    raw16.bitsPerPixel = 12;
    require(raw16.pixelFormat != amscope::PixelFormat::RGB24 &&
                raw16.bitsPerPixel > 8,
            "higher-bit-depth RAW is represented separately from RGB24");

    AmcamAutoExposureMode mode = AmcamAutoExposureMode::Off;
    require(amscope::parseAmcamAutoExposureMode("Continuous", mode) &&
                mode == AmcamAutoExposureMode::Continuous,
            "Continuous parses to native SDK mode 1");
    require(amscope::parseAmcamAutoExposureMode("Off", mode) &&
                mode == AmcamAutoExposureMode::Off,
            "Off parses to native SDK mode 0");
    require(!amscope::parseAmcamAutoExposureMode("Once ", mode),
            "feature enum symbols are exact and do not silently normalize");
    require(!amscope::parseAmcamAutoExposureMode("Auto", mode),
            "unknown exposure symbols are rejected");

    std::cout << "All Amcam camera-type and exposure-mode tests passed.\n";
    return EXIT_SUCCESS;
}
