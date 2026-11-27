#pragma once
#include "microscope/core/ids/Ids.h"
#include "microscope/core/result/Result.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace microscope::controller {
using Nanometres = std::int64_t;
using Radians = double;
using StageAxisId = core::StageAxisId;
using StageConfigurationId = core::StageConfigurationId;
using StageSlotId = core::StageSlotId;
using EquipmentComponentId = core::EquipmentComponentId;

enum class StageModelError {
    InvalidRange, InvalidAngularRange, InvalidAxisSpecification, InvalidSlotRange,
    InvalidDriver, DuplicateAxisId, DuplicateConfigurationId, DuplicateWorkingAxis,
    DuplicateSlotId, MissingXWorkingAxis, MissingYWorkingAxis, MissingZWorkingAxis,
    UnknownWorkingAxis, EffectiveRangeOutsideMechanicalRange, SlotOutsideWorkingRange,
    OverlappingSlots, NoSlots, ConfigurationNotFound, PositionUnknown,
    TargetOutsideWorkingRange, TargetOutsideSlot, RotationTargetMismatch,
    CoordinateOverflow, DriverFailure
};

class NanometreRange final {
public:
    static core::Result<NanometreRange, StageModelError> create(Nanometres min, Nanometres max);
    Nanometres minimum() const noexcept;
    Nanometres maximum() const noexcept;
    bool contains(Nanometres value) const noexcept;
private:
    NanometreRange(Nanometres min, Nanometres max) noexcept;
    Nanometres min_, max_;
};

class RadianRange final {
public:
    static core::Result<RadianRange, StageModelError> create(Radians min, Radians max);
    Radians minimum() const noexcept;
    Radians maximum() const noexcept;
    bool contains(Radians value) const noexcept;
private:
    RadianRange(Radians min, Radians max) noexcept;
    Radians min_, max_;
};

struct StagePosition2D final { Nanometres x=0, y=0; };
struct StagePosition3D final { Nanometres x=0, y=0, z=0; };
struct SlotRelativePosition final { Nanometres x=0, y=0; };

enum class LinearAxisRole { X, Y, Z };
class LinearAxisSpecification final {
public:
    static core::Result<LinearAxisSpecification, StageModelError> create(
        StageAxisId id, LinearAxisRole role, NanometreRange mechanicalRange,
        Nanometres minimumIncrement, Nanometres accuracy, Nanometres precision);
    const StageAxisId& id() const noexcept;
    LinearAxisRole role() const noexcept;
    const NanometreRange& mechanicalTravelRange() const noexcept;
    Nanometres minimumIncrement() const noexcept;
    Nanometres accuracy() const noexcept;
    Nanometres precision() const noexcept;
private:
    LinearAxisSpecification(StageAxisId, LinearAxisRole, NanometreRange,
        Nanometres, Nanometres, Nanometres);
    StageAxisId id_;
    LinearAxisRole role_;
    NanometreRange mechanicalRange_;
    Nanometres minimumIncrement_, accuracy_, precision_;
};

enum class RotationAxisDirection { X, Y, Z };
struct RotationAxisGeometry final {
    StagePosition3D pointOnAxis;
    RotationAxisDirection direction = RotationAxisDirection::Z;
};
class RotationAxisSpecification final {
public:
    static core::Result<RotationAxisSpecification, StageModelError> create(
        StageAxisId id, RotationAxisGeometry geometry, RadianRange mechanicalRange,
        Radians minimumIncrement, Radians accuracy, Radians precision);
    const StageAxisId& id() const noexcept;
    const RotationAxisGeometry& geometry() const noexcept;
    const RadianRange& mechanicalTravelRange() const noexcept;
    Radians minimumIncrement() const noexcept;
    Radians accuracy() const noexcept;
    Radians precision() const noexcept;
private:
    RotationAxisSpecification(StageAxisId, RotationAxisGeometry, RadianRange,
        Radians, Radians, Radians);
    StageAxisId id_;
    RotationAxisGeometry geometry_;
    RadianRange mechanicalRange_;
    Radians minimumIncrement_, accuracy_, precision_;
};

struct WorkingLinearAxis final { StageAxisId axisId; NanometreRange effectiveRange; };
struct WorkingRotationAxis final { StageAxisId axisId; RadianRange effectiveRange; };

class StageSlot final {
public:
    static core::Result<StageSlot, StageModelError> create(
        StageSlotId id, StagePosition2D origin, NanometreRange xRange, NanometreRange yRange);
    const StageSlotId& id() const noexcept;
    const StagePosition2D& originInStageCoordinates() const noexcept;
    const NanometreRange& xRange() const noexcept;
    const NanometreRange& yRange() const noexcept;
private:
    StageSlot(StageSlotId, StagePosition2D, NanometreRange, NanometreRange);
    StageSlotId id_;
    StagePosition2D origin_;
    NanometreRange xRange_, yRange_;
};

