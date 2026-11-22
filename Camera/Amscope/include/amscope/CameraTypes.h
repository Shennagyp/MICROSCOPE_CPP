#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace amscope {
enum class CameraState : std::uint8_t { Closed, Open, Starting, Streaming, Stopping, Faulted };
struct Resolution { std::uint32_t width = 0; std::uint32_t height = 0; };
struct StillResolution {
    std::uint32_t index = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};
struct CameraInfo {
    std::string id;
    std::string displayName;
    std::string modelName;
    std::string modelCode = "Unknown";
    std::uint64_t modelFlags = 0;
    bool hasModelDescriptor = false;
    std::optional<std::uint32_t> maximumSpeedLevel;
    std::optional<std::uint32_t> maximumFanSpeed;
    std::vector<Resolution> previewResolutions;
};
struct CameraError {
    std::int32_t vendorCode = 0;
    std::string operation;
    std::string message;
};
enum class PixelFormat : std::uint8_t { RGB24, Raw8, Raw16 };
enum class StillCaptureMode : std::uint8_t { Processed, Raw };

struct StillCaptureRequest {
    StillCaptureMode mode = StillCaptureMode::Processed;
    // UINT32_MAX selects the still-resolution entry matching live preview dimensions.
    std::uint32_t resolutionIndex = 0xffffffffU;
    std::uint32_t timeoutMilliseconds = 30000;
};

struct Frame {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t strideBytes = 0;
    PixelFormat pixelFormat = PixelFormat::RGB24;
    // Amcam RAW frames retain the SDK FourCC and significant sensor bit depth.
    // RGB24 uses pixelFormatCode=0 and bitsPerPixel=24.
    // Significant pixel depth reported by the sensor/RAW format.
    std::uint32_t pixelFormatCode = 0;
    std::uint8_t bitsPerPixel = 24;
    // Actual payload storage container width (8/16 for Amcam RAW).
    std::uint8_t containerBitsPerPixel = 24;
    bool isStill = false;
    std::uint32_t sequence = 0;
    std::uint64_t timestampMicroseconds = 0;
    std::uint32_t sdkFlags = 0;
    std::uint32_t exposureMicroseconds = 0;
    std::uint16_t exposureGainPercent = 0;
    std::uint16_t blackLevel = 0;
    std::vector<std::uint8_t> pixels;
    [[nodiscard]] const std::uint8_t* data() const noexcept {
        return pixels.empty() ? nullptr : pixels.data();
    }
    [[nodiscard]] std::size_t byteSize() const noexcept { return pixels.size(); }
};
using FramePtr = std::shared_ptr<const Frame>;
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
struct FrameRateStatistics { double framesPerSecond = 0.0; std::uint32_t totalFrames = 0; };
} // namespace amscope
