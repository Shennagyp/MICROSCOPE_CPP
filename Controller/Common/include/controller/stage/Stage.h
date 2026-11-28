#pragma once
#include "microscope/core/ids/Ids.h"
#include "microscope/core/motion/StageMotionPort.h"
#include "microscope/core/result/Result.h"
#include <cstdint>
#include <optional>
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
    DuplicateAxisId, DuplicateConfigurationId, DuplicateWorkingAxis, DuplicateSlotId,
    MissingXWorkingAxis, MissingYWorkingAxis, MissingZWorkingAxis, UnknownWorkingAxis,
    EffectiveRangeOutsideMechanicalRange, SlotOutsideWorkingRange, OverlappingSlots,
    NoSlots, ConfigurationNotFound, PositionUnknown, TargetOutsideWorkingRange,
    TargetOutsideSlot, RotationTargetMismatch, CoordinateOverflow, MotionInProgress,
    StaleMotionPlan, RefreshNotReady, HomeReferenceEvidenceRequired,
    InvalidHomeReferenceEvidence, MotionSequenceExhausted
};

class NanometreRange final {
public:
    static core::Result<NanometreRange,StageModelError> create(Nanometres,Nanometres);
    Nanometres minimum() const noexcept;
    Nanometres maximum() const noexcept;
    bool contains(Nanometres) const noexcept;
private:
    NanometreRange(Nanometres,Nanometres) noexcept;
    Nanometres minimum_,maximum_;
};
class RadianRange final {
public:
    static core::Result<RadianRange,StageModelError> create(Radians,Radians);
    Radians minimum() const noexcept;
    Radians maximum() const noexcept;
    bool contains(Radians) const noexcept;
private:
    RadianRange(Radians,Radians) noexcept;
    Radians minimum_,maximum_;
};

struct StagePosition2D final { Nanometres x=0,y=0; };
struct StagePosition3D final { Nanometres x=0,y=0,z=0; };
struct SlotRelativePosition final { Nanometres x=0,y=0; };
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

enum class LinearAxisRole { X,Y,Z };
class LinearAxis final {
public:
    static core::Result<LinearAxis,StageModelError> create(
        StageAxisId,LinearAxisRole,NanometreRange,Nanometres,Nanometres,Nanometres,
        std::optional<Nanometres> measurementUncertainty = std::nullopt);
    const StageAxisId& id() const noexcept;
    LinearAxisRole role() const noexcept;
    const NanometreRange& mechanicalTravelRange() const noexcept;
    Nanometres minimumIncrement() const noexcept;
    Nanometres accuracy() const noexcept;
    Nanometres precision() const noexcept;
    const std::optional<Nanometres>& measurementUncertainty() const noexcept;

    // Constructs an axis command. Hardware execution is delegated through
    // Core's existing IStageMotionPort and correlated operation runtime.
    StageAxisId homeCommand() const;
    core::Result<LinearAxisTarget,StageModelError> moveTo(Nanometres) const;
    core::Result<LinearAxisTarget,StageModelError> moveBy(
        Nanometres currentPosition,Nanometres displacement) const;
private:
    LinearAxis(StageAxisId,LinearAxisRole,NanometreRange,Nanometres,Nanometres,Nanometres,
        std::optional<Nanometres>);
    StageAxisId id_;
    LinearAxisRole role_;
    NanometreRange mechanicalTravelRange_;
    Nanometres minimumIncrement_,accuracy_,precision_;
    std::optional<Nanometres> measurementUncertainty_;
};

enum class RotationAxisDirection { X,Y,Z };
struct RotationAxisGeometry final {
    StagePosition3D pointOnAxis;
    RotationAxisDirection direction=RotationAxisDirection::Z;
};
class RotationAxis final {
public:
    static core::Result<RotationAxis,StageModelError> create(
        StageAxisId,RotationAxisGeometry,RadianRange,Radians,Radians,Radians,
        std::optional<Radians> measurementUncertainty = std::nullopt);
    const StageAxisId& id() const noexcept;
    const RotationAxisGeometry& geometry() const noexcept;
    const RadianRange& mechanicalTravelRange() const noexcept;
    Radians minimumIncrement() const noexcept;
    Radians accuracy() const noexcept;
    Radians precision() const noexcept;
    const std::optional<Radians>& measurementUncertainty() const noexcept;

    StageAxisId homeCommand() const;
    core::Result<RotationAxisTarget,StageModelError> rotateTo(Radians) const;
    core::Result<RotationAxisTarget,StageModelError> rotateBy(Radians current,Radians delta) const;
private:
    RotationAxis(StageAxisId,RotationAxisGeometry,RadianRange,Radians,Radians,Radians,
        std::optional<Radians>);
    StageAxisId id_;
    RotationAxisGeometry geometry_;
    RadianRange mechanicalTravelRange_;
    Radians minimumIncrement_,accuracy_,precision_;
    std::optional<Radians> measurementUncertainty_;
};

struct WorkingLinearAxis final { StageAxisId axisId; NanometreRange effectiveRange; };
struct WorkingRotationAxis final { StageAxisId axisId; RadianRange effectiveRange; };

class StageSlot final {
public:
    static core::Result<StageSlot,StageModelError> create(
        StageSlotId,StagePosition2D,NanometreRange,NanometreRange);
    const StageSlotId& id() const noexcept;
    const StagePosition2D& originInStageCoordinates() const noexcept;
    const NanometreRange& xRange() const noexcept;
    const NanometreRange& yRange() const noexcept;
private:
    StageSlot(StageSlotId,StagePosition2D,NanometreRange,NanometreRange);
    StageSlotId id_;
    StagePosition2D origin_;
    NanometreRange xRange_,yRange_;
};

