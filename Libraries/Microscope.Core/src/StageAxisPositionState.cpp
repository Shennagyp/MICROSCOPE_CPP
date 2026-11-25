#include "microscope/core/motion/StageAxisPositionState.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace microscope::core {
namespace {
bool physicalUnit(AxisCoordinateUnit u) noexcept {
    return u==AxisCoordinateUnit::Micrometers||u==AxisCoordinateUnit::Millimeters||
           u==AxisCoordinateUnit::Degrees||u==AxisCoordinateUnit::Radians;
}
bool knownEvidence(StagePositionEvidence e) noexcept {
    switch(e) {
    case StagePositionEvidence::Unknown:
    case StagePositionEvidence::CommandDerivedEstimate:
    case StagePositionEvidence::ControllerReported:
    case StagePositionEvidence::EncoderMeasured:
    case StagePositionEvidence::ExternalMeasured:return true;
    }
    return false;
}
bool observationEvidence(StagePositionEvidence e) noexcept {
    return e==StagePositionEvidence::ControllerReported||
           e==StagePositionEvidence::EncoderMeasured||
           e==StagePositionEvidence::ExternalMeasured;
}
bool usableSource(ObservationSource s) noexcept {
    return s==ObservationSource::HardwareReadback||s==ObservationSource::DeviceEvent||
           s==ObservationSource::SdkStatus;
}
bool validError(std::optional<double> v) noexcept {
    return !v||(std::isfinite(*v)&&*v>=0.0);
}
}
StageAxisPositionState::StageAxisPositionState(
    EquipmentComponentId stageComponentId,StageAxisId axisId,AxisCoordinateUnit unit,
    std::optional<AxisPosition> position,
    std::optional<double> error,StagePositionEvidence evidence,ObservationSource source,
    StageMachineReferenceStatus reference)
    :stageComponentId_(std::move(stageComponentId)),axisId_(std::move(axisId)),unit_(unit),position_(std::move(position)),
     accumulatedErrorBound_(error),evidence_(evidence),source_(source),
     referenceStatus_(reference) {}

Result<StageAxisPositionState,StageAxisPositionStateError>
StageAxisPositionState::unknown(EquipmentComponentId stageComponentId,StageAxisId axisId,AxisCoordinateUnit unit) {
    if(!physicalUnit(unit))
        return Result<StageAxisPositionState,StageAxisPositionStateError>::failure(
            StageAxisPositionStateError::UnsupportedCoordinateUnit);
    return Result<StageAxisPositionState,StageAxisPositionStateError>::success(
        StageAxisPositionState(std::move(stageComponentId),std::move(axisId),unit,std::nullopt,std::nullopt,
            StagePositionEvidence::Unknown,ObservationSource::Unknown,
            StageMachineReferenceStatus::Unreferenced));
}

Result<StageAxisPositionState,StageAxisPositionStateError>
StageAxisPositionState::homed(
    EquipmentComponentId stageComponentId,AxisPosition position,double error,
    StagePositionEvidence evidence,ObservationSource source) {
    if(!std::isfinite(error)||error<0)
        return Result<StageAxisPositionState,StageAxisPositionStateError>::failure(
            StageAxisPositionStateError::InvalidUncertainty);
    if(!knownEvidence(evidence)||!observationEvidence(evidence))
        return Result<StageAxisPositionState,StageAxisPositionStateError>::failure(
            StageAxisPositionStateError::InvalidEvidence);
    if(!usableSource(source))
        return Result<StageAxisPositionState,StageAxisPositionStateError>::failure(
            StageAxisPositionStateError::InvalidObservationSource);
    const auto axis=position.axisId();
    const auto unit=position.unit();
    return Result<StageAxisPositionState,StageAxisPositionStateError>::success(
        StageAxisPositionState(std::move(stageComponentId),axis,unit,std::move(position),error,evidence,source,
            StageMachineReferenceStatus::Referenced));
}

