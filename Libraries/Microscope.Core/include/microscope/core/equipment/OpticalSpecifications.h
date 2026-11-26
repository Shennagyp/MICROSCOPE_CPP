#pragma once

#include "microscope/core/ids/Ids.h"
#include "microscope/core/result/Result.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace microscope::core {

enum class PositiveFiniteValueError {
    NotFinite,
    NotPositive
};

namespace detail {
struct PositiveMicrometersTag final {};
struct NominalMagnificationTag final {};
struct OpticalMagnificationFactorTag final {};
struct NumericalApertureTag final {};
struct PositiveNanometersTag final {};
struct PositiveMicrosecondsTag final {};
struct CondenserApertureTag final {};
struct AxisResolutionTag final {};
struct AxisAccuracyTag final {};
struct AxisRepeatabilityTag final {};
struct AxisUncertaintyTag final {};
} // namespace detail

// Domain-specific positive scalar values. These are not a general unit system.
template <typename Tag>
class PositiveFiniteValue final {
public:
    [[nodiscard]] static Result<
        PositiveFiniteValue,
        PositiveFiniteValueError> create(double value) {
        if (!std::isfinite(value)) {
            return Result<
                PositiveFiniteValue,
                PositiveFiniteValueError>::failure(
                    PositiveFiniteValueError::NotFinite);
        }
        if (value <= 0.0) {
            return Result<
                PositiveFiniteValue,
                PositiveFiniteValueError>::failure(
                    PositiveFiniteValueError::NotPositive);
        }
        return Result<
            PositiveFiniteValue,
            PositiveFiniteValueError>::success(
                PositiveFiniteValue(value));
    }

    [[nodiscard]] double value() const noexcept {
        return value_;
    }

private:
    explicit PositiveFiniteValue(double value) noexcept : value_(value) {}

    double value_;
};

using PositiveMicrometers = PositiveFiniteValue<detail::PositiveMicrometersTag>;
using NominalMagnification = PositiveFiniteValue<detail::NominalMagnificationTag>;
// A factor relative to the objective's rated magnification. Values below 1.0
// represent reduction optics; values above 1.0 represent magnifying relays.
using PositiveOpticalMagnificationFactor =
    PositiveFiniteValue<detail::OpticalMagnificationFactorTag>;
using NumericalAperture = PositiveFiniteValue<detail::NumericalApertureTag>;
using PositiveNanometers = PositiveFiniteValue<detail::PositiveNanometersTag>;
using PositiveMicroseconds = PositiveFiniteValue<detail::PositiveMicrosecondsTag>;

template <typename Tag>
class NonNegativeFiniteValue final {
public:
    [[nodiscard]] static Result<
        NonNegativeFiniteValue,
        PositiveFiniteValueError> create(double value) {
        if (!std::isfinite(value)) {
            return Result<
                NonNegativeFiniteValue,
                PositiveFiniteValueError>::failure(
                    PositiveFiniteValueError::NotFinite);
        }
        if (value < 0.0) {
            return Result<
                NonNegativeFiniteValue,
                PositiveFiniteValueError>::failure(
                    PositiveFiniteValueError::NotPositive);
        }
        return Result<
            NonNegativeFiniteValue,
            PositiveFiniteValueError>::success(
                NonNegativeFiniteValue(value));
    }

    [[nodiscard]] double value() const noexcept {
        return value_;
    }

private:
    explicit NonNegativeFiniteValue(double value) noexcept : value_(value) {}

    double value_;
};

using NonNegativeNumericalAperture =
    NonNegativeFiniteValue<detail::CondenserApertureTag>;
using PositiveAxisResolution = PositiveFiniteValue<detail::AxisResolutionTag>;
using NonNegativeAxisAccuracy = NonNegativeFiniteValue<detail::AxisAccuracyTag>;
using NonNegativeAxisRepeatability =
    NonNegativeFiniteValue<detail::AxisRepeatabilityTag>;
using NonNegativeAxisUncertainty =
    NonNegativeFiniteValue<detail::AxisUncertaintyTag>;

enum class ObjectiveChromaticCorrection {
    Achromatic,
    SemiApochromatic,
    Apochromatic,
    Other,
    Unknown
};

enum class ObjectiveFieldCorrection {
    Plan,
    NonPlan,
    Unknown
};

// Numeric values are stable persistence identities. Add new values without renumbering.
enum class ObjectiveImmersionMedium {
    Air = 0,
    Water = 1,
    Oil = 2,
    Glycerol = 3,
    Silicone = 4,
    Other = 5,
    Unknown = 6
};

