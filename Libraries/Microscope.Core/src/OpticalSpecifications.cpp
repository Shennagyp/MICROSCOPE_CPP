#include "microscope/core/equipment/OpticalSpecifications.h"

#include <map>
#include <set>
#include <cmath>
#include <utility>

namespace microscope::core {
namespace {

[[nodiscard]] bool isKnownObjectiveChromaticCorrection(
    ObjectiveChromaticCorrection value) noexcept {
    switch (value) {
    case ObjectiveChromaticCorrection::Achromatic:
    case ObjectiveChromaticCorrection::SemiApochromatic:
    case ObjectiveChromaticCorrection::Apochromatic:
    case ObjectiveChromaticCorrection::Other:
    case ObjectiveChromaticCorrection::Unknown:
        return true;
    }
    return false;
}

[[nodiscard]] bool isKnownObjectiveFieldCorrection(
    ObjectiveFieldCorrection value) noexcept {
    switch (value) {
    case ObjectiveFieldCorrection::Plan:
    case ObjectiveFieldCorrection::NonPlan:
    case ObjectiveFieldCorrection::Unknown:
        return true;
    }
    return false;
}

[[nodiscard]] bool isKnownImmersionMedium(
    ObjectiveImmersionMedium value) noexcept {
    switch (value) {
    case ObjectiveImmersionMedium::Air:
    case ObjectiveImmersionMedium::Water:
    case ObjectiveImmersionMedium::Oil:
    case ObjectiveImmersionMedium::Glycerol:
    case ObjectiveImmersionMedium::Silicone:
    case ObjectiveImmersionMedium::Other:
    case ObjectiveImmersionMedium::Unknown:
        return true;
    }
    return false;
}

[[nodiscard]] bool isKnownCameraPixelLayout(CameraPixelLayout value) noexcept {
    switch (value) {
    case CameraPixelLayout::Monochrome:
    case CameraPixelLayout::Bayer:
    case CameraPixelLayout::InterleavedColor:
    case CameraPixelLayout::PlanarColor:
    case CameraPixelLayout::Other:
        return true;
    }
    return false;
}

} // namespace

ObjectiveSpecification::ObjectiveSpecification(
    EquipmentComponentId componentId,
    NominalMagnification nominalMagnification,
    NumericalAperture numericalAperture,
    PositiveMicrometers workingDistance,
    ObjectiveChromaticCorrection chromaticCorrection,
    ObjectiveFieldCorrection fieldCorrection,
    ObjectiveImmersionMedium immersionMedium,
    std::optional<PositiveMicrometers> designCoverglassThickness)
    : componentId_(std::move(componentId)),
      nominalMagnification_(nominalMagnification),
      numericalAperture_(numericalAperture),
      workingDistance_(workingDistance),
      chromaticCorrection_(chromaticCorrection),
      fieldCorrection_(fieldCorrection),
      immersionMedium_(immersionMedium),
      designCoverglassThickness_(std::move(designCoverglassThickness)) {}

Result<
    ObjectiveSpecification,
    ObjectiveSpecificationError> ObjectiveSpecification::create(
        EquipmentComponentId componentId,
        NominalMagnification nominalMagnification,
        NumericalAperture numericalAperture,
        PositiveMicrometers workingDistance,
        ObjectiveChromaticCorrection chromaticCorrection,
        ObjectiveFieldCorrection fieldCorrection,
        ObjectiveImmersionMedium immersionMedium,
        std::optional<PositiveMicrometers> designCoverglassThickness) {
    if (!isKnownObjectiveChromaticCorrection(chromaticCorrection)) {
        return Result<
            ObjectiveSpecification,
            ObjectiveSpecificationError>::failure(
                ObjectiveSpecificationError::InvalidChromaticCorrection);
    }

    if (!isKnownObjectiveFieldCorrection(fieldCorrection)) {
        return Result<
            ObjectiveSpecification,
            ObjectiveSpecificationError>::failure(
                ObjectiveSpecificationError::InvalidFieldCorrection);
    }

    if (!isKnownImmersionMedium(immersionMedium)) {
        return Result<
            ObjectiveSpecification,
            ObjectiveSpecificationError>::failure(
                ObjectiveSpecificationError::InvalidImmersionMedium);
    }

    return Result<
        ObjectiveSpecification,
        ObjectiveSpecificationError>::success(
            ObjectiveSpecification(
                std::move(componentId),
                nominalMagnification,
                numericalAperture,
                workingDistance,
                chromaticCorrection,
                fieldCorrection,
                immersionMedium,
                std::move(designCoverglassThickness)));
}

const EquipmentComponentId& ObjectiveSpecification::componentId() const noexcept {
    return componentId_;
}

double ObjectiveSpecification::nominalMagnification() const noexcept {
    return nominalMagnification_.value();
}

double ObjectiveSpecification::numericalAperture() const noexcept {
    return numericalAperture_.value();
}

double ObjectiveSpecification::workingDistanceMicrometers() const noexcept {
    return workingDistance_.value();
}

ObjectiveChromaticCorrection
ObjectiveSpecification::chromaticCorrection() const noexcept {
    return chromaticCorrection_;
}

ObjectiveFieldCorrection ObjectiveSpecification::fieldCorrection() const noexcept {
    return fieldCorrection_;
}

ObjectiveImmersionMedium ObjectiveSpecification::immersionMedium() const noexcept {
    return immersionMedium_;
}

const std::optional<PositiveMicrometers>&
ObjectiveSpecification::designCoverglassThickness() const noexcept {
    return designCoverglassThickness_;
}

CondenserSpecification::CondenserSpecification(
    EquipmentComponentId componentId,
    NonNegativeNumericalAperture minimumAperture,
    NumericalAperture maximumAperture,
    CondenserDesign design,
    CondenserImmersionMedium immersionMedium,
    std::optional<PositiveMicrometers> workingDistance)
    : componentId_(std::move(componentId)),
      minimumAperture_(minimumAperture),
      maximumAperture_(maximumAperture),
      design_(design),
      immersionMedium_(immersionMedium),
      workingDistance_(std::move(workingDistance)) {}

Result<
    CondenserSpecification,
    CondenserSpecificationError> CondenserSpecification::create(
        EquipmentComponentId componentId,
        NonNegativeNumericalAperture minimumAperture,
        NumericalAperture maximumAperture,
        CondenserDesign design,
        CondenserImmersionMedium immersionMedium,
        std::optional<PositiveMicrometers> workingDistance) {
    switch (design) {
    case CondenserDesign::Abbe:
    case CondenserDesign::Achromatic:
    case CondenserDesign::AchromatAplanatic:
    case CondenserDesign::Other:
    case CondenserDesign::Unknown:
        break;
    default:
        return Result<
            CondenserSpecification,
            CondenserSpecificationError>::failure(
                CondenserSpecificationError::InvalidDesign);
    }

    switch (immersionMedium) {
    case CondenserImmersionMedium::Air:
    case CondenserImmersionMedium::Water:
    case CondenserImmersionMedium::Oil:
    case CondenserImmersionMedium::Other:
    case CondenserImmersionMedium::Unknown:
        break;
    default:
        return Result<
            CondenserSpecification,
            CondenserSpecificationError>::failure(
                CondenserSpecificationError::InvalidImmersionMedium);
    }

    if (minimumAperture.value() > maximumAperture.value()) {
        return Result<
            CondenserSpecification,
            CondenserSpecificationError>::failure(
                CondenserSpecificationError::InvertedApertureRange);
    }

    return Result<
        CondenserSpecification,
        CondenserSpecificationError>::success(
            CondenserSpecification(
                std::move(componentId),
                minimumAperture,
                maximumAperture,
                design,
                immersionMedium,
                std::move(workingDistance)));
}

const EquipmentComponentId& CondenserSpecification::componentId() const noexcept {
    return componentId_;
}

double CondenserSpecification::minimumApertureNA() const noexcept {
    return minimumAperture_.value();
}

double CondenserSpecification::maximumApertureNA() const noexcept {
    return maximumAperture_.value();
}

CondenserDesign CondenserSpecification::design() const noexcept {
    return design_;
}

CondenserImmersionMedium CondenserSpecification::immersionMedium() const noexcept {
    return immersionMedium_;
}

const std::optional<PositiveMicrometers>&
CondenserSpecification::workingDistance() const noexcept {
    return workingDistance_;
}

ExposureTimeRange::ExposureTimeRange(
    PositiveMicroseconds minimum,
    PositiveMicroseconds maximum)
    : minimum_(minimum),
      maximum_(maximum) {}

Result<ExposureTimeRange, ExposureTimeRangeError> ExposureTimeRange::create(
    PositiveMicroseconds minimum,
    PositiveMicroseconds maximum) {
    if (minimum.value() > maximum.value()) {
        return Result<ExposureTimeRange, ExposureTimeRangeError>::failure(
            ExposureTimeRangeError::InvertedRange);
    }
    return Result<ExposureTimeRange, ExposureTimeRangeError>::success(
        ExposureTimeRange(minimum, maximum));
}

double ExposureTimeRange::minimumMicroseconds() const noexcept {
    return minimum_.value();
}

double ExposureTimeRange::maximumMicroseconds() const noexcept {
    return maximum_.value();
}

PixelDimensions::PixelDimensions(
    std::uint32_t width,
    std::uint32_t height)
    : width_(width),
      height_(height) {}

Result<PixelDimensions, PixelDimensionsError> PixelDimensions::create(
    std::uint32_t width,
    std::uint32_t height) {
    if (width == 0) {
        return Result<PixelDimensions, PixelDimensionsError>::failure(
            PixelDimensionsError::ZeroWidth);
    }
    if (height == 0) {
        return Result<PixelDimensions, PixelDimensionsError>::failure(
            PixelDimensionsError::ZeroHeight);
    }

    return Result<PixelDimensions, PixelDimensionsError>::success(
        PixelDimensions(width, height));
}

std::uint32_t PixelDimensions::width() const noexcept {
    return width_;
}

std::uint32_t PixelDimensions::height() const noexcept {
    return height_;
}

SensorRegionOfInterest::SensorRegionOfInterest(
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t width,
    std::uint32_t height)
    : x_(x),
      y_(y),
      width_(width),
      height_(height) {}

Result<SensorRegionOfInterest, SensorRegionError>
SensorRegionOfInterest::create(
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t width,
    std::uint32_t height) {
    if (width == 0) {
        return Result<SensorRegionOfInterest, SensorRegionError>::failure(
            SensorRegionError::ZeroWidth);
    }
    if (height == 0) {
        return Result<SensorRegionOfInterest, SensorRegionError>::failure(
            SensorRegionError::ZeroHeight);
    }

    return Result<SensorRegionOfInterest, SensorRegionError>::success(
        SensorRegionOfInterest(x, y, width, height));
}

std::uint32_t SensorRegionOfInterest::x() const noexcept {
    return x_;
}

std::uint32_t SensorRegionOfInterest::y() const noexcept {
    return y_;
}

std::uint32_t SensorRegionOfInterest::width() const noexcept {
    return width_;
}

std::uint32_t SensorRegionOfInterest::height() const noexcept {
    return height_;
}

PixelBinning::PixelBinning(
    std::uint32_t horizontalFactor,
    std::uint32_t verticalFactor)
    : horizontalFactor_(horizontalFactor),
      verticalFactor_(verticalFactor) {}

Result<PixelBinning, PixelBinningError> PixelBinning::create(
    std::uint32_t horizontalFactor,
    std::uint32_t verticalFactor) {
    if (horizontalFactor == 0) {
        return Result<PixelBinning, PixelBinningError>::failure(
            PixelBinningError::ZeroHorizontalFactor);
    }
    if (verticalFactor == 0) {
        return Result<PixelBinning, PixelBinningError>::failure(
            PixelBinningError::ZeroVerticalFactor);
    }

    return Result<PixelBinning, PixelBinningError>::success(
        PixelBinning(horizontalFactor, verticalFactor));
}

std::uint32_t PixelBinning::horizontalFactor() const noexcept {
    return horizontalFactor_;
}

std::uint32_t PixelBinning::verticalFactor() const noexcept {
    return verticalFactor_;
}

CameraOutputMode::CameraOutputMode(
    CameraModeId id,
    SensorRegionOfInterest sensorRegion,
    PixelBinning binning,
    PixelDimensions deliveredDimensions,
    CameraPixelLayout pixelLayout,
    std::string pixelFormatCode)
    : id_(std::move(id)),
      sensorRegion_(sensorRegion),
      binning_(binning),
      deliveredDimensions_(deliveredDimensions),
      pixelLayout_(pixelLayout),
      pixelFormatCode_(std::move(pixelFormatCode)) {}

Result<CameraOutputMode, CameraOutputModeError> CameraOutputMode::create(
    CameraModeId id,
    SensorRegionOfInterest sensorRegion,
    PixelBinning binning,
    PixelDimensions deliveredDimensions,
    CameraPixelLayout pixelLayout,
    std::string pixelFormatCode) {
    if (!isKnownCameraPixelLayout(pixelLayout)) {
        return Result<CameraOutputMode, CameraOutputModeError>::failure(
            CameraOutputModeError::InvalidPixelLayout);
    }

    const auto formatCodeValidation = validateOpaqueIdentifier(pixelFormatCode);
    if (!formatCodeValidation ||
        pixelFormatCode.find_first_of(" \t\n\r\f\v") != std::string::npos) {
        return Result<CameraOutputMode, CameraOutputModeError>::failure(
            CameraOutputModeError::InvalidPixelFormatCode);
    }

    if (binning.horizontalFactor() > sensorRegion.width() ||
        binning.verticalFactor() > sensorRegion.height()) {
        return Result<CameraOutputMode, CameraOutputModeError>::failure(
            CameraOutputModeError::BinningExceedsSensorRegion);
    }

    return Result<CameraOutputMode, CameraOutputModeError>::success(
        CameraOutputMode(
            std::move(id),
            sensorRegion,
            binning,
            deliveredDimensions,
            pixelLayout,
            std::move(pixelFormatCode)));
}

const CameraModeId& CameraOutputMode::id() const noexcept {
    return id_;
}

const SensorRegionOfInterest& CameraOutputMode::sensorRegion() const noexcept {
    return sensorRegion_;
}

const PixelBinning& CameraOutputMode::binning() const noexcept {
    return binning_;
}

const PixelDimensions& CameraOutputMode::deliveredDimensions() const noexcept {
    return deliveredDimensions_;
}

CameraPixelLayout CameraOutputMode::pixelLayout() const noexcept {
    return pixelLayout_;
}

const std::string& CameraOutputMode::pixelFormatCode() const noexcept {
    return pixelFormatCode_;
}

double CameraOutputMode::sensorPixelsPerDeliveredPixelX() const noexcept {
    return static_cast<double>(sensorRegion_.width()) /
           static_cast<double>(deliveredDimensions_.width());
}

double CameraOutputMode::sensorPixelsPerDeliveredPixelY() const noexcept {
    return static_cast<double>(sensorRegion_.height()) /
           static_cast<double>(deliveredDimensions_.height());
}

namespace {

[[nodiscard]] bool isKnownOpticalFilterRole(OpticalFilterRole role) noexcept {
    switch (role) {
    case OpticalFilterRole::Excitation:
    case OpticalFilterRole::Emission:
    case OpticalFilterRole::Dichroic:
    case OpticalFilterRole::NeutralDensity:
    case OpticalFilterRole::Other:
        return true;
    }
    return false;
}

} // namespace

OpticalFilterSpecification::OpticalFilterSpecification(
    EquipmentComponentId componentId,
    OpticalFilterRole role,
    std::vector<OpticalFilterSpectralSample> spectralResponse,
    std::optional<OpticalCompatibilityId> compatibilityId)
    : componentId_(std::move(componentId)),
      role_(role),
      spectralResponse_(std::move(spectralResponse)),
      compatibilityId_(std::move(compatibilityId)) {}

Result<
    OpticalFilterSpecification,
    OpticalFilterSpecificationError> OpticalFilterSpecification::create(
        EquipmentComponentId componentId,
        OpticalFilterRole role,
        std::vector<OpticalFilterSpectralSample> spectralResponse,
        std::optional<OpticalCompatibilityId> compatibilityId) {
    if (!isKnownOpticalFilterRole(role)) {
        return Result<
            OpticalFilterSpecification,
            OpticalFilterSpecificationError>::failure(
                OpticalFilterSpecificationError::InvalidRole);
    }
    if (spectralResponse.size() < 2) {
        return Result<
            OpticalFilterSpecification,
            OpticalFilterSpecificationError>::failure(
                OpticalFilterSpecificationError::TooFewSamples);
    }

    double previousWavelength = 0.0;
    for (const auto& sample : spectralResponse) {
        const double wavelength = sample.wavelength.value();
        if (wavelength <= previousWavelength) {
            return Result<
                OpticalFilterSpecification,
                OpticalFilterSpecificationError>::failure(
                    OpticalFilterSpecificationError::WavelengthsNotIncreasing);
        }
        previousWavelength = wavelength;

        if (!std::isfinite(sample.transmissionFraction) ||
            sample.transmissionFraction < 0.0 ||
            sample.transmissionFraction > 1.0) {
            return Result<
                OpticalFilterSpecification,
                OpticalFilterSpecificationError>::failure(
                    OpticalFilterSpecificationError::InvalidTransmission);
        }

        if (sample.reflectionFraction &&
            (!std::isfinite(*sample.reflectionFraction) ||
             *sample.reflectionFraction < 0.0 ||
             *sample.reflectionFraction > 1.0 ||
             sample.transmissionFraction + *sample.reflectionFraction > 1.000001)) {
            return Result<
                OpticalFilterSpecification,
                OpticalFilterSpecificationError>::failure(
                    OpticalFilterSpecificationError::InvalidReflection);
        }

        if (role == OpticalFilterRole::Dichroic &&
            !sample.reflectionFraction) {
            return Result<
                OpticalFilterSpecification,
                OpticalFilterSpecificationError>::failure(
                    OpticalFilterSpecificationError::DichroicRequiresTransmissionAndReflection);
        }
    }

    return Result<
        OpticalFilterSpecification,
        OpticalFilterSpecificationError>::success(
            OpticalFilterSpecification(
                std::move(componentId),
                role,
                std::move(spectralResponse),
                std::move(compatibilityId)));
}

const EquipmentComponentId&
OpticalFilterSpecification::componentId() const noexcept {
    return componentId_;
}

OpticalFilterRole OpticalFilterSpecification::role() const noexcept {
    return role_;
}

const std::vector<OpticalFilterSpectralSample>&
OpticalFilterSpecification::spectralResponse() const noexcept {
    return spectralResponse_;
}

const std::optional<OpticalCompatibilityId>&
OpticalFilterSpecification::compatibilityId() const noexcept {
    return compatibilityId_;
}

CameraSpecification::CameraSpecification(
    EquipmentComponentId componentId,
    PixelDimensions activeSensorDimensions,
    PositiveMicrometers pixelPitchX,
    PositiveMicrometers pixelPitchY,
    std::vector<CameraOutputMode> outputModes,
    std::optional<ExposureTimeRange> exposureTimeRange)
    : componentId_(std::move(componentId)),
      activeSensorDimensions_(activeSensorDimensions),
      pixelPitchX_(pixelPitchX),
      pixelPitchY_(pixelPitchY),
      outputModes_(std::move(outputModes)),
      exposureTimeRange_(std::move(exposureTimeRange)) {}

Result<
    CameraSpecification,
    CameraSpecificationError> CameraSpecification::create(
        EquipmentComponentId componentId,
        PixelDimensions activeSensorDimensions,
        PositiveMicrometers pixelPitchX,
        PositiveMicrometers pixelPitchY,
        std::vector<CameraOutputMode> outputModes,
        std::optional<ExposureTimeRange> exposureTimeRange) {
    if (outputModes.empty()) {
        return Result<
            CameraSpecification,
            CameraSpecificationError>::failure(
                CameraSpecificationError::NoOutputModes);
    }

    std::set<CameraModeId> modeIds;
    for (const auto& mode : outputModes) {
        if (!modeIds.insert(mode.id()).second) {
            return Result<
                CameraSpecification,
                CameraSpecificationError>::failure(
                    CameraSpecificationError::DuplicateOutputModeId);
        }

        const auto& region = mode.sensorRegion();
        if (region.width() > activeSensorDimensions.width() ||
            region.height() > activeSensorDimensions.height() ||
            region.x() > activeSensorDimensions.width() - region.width() ||
            region.y() > activeSensorDimensions.height() - region.height()) {
            return Result<
                CameraSpecification,
                CameraSpecificationError>::failure(
                    CameraSpecificationError::SensorRegionOutsideActiveArea);
        }
    }

    return Result<
        CameraSpecification,
        CameraSpecificationError>::success(
            CameraSpecification(
                std::move(componentId),
                activeSensorDimensions,
                pixelPitchX,
                pixelPitchY,
                std::move(outputModes),
                std::move(exposureTimeRange)));
}

const EquipmentComponentId& CameraSpecification::componentId() const noexcept {
    return componentId_;
}

const PixelDimensions& CameraSpecification::activeSensorDimensions() const noexcept {
    return activeSensorDimensions_;
}

double CameraSpecification::pixelPitchXMicrometers() const noexcept {
    return pixelPitchX_.value();
}

double CameraSpecification::pixelPitchYMicrometers() const noexcept {
    return pixelPitchY_.value();
}

const std::vector<CameraOutputMode>& CameraSpecification::outputModes() const noexcept {
    return outputModes_;
}

const std::optional<ExposureTimeRange>&
CameraSpecification::exposureTimeRange() const noexcept {
    return exposureTimeRange_;
}

} // namespace microscope::core