class StageConfiguration final {
public:
    static core::Result<StageConfiguration,StageModelError> create(
        StageConfigurationId,std::vector<WorkingLinearAxis>,
        std::vector<WorkingRotationAxis>,std::vector<StageSlot>);
    const StageConfigurationId& id() const noexcept;
    const std::vector<WorkingLinearAxis>& workingLinearAxes() const noexcept;
    const std::vector<WorkingRotationAxis>& workingRotationAxes() const noexcept;
    const std::vector<StageSlot>& slots() const noexcept;
private:
    StageConfiguration(StageConfigurationId,std::vector<WorkingLinearAxis>,
        std::vector<WorkingRotationAxis>,std::vector<StageSlot>);
    StageConfigurationId id_;
    std::vector<WorkingLinearAxis> workingLinearAxes_;
    std::vector<WorkingRotationAxis> workingRotationAxes_;
    std::vector<StageSlot> slots_;
};

class StageSpecification final {
public:
    static core::Result<StageSpecification,StageModelError> create(
        EquipmentComponentId,std::vector<LinearAxis>,std::vector<RotationAxis>,
        std::vector<StageConfiguration>);
    const EquipmentComponentId& componentId() const noexcept;
    const std::vector<LinearAxis>& linearAxes() const noexcept;
    const std::vector<RotationAxis>& rotationAxes() const noexcept;
    const std::vector<StageConfiguration>& configurations() const noexcept;
    const StageConfiguration* configuration(const StageConfigurationId&) const noexcept;
    const LinearAxis* linearAxis(const StageAxisId&) const noexcept;
    const LinearAxis* linearAxis(LinearAxisRole) const noexcept;
    const RotationAxis* rotationAxis(const StageAxisId&) const noexcept;
private:
    StageSpecification(EquipmentComponentId,std::vector<LinearAxis>,
        std::vector<RotationAxis>,std::vector<StageConfiguration>);
    EquipmentComponentId componentId_;
    std::vector<LinearAxis> linearAxes_;
    std::vector<RotationAxis> rotationAxes_;
    std::vector<StageConfiguration> configurations_;
};

enum class StageMotionAction { Home,MoveTo };
struct StageMotionPlan final {
    std::uint64_t sequence=0;
    std::optional<StageConfigurationId> configurationId;
    StageMotionAction action=StageMotionAction::MoveTo;
    std::vector<StageAxisId> axesToHome;
    std::vector<LinearAxisTarget> linearTargets;
    std::vector<RotationAxisTarget> rotationTargets;
    bool refreshRestoration=false;
};
struct StageRefreshPlan final {
    StageMotionPlan homePlan;
    StageAbsolutePosition restorePosition;
    StageConfigurationId configurationId;
};

enum class StagePositionEvidence { Unknown,HomingReference,CommandDerived };
struct StagePositionSnapshot final {
    std::optional<Nanometres> x,y,z;
    std::vector<RotationAxisTarget> rotations;
    StagePositionEvidence evidence=StagePositionEvidence::Unknown;
    bool xyKnown() const noexcept;
    bool xyzKnown() const noexcept;
};

class Stage final {
public:
    static core::Result<Stage,StageModelError> create(
        StageSpecification,StageConfigurationId);

    core::Result<StageMotionPlan,StageModelError> home();
    core::Result<StageRefreshPlan,StageModelError> refreshPosition();
    core::Result<StageMotionPlan,StageModelError> restorePositionAfterRefresh(
        const StageRefreshPlan&);

    core::Result<StageMotionPlan,StageModelError> moveTo(
        SlotRelativePosition,const StageSlotId&);
    core::Result<StageMotionPlan,StageModelError> moveTo(const StageAbsolutePosition&);
    core::Result<StageMotionPlan,StageModelError> moveBy(const StageDisplacement&);

    // A MoveTo changes the estimate only after its exact completion is accepted.
    // A home additionally requires the normalized machine-reference fact.
    core::Status<StageModelError> validateForDispatch(const StageMotionPlan&) const;
    core::Status<StageModelError> cancelUndispatchedPlan(const StageMotionPlan&);
    core::Status<StageModelError> recordCompletedMotion(
        const StageMotionPlan&, const core::StageMotionActionCompletion&);
    core::Status<StageModelError> recordCompletedHome(
        const StageMotionPlan&, const core::StageMotionActionCompletion&,
        const core::StageHomeReferenceObservation&);
    core::Status<StageModelError> recordFailedMotion(const StageMotionPlan&);
    void invalidatePosition() noexcept;
    core::Status<StageModelError> activateConfiguration(const StageConfigurationId&);

    const StageSpecification& specification() const noexcept;
    const StageConfiguration& activeConfiguration() const noexcept;
    const StagePositionSnapshot& currentPosition() const noexcept;
    std::optional<StageSlotId> currentSlotId() const noexcept;

private:
    Stage(StageSpecification,StageConfigurationId);
    const WorkingLinearAxis* workingLinearAxis(LinearAxisRole) const noexcept;
    core::Result<StageMotionPlan,StageModelError> createMovePlan(
        std::vector<LinearAxisTarget>,std::vector<RotationAxisTarget>,bool restoration=false);
    core::Status<StageModelError> validatePendingPlan(const StageMotionPlan&) const;

    StageSpecification specification_;
    StageConfigurationId activeConfigurationId_;
    StagePositionSnapshot position_;
    std::uint64_t nextSequence_=1;
    std::optional<StageMotionPlan> pendingPlan_;
    std::optional<StageMotionPlan> lastCompletedPlan_;
};
} // namespace microscope::controller
