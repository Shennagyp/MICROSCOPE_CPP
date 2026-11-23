#include "amscope/AmcamCamera.h"

static_assert(static_cast<std::uint8_t>(amscope::AmcamIoLine::OptoIsolatedInput) == 0);
static_assert(static_cast<std::uint8_t>(amscope::AmcamIoLine::OptoIsolatedOutput) == 1);
static_assert(static_cast<std::uint8_t>(amscope::AmcamIoLine::GPIO0) == 2);
static_assert(static_cast<std::uint8_t>(amscope::AmcamIoLine::GPIO1) == 3);
static_assert(static_cast<std::uint8_t>(amscope::AmcamIoDirection::Input) == 0);
static_assert(static_cast<std::uint8_t>(amscope::AmcamIoDirection::Output) == 1);
static_assert(static_cast<std::uint8_t>(amscope::AmcamIoOutputMode::Strobe) == 2);

#include <cstdlib>
#include <iostream>
#include <memory>
#include <type_traits>
#include <utility>

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

static_assert(std::is_same_v<
                  decltype(std::declval<amscope::AmcamCamera&>().readFrameRateMeasurement()),
                  camera::CameraOperationResult<camera::FrameRateMeasurement>>,
              "The typed frame-rate API must expose the common measurement result");

int main() {
    using amscope::AmcamAutoExposureMode;

    const amscope::FrameRateStatistics nativeRate{91.0, 1322, 91, 1000};
    const auto commonRate = amscope::toCommonFrameRateMeasurement(nativeRate);
    const auto commonRateValue = commonRate.framesPerSecond();
    require(commonRateValue && *commonRateValue == 91.0 &&
                commonRate.cumulativeFrames &&
                *commonRate.cumulativeFrames == 1322,
            "AmScope frame-rate telemetry preserves interval and cumulative counts");

    const amscope::CameraError nativeError{
        -2147467259, "camera.start", "SDK rejected acquisition startup"};
    const auto commonError = amscope::toCommonOperationError(nativeError);
    camera::CameraOperationResult<camera::FrameRateMeasurement> typedRateResult{
        commonRate};
    require(std::holds_alternative<camera::FrameRateMeasurement>(typedRateResult),
            "typed operation result carries the common measured-rate value");
    typedRateResult = commonError;
    require(std::holds_alternative<camera::CameraOperationError>(typedRateResult) &&
                std::get<camera::CameraOperationError>(typedRateResult).native &&
                std::get<camera::CameraOperationError>(typedRateResult).native->code ==
                    nativeError.vendorCode,
            "typed operation result carries the converted native error without loss");
    require(commonError.code == nativeError.commonCode &&
                commonError.operation == "camera.start" &&
                commonError.message == "SDK rejected acquisition startup",
            "AmScope operation errors map to a common category with operation context");
    require(commonError.native &&
                commonError.native->domain == "amcam.sdk" &&
                commonError.native->code == -2147467259,
            "AmScope native error identity is preserved without reinterpreting its code");

    const amscope::CameraError invalidStateError{
        -1, "readFrameRate", "Acquisition must be running.",
        camera::CameraErrorCode::InvalidState};
    const auto commonInvalidState = amscope::toCommonOperationError(invalidStateError);
    require(commonInvalidState.code == camera::CameraErrorCode::InvalidState,
            "typed operation errors preserve explicit local precondition classification");

    static_assert(std::is_same_v<amscope::FramePtr, camera::FramePtr>,
                  "The AmScope frame callback must expose the common frame lease");
    static_assert(std::is_base_of_v<camera::Frame, amscope::AmcamFrame>,
                  "Native frame metadata must extend, not replace, the common frame");

    amscope::AmcamFrame raw8;
    raw8.pixelFormat = amscope::PixelFormat::Raw8;
    raw8.pixelFormatIdentifier =
        camera::PixelFormatIdentifier{"amcam.fourcc", 0x42474752U}; // RGGB FourCC
    raw8.significantBitsPerPixel = 10;
    raw8.storageBitsPerPixel = 8;
    raw8.strideBytes = 1280;
    require(raw8.pixelFormat != amscope::PixelFormat::RGB24,
            "RAW8 storage is not misidentified as RGB24");
    require(raw8.significantBitsPerPixel == 10 &&
                raw8.storageBitsPerPixel == 8 &&
                raw8.pixelFormatIdentifier &&
                raw8.pixelFormatIdentifier->scheme == "amcam.fourcc" &&
                raw8.pixelFormatIdentifier->value == 0x42474752U &&
                raw8.strideBytes == 1280,
            "RAW frame retains its opaque FourCC and source depth separately from its storage container");

    amscope::AmcamFrame raw16;
    raw16.pixelFormat = amscope::PixelFormat::Raw16;
    raw16.significantBitsPerPixel = 12;
    raw16.storageBitsPerPixel = 16;
    raw16.strideBytes = 2560;
    require(raw16.pixelFormat != amscope::PixelFormat::RGB24 &&
                raw16.significantBitsPerPixel > 8 &&
                raw16.storageBitsPerPixel == 16 &&
                raw16.strideBytes == 2560,
            "12-bit sensor samples can be represented in a 16-bit storage container");

    // An SDK validity bit distinguishes a reported zero from unavailable data.
    raw8.sequenceNumber = 0;
    raw8.timestamp = camera::FrameTimestamp{0, 1000000ULL, std::nullopt};
    raw8.exposure = camera::FrameExposure{0, 1000000ULL};
    raw8.sdkFlags = 0x1U | 0x2U | 0x4U;
    raw8.exposureGainPercent = 100;
    raw8.blackLevel = 0;

    camera::FramePtr commonLease = std::make_shared<amscope::AmcamFrame>(raw8);
    require(commonLease->sequenceNumber && *commonLease->sequenceNumber == 0,
            "A present zero sequence value remains distinguishable from unavailable sequence metadata");
    require(commonLease->timestamp && commonLease->timestamp->ticks == 0 &&
                commonLease->timestamp->ticksPerSecond == 1000000ULL &&
                !commonLease->timestamp->clockDomain,
            "A zero timestamp retains its timebase and unknown clock domain");
    require(commonLease->exposure &&
                commonLease->exposure->durationTicks == 0 &&
                commonLease->exposure->ticksPerSecond == 1000000ULL,
            "A present zero exposure sample is not confused with missing metadata");
    const auto nativeLease =
        std::dynamic_pointer_cast<const amscope::AmcamFrame>(commonLease);
    require(nativeLease && nativeLease->sdkFlags == raw8.sdkFlags &&
                nativeLease->exposureGainPercent == 100 &&
                nativeLease->blackLevel == 0,
            "Amcam-specific frame flags and telemetry remain accessible through the native extension");

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