Result<StageAxisPositionState,StageAxisPositionStateError>
StageAxisPositionState::afterCommandedDisplacement(
    const AxisDisplacement& displacement,std::optional<double> additionalError) const {
    if(displacement.axisId()!=axisId_)
        return Result<StageAxisPositionState,StageAxisPositionStateError>::failure(
            StageAxisPositionStateError::AxisMismatch);
    if(displacement.unit()!=unit_)
        return Result<StageAxisPositionState,StageAxisPositionStateError>::failure(
            StageAxisPositionStateError::UnitMismatch);
    if(!validError(additionalError))
        return Result<StageAxisPositionState,StageAxisPositionStateError>::failure(
            StageAxisPositionStateError::InvalidUncertainty);

    if(referenceStatus_!=StageMachineReferenceStatus::Referenced||!position_) {
        return Result<StageAxisPositionState,StageAxisPositionStateError>::success(
            StageAxisPositionState(stageComponentId_,axisId_,unit_,position_,std::nullopt,
                StagePositionEvidence::Unknown,ObservationSource::Unknown,
                referenceStatus_));
    }

    const double updated=position_->value()+displacement.value();
    auto newPosition=AxisPosition::create(axisId_,updated,unit_);
    if(!newPosition)
        return Result<StageAxisPositionState,StageAxisPositionStateError>::failure(
            StageAxisPositionStateError::PositionOverflow);

    std::optional<double> newError;
    if(accumulatedErrorBound_&&additionalError) {
        const double sum=*accumulatedErrorBound_+*additionalError;
        if(std::isfinite(sum))newError=sum;
    }

    return Result<StageAxisPositionState,StageAxisPositionStateError>::success(
        StageAxisPositionState(stageComponentId_,axisId_,unit_,std::move(newPosition).value(),newError,
            StagePositionEvidence::CommandDerivedEstimate,
            ObservationSource::CalibratedEstimate,referenceStatus_));
}

Result<StageAxisPositionState,StageAxisPositionStateError>
StageAxisPositionState::afterPositionObservation(
    AxisPosition observed,double uncertainty,StagePositionEvidence evidence,
    ObservationSource source) const {
    if(observed.axisId()!=axisId_)
        return Result<StageAxisPositionState,StageAxisPositionStateError>::failure(
            StageAxisPositionStateError::AxisMismatch);
    if(observed.unit()!=unit_)
        return Result<StageAxisPositionState,StageAxisPositionStateError>::failure(
            StageAxisPositionStateError::UnitMismatch);
    if(!std::isfinite(uncertainty)||uncertainty<0)
        return Result<StageAxisPositionState,StageAxisPositionStateError>::failure(
            StageAxisPositionStateError::InvalidUncertainty);
    if(!knownEvidence(evidence)||!observationEvidence(evidence))
        return Result<StageAxisPositionState,StageAxisPositionStateError>::failure(
            StageAxisPositionStateError::InvalidEvidence);
    if(!usableSource(source))
        return Result<StageAxisPositionState,StageAxisPositionStateError>::failure(
            StageAxisPositionStateError::InvalidObservationSource);

    std::optional<double> error=uncertainty;
    if(evidence==StagePositionEvidence::ControllerReported) {
        // Controller readback may be its internal commanded coordinate, not an
        // independent sensor. It cannot erase an already accumulated bound.
        if(!accumulatedErrorBound_)error=std::nullopt;
        else error=std::max(*accumulatedErrorBound_,uncertainty);
    }

    return Result<StageAxisPositionState,StageAxisPositionStateError>::success(
        StageAxisPositionState(stageComponentId_,axisId_,unit_,std::move(observed),error,evidence,source,
            referenceStatus_));
}

StageAxisPositionState StageAxisPositionState::invalidateMachineReference() const {
    return StageAxisPositionState(stageComponentId_,axisId_,unit_,position_,accumulatedErrorBound_,
        evidence_,source_,StageMachineReferenceStatus::Unreferenced);
}

const EquipmentComponentId& StageAxisPositionState::stageComponentId()const noexcept{return stageComponentId_;}
const StageAxisId& StageAxisPositionState::axisId()const noexcept{return axisId_;}
AxisCoordinateUnit StageAxisPositionState::unit()const noexcept{return unit_;}
const std::optional<AxisPosition>& StageAxisPositionState::position()const noexcept{return position_;}
const std::optional<double>& StageAxisPositionState::accumulatedPositionErrorBound()const noexcept{return accumulatedErrorBound_;}
StagePositionEvidence StageAxisPositionState::evidence()const noexcept{return evidence_;}
ObservationSource StageAxisPositionState::source()const noexcept{return source_;}
StageMachineReferenceStatus StageAxisPositionState::referenceStatus()const noexcept{return referenceStatus_;}
bool StageAxisPositionState::positionIsUsable()const noexcept {
    return referenceStatus_==StageMachineReferenceStatus::Referenced&&position_.has_value();
}
bool StageAxisPositionState::requiresHoming(double threshold)const noexcept {
    return !positionIsUsable()||!accumulatedErrorBound_||
           !std::isfinite(threshold)||threshold<0||*accumulatedErrorBound_>threshold;
}
} // namespace microscope::core