enum class ObjectiveSpecificationError {
    InvalidChromaticCorrection,
    InvalidFieldCorrection,
    InvalidImmersionMedium
};

// Specifications belong to an installed physical component. They describe
// capability data only; they do not attach behavior to an objective class.
class ObjectiveSpecification final {
public:
    [[nodiscard]] static Result<
        ObjectiveSpecification,
        ObjectiveSpecificationError> create(
            EquipmentComponentId componentId,
            NominalMagnification nominalMagnification,
            NumericalAperture numericalAperture,
            PositiveMicrometers workingDistance,
            ObjectiveChromaticCorrection chromaticCorrection,
            ObjectiveFieldCorrection fieldCorrection,
            ObjectiveImmersionMedium immersionMedium,
            std::optional<PositiveMicrometers> designCoverglassThickness =
                std::nullopt);

    [[nodiscard]] const EquipmentComponentId& componentId() const noexcept;
    [[nodiscard]] double nominalMagnification() const noexcept;
    [[nodiscard]] double numericalAperture() const noexcept;
    [[nodiscard]] double workingDistanceMicrometers() const noexcept;
    [[nodiscard]] ObjectiveChromaticCorrection chromaticCorrection() const noexcept;
    [[nodiscard]] ObjectiveFieldCorrection fieldCorrection() const noexcept;
    [[nodiscard]] ObjectiveImmersionMedium immersionMedium() const noexcept;
    [[nodiscard]] const std::optional<PositiveMicrometers>&
    designCoverglassThickness() const noexcept;

private:
    ObjectiveSpecification(
        EquipmentComponentId componentId,
        NominalMagnification nominalMagnification,
        NumericalAperture numericalAperture,
        PositiveMicrometers workingDistance,
        ObjectiveChromaticCorrection chromaticCorrection,
        ObjectiveFieldCorrection fieldCorrection,
        ObjectiveImmersionMedium immersionMedium,
        std::optional<PositiveMicrometers> designCoverglassThickness);

    EquipmentComponentId componentId_;
    NominalMagnification nominalMagnification_;
    NumericalAperture numericalAperture_;
    PositiveMicrometers workingDistance_;
    ObjectiveChromaticCorrection chromaticCorrection_;
    ObjectiveFieldCorrection fieldCorrection_;
    ObjectiveImmersionMedium immersionMedium_;
    std::optional<PositiveMicrometers> designCoverglassThickness_;
};

enum class OpticalRelayRole {
    TubeLens,
    IntermediateRelay,
    CameraAdapter,
    ReductionAdapter,
    MagnificationChanger,
    Other
};

enum class OpticalRelaySpecificationError {
    InvalidRole,
    InvalidMagnificationFactor
};

// A physical relay optic contributes a factor along imaging paths that traverse
// its component. Unknown manufacturer/calibration data stays explicitly unknown.
class OpticalRelaySpecification final {
public:
    [[nodiscard]] static Result<
        OpticalRelaySpecification,
        OpticalRelaySpecificationError> create(
            EquipmentComponentId componentId,
            OpticalRelayRole role,
            std::optional<double> magnificationFactor = std::nullopt);

    [[nodiscard]] const EquipmentComponentId& componentId() const noexcept;
    [[nodiscard]] OpticalRelayRole role() const noexcept;
    [[nodiscard]] const std::optional<PositiveOpticalMagnificationFactor>&
    magnificationFactor() const noexcept;

private:
    OpticalRelaySpecification(
        EquipmentComponentId componentId,
        OpticalRelayRole role,
        std::optional<PositiveOpticalMagnificationFactor> magnificationFactor);

    EquipmentComponentId componentId_;
    OpticalRelayRole role_;
    std::optional<PositiveOpticalMagnificationFactor> magnificationFactor_;
};

// Numeric values are stable persistence identities. Add new values without renumbering.
enum class CondenserDesign {
    Abbe = 0,
    Achromatic = 1,
    AchromatAplanatic = 2,
    Other = 3,
    Unknown = 4
};

// Numeric values are stable persistence identities. Add new values without renumbering.
enum class CondenserImmersionMedium {
    Air = 0,
    Water = 1,
    Oil = 2,
    Other = 3,
    Unknown = 4
};

enum class CondenserSpecificationError {
    InvalidDesign,
    InvalidImmersionMedium,
    InvertedApertureRange
};

