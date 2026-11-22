#pragma once
#include "amscope/CameraTypes.h"
#include "amscope/AmcamCameraProfiles.h"
#include "camera/CameraFeatureAccess.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace amscope {
class AmcamCamera final : public camera::ICameraFeatureAccess {
public:
    // Both callbacks execute on the acquisition worker. They must not call
    // start/stop/close or destroy this AmcamCamera synchronously. Queue camera
    // lifecycle and destruction to the owning application/control thread.
    using FrameCallback = std::function<void(FramePtr)>;
    using ErrorCallback = std::function<void(const CameraError&)>;

    explicit AmcamCamera(
        AmcamCameraProfileRegistry profiles = AmcamCameraProfileRegistry{});
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

    // Non-feature camera command and telemetry surfaces.
    bool autoWhiteBalance(CameraError* error = nullptr);
    bool readFrameRate(FrameRateStatistics& statistics, CameraError* error = nullptr);
    bool readTriggerConfiguration(
        AmcamTriggerConfiguration& configuration, CameraError* error = nullptr);
    // Trigger configuration is stopped-state-only. Triggered modes require
    // the SDK hardware heartbeat capability for reliable idle-period health.
    bool configureTrigger(
        const AmcamTriggerConfiguration& configuration,
        CameraError* error = nullptr);
    // Requests a finite burst; zero and 0xffff (continuous) are intentionally
    // excluded. The existing callback/worker/PullImage path delivers frames.
    bool sendSoftwareTrigger(std::uint16_t imageCount = 1,
                             CameraError* error = nullptr);
    bool readEmbeddedLightSource(
        EmbeddedLightSourceState& state, CameraError* error = nullptr);
    bool setEmbeddedLightSourceLevel(
        std::uint32_t level, CameraError* error = nullptr);
    bool readFilterWheelState(
        FilterWheelState& state, CameraError* error = nullptr);
    // Submits a move command. Motion completion is observed through
    // readFilterWheelState(); this call does not block until the wheel arrives.
    bool moveFilterWheel(
        std::uint32_t position,
        FilterWheelDirection direction = FilterWheelDirection::Automatic,
        CameraError* error = nullptr);
    // A Stop command must use duration 0; directional guide pulses require
    // a positive duration in milliseconds.
    bool st4Guide(
        ST4GuideDirection direction,
        std::uint32_t durationMilliseconds,
        CameraError* error = nullptr);
    bool readST4GuideState(
        ST4GuideState& state, CameraError* error = nullptr);
    bool readAstroAutoFocuserState(
        AstroAutoFocuserState& state, CameraError* error = nullptr);
    // Move is asynchronous at device level; read state to observe completion.
    bool moveAstroAutoFocuser(
        std::int32_t targetPosition, CameraError* error = nullptr);
    bool haltAstroAutoFocuser(CameraError* error = nullptr);
    bool readLensFocusState(LensFocusState& state, CameraError* error = nullptr);
    // Position is accepted only in physical manual-focus mode and inside the
    // near/far limits reported by Amcam_get_LensInfo.
    bool setManualLensFocusPosition(
        std::int32_t position, CameraError* error = nullptr);
    // posAM is an index into AmcamLensInfo::arrayFN, not an f-number value.
    bool setLensAperturePosition(
        std::uint32_t positionIndex, CameraError* error = nullptr);
    bool readStillResolutions(std::vector<StillResolution>& resolutions,
                              CameraError* error = nullptr);
    // Captures a still frame while live acquisition remains active.
    bool captureStill(const StillCaptureRequest& request,
                      FramePtr& frame,
                      CameraError* error = nullptr);

    camera::CameraFeatureResult<std::vector<camera::CameraFeatureDescriptor>>
    featureDescriptors() override;
    camera::CameraFeatureResult<camera::CameraFeatureValue>
    readFeature(std::string_view name) override;
    camera::CameraFeatureResult<std::monostate>
    writeFeature(std::string_view name, const camera::CameraFeatureValue& value) override;

private:
    // Backend-only compatibility primitives. Application settings must use
    // ICameraFeatureAccess so feature validation and model policy cannot be bypassed.
    bool setResolution(std::uint32_t resolutionIndex, CameraError* error = nullptr);
    bool setAutoExposure(bool enabled, CameraError* error = nullptr);
    bool setAutoExposureMode(AmcamAutoExposureMode mode, CameraError* error = nullptr);
    bool setExposureTime(std::uint32_t microseconds, CameraError* error = nullptr);
    bool setExposureGain(std::uint16_t gainPercent, CameraError* error = nullptr);
    bool readExposure(ExposureSettings& settings, CameraError* error = nullptr);
    bool setWhiteBalance(int temperature, int tint, CameraError* error = nullptr);
    bool readWhiteBalance(WhiteBalanceSettings& settings, CameraError* error = nullptr);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace amscope
