#pragma once

#include "microscope/core/ids/Ids.h"
#include "microscope/core/result/Result.h"
#include "microscope/core/equipment/OpticalSpecifications.h"

#include <optional>
#include <string>
#include <vector>

namespace microscope::core {

enum class AxisMotionType {
    Linear,
    Rotary,
    Other,
    Unknown
};

enum class AxisCoordinateUnit {
    Micrometers,
    Millimeters,
    Degrees,
    Radians,
    DeviceDefined,
    Unknown
};

enum class AxisOperatingRangeError {
    InvalidUnit,
    NonFiniteBound,
    NonIncreasingRange
};

class AxisOperatingRange final {
public:
    [[nodiscard]] static Result<AxisOperatingRange, AxisOperatingRangeError> create(
        double minimum,
        double maximum,
        AxisCoordinateUnit unit);

    [[nodiscard]] double minimum() const noexcept;
    [[nodiscard]] double maximum() const noexcept;
    [[nodiscard]] AxisCoordinateUnit unit() const noexcept;

private:
    AxisOperatingRange(
        double minimum,
        double maximum,
        AxisCoordinateUnit unit);

    double minimum_;
    double maximum_;
    AxisCoordinateUnit unit_;
};

enum class AxisAccuracyError {
    InvalidValue,
    EmptyTestConditions
};

// Accuracy is stored as an independently supplied maximum absolute error.
// It is never derived from nominal command/readout resolution.
class AxisAccuracySpecification final {
public:
    [[nodiscard]] static Result<
        AxisAccuracySpecification,
        AxisAccuracyError> create(
            NonNegativeAxisAccuracy maximumAbsoluteError,
            std::string testConditions);

    [[nodiscard]] double maximumAbsoluteError() const noexcept;
    [[nodiscard]] const std::string& testConditions() const noexcept;

private:
    AxisAccuracySpecification(
        NonNegativeAxisAccuracy maximumAbsoluteError,
        std::string testConditions);

    NonNegativeAxisAccuracy maximumAbsoluteError_;
    std::string testConditions_;
};

enum class AxisRepeatabilityMetric {
    MaximumDeviation,
    StandardDeviation,
    RootMeanSquare,
    Unknown
};

enum class AxisRepeatabilityError {
    InvalidMetric,
    EmptyTestConditions
};

class AxisRepeatabilitySpecification final {
public:
    [[nodiscard]] static Result<
        AxisRepeatabilitySpecification,
        AxisRepeatabilityError> create(
            NonNegativeAxisRepeatability value,
            AxisRepeatabilityMetric metric,
            std::string testConditions);

    [[nodiscard]] double value() const noexcept;
    [[nodiscard]] AxisRepeatabilityMetric metric() const noexcept;
    [[nodiscard]] const std::string& testConditions() const noexcept;

private:
    AxisRepeatabilitySpecification(
        NonNegativeAxisRepeatability value,
        AxisRepeatabilityMetric metric,
        std::string testConditions);

    NonNegativeAxisRepeatability value_;
    AxisRepeatabilityMetric metric_;
    std::string testConditions_;
};

enum class AxisUncertaintyError {
    EmptyTestConditions
};

class AxisMeasurementUncertaintySpecification final {
public:
    [[nodiscard]] static Result<
        AxisMeasurementUncertaintySpecification,
        AxisUncertaintyError> create(
            NonNegativeAxisUncertainty value,
            std::string testConditions);

    [[nodiscard]] double value() const noexcept;
    [[nodiscard]] const std::string& testConditions() const noexcept;

private:
    AxisMeasurementUncertaintySpecification(
        NonNegativeAxisUncertainty value,
        std::string testConditions);