// Condenser geometry/capability data. Compatibility with objectives,
// contrast methods and illumination is resolved later from declarative profiles.
class CondenserSpecification final {
public:
    [[nodiscard]] static Result<
        CondenserSpecification,
        CondenserSpecificationError> create(
            EquipmentComponentId componentId,
            NonNegativeNumericalAperture minimumAperture,
            NumericalAperture maximumAperture,
            CondenserDesign design,
            CondenserImmersionMedium immersionMedium,
            std::optional<PositiveMicrometers> workingDistance = std::nullopt);

    [[nodiscard]] const EquipmentComponentId& componentId() const noexcept;
    [[nodiscard]] double minimumApertureNA() const noexcept;
    [[nodiscard]] double maximumApertureNA() const noexcept;
    [[nodiscard]] CondenserDesign design() const noexcept;
    [[nodiscard]] CondenserImmersionMedium immersionMedium() const noexcept;
    [[nodiscard]] const std::optional<PositiveMicrometers>&
    workingDistance() const noexcept;

private:
    CondenserSpecification(
        EquipmentComponentId componentId,
        NonNegativeNumericalAperture minimumAperture,
        NumericalAperture maximumAperture,
        CondenserDesign design,
        CondenserImmersionMedium immersionMedium,
        std::optional<PositiveMicrometers> workingDistance);

    EquipmentComponentId componentId_;
    NonNegativeNumericalAperture minimumAperture_;
    NumericalAperture maximumAperture_;
    CondenserDesign design_;
    CondenserImmersionMedium immersionMedium_;
    std::optional<PositiveMicrometers> workingDistance_;
};

enum class PixelDimensionsError {
    ZeroWidth,
    ZeroHeight
};

class PixelDimensions final {
public:
    [[nodiscard]] static Result<PixelDimensions, PixelDimensionsError> create(
        std::uint32_t width,
        std::uint32_t height);

    [[nodiscard]] std::uint32_t width() const noexcept;
    [[nodiscard]] std::uint32_t height() const noexcept;

private:
    PixelDimensions(std::uint32_t width, std::uint32_t height);

    std::uint32_t width_;
    std::uint32_t height_;
};

enum class SensorRegionError {
    ZeroWidth,
    ZeroHeight
};

// Coordinates and extents are measured in physical sensor pixels.
class SensorRegionOfInterest final {
public:
    [[nodiscard]] static Result<
        SensorRegionOfInterest,
        SensorRegionError> create(
            std::uint32_t x,
            std::uint32_t y,
            std::uint32_t width,
            std::uint32_t height);

    [[nodiscard]] std::uint32_t x() const noexcept;
    [[nodiscard]] std::uint32_t y() const noexcept;
    [[nodiscard]] std::uint32_t width() const noexcept;
    [[nodiscard]] std::uint32_t height() const noexcept;

private:
    SensorRegionOfInterest(
        std::uint32_t x,
        std::uint32_t y,
        std::uint32_t width,
        std::uint32_t height);

    std::uint32_t x_;
    std::uint32_t y_;
    std::uint32_t width_;
    std::uint32_t height_;
};

enum class PixelBinningError {
    ZeroHorizontalFactor,
    ZeroVerticalFactor
};

class PixelBinning final {
public:
    [[nodiscard]] static Result<PixelBinning, PixelBinningError> create(
        std::uint32_t horizontalFactor,
        std::uint32_t verticalFactor);

    [[nodiscard]] std::uint32_t horizontalFactor() const noexcept;
    [[nodiscard]] std::uint32_t verticalFactor() const noexcept;

private:
    PixelBinning(
        std::uint32_t horizontalFactor,
        std::uint32_t verticalFactor);

    std::uint32_t horizontalFactor_;
    std::uint32_t verticalFactor_;
};

enum class CameraPixelLayout {
    Monochrome,
    Bayer,
    InterleavedColor,
    PlanarColor,
    Other
};

enum class CameraOutputModeError {
    InvalidPixelLayout,
    InvalidPixelFormatCode,
    BinningExceedsSensorRegion
};

// The sensor ROI and delivered dimensions define the physical sensor-pixel
// mapping for an output pixel. This captures binning, decimation and resampling
// without assuming that sensor pixels equal delivered image pixels.
class CameraOutputMode final {
public:
    [[nodiscard]] static Result<
        CameraOutputMode,
        CameraOutputModeError> create(
            CameraModeId id,
            SensorRegionOfInterest sensorRegion,
            PixelBinning binning,
            PixelDimensions deliveredDimensions,
            CameraPixelLayout pixelLayout,
            std::string pixelFormatCode);

