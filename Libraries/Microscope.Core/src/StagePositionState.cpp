#include "microscope/core/motion/StagePositionState.h"
#include <utility>

namespace microscope::core {

StagePositionState::StagePositionState(
    EquipmentComponentId stageComponentId,
    StageAxisPositionState horizontal,
    StageAxisPositionState transverse)
    :stageComponentId_(std::move(stageComponentId)),
     horizontal_(std::move(horizontal)),
     transverse_(std::move(transverse)) {}

Result<StagePositionState,StageAxisPositionStateError>
StagePositionState::create(const DualSlideCarrierSpecification& specification) {
    auto horizontal=StageAxisPositionState::unknown(
        specification.stageComponentId(),
        specification.horizontalAxisId(),
        specification.horizontalUnit());
    if(!horizontal)
        return Result<StagePositionState,StageAxisPositionStateError>::failure(
            horizontal.error());

    auto transverse=StageAxisPositionState::unknown(
        specification.stageComponentId(),
        specification.transverseAxisId(),
        specification.transverseUnit());
    if(!transverse)
        return Result<StagePositionState,StageAxisPositionStateError>::failure(
            transverse.error());

    return Result<StagePositionState,StageAxisPositionStateError>::success(
        StagePositionState(
            specification.stageComponentId(),
            std::move(horizontal).value(),
            std::move(transverse).value()));
}

const EquipmentComponentId& StagePositionState::stageComponentId()const noexcept {
    return stageComponentId_;
}
const StageAxisPositionState& StagePositionState::horizontal()const noexcept {
    return horizontal_;
}
const StageAxisPositionState& StagePositionState::transverse()const noexcept {
    return transverse_;
}

Result<StagePositionState,StageAxisPositionStateError>
StagePositionState::afterCommandedDisplacement(
    const AxisDisplacement& displacement,
    std::optional<double> additionalErrorBound) const {
    if(displacement.axisId()==horizontal_.axisId()) {
        auto updated=horizontal_.afterCommandedDisplacement(
            displacement,additionalErrorBound);
        if(!updated)
            return Result<StagePositionState,StageAxisPositionStateError>::failure(
                updated.error());
        return Result<StagePositionState,StageAxisPositionStateError>::success(
            StagePositionState(
                stageComponentId_,std::move(updated).value(),transverse_));
    }
    if(displacement.axisId()==transverse_.axisId()) {
        auto updated=transverse_.afterCommandedDisplacement(
            displacement,additionalErrorBound);
        if(!updated)
            return Result<StagePositionState,StageAxisPositionStateError>::failure(
                updated.error());
        return Result<StagePositionState,StageAxisPositionStateError>::success(
            StagePositionState(
                stageComponentId_,horizontal_,std::move(updated).value()));
    }
    return Result<StagePositionState,StageAxisPositionStateError>::failure(
        StageAxisPositionStateError::AxisMismatch);
}

Result<StagePositionState,StageAxisPositionStateError>
StagePositionState::afterCommandedPosition(
    const AxisPosition& target,
    std::optional<double> additionalErrorBound) const {
    if(target.axisId()==horizontal_.axisId()) {
        auto updated=horizontal_.afterCommandedPosition(target,additionalErrorBound);
        if(!updated)
            return Result<StagePositionState,StageAxisPositionStateError>::failure(updated.error());
        return Result<StagePositionState,StageAxisPositionStateError>::success(
            StagePositionState(stageComponentId_,std::move(updated).value(),transverse_));
    }
    if(target.axisId()==transverse_.axisId()) {
        auto updated=transverse_.afterCommandedPosition(target,additionalErrorBound);
        if(!updated)
            return Result<StagePositionState,StageAxisPositionStateError>::failure(updated.error());
        return Result<StagePositionState,StageAxisPositionStateError>::success(
            StagePositionState(stageComponentId_,horizontal_,std::move(updated).value()));
    }
    return Result<StagePositionState,StageAxisPositionStateError>::failure(
        StageAxisPositionStateError::AxisMismatch);
}

Result<StagePositionState,StageAxisPositionStateError>
StagePositionState::afterPositionObservation(
    const AxisPositionObservation& observation,ObservationSource source) const {
    const auto& axisId=observation.position().axisId();
    if(axisId==horizontal_.axisId()) {
        auto updated=horizontal_.afterPositionObservation(observation,source);
        if(!updated)
            return Result<StagePositionState,StageAxisPositionStateError>::failure(
                updated.error());
        return Result<StagePositionState,StageAxisPositionStateError>::success(
            StagePositionState(
                stageComponentId_,std::move(updated).value(),transverse_));
    }
    if(axisId==transverse_.axisId()) {
        auto updated=transverse_.afterPositionObservation(observation,source);
        if(!updated)
            return Result<StagePositionState,StageAxisPositionStateError>::failure(
                updated.error());
        return Result<StagePositionState,StageAxisPositionStateError>::success(
            StagePositionState(
                stageComponentId_,horizontal_,std::move(updated).value()));
    }
    return Result<StagePositionState,StageAxisPositionStateError>::failure(
        StageAxisPositionStateError::AxisMismatch);
}

Result<StagePositionState,StageAxisPositionStateError>
StagePositionState::afterVerifiedHomingObservation(
    const AxisPositionObservation& observation,ObservationSource source) const {
    const auto& axisId=observation.position().axisId();
    if(axisId==horizontal_.axisId()) {
        auto updated=StageAxisPositionState::homed(
            stageComponentId_,observation,source);
        if(!updated)
            return Result<StagePositionState,StageAxisPositionStateError>::failure(
                updated.error());
        return Result<StagePositionState,StageAxisPositionStateError>::success(
            StagePositionState(
                stageComponentId_,std::move(updated).value(),transverse_));
    }
    if(axisId==transverse_.axisId()) {
        auto updated=StageAxisPositionState::homed(
            stageComponentId_,observation,source);
        if(!updated)
            return Result<StagePositionState,StageAxisPositionStateError>::failure(
                updated.error());
        return Result<StagePositionState,StageAxisPositionStateError>::success(
            StagePositionState(
                stageComponentId_,horizontal_,std::move(updated).value()));
    }
    return Result<StagePositionState,StageAxisPositionStateError>::failure(
        StageAxisPositionStateError::AxisMismatch);
}

StagePositionState StagePositionState::invalidateMachineReference() const {
    return StagePositionState(
        stageComponentId_,
        horizontal_.invalidateMachineReference(),
        transverse_.invalidateMachineReference());
}

Result<SlidePositionEstimate,CurrentSlidePositionError>
StagePositionState::currentSlidePosition(
    const DualSlideCarrierState& carrier,const SlideId& slideId) const {
    if(carrier.specification().stageComponentId()!=stageComponentId_)
        return Result<SlidePositionEstimate,CurrentSlidePositionError>::failure(
            CurrentSlidePositionError::StageComponentMismatch);
    return carrier.currentSlidePosition(slideId,horizontal_,transverse_);
}

} // namespace microscope::core
