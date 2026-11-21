#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace amscope {
enum class CameraState : std::uint8_t { Closed, Open, Starting, Streaming, Stopping, Faulted };
struct Resolution { std::uint32_t width = 0; std::uint32_t height = 0; };
struct CameraInfo {
    std::string id;
    std::string displayName;
    std::string modelName;
    std::string modelCode = "Unknown";
    std::uint64_t modelFlags = 0;
    bool hasModelDescriptor = false;
    std::optional<std::uint32_t> maximumSpeedLevel;
    std::vector<Resolution> previewResolutions;
};
struct CameraError {
    std::int32_t vendorCode = 0;
    std::string operation;
    std::string message;
};
enum class PixelFormat : std::uint8_t { RGB24 };
struct Frame {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t strideBytes = 0;
    PixelFormat pixelFormat = PixelFormat::RGB24;
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
enum class AmcamAutoExposureMode : std::int32_t {
    Off = 0,
    Continuous = 1,
    Once = 2
};

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