    NonNegativeAxisUncertainty value_;
    std::string testConditions_;
};

enum class AxisPositionFeedbackCapability {
    None,
    Encoder,
    LinearScale,
    DeviceReported,
    ExternalSensor,
    Other,
    Unknown
};

enum class AxisMeasurementLocation {
    Motor,
    Axis,
    Specimen,
    External,
    Other,
    Unknown
};

enum class AxisSpecificationError {
    InvalidMotionType,
    InvalidCoordinateUnit,
    UnitNotCompatibleWithMotion,
    OperatingRangeUnitMismatch,
    NoFeedbackHasMeasurementLocation,
    InvalidFeedbackCapability,
    InvalidMeasurementLocation
};

// Static axis capability/performance declaration. It does not store current
// position. Requested, device-reported, calibrated-estimated and unknown
// positions are runtime concepts. Values use the axis's declared coordinate
// unit; resolution, accuracy, repeatability and uncertainty are independent.
class AxisSpecification final {
public:
    [[nodiscard]] static Result<AxisSpecification, AxisSpecificationError> create(
        StageAxisId id,
        AxisMotionType motionType,
        AxisCoordinateUnit coordinateUnit,
        AxisOperatingRange operatingRange,
        PositiveAxisResolution nominalResolution,
        std::optional<AxisAccuracySpecification> accuracy,
        std::optional<AxisRepeatabilitySpecification> repeatability,
        std::optional<AxisMeasurementUncertaintySpecification> measurementUncertainty,
        AxisPositionFeedbackCapability positionFeedback,
        std::optional<AxisMeasurementLocation> measurementLocation = std::nullopt);

    [[nodiscard]] const StageAxisId& id() const noexcept;
    [[nodiscard]] AxisMotionType motionType() const noexcept;
    [[nodiscard]] AxisCoordinateUnit coordinateUnit() const noexcept;
    [[nodiscard]] const AxisOperatingRange& operatingRange() const noexcept;
    [[nodiscard]] double nominalResolution() const noexcept;
    [[nodiscard]] const std::optional<AxisAccuracySpecification>&
    accuracy() const noexcept;
    [[nodiscard]] const std::optional<AxisRepeatabilitySpecification>&
    repeatability() const noexcept;
    [[nodiscard]] const std::optional<AxisMeasurementUncertaintySpecification>&
    measurementUncertainty() const noexcept;
    [[nodiscard]] AxisPositionFeedbackCapability positionFeedback() const noexcept;
    [[nodiscard]] const std::optional<AxisMeasurementLocation>&
    measurementLocation() const noexcept;

private:
    AxisSpecification(
        StageAxisId id,
        AxisMotionType motionType,
        AxisCoordinateUnit coordinateUnit,
        AxisOperatingRange operatingRange,
        PositiveAxisResolution nominalResolution,
        std::optional<AxisAccuracySpecification> accuracy,
        std::optional<AxisRepeatabilitySpecification> repeatability,
        std::optional<AxisMeasurementUncertaintySpecification> measurementUncertainty,
        AxisPositionFeedbackCapability positionFeedback,
        std::optional<AxisMeasurementLocation> measurementLocation);

    StageAxisId id_;
    AxisMotionType motionType_;
    AxisCoordinateUnit coordinateUnit_;
    AxisOperatingRange operatingRange_;
    PositiveAxisResolution nominalResolution_;
    std::optional<AxisAccuracySpecification> accuracy_;
    std::optional<AxisRepeatabilitySpecification> repeatability_;
    std::optional<AxisMeasurementUncertaintySpecification> measurementUncertainty_;
    AxisPositionFeedbackCapability positionFeedback_;
    std::optional<AxisMeasurementLocation> measurementLocation_;
};

enum class StageSpecificationError {
    NoAxes,
    DuplicateAxisId
};

// Stage is one installed mechanical assembly with one or more independently
// specified linear/rotary axes. Stage identity is its EquipmentComponentId.
class StageSpecification final {
public:
    [[nodiscard]] static Result<StageSpecification, StageSpecificationError> create(
        EquipmentComponentId componentId,
        std::vector<AxisSpecification> axes);

    [[nodiscard]] const EquipmentComponentId& componentId() const noexcept;
    [[nodiscard]] const std::vector<AxisSpecification>& axes() const noexcept;

private:
    StageSpecification(
        EquipmentComponentId componentId,
        std::vector<AxisSpecification> axes);

    EquipmentComponentId componentId_;
    std::vector<AxisSpecification> axes_;
};

} // namespace microscope::core