    [[nodiscard]] const CameraModeId& id() const noexcept;
    [[nodiscard]] const SensorRegionOfInterest& sensorRegion() const noexcept;
    [[nodiscard]] const PixelBinning& binning() const noexcept;
    [[nodiscard]] const PixelDimensions& deliveredDimensions() const noexcept;
    [[nodiscard]] CameraPixelLayout pixelLayout() const noexcept;
    [[nodiscard]] const std::string& pixelFormatCode() const noexcept;

    [[nodiscard]] double sensorPixelsPerDeliveredPixelX() const noexcept;
    [[nodiscard]] double sensorPixelsPerDeliveredPixelY() const noexcept;

private:
    CameraOutputMode(
        CameraModeId id,
        SensorRegionOfInterest sensorRegion,
        PixelBinning binning,
        PixelDimensions deliveredDimensions,
        CameraPixelLayout pixelLayout,
        std::string pixelFormatCode);

    CameraModeId id_;
    SensorRegionOfInterest sensorRegion_;
    PixelBinning binning_;
    PixelDimensions deliveredDimensions_;
    CameraPixelLayout pixelLayout_;
    std::string pixelFormatCode_;
};

enum class ExposureTimeRangeError {
    InvertedRange
};

// Camera exposure time is expressed in microseconds.
class ExposureTimeRange final {
public:
    [[nodiscard]] static Result<ExposureTimeRange, ExposureTimeRangeError> create(
        PositiveMicroseconds minimum,
        PositiveMicroseconds maximum);

    [[nodiscard]] double minimumMicroseconds() const noexcept;
    [[nodiscard]] double maximumMicroseconds() const noexcept;

private:
    ExposureTimeRange(
        PositiveMicroseconds minimum,
        PositiveMicroseconds maximum);

    PositiveMicroseconds minimum_;
    PositiveMicroseconds maximum_;
};

enum class OpticalFilterRole {
    Excitation,
    Emission,
    Dichroic,
    NeutralDensity,
    Other
};

struct OpticalFilterSpectralSample {
    PositiveNanometers wavelength;
    double transmissionFraction = 0.0;
    std::optional<double> reflectionFraction;
};

enum class OpticalFilterSpecificationError {
    InvalidRole,
    TooFewSamples,
    InvalidTransmission,
    InvalidReflection,
    WavelengthsNotIncreasing,
    DichroicRequiresTransmissionAndReflection
};

// Installed spectral filter data. The transmission/reflection curve is a
// measured or manufacturer-specified response, not a binary passband label.
// Angle/polarization dependencies may be represented by separate filter
// profiles when the manufacturer's data make them material.
class OpticalFilterSpecification final {
public:
    [[nodiscard]] static Result<
        OpticalFilterSpecification,
        OpticalFilterSpecificationError> create(
            EquipmentComponentId componentId,
            OpticalFilterRole role,
            std::vector<OpticalFilterSpectralSample> spectralResponse,
            std::optional<OpticalCompatibilityId> compatibilityId = std::nullopt);

    [[nodiscard]] const EquipmentComponentId& componentId() const noexcept;
    [[nodiscard]] OpticalFilterRole role() const noexcept;
    [[nodiscard]] const std::vector<OpticalFilterSpectralSample>&
    spectralResponse() const noexcept;
    // Linear interpolation between measured samples; no extrapolation outside
    // the characterized wavelength range.
    [[nodiscard]] std::optional<double> transmissionAt(
        double wavelengthNanometers) const noexcept;
    [[nodiscard]] std::optional<double> reflectionAt(
        double wavelengthNanometers) const noexcept;
    [[nodiscard]] const std::optional<OpticalCompatibilityId>&
    compatibilityId() const noexcept;

private:
    OpticalFilterSpecification(
        EquipmentComponentId componentId,
        OpticalFilterRole role,
        std::vector<OpticalFilterSpectralSample> spectralResponse,
        std::optional<OpticalCompatibilityId> compatibilityId);

    EquipmentComponentId componentId_;
    OpticalFilterRole role_;
    std::vector<OpticalFilterSpectralSample> spectralResponse_;
    std::optional<OpticalCompatibilityId> compatibilityId_;
};

enum class FluorescenceEmissionSpectrumError {
    TooFewSamples,
    WavelengthsNotIncreasing,
    InvalidRelativeIntensity,
    NoPositiveEmission
};

struct FluorescenceEmissionSample {
    PositiveNanometers wavelength;
    double relativeIntensity = 0.0;
};

