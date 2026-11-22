#pragma once

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace camera {

// Generic image dimensions. A resolution is a value, not a vendor table index.
struct ImageResolution {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// Identity facts are optional because discovery APIs do not all expose the same
// fields. The opaque enumeration id is deliberately not treated as a serial
// number or stable identity across disconnect/replug.
struct CameraIdentity {
    std::optional<std::string> manufacturer;
    std::optional<std::string> model;
    std::optional<std::string> serialNumber;
    std::optional<std::string> firmwareVersion;
    std::optional<std::string> deviceVersion;
};

enum class CameraCapability : std::uint8_t {
    VideoAcquisition,
    RawPixelOutput,
    HardwareRegionOfInterest,
    BinningOrSkipping,
    SoftwareTrigger,
    ExternalTrigger,
    SingleFrameTrigger,
    SelfTrigger,
    HardwareEvents,
    SensorTemperatureTelemetry,
    ThermoelectricCooling,
    CoolingFan,
    FocusMotor,
    AutoFocus,
    AstroAutoFocuser,
    ST4GuidePort,
    FilterWheel,
    EmbeddedLightSource,
    StandaloneLightSource,
    HardwareFrameBuffer,
    LargeHardwareFrameBuffer,
    ImageSignalProcessing,
    BlackLevelControl,
    PreciseFrameRateControl,
    AntiFogHeater,
    HighFullWell,
    HDRConversionGain,
    Count
};

enum class CapabilityStatus : std::uint8_t {
    Unknown,
    Unsupported,
    Supported
};

// A model capability is separate from a feature's current availability or
// writability. Unknown is distinct from known-unsupported, which is important
// when a backend has no model descriptor or incomplete discovery metadata.
class CameraCapabilities final {
public:
    static constexpr std::size_t count =
        static_cast<std::size_t>(CameraCapability::Count);

    void set(CameraCapability capability, bool supported) noexcept {
        const auto index = indexOf(capability);
        if (index >= count) return;
        known_.set(index);
        supported_.set(index, supported);
    }

    [[nodiscard]] CapabilityStatus status(
        CameraCapability capability) const noexcept {
        const auto index = indexOf(capability);
        if (index >= count || !known_.test(index))
            return CapabilityStatus::Unknown;
        return supported_.test(index)
            ? CapabilityStatus::Supported
            : CapabilityStatus::Unsupported;
    }

    [[nodiscard]] bool isKnown(CameraCapability capability) const noexcept {
        return status(capability) != CapabilityStatus::Unknown;
    }

    [[nodiscard]] bool supports(CameraCapability capability) const noexcept {
        return status(capability) == CapabilityStatus::Supported;
    }

private:
    [[nodiscard]] static constexpr std::size_t indexOf(
        CameraCapability capability) noexcept {
        return static_cast<std::size_t>(capability);
    }

    std::bitset<count> known_{};
    std::bitset<count> supported_{};
};

// Backend-neutral discovery information. Backend-specific model flags,
// transport codes, hardware indices, and accessory limits belong in the
// backend's derived information type rather than in this contract.
struct CameraDeviceInfo {
    std::string id;
    std::string displayName;
    CameraIdentity identity;
    CameraCapabilities capabilities;
    std::vector<ImageResolution> previewResolutions;
};

} // namespace camera
