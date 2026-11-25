#pragma once
#include "microscope/core/motion/StageMotionPort.h"
#include <optional>

namespace microscope::core {

enum class StageMachineReferenceStatus { Unreferenced, Referenced };

enum class StageAxisPositionStateError {
    UnsupportedCoordinateUnit,
    InvalidUncertainty,
    AxisMismatch,
    UnitMismatch,
    InvalidEvidence,
    InvalidObservationSource,
    PositionOverflow
};

// Immutable per-axis runtime state. The error bound is a conservative bound
// accumulated from modeled move errors. It is not a measured tracking error.
class StageAxisPositionState final {
public:
    static Result<StageAxisPositionState,StageAxisPositionStateError> unknown(
        EquipmentComponentId stageComponentId,StageAxisId axisId,AxisCoordinateUnit unit);

    // Called only after Core has verified a completed homing/reference action
    // and received the final reference position evidence.
    static Result<StageAxisPositionState,StageAxisPositionStateError> homed(
        EquipmentComponentId stageComponentId,AxisPosition homePosition,double positionErrorBound,
        StagePositionEvidence evidence,ObservationSource source);

    Result<StageAxisPositionState,StageAxisPositionStateError> afterCommandedDisplacement(
        const AxisDisplacement&,std::optional<double> additionalErrorBound) const;

    Result<StageAxisPositionState,StageAxisPositionStateError> afterPositionObservation(
        const AxisPositionObservation&,ObservationSource source) const;
    Result<StageAxisPositionState,StageAxisPositionStateError> afterPositionObservation(
        AxisPosition observedPosition,double observationErrorBound,
        StagePositionEvidence evidence,ObservationSource source) const;

    StageAxisPositionState invalidateMachineReference() const;

    const EquipmentComponentId& stageComponentId() const noexcept;
    const StageAxisId& axisId() const noexcept;
    AxisCoordinateUnit unit() const noexcept;
    const std::optional<AxisPosition>& position() const noexcept;
    const std::optional<double>& accumulatedPositionErrorBound() const noexcept;
    StagePositionEvidence evidence() const noexcept;
    ObservationSource source() const noexcept;
    StageMachineReferenceStatus referenceStatus() const noexcept;
    bool positionIsUsable() const noexcept;

    // threshold is expressed in this axis's declared coordinate unit.
    // Unknown/unbounded error or an invalid reference always requires homing.
    bool requiresHoming(double threshold) const noexcept;

private:
    StageAxisPositionState(
        EquipmentComponentId stageComponentId,StageAxisId axisId,AxisCoordinateUnit unit,
        std::optional<AxisPosition> position,
        std::optional<double> accumulatedErrorBound,
        StagePositionEvidence evidence,ObservationSource source,
        StageMachineReferenceStatus referenceStatus);

    EquipmentComponentId stageComponentId_;
    StageAxisId axisId_;
    AxisCoordinateUnit unit_;
    std::optional<AxisPosition> position_;
    std::optional<double> accumulatedErrorBound_;
    StagePositionEvidence evidence_;
    ObservationSource source_;
    StageMachineReferenceStatus referenceStatus_;
};

} // namespace microscope::core