// Relative spectral photon-flux density (per wavelength interval) for a
// fluorophore or task-specific emission profile. The samples describe spectral
// shape; absolute emitted power is not inferred from them.
class FluorescenceEmissionSpectrum final {
public:
    [[nodiscard]] static Result<
        FluorescenceEmissionSpectrum,
        FluorescenceEmissionSpectrumError> create(
            std::vector<FluorescenceEmissionSample> samples);

    [[nodiscard]] const std::vector<FluorescenceEmissionSample>&
    samples() const noexcept;

private:
    explicit FluorescenceEmissionSpectrum(
        std::vector<FluorescenceEmissionSample> samples);

    std::vector<FluorescenceEmissionSample> samples_;
};

enum class CameraSensorTechnology {
    CCD,
    CMOS,
    ScientificCMOS,
    Unknown
};

enum class CameraShutterType {
    Global,
    Rolling,
    FrameTransfer,
    GlobalReset,
    Unknown
};

enum class CameraSensorColorMode {
    Monochrome,
    Color,
    Unknown
};

enum class CameraSpecificationProvenance {
    ManufacturerDatasheet,
    VendorModelDescriptor,
    UserVerified,
    Measured,
    Unknown
};

struct CameraQuantumEfficiencySample {
    double wavelengthNanometers = 0.0;
    // Fraction of incident photons converted to detected electrons [0, 1].
    double quantumEfficiency = 0.0;
};

// Core-side copy of model facts. The Camera project discovers/enriches these
// facts; Application translates them into Core without adding a Camera::Common
// dependency to the microscope engine.
struct CameraSensorCharacteristics {
    std::optional<CameraSensorTechnology> technology;
    std::optional<CameraShutterType> shutterType;
    std::optional<CameraSensorColorMode> colorMode;
    std::optional<double> readNoiseElectronsRms;
    std::optional<double> darkCurrentElectronsPerPixelPerSecond;
    std::optional<double> fullWellCapacityElectrons;
    std::optional<double> maximumFrameRateFramesPerSecond;
    std::vector<CameraQuantumEfficiencySample> quantumEfficiency;
    CameraSpecificationProvenance provenance =
        CameraSpecificationProvenance::Unknown;
    std::optional<std::string> provenanceReference;
};

enum class CameraSpecificationError {
    NoOutputModes,
    DuplicateOutputModeId,
    SensorRegionOutsideActiveArea,
    InvalidSensorCharacteristics
};

// One camera component owns one sensor specification and its supported
// output/readout modes. Adapter observations remain separate runtime data.
class CameraSpecification final {
public:
    [[nodiscard]] static Result<
        CameraSpecification,
        CameraSpecificationError> create(
            EquipmentComponentId componentId,
            PixelDimensions activeSensorDimensions,
            PositiveMicrometers pixelPitchX,
            PositiveMicrometers pixelPitchY,
            std::vector<CameraOutputMode> outputModes,
            std::optional<ExposureTimeRange> exposureTimeRange = std::nullopt,
            std::optional<CameraSensorCharacteristics> sensorCharacteristics =
                std::nullopt);

    [[nodiscard]] const EquipmentComponentId& componentId() const noexcept;
    [[nodiscard]] const PixelDimensions& activeSensorDimensions() const noexcept;
    [[nodiscard]] double pixelPitchXMicrometers() const noexcept;
    [[nodiscard]] double pixelPitchYMicrometers() const noexcept;
    [[nodiscard]] const std::vector<CameraOutputMode>& outputModes() const noexcept;
    [[nodiscard]] const std::optional<ExposureTimeRange>&
    exposureTimeRange() const noexcept;
    [[nodiscard]] const std::optional<CameraSensorCharacteristics>&
    sensorCharacteristics() const noexcept;

private:
    CameraSpecification(
        EquipmentComponentId componentId,
        PixelDimensions activeSensorDimensions,
        PositiveMicrometers pixelPitchX,
        PositiveMicrometers pixelPitchY,
        std::vector<CameraOutputMode> outputModes,
        std::optional<ExposureTimeRange> exposureTimeRange,
        std::optional<CameraSensorCharacteristics> sensorCharacteristics);

    EquipmentComponentId componentId_;
    PixelDimensions activeSensorDimensions_;
    PositiveMicrometers pixelPitchX_;
    PositiveMicrometers pixelPitchY_;
    std::vector<CameraOutputMode> outputModes_;
    std::optional<ExposureTimeRange> exposureTimeRange_;
    std::optional<CameraSensorCharacteristics> sensorCharacteristics_;
};

} // namespace microscope::core
