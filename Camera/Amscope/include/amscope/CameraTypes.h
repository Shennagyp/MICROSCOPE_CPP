#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "camera/CameraModels.h"
#include "camera/CameraFrames.h"
#include "camera/CameraOperationError.h"
#include "camera/CameraFrameRate.h"

namespace amscope {
enum class CameraState : std::uint8_t { Closed, Open, Starting, Streaming, Stopping, Faulted };

// Preview dimensions are common across camera backends; Amcam-specific
// resolution indices remain in the native StillResolution type.
using Resolution = camera::ImageResolution;

struct StillResolution {
    std::uint32_t index = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// Common discovery identity/capabilities are inherited from CameraDeviceInfo.
// Model flags, speed/fan levels, and I/O line counts remain native Amcam facts.
struct CameraInfo : camera::CameraDeviceInfo {
    std::string modelName;
    std::string modelCode = "Unknown";
    std::uint64_t modelFlags = 0;
    bool hasModelDescriptor = false;
    std::optional<std::uint32_t> maximumSpeedLevel;
    std::optional<std::uint32_t> maximumFanSpeed;
    std::uint32_t ioControlLineCount = 0;
};
struct CameraError {
    std::int32_t vendorCode = 0;
    std::string operation;
    std::string message;
    // Classified only from documented Amcam HRESULT meanings. Unknown native
    // codes remain BackendFailure; vendor identity is preserved separately.
    camera::CameraErrorCode commonCode = camera::CameraErrorCode::BackendFailure;
};

// Explicit boundary conversion for operation-level APIs. The native code is
// preserved in its Amcam domain; it is not reinterpreted as a common code.
[[nodiscard]] inline camera::CameraOperationError toCommonOperationError(
    const CameraError& error) {
    camera::CameraOperationError common;
    common.code = error.commonCode;
    common.operation = error.operation;
    common.message = error.message;
    common.native = camera::CameraNativeError{"amcam.sdk", error.vendorCode};
    return common;
}
using PixelFormat = camera::PixelFormat;
using Frame = camera::Frame;
using FramePtr = camera::FramePtr;

// Amcam-only metadata remains available to native consumers through a typed
// frame extension. Generic consumers use camera::Frame / camera::FramePtr.
struct AmcamFrame final : camera::Frame {
    std::uint32_t sdkFlags = 0;
    std::optional<std::uint16_t> exposureGainPercent;
    std::optional<std::uint16_t> blackLevel;
};

using AmcamFramePtr = std::shared_ptr<const AmcamFrame>;
enum class StillCaptureMode : std::uint8_t { Processed, Raw };

struct StillCaptureRequest {
    StillCaptureMode mode = StillCaptureMode::Processed;
    // UINT32_MAX selects the still-resolution entry matching live preview dimensions.
    std::uint32_t resolutionIndex = 0xffffffffU;
    std::uint32_t timeoutMilliseconds = 30000;
};

// Values intentionally match Amcam_get/put_AutoExpoEnable and Swift's
// AmcamAutoExposureMode raw values. Once is the vendor's native one-shot mode.
enum class AmcamAutoExposureMode : std::int32_t {
    Off = 0,
    Continuous = 1,
    Once = 2
};

[[nodiscard]] constexpr bool isAutomaticExposureMode(
    AmcamAutoExposureMode mode) noexcept {
    return mode != AmcamAutoExposureMode::Off;
}

[[nodiscard]] constexpr std::string_view amcamAutoExposureModeSymbol(
    AmcamAutoExposureMode mode) noexcept {
    switch (mode) {
    case AmcamAutoExposureMode::Off: return "Off";
    case AmcamAutoExposureMode::Once: return "Once";
    case AmcamAutoExposureMode::Continuous: return "Continuous";
    }
    return {};
}

[[nodiscard]] constexpr bool parseAmcamAutoExposureMode(
    std::string_view symbol,
    AmcamAutoExposureMode& mode) noexcept {
    if (symbol == "Off") mode = AmcamAutoExposureMode::Off;
    else if (symbol == "Once") mode = AmcamAutoExposureMode::Once;
    else if (symbol == "Continuous") mode = AmcamAutoExposureMode::Continuous;
    else return false;
    return true;
}

struct ExposureSettings {
    AmcamAutoExposureMode autoMode = AmcamAutoExposureMode::Off;
    bool automatic = false;
    std::uint32_t timeMicroseconds = 0;
    std::uint32_t minimumTimeMicroseconds = 0;
    std::uint32_t maximumTimeMicroseconds = 0;
    std::uint32_t defaultTimeMicroseconds = 0;
    std::uint16_t gainPercent = 0;
    std::uint16_t minimumGainPercent = 0;
    std::uint16_t maximumGainPercent = 0;
    std::uint16_t defaultGainPercent = 0;
};
struct WhiteBalanceSettings { int temperature = 0; int tint = 0; };
struct FrameRateStatistics {
    double framesPerSecond = 0.0;
    std::uint32_t totalFrames = 0;
    // Preserve the SDK's raw measurement interval so common consumers do not
    // have to reverse-engineer it from a rounded floating-point rate.
    std::uint32_t framesInInterval = 0;
    std::uint32_t intervalMilliseconds = 0;
};

[[nodiscard]] inline camera::FrameRateMeasurement toCommonFrameRateMeasurement(
    const FrameRateStatistics& statistics) {
    return camera::FrameRateMeasurement{
        statistics.framesInInterval,
        statistics.intervalMilliseconds,
        1000,
        statistics.totalFrames
    };
}

struct EmbeddedLightSourceState {
    std::uint32_t level = 0;
    std::uint32_t maximumLevel = 0;
};

enum class FilterWheelDirection : std::uint8_t {
    Clockwise,
    Automatic
};

struct FilterWheelState {
    std::uint32_t slotCount = 0;
    // Empty only while the SDK reports motion in progress.
    std::optional<std::uint32_t> position;
    bool moving = false;
};

enum class ST4GuideDirection : std::uint8_t {
    North = 0,
    South = 1,
    East = 2,
    West = 3,
    Stop = 4
};

struct ST4GuideState {
    bool pulseGuiding = false;
};

struct AstroAutoFocuserState {
    std::int32_t position = 0;
    std::int32_t minimumPosition = 0;
    std::int32_t maximumPosition = 0;
    std::int32_t stepSize = 0;
    bool moving = false;
    std::optional<std::int32_t> temperatureTenthsC;
    std::optional<std::int32_t> ambientTemperatureSdkUnits;
};


enum class AmcamIoLine : std::uint8_t {
    OptoIsolatedInput = 0, OptoIsolatedOutput = 1, GPIO0 = 2, GPIO1 = 3
};
enum class AmcamIoDirection : std::uint8_t { Input = 0, Output = 1 };
enum class AmcamIoSignalFormat : std::uint8_t {
    NotConnected = 0, TriState = 1, TTL = 2, LVDS = 3, RS422 = 4, OptoCoupled = 5
};
enum class AmcamIoInputActivation : std::uint8_t {
    RisingEdge = 0, FallingEdge = 1, LevelHigh = 2, LevelLow = 3
};
enum class AmcamIoOutputMode : std::uint8_t {
    FrameTriggerWait = 0, ExposureActive = 1, Strobe = 2,
    User = 3, Counter = 4, Timer = 5
};
struct AmcamIoLineState {
    AmcamIoLine line = AmcamIoLine::OptoIsolatedInput;
    // Bit 0 => input supported; bit 1 => output supported.
    std::uint8_t supportedModes = 0;
    std::optional<AmcamIoDirection> direction;
    std::optional<AmcamIoSignalFormat> signalFormat;
    std::optional<bool> outputInverted;
    std::optional<AmcamIoInputActivation> inputActivation;
    std::optional<std::uint32_t> debounceTimeMicroseconds;
    std::optional<bool> inputHigh;
    std::optional<AmcamIoOutputMode> outputMode;
    std::optional<bool> strobeDelayMode;
    std::optional<std::uint32_t> strobeDelayMicroseconds;
    std::optional<std::uint32_t> strobeDurationMicroseconds;
};
struct AmcamIoLineConfiguration {
    AmcamIoLine line = AmcamIoLine::OptoIsolatedInput;
    std::optional<AmcamIoDirection> direction;
    std::optional<AmcamIoSignalFormat> signalFormat;
    std::optional<bool> outputInverted;
    std::optional<AmcamIoInputActivation> inputActivation;
    std::optional<std::uint32_t> debounceTimeMicroseconds;
    std::optional<AmcamIoOutputMode> outputMode;
    std::optional<bool> strobeDelayMode;
    std::optional<std::uint32_t> strobeDelayMicroseconds;
    std::optional<std::uint32_t> strobeDurationMicroseconds;
};

enum class AmcamTriggerMode : std::uint8_t {
    Video = 0,
    SoftwareOrSimulated = 1,
    External = 2,
    ExternalAndSoftware = 3
};

enum class AmcamTriggerSource : std::uint8_t {
    OptoIsolatedInput = 0,
    GPIO0 = 1,
    GPIO1 = 2,
    Counter = 3,
    PWM = 4,
    Software = 5
};

struct AmcamTriggerConfiguration {
    AmcamTriggerMode mode = AmcamTriggerMode::Video;
    std::optional<AmcamTriggerSource> externalSource;
    std::uint32_t heartbeatIntervalMilliseconds = 1000;
};

struct LensFocusState {
    std::uint16_t lensId = 0;
    std::uint8_t lensType = 0;
    std::uint8_t lensAfMfStatus = 0;
    bool manualFocusSelected = false;
    std::uint16_t minimumFocalLength = 0;
    std::uint16_t currentFocalLength = 0;
    std::uint16_t maximumFocalLength = 0;
    std::int32_t nearFocusMotorPosition = 0;
    std::int32_t currentFocusMotorPosition = 0;
    std::int32_t farFocusMotorPosition = 0;
    std::uint16_t minimumFocusDistance = 0;
    std::uint16_t maximumFocusDistance = 0;
    std::int32_t currentApertureValue = 0;
    std::uint8_t minimumApertureIndex = 0;
    std::uint8_t maximumApertureIndex = 0;
    std::uint8_t aperturePositionIndex = 0;
    // Index-aligned labels copied from AmcamLensInfo::arrayFN.
    std::vector<std::string> apertureLabels;
    // Preserve raw SDK enum values, including values added by later SDKs.
    std::optional<std::uint32_t> autofocusMode;
    std::optional<std::uint32_t> autofocusStatus;
    std::optional<bool> apertureCalibrated;
};
} // namespace amscope
