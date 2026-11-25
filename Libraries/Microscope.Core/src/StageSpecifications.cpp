#include "microscope/core/equipment/StageSpecifications.h"

#include <cmath>
#include <set>
#include <utility>

namespace microscope::core {
namespace {

[[nodiscard]] bool hasMeaningfulText(const std::string& value) noexcept {
    return value.find('\0') == std::string::npos &&
           value.find_first_not_of(" \t\n\r\f\v") != std::string::npos;
}

[[nodiscard]] bool isKnownAxisMotionType(AxisMotionType value) noexcept {
    switch (value) {
    case AxisMotionType::Linear:
    case AxisMotionType::Rotary:
    case AxisMotionType::Other:
    case AxisMotionType::Unknown:
        return true;
    }
    return false;
}

[[nodiscard]] bool isKnownAxisCoordinateUnit(AxisCoordinateUnit value) noexcept {
    switch (value) {
    case AxisCoordinateUnit::Micrometers:
    case AxisCoordinateUnit::Millimeters:
    case AxisCoordinateUnit::Degrees:
    case AxisCoordinateUnit::Radians:
    case AxisCoordinateUnit::DeviceDefined:
    case AxisCoordinateUnit::Unknown:
        return true;
    }
    return false;
}

[[nodiscard]] bool isCoordinateUnitCompatible(
    AxisMotionType motion,
    AxisCoordinateUnit unit) noexcept {
    if (unit == AxisCoordinateUnit::DeviceDefined ||
        unit == AxisCoordinateUnit::Unknown ||
        motion == AxisMotionType::Unknown) {
        return true;
    }

    switch (motion) {
    case AxisMotionType::Linear:
        return unit == AxisCoordinateUnit::Micrometers ||
               unit == AxisCoordinateUnit::Millimeters;
    case AxisMotionType::Rotary:
        return unit == AxisCoordinateUnit::Degrees ||
               unit == AxisCoordinateUnit::Radians;
    case AxisMotionType::Other:
        return false;
    case AxisMotionType::Unknown:
        return true;
    }
    return false;
}

[[nodiscard]] bool isKnownPositionFeedback(
    AxisPositionFeedbackCapability value) noexcept {
    switch (value) {
    case AxisPositionFeedbackCapability::None:
    case AxisPositionFeedbackCapability::Encoder:
    case AxisPositionFeedbackCapability::LinearScale:
    case AxisPositionFeedbackCapability::DeviceReported:
    case AxisPositionFeedbackCapability::ExternalSensor:
    case AxisPositionFeedbackCapability::Other:
    case AxisPositionFeedbackCapability::Unknown:
        return true;
    }
    return false;
}

[[nodiscard]] bool isKnownMeasurementLocation(
    AxisMeasurementLocation value) noexcept {
    switch (value) {
    case AxisMeasurementLocation::Motor:
    case AxisMeasurementLocation::Axis:
    case AxisMeasurementLocation::Specimen:
    case AxisMeasurementLocation::External:
    case AxisMeasurementLocation::Other:
    case AxisMeasurementLocation::Unknown:
        return true;
    }
    return false;
}

} // namespace

AxisOperatingRange::AxisOperatingRange(
    double minimum,
    double maximum,
    AxisCoordinateUnit unit)
    : minimum_(minimum),
      maximum_(maximum),
      unit_(unit) {}

Result<AxisOperatingRange, AxisOperatingRangeError> AxisOperatingRange::create(
    double minimum,
    double maximum,
    AxisCoordinateUnit unit) {
    if (!isKnownAxisCoordinateUnit(unit)) {
        return Result<AxisOperatingRange, AxisOperatingRangeError>::failure(
            AxisOperatingRangeError::InvalidUnit);
    }

    if (!std::isfinite(minimum) || !std::isfinite(maximum)) {
        return Result<AxisOperatingRange, AxisOperatingRangeError>::failure(
            AxisOperatingRangeError::NonFiniteBound);
    }

    if (minimum >= maximum) {
        return Result<AxisOperatingRange, AxisOperatingRangeError>::failure(
            AxisOperatingRangeError::NonIncreasingRange);
    }

    return Result<AxisOperatingRange, AxisOperatingRangeError>::success(
        AxisOperatingRange(minimum, maximum, unit));
}

double AxisOperatingRange::minimum() const noexcept {
    return minimum_;
}

double AxisOperatingRange::maximum() const noexcept {
    return maximum_;
}

AxisCoordinateUnit AxisOperatingRange::unit() const noexcept {
    return unit_;
}

AxisAccuracySpecification::AxisAccuracySpecification(
    NonNegativeAxisAccuracy maximumAbsoluteError,
    std::string testConditions)
    : maximumAbsoluteError_(maximumAbsoluteError),
      testConditions_(std::move(testConditions)) {}

Result<
    AxisAccuracySpecification,
    AxisAccuracyError> AxisAccuracySpecification::create(
        NonNegativeAxisAccuracy maximumAbsoluteError,
        std::string testConditions) {
    if (!hasMeaningfulText(testConditions)) {
        return Result<
            AxisAccuracySpecification,
            AxisAccuracyError>::failure(
                AxisAccuracyError::EmptyTestConditions);
    }

    return Result<
        AxisAccuracySpecification,
        AxisAccuracyError>::success(
            AxisAccuracySpecification(
                maximumAbsoluteError,
                std::move(testConditions)));
}

double AxisAccuracySpecification::maximumAbsoluteError() const noexcept {
    return maximumAbsoluteError_.value();
}

const std::string& AxisAccuracySpecification::testConditions() const noexcept {
    return testConditions_;
}

AxisRepeatabilitySpecification::AxisRepeatabilitySpecification(
    NonNegativeAxisRepeatability value,
    AxisRepeatabilityMetric metric,
    std::string testConditions)
    : value_(value),
      metric_(metric),
      testConditions_(std::move(testConditions)) {}

Result<
    AxisRepeatabilitySpecification,
    AxisRepeatabilityError> AxisRepeatabilitySpecification::create(
        NonNegativeAxisRepeatability value,
        AxisRepeatabilityMetric metric,
        std::string testConditions) {
    switch (metric) {
    case AxisRepeatabilityMetric::MaximumDeviation:
    case AxisRepeatabilityMetric::StandardDeviation:
    case AxisRepeatabilityMetric::RootMeanSquare:
    case AxisRepeatabilityMetric::Unknown:
        break;
    default:
        return Result<
            AxisRepeatabilitySpecification,
            AxisRepeatabilityError>::failure(
                AxisRepeatabilityError::InvalidMetric);
    }

    if (!hasMeaningfulText(testConditions)) {
        return Result<
            AxisRepeatabilitySpecification,
            AxisRepeatabilityError>::failure(
                AxisRepeatabilityError::EmptyTestConditions);
    }

    return Result<
        AxisRepeatabilitySpecification,
        AxisRepeatabilityError>::success(
            AxisRepeatabilitySpecification(
                value,
                metric,
                std::move(testConditions)));
}

double AxisRepeatabilitySpecification::value() const noexcept {
    return value_.value();
}

AxisRepeatabilityMetric AxisRepeatabilitySpecification::metric() const noexcept {
    return metric_;
}

const std::string& AxisRepeatabilitySpecification::testConditions() const noexcept {
    return testConditions_;
}

AxisMeasurementUncertaintySpecification::AxisMeasurementUncertaintySpecification(
    NonNegativeAxisUncertainty value,
    std::string testConditions)
    : value_(value),
      testConditions_(std::move(testConditions)) {}

Result<
    AxisMeasurementUncertaintySpecification,
    AxisUncertaintyError> AxisMeasurementUncertaintySpecification::create(
        NonNegativeAxisUncertainty value,
        std::string testConditions) {
    if (!hasMeaningfulText(testConditions)) {
        return Result<
            AxisMeasurementUncertaintySpecification,
            AxisUncertaintyError>::failure(
                AxisUncertaintyError::EmptyTestConditions);
    }

    return Result<
        AxisMeasurementUncertaintySpecification,
        AxisUncertaintyError>::success(
            AxisMeasurementUncertaintySpecification(
                value,
                std::move(testConditions)));
}

double AxisMeasurementUncertaintySpecification::value() const noexcept {
    return value_.value();
}

const std::string&
AxisMeasurementUncertaintySpecification::testConditions() const noexcept {
    return testConditions_;
}

AxisSpecification::AxisSpecification(
    StageAxisId id,
    AxisMotionType motionType,
    AxisCoordinateUnit coordinateUnit,
    AxisOperatingRange operatingRange,
    PositiveAxisResolution nominalResolution,
    std::optional<AxisAccuracySpecification> accuracy,
    std::optional<AxisRepeatabilitySpecification> repeatability,
    std::optional<AxisMeasurementUncertaintySpecification> measurementUncertainty,
    AxisPositionFeedbackCapability positionFeedback,
    std::optional<AxisMeasurementLocation> measurementLocation)
    : id_(std::move(id)),
      motionType_(motionType),
      coordinateUnit_(coordinateUnit),
      operatingRange_(operatingRange),
      nominalResolution_(nominalResolution),
      accuracy_(std::move(accuracy)),
      repeatability_(std::move(repeatability)),
      measurementUncertainty_(std::move(measurementUncertainty)),
      positionFeedback_(positionFeedback),
      measurementLocation_(measurementLocation) {}

Result<AxisSpecification, AxisSpecificationError> AxisSpecification::create(
    StageAxisId id,
    AxisMotionType motionType,
    AxisCoordinateUnit coordinateUnit,
    AxisOperatingRange operatingRange,
    PositiveAxisResolution nominalResolution,
    std::optional<AxisAccuracySpecification> accuracy,
    std::optional<AxisRepeatabilitySpecification> repeatability,
    std::optional<AxisMeasurementUncertaintySpecification> measurementUncertainty,
    AxisPositionFeedbackCapability positionFeedback,
    std::optional<AxisMeasurementLocation> measurementLocation) {
    if (!isKnownAxisMotionType(motionType)) {
        return Result<AxisSpecification, AxisSpecificationError>::failure(
            AxisSpecificationError::InvalidMotionType);
    }

    if (!isKnownAxisCoordinateUnit(coordinateUnit)) {
        return Result<AxisSpecification, AxisSpecificationError>::failure(
            AxisSpecificationError::InvalidCoordinateUnit);
    }

    if (operatingRange.unit() != coordinateUnit) {
        return Result<AxisSpecification, AxisSpecificationError>::failure(
            AxisSpecificationError::OperatingRangeUnitMismatch);
    }

    if (!isCoordinateUnitCompatible(motionType, coordinateUnit)) {
        return Result<AxisSpecification, AxisSpecificationError>::failure(
            AxisSpecificationError::UnitNotCompatibleWithMotion);
    }

    if (!isKnownPositionFeedback(positionFeedback)) {
        return Result<AxisSpecification, AxisSpecificationError>::failure(
            AxisSpecificationError::InvalidFeedbackCapability);
    }

    if (measurementLocation &&
        !isKnownMeasurementLocation(*measurementLocation)) {
        return Result<AxisSpecification, AxisSpecificationError>::failure(
            AxisSpecificationError::InvalidMeasurementLocation);
    }

    if (positionFeedback == AxisPositionFeedbackCapability::None &&
        measurementLocation) {
        return Result<AxisSpecification, AxisSpecificationError>::failure(
            AxisSpecificationError::NoFeedbackHasMeasurementLocation);
    }

    return Result<AxisSpecification, AxisSpecificationError>::success(
        AxisSpecification(
            std::move(id),
            motionType,
            coordinateUnit,
            operatingRange,
            nominalResolution,
            std::move(accuracy),
            std::move(repeatability),
            std::move(measurementUncertainty),
            positionFeedback,
            measurementLocation));
}

const StageAxisId& AxisSpecification::id() const noexcept {
    return id_;
}

AxisMotionType AxisSpecification::motionType() const noexcept {
    return motionType_;
}

AxisCoordinateUnit AxisSpecification::coordinateUnit() const noexcept {
    return coordinateUnit_;
}

const AxisOperatingRange& AxisSpecification::operatingRange() const noexcept {
    return operatingRange_;
}

double AxisSpecification::nominalResolution() const noexcept {
    return nominalResolution_.value();
}

const std::optional<AxisAccuracySpecification>&
AxisSpecification::accuracy() const noexcept {
    return accuracy_;
}

const std::optional<AxisRepeatabilitySpecification>&
AxisSpecification::repeatability() const noexcept {
    return repeatability_;
}

const std::optional<AxisMeasurementUncertaintySpecification>&
AxisSpecification::measurementUncertainty() const noexcept {
    return measurementUncertainty_;
}

AxisPositionFeedbackCapability
AxisSpecification::positionFeedback() const noexcept {
    return positionFeedback_;
}

const std::optional<AxisMeasurementLocation>&
AxisSpecification::measurementLocation() const noexcept {
    return measurementLocation_;
}

StageSpecification::StageSpecification(
    EquipmentComponentId componentId,
    std::vector<AxisSpecification> axes)
    : componentId_(std::move(componentId)),
      axes_(std::move(axes)) {}

Result<StageSpecification, StageSpecificationError> StageSpecification::create(
    EquipmentComponentId componentId,
    std::vector<AxisSpecification> axes) {
    if (axes.empty()) {
        return Result<StageSpecification, StageSpecificationError>::failure(
            StageSpecificationError::NoAxes);
    }

    std::set<StageAxisId> axisIds;
    for (const auto& axis : axes) {
        if (!axisIds.insert(axis.id()).second) {
            return Result<StageSpecification, StageSpecificationError>::failure(
                StageSpecificationError::DuplicateAxisId);
        }
    }

    return Result<StageSpecification, StageSpecificationError>::success(
        StageSpecification(std::move(componentId), std::move(axes)));
}

const EquipmentComponentId& StageSpecification::componentId() const noexcept {
    return componentId_;
}

const std::vector<AxisSpecification>& StageSpecification::axes() const noexcept {
    return axes_;
}

} // namespace microscope::core
