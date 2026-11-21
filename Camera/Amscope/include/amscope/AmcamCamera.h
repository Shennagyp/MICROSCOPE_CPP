#pragma once
#include "amscope/CameraTypes.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace amscope {
class AmcamCamera final {
public:
    using FrameCallback = std::function<void(FramePtr)>;
    using ErrorCallback = std::function<void(const CameraError&)>;

    AmcamCamera();
    ~AmcamCamera();
    AmcamCamera(const AmcamCamera&) = delete;
    AmcamCamera& operator=(const AmcamCamera&) = delete;
    AmcamCamera(AmcamCamera&&) = delete;
    AmcamCamera& operator=(AmcamCamera&&) = delete;

    [[nodiscard]] static std::vector<CameraInfo> enumerate(CameraError* error = nullptr);
    // Use the opaque id from enumerate(); it may change after disconnect/replug.
    bool open(const std::string& deviceId, CameraError* error = nullptr);
    bool close(CameraError* error = nullptr);
    bool start(FrameCallback onFrame, ErrorCallback onError = {}, CameraError* error = nullptr);
    bool stop(CameraError* error = nullptr);

    [[nodiscard]] CameraState state() const noexcept;
    [[nodiscard]] std::uint64_t droppedFrames() const noexcept;

    // Resolution changes are allowed only while open and acquisition is stopped.
    bool setResolution(std::uint32_t resolutionIndex, CameraError* error = nullptr);
    bool setAutoExposure(bool enabled, CameraError* error = nullptr);
    bool setExposureTime(std::uint32_t microseconds, CameraError* error = nullptr);
    bool setExposureGain(std::uint16_t gainPercent, CameraError* error = nullptr);
    bool readExposure(ExposureSettings& settings, CameraError* error = nullptr);
    bool setWhiteBalance(int temperature, int tint, CameraError* error = nullptr);
    bool readWhiteBalance(WhiteBalanceSettings& settings, CameraError* error = nullptr);
    bool autoWhiteBalance(CameraError* error = nullptr);
    bool readFrameRate(FrameRateStatistics& statistics, CameraError* error = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace amscope
