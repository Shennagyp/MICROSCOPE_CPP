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
struct NumericalApertureTag final {};
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
using NumericalAperture = PositiveFiniteValue<detail::NumericalApertureTag>;

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

enum class ObjectiveImmersionMedium {
    Air,
    Water,
    Oil,
    Glycerol,
    Silicone,
    Other,
    Unknown
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

enum class CameraSpecificationError {
    NoOutputModes,
    DuplicateOutputModeId,
    SensorRegionOutsideActiveArea
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
            std::vector<CameraOutputMode> outputModes);

    [[nodiscard]] const EquipmentComponentId& componentId() const noexcept;
    [[nodiscard]] const PixelDimensions& activeSensorDimensions() const noexcept;
    [[nodiscard]] double pixelPitchXMicrometers() const noexcept;
    [[nodiscard]] double pixelPitchYMicrometers() const noexcept;
    [[nodiscard]] const std::vector<CameraOutputMode>& outputModes() const noexcept;

private:
    CameraSpecification(
        EquipmentComponentId componentId,
        PixelDimensions activeSensorDimensions,
        PositiveMicrometers pixelPitchX,
        PositiveMicrometers pixelPitchY,
        std::vector<CameraOutputMode> outputModes);

    EquipmentComponentId componentId_;
    PixelDimensions activeSensorDimensions_;
    PositiveMicrometers pixelPitchX_;
    PositiveMicrometers pixelPitchY_;
    std::vector<CameraOutputMode> outputModes_;
};

} // namespace microscope::core
