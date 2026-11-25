#pragma once

#include "microscope/core/motion/StageAxisPositionState.h"
#include "microscope/core/navigation/DualSlideNavigation.h"
#include <optional>

namespace microscope::core {

// Immutable aggregate position state for the two-axis plane used by the
// installed dual-slide carrier. One instance is owned by one serialized
// runtime writer; driver callbacks never mutate it directly.
class StagePositionState final {
public:
    static Result<StagePositionState,StageAxisPositionStateError> create(
        const DualSlideCarrierSpecification& specification);

    const EquipmentComponentId& stageComponentId() const noexcept;
    const StageAxisPositionState& horizontal() const noexcept;
    const StageAxisPositionState& transverse() const noexcept;

    // Apply only after the matching physical action has completed. The error
    // contribution is supplied by the active axis motion calibration.
    Result<StagePositionState,StageAxisPositionStateError> afterCommandedDisplacement(
        const AxisDisplacement&,std::optional<double> additionalErrorBound) const;
    Result<StagePositionState,StageAxisPositionStateError> afterCommandedPosition(
        const AxisPosition&,std::optional<double> additionalErrorBound) const;

    Result<StagePositionState,StageAxisPositionStateError> afterPositionObservation(
        const AxisPositionObservation&,ObservationSource source) const;

    // Call only after the operation runtime has verified the home action.
    Result<StagePositionState,StageAxisPositionStateError> afterVerifiedHomingObservation(
        const AxisPositionObservation&,ObservationSource source) const;

    StagePositionState invalidateMachineReference() const;

    Result<SlidePositionEstimate,CurrentSlidePositionError> currentSlidePosition(
        const DualSlideCarrierState&,const SlideId&) const;

private:
    StagePositionState(
        EquipmentComponentId stageComponentId,
        StageAxisPositionState horizontal,
        StageAxisPositionState transverse);

    EquipmentComponentId stageComponentId_;
    StageAxisPositionState horizontal_;
    StageAxisPositionState transverse_;
};

} // namespace microscope::core
