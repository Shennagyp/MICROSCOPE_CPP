#pragma once

#include "microscope/core/ids/Ids.h"
#include "microscope/core/result/Result.h"
#include "microscope/core/equipment/OpticalSpecifications.h"

#include <cstdint>
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

using StageNanometres = std::int64_t;
enum class StageLinearAxisRole { X, Y, Z };
enum class StageRotationAxisDirection { X, Y, Z };
struct StagePosition2DNanometres final { StageNanometres x=0, y=0; };
struct StagePosition3DNanometres final { StageNanometres x=0, y=0, z=0; };
struct StageRotationAxisGeometry final {
    StagePosition3DNanometres pointOnAxis;
    StageRotationAxisDirection direction = StageRotationAxisDirection::Z;
};

enum class StageNanometreRangeError { NonIncreasingRange };
class StageNanometreRange final {
public:
    [[nodiscard]] static Result<StageNanometreRange,StageNanometreRangeError>
    create(StageNanometres minimum,StageNanometres maximum);
    [[nodiscard]] StageNanometres minimum() const noexcept;
    [[nodiscard]] StageNanometres maximum() const noexcept;
    [[nodiscard]] bool contains(StageNanometres value) const noexcept;
private:
    StageNanometreRange(StageNanometres minimum,StageNanometres maximum) noexcept;
    StageNanometres minimum_,maximum_;
};

enum class StageRadianRangeError { InvalidRange };
class StageRadianRange final {
public:
    [[nodiscard]] static Result<StageRadianRange,StageRadianRangeError>
    create(double minimum,double maximum);
    [[nodiscard]] double minimum() const noexcept;
    [[nodiscard]] double maximum() const noexcept;
    [[nodiscard]] bool contains(double value) const noexcept;
private:
    StageRadianRange(double minimum,double maximum) noexcept;
    double minimum_,maximum_;
};

enum class StageSlotSpecificationError { InvalidLocalRange, CoordinateOverflow };
class StageSlotSpecification final {
public:
    [[nodiscard]] static Result<StageSlotSpecification,StageSlotSpecificationError>
    create(StageSlotId id,StagePosition2DNanometres origin,
        StageNanometreRange xRange,StageNanometreRange yRange);
    [[nodiscard]] const StageSlotId& id() const noexcept;
    [[nodiscard]] const StagePosition2DNanometres& originInStageCoordinates() const noexcept;
    [[nodiscard]] const StageNanometreRange& xRange() const noexcept;
    [[nodiscard]] const StageNanometreRange& yRange() const noexcept;
private:
    StageSlotSpecification(StageSlotId,StagePosition2DNanometres,
        StageNanometreRange,StageNanometreRange);
    StageSlotId id_;
    StagePosition2DNanometres origin_;
    StageNanometreRange xRange_,yRange_;
};

struct StageWorkingLinearAxisSpecification final {
    StageAxisId axisId;
    StageNanometreRange effectiveRange;
};
struct StageWorkingRotationAxisSpecification final {
    StageAxisId axisId;
    StageRadianRange effectiveRange;
};
enum class StageConfigurationSpecificationError {
    NoSlots, DuplicateWorkingAxis, DuplicateSlotId, OverlappingSlots
};
class StageConfigurationSpecification final {
public:
    [[nodiscard]] static Result<StageConfigurationSpecification,
        StageConfigurationSpecificationError> create(
            StageConfigurationId,
            std::vector<StageWorkingLinearAxisSpecification>,
            std::vector<StageWorkingRotationAxisSpecification>,
            std::vector<StageSlotSpecification>);
    [[nodiscard]] const StageConfigurationId& id() const noexcept;
    [[nodiscard]] const std::vector<StageWorkingLinearAxisSpecification>&
    workingLinearAxes() const noexcept;
    [[nodiscard]] const std::vector<StageWorkingRotationAxisSpecification>&
    workingRotationAxes() const noexcept;
    [[nodiscard]] const std::vector<StageSlotSpecification>& slots() const noexcept;
private:
    StageConfigurationSpecification(StageConfigurationId,
        std::vector<StageWorkingLinearAxisSpecification>,
        std::vector<StageWorkingRotationAxisSpecification>,
        std::vector<StageSlotSpecification>);
    StageConfigurationId id_;
    std::vector<StageWorkingLinearAxisSpecification> workingLinearAxes_;
    std::vector<StageWorkingRotationAxisSpecification> workingRotationAxes_;
    std::vector<StageSlotSpecification> slots_;
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
        std::optional<AxisMeasurementLocation> measurementLocation = std::nullopt,
        std::optional<StageLinearAxisRole> linearRole = std::nullopt,
        std::optional<StageRotationAxisGeometry> rotationGeometry = std::nullopt);

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
    [[nodiscard]] const std::optional<StageLinearAxisRole>& linearRole() const noexcept;
    [[nodiscard]] const std::optional<StageRotationAxisGeometry>& rotationGeometry() const noexcept;
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
        std::optional<AxisMeasurementLocation> measurementLocation,
        std::optional<StageLinearAxisRole> linearRole,
        std::optional<StageRotationAxisGeometry> rotationGeometry);

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
    std::optional<StageLinearAxisRole> linearRole_;
    std::optional<StageRotationAxisGeometry> rotationGeometry_;
};

enum class StageSpecificationError {
    NoAxes, DuplicateAxisId, DuplicateLinearAxisRole, InvalidAxisRoleBinding,
    UnknownWorkingAxis, InvalidEffectiveAxisRange, MissingXWorkingAxis,
    MissingYWorkingAxis, MissingZWorkingAxis, SlotOutsideWorkingRange,
    DuplicateConfigurationId
};

// Stage is one installed mechanical assembly with one or more independently
// specified linear/rotary axes. Stage identity is its EquipmentComponentId.
class StageSpecification final {
public:
    [[nodiscard]] static Result<StageSpecification, StageSpecificationError> create(
        EquipmentComponentId componentId,
        std::vector<AxisSpecification> axes,
        std::vector<StageConfigurationSpecification> configurations = {});

    [[nodiscard]] const EquipmentComponentId& componentId() const noexcept;
    [[nodiscard]] const std::vector<AxisSpecification>& axes() const noexcept;
    [[nodiscard]] const std::vector<StageConfigurationSpecification>& configurations() const noexcept;
    [[nodiscard]] const AxisSpecification* axis(const StageAxisId&) const noexcept;
    [[nodiscard]] const AxisSpecification* linearAxis(StageLinearAxisRole) const noexcept;

private:
    StageSpecification(
        EquipmentComponentId componentId,
        std::vector<AxisSpecification> axes,
        std::vector<StageConfigurationSpecification> configurations);

    EquipmentComponentId componentId_;
    std::vector<AxisSpecification> axes_;
    std::vector<StageConfigurationSpecification> configurations_;
};

} // namespace microscope::core
