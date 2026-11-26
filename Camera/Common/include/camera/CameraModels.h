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
    GlobalShutter,
    MonochromeSensor,
    SensorCCDProgressive,
    SensorCCDInterlaced,
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

// Static sensor technology is model data. Do not infer a rolling shutter
// merely because a backend did not advertise global-shutter support.
enum class CameraSensorTechnology : std::uint8_t {
    CCD,
    CMOS,
    ScientificCMOS,
    Unknown
};

enum class CameraShutterType : std::uint8_t {
    Global,
    Rolling,
    FrameTransfer,
    GlobalReset,
    Unknown
};

enum class CameraColorMode : std::uint8_t {
    Monochrome,
    Color,
    Unknown
};

enum class CameraSpecificationProvenance : std::uint8_t {
    ManufacturerDatasheet,
    VendorModelDescriptor,
    UserVerified,
    Measured,
    Unknown
};

struct CameraQuantumEfficiencySample {
    double wavelengthNanometers = 0.0;
    // Fraction of incident photons converted to detected electrons: [0, 1].
    double quantumEfficiency = 0.0;
};

// Persistable model-level imaging data. Fields remain optional because many
// SDKs expose only model identity/capability flags, not scientific sensor data.
// QE samples are wavelength-dependent and must not be replaced by one
// wavelength-independent "sensitivity" number.
struct CameraSensorCharacteristics {
    std::optional<CameraSensorTechnology> technology;
    std::optional<CameraShutterType> shutterType;
    std::optional<CameraColorMode> colorMode;
    std::optional<ImageResolution> activeSensorDimensions;
    std::optional<double> pixelPitchXMicrometers;
    std::optional<double> pixelPitchYMicrometers;
    std::optional<double> readNoiseElectronsRms;
    std::optional<double> darkCurrentElectronsPerPixelPerSecond;
    std::optional<double> fullWellCapacityElectrons;
    std::optional<double> maximumFrameRateFramesPerSecond;
    std::vector<CameraQuantumEfficiencySample> quantumEfficiency;
    CameraSpecificationProvenance provenance =
        CameraSpecificationProvenance::Unknown;
};

// Backend-neutral discovery information. Backend-specific model flags,
// transport codes, hardware indices, and accessory limits belong in the
// backend's derived information type rather than in this contract.
struct CameraDeviceInfo {
    std::string id;
    std::string displayName;
    CameraIdentity identity;
    CameraCapabilities capabilities;
    // Optional model profile loaded from the persistent camera catalog or
    // enriched by a backend with authoritative manufacturer/model data.
    // It is deliberately separate from current feature availability.
    std::optional<CameraSensorCharacteristics> sensorCharacteristics;
    std::vector<ImageResolution> previewResolutions;
};

} // namespace camera