class StageConfiguration final {
public:
    static core::Result<StageConfiguration, StageModelError> create(
        StageConfigurationId id, std::vector<WorkingLinearAxis> linearAxes,
        std::vector<WorkingRotationAxis> rotationAxes, std::vector<StageSlot> slots);
    const StageConfigurationId& id() const noexcept;
    const std::vector<WorkingLinearAxis>& workingLinearAxes() const noexcept;
    const std::vector<WorkingRotationAxis>& workingRotationAxes() const noexcept;
    const std::vector<StageSlot>& slots() const noexcept;
private:
    StageConfiguration(StageConfigurationId, std::vector<WorkingLinearAxis>,
        std::vector<WorkingRotationAxis>, std::vector<StageSlot>);
    StageConfigurationId id_;
    std::vector<WorkingLinearAxis> linearAxes_;
    std::vector<WorkingRotationAxis> rotationAxes_;
    std::vector<StageSlot> slots_;
};

class StageSpecification final {
public:
    static core::Result<StageSpecification, StageModelError> create(
        EquipmentComponentId id, std::vector<LinearAxisSpecification> linearAxes,
        std::vector<RotationAxisSpecification> rotationAxes,
        std::vector<StageConfiguration> configurations);
    const EquipmentComponentId& componentId() const noexcept;
    const std::vector<LinearAxisSpecification>& linearAxes() const noexcept;
    const std::vector<RotationAxisSpecification>& rotationAxes() const noexcept;
    const std::vector<StageConfiguration>& configurations() const noexcept;
    const StageConfiguration* configuration(const StageConfigurationId&) const noexcept;
    const LinearAxisSpecification* linearAxis(const StageAxisId&) const noexcept;
    const LinearAxisSpecification* linearAxis(LinearAxisRole) const noexcept;
    const RotationAxisSpecification* rotationAxis(const StageAxisId&) const noexcept;
private:
    StageSpecification(EquipmentComponentId, std::vector<LinearAxisSpecification>,
        std::vector<RotationAxisSpecification>, std::vector<StageConfiguration>);
    EquipmentComponentId id_;
    std::vector<LinearAxisSpecification> linearAxes_;
    std::vector<RotationAxisSpecification> rotationAxes_;
    std::vector<StageConfiguration> configurations_;
};

struct LinearAxisTarget final { StageAxisId axisId; Nanometres position=0; };
struct RotationAxisTarget final { StageAxisId axisId; Radians angle=0.0; };
struct RotationAxisDisplacement final { StageAxisId axisId; Radians displacement=0.0; };
struct StageAbsolutePosition final {
    StagePosition3D linear;
    std::vector<RotationAxisTarget> rotations;
};
struct StageDisplacement final {
    StagePosition3D linear;
    std::vector<RotationAxisDisplacement> rotations;
};

enum class StageDriverErrorCode {
    NotConnected, Busy, Unsupported, LimitReached, Timeout, Fault, CommunicationFailure, Other
};
struct StageDriverError final {
    StageDriverErrorCode code=StageDriverErrorCode::Other;
    std::string detail;
};

// A driver must wait for terminal completion. "Command accepted" is not success.
class IStageDriver {
public:
    virtual ~IStageDriver() = default;
    virtual core::Status<StageDriverError> homeLinearAxis(const StageAxisId&) = 0;
    virtual core::Status<StageDriverError> homeRotationAxis(const StageAxisId&) = 0;
    virtual core::Status<StageDriverError> moveLinearAxesTo(
        const std::vector<LinearAxisTarget>& targets) = 0;
    virtual core::Status<StageDriverError> moveRotationAxesTo(
        const std::vector<RotationAxisTarget>& targets) = 0;
};

enum class StagePositionEvidence { Unknown, HomingReference, CommandDerived };
struct StagePositionSnapshot final {
    std::optional<Nanometres> x, y, z;
    std::vector<RotationAxisTarget> rotations;
    StagePositionEvidence evidence=StagePositionEvidence::Unknown;
    bool xyKnown() const noexcept;
    bool xyzKnown() const noexcept;
};
struct StageOperationError final {
    StageModelError code=StageModelError::DriverFailure;
    std::optional<StageDriverError> driverError;
};

class Stage final {
public:
    static core::Result<Stage, StageModelError> create(
        StageSpecification specification, std::shared_ptr<IStageDriver> driver,
        StageConfigurationId activeConfigurationId);
    core::Status<StageOperationError> home();
    core::Status<StageOperationError> refreshPosition();
    core::Status<StageOperationError> moveTo(SlotRelativePosition, const StageSlotId&);
    core::Status<StageOperationError> moveTo(const StageAbsolutePosition&);
    core::Status<StageOperationError> moveBy(const StageDisplacement&);
    core::Status<StageOperationError> activateConfiguration(const StageConfigurationId&);
    const StageSpecification& specification() const noexcept;
    const StageConfiguration& activeConfiguration() const noexcept;
    const StagePositionSnapshot& currentPosition() const noexcept;
    std::optional<StageSlotId> currentSlotId() const noexcept;
private:
    Stage(StageSpecification, std::shared_ptr<IStageDriver>, StageConfigurationId);
    const WorkingLinearAxis* workingLinearAxis(LinearAxisRole) const noexcept;
    void invalidatePosition() noexcept;
    core::Status<StageOperationError> driverFailure(const StageDriverError&);
    StageSpecification specification_;
    std::shared_ptr<IStageDriver> driver_;
    StageConfigurationId activeConfigurationId_;
    StagePositionSnapshot position_;
};

} // namespace microscope::controller
