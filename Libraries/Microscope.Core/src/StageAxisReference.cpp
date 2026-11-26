#include "microscope/core/stage/StageAxisReference.h"
#include <cmath>
#include <utility>

namespace microscope::core {
namespace {
bool validEnd(AxisLimitEnd v)noexcept{return v==AxisLimitEnd::Negative||v==AxisLimitEnd::Positive;}
bool validMethod(StageAxisReferenceMethod v)noexcept{return v==StageAxisReferenceMethod::PhysicalEndstop||v==StageAxisReferenceMethod::FullStrokeTraversal;}
bool linearUnit(AxisCoordinateUnit u)noexcept{return u==AxisCoordinateUnit::Micrometers||u==AxisCoordinateUnit::Millimeters;}
}
StageAxisReferenceSpecification::StageAxisReferenceSpecification(StageAxisId id,AxisCoordinateUnit unit,
    StageAxisReferenceMethod method,AxisLimitEnd end,double min,double max,double uncertainty,double tolerance)
    :axisId_(std::move(id)),unit_(unit),method_(method),referenceEnd_(end),minimumPosition_(min),
     maximumPosition_(max),maximumReferenceUncertainty_(uncertainty),positionTolerance_(tolerance){}
Result<StageAxisReferenceSpecification,StageAxisReferenceError>
StageAxisReferenceSpecification::create(const AxisSpecification& axis,StageAxisReferenceMethod method,
    AxisLimitEnd end,double uncertainty,double tolerance) {
    if(!validMethod(method))return Result<StageAxisReferenceSpecification,StageAxisReferenceError>::failure(StageAxisReferenceError::InvalidMethod);
    if(!validEnd(end))return Result<StageAxisReferenceSpecification,StageAxisReferenceError>::failure(StageAxisReferenceError::InvalidEnd);
    if(!std::isfinite(uncertainty)||uncertainty<0)return Result<StageAxisReferenceSpecification,StageAxisReferenceError>::failure(StageAxisReferenceError::InvalidUncertainty);
    if(!std::isfinite(tolerance)||tolerance<=0)return Result<StageAxisReferenceSpecification,StageAxisReferenceError>::failure(StageAxisReferenceError::InvalidPositionTolerance);
    if(axis.motionType()!=AxisMotionType::Linear||!linearUnit(axis.coordinateUnit()))
        return Result<StageAxisReferenceSpecification,StageAxisReferenceError>::failure(StageAxisReferenceError::UnsupportedAxisUnit);
    const auto& range=axis.operatingRange();
    if(range.maximum()-range.minimum()<=2.0*tolerance)
        return Result<StageAxisReferenceSpecification,StageAxisReferenceError>::failure(StageAxisReferenceError::InvalidPositionTolerance);
    return Result<StageAxisReferenceSpecification,StageAxisReferenceError>::success(
        StageAxisReferenceSpecification(axis.id(),axis.coordinateUnit(),method,end,range.minimum(),
            range.maximum(),uncertainty,tolerance));
}
const StageAxisId& StageAxisReferenceSpecification::axisId()const noexcept{return axisId_;}
AxisCoordinateUnit StageAxisReferenceSpecification::unit()const noexcept{return unit_;}
StageAxisReferenceMethod StageAxisReferenceSpecification::method()const noexcept{return method_;}
AxisLimitEnd StageAxisReferenceSpecification::referenceEnd()const noexcept{return referenceEnd_;}
AxisLimitEnd StageAxisReferenceSpecification::startingEnd()const noexcept{return referenceEnd_==AxisLimitEnd::Negative?AxisLimitEnd::Positive:AxisLimitEnd::Negative;}
double StageAxisReferenceSpecification::minimumPosition()const noexcept{return minimumPosition_;}
double StageAxisReferenceSpecification::maximumPosition()const noexcept{return maximumPosition_;}
double StageAxisReferenceSpecification::referencePosition()const noexcept{return referenceEnd_==AxisLimitEnd::Negative?minimumPosition_:maximumPosition_;}
double StageAxisReferenceSpecification::startingPosition()const noexcept{return referenceEnd_==AxisLimitEnd::Negative?maximumPosition_:minimumPosition_;}
double StageAxisReferenceSpecification::fullStrokeDistance()const noexcept{return maximumPosition_-minimumPosition_;}
double StageAxisReferenceSpecification::maximumReferenceUncertainty()const noexcept{return maximumReferenceUncertainty_;}
double StageAxisReferenceSpecification::positionTolerance()const noexcept{return positionTolerance_;}

StageAxisReferenceState::StageAxisReferenceState(StageAxisReferenceSpecification spec)
    :specification_(std::move(spec)),status_(StageAxisReferenceStatus::Unreferenced),
     evidence_(StageAxisReferenceEvidence::None){}
StageAxisReferenceState StageAxisReferenceState::create(StageAxisReferenceSpecification spec){return StageAxisReferenceState(std::move(spec));}
bool StageAxisReferenceState::isAt(double p,double target)const noexcept{return std::isfinite(p)&&std::abs(p-target)<=specification_.positionTolerance();}
bool StageAxisReferenceState::isMeasuredEvidence(StagePositionEvidence e)const noexcept {
    return e==StagePositionEvidence::EncoderMeasured||e==StagePositionEvidence::ExternalMeasured;
}
StageAxisReferenceSnapshot StageAxisReferenceState::makeSnapshot()const noexcept {
    return {status_,evidence_,referencePosition_,uncertainty_,traversalStartPosition_};
}
Result<StageAxisReferenceSnapshot,StageAxisReferenceError>
StageAxisReferenceState::recordPhysicalEndstop(const AxisLimitObservation& observation) {
    if(specification_.method()!=StageAxisReferenceMethod::PhysicalEndstop)
        return Result<StageAxisReferenceSnapshot,StageAxisReferenceError>::failure(StageAxisReferenceError::WrongReferenceMethod);
    if(observation.axisId()!=specification_.axisId())
        return Result<StageAxisReferenceSnapshot,StageAxisReferenceError>::failure(StageAxisReferenceError::AxisMismatch);
    if(observation.end()!=specification_.referenceEnd())
        return Result<StageAxisReferenceSnapshot,StageAxisReferenceError>::failure(StageAxisReferenceError::WrongEndstop);
    if(!observation.active())
        return Result<StageAxisReferenceSnapshot,StageAxisReferenceError>::failure(StageAxisReferenceError::EndstopNotActive);
    status_=StageAxisReferenceStatus::ReferencedPhysicalEndstop;
    evidence_=StageAxisReferenceEvidence::PhysicalLimitSwitch;
    referencePosition_=specification_.referencePosition();
    uncertainty_=specification_.maximumReferenceUncertainty();
    traversalStartPosition_.reset();
    return Result<StageAxisReferenceSnapshot,StageAxisReferenceError>::success(makeSnapshot());
}
Result<StageAxisReferenceSnapshot,StageAxisReferenceError>
StageAxisReferenceState::beginFullStrokeTraversal(const AxisPosition& start,StagePositionEvidence evidence,double uncertainty) {
    if(specification_.method()!=StageAxisReferenceMethod::FullStrokeTraversal)
        return Result<StageAxisReferenceSnapshot,StageAxisReferenceError>::failure(StageAxisReferenceError::WrongReferenceMethod);
    if(start.axisId()!=specification_.axisId())
        return Result<StageAxisReferenceSnapshot,StageAxisReferenceError>::failure(StageAxisReferenceError::AxisMismatch);
    if(start.unit()!=specification_.unit())
        return Result<StageAxisReferenceSnapshot,StageAxisReferenceError>::failure(StageAxisReferenceError::UnitMismatch);
    if(!std::isfinite(uncertainty)||uncertainty<0||uncertainty>specification_.maximumReferenceUncertainty())
        return Result<StageAxisReferenceSnapshot,StageAxisReferenceError>::failure(StageAxisReferenceError::UncertaintyExceedsPolicy);
    if(!isMeasuredEvidence(evidence))
        return Result<StageAxisReferenceSnapshot,StageAxisReferenceError>::failure(StageAxisReferenceError::StartingEvidenceInsufficient);
    if(!isAt(start.value(),specification_.startingPosition()))
        return Result<StageAxisReferenceSnapshot,StageAxisReferenceError>::failure(StageAxisReferenceError::WrongStartingBoundary);
    status_=StageAxisReferenceStatus::TraversingFullStroke;
    evidence_=StageAxisReferenceEvidence::None;
    referencePosition_.reset();uncertainty_.reset();traversalStartPosition_=start.value();
    return Result<StageAxisReferenceSnapshot,StageAxisReferenceError>::success(makeSnapshot());
}
Result<StageAxisReferenceSnapshot,StageAxisReferenceError>
StageAxisReferenceState::completeMeasuredFullStrokeTraversal(const AxisPositionObservation& observation) {
    if(specification_.method()!=StageAxisReferenceMethod::FullStrokeTraversal)
        return Result<StageAxisReferenceSnapshot,StageAxisReferenceError>::failure(StageAxisReferenceError::WrongReferenceMethod);
    if(status_!=StageAxisReferenceStatus::TraversingFullStroke||!traversalStartPosition_)
        return Result<StageAxisReferenceSnapshot,StageAxisReferenceError>::failure(StageAxisReferenceError::StrokeNotInProgress);
    const auto& p=observation.position();
    if(p.axisId()!=specification_.axisId())return Result<StageAxisReferenceSnapshot,StageAxisReferenceError>::failure(StageAxisReferenceError::AxisMismatch);
    if(p.unit()!=specification_.unit())return Result<StageAxisReferenceSnapshot,StageAxisReferenceError>::failure(StageAxisReferenceError::UnitMismatch);
    if(!isAt(p.value(),specification_.referencePosition()))
        return Result<StageAxisReferenceSnapshot,StageAxisReferenceError>::failure(StageAxisReferenceError::TargetBoundaryNotReached);
    if(!isMeasuredEvidence(observation.evidence())||observation.uncertaintyInAxisUnit()>specification_.maximumReferenceUncertainty())
        return Result<StageAxisReferenceSnapshot,StageAxisReferenceError>::failure(StageAxisReferenceError::UncertaintyExceedsPolicy);
    status_=StageAxisReferenceStatus::ReferencedMeasuredFullStroke;
    evidence_=observation.evidence()==StagePositionEvidence::EncoderMeasured?
        StageAxisReferenceEvidence::EncoderFullStroke:StageAxisReferenceEvidence::ExternalMeasurementFullStroke;
    referencePosition_=p.value();uncertainty_=observation.uncertaintyInAxisUnit();
    return Result<StageAxisReferenceSnapshot,StageAxisReferenceError>::success(makeSnapshot());
}
Result<StageAxisReferenceSnapshot,StageAxisReferenceError>
StageAxisReferenceState::completeCalibratedFullStrokeTraversal(const AxisPosition& end,double uncertainty) {
    if(specification_.method()!=StageAxisReferenceMethod::FullStrokeTraversal)
        return Result<StageAxisReferenceSnapshot,StageAxisReferenceError>::failure(StageAxisReferenceError::WrongReferenceMethod);
    if(status_!=StageAxisReferenceStatus::TraversingFullStroke||!traversalStartPosition_)
        return Result<StageAxisReferenceSnapshot,StageAxisReferenceError>::failure(StageAxisReferenceError::StrokeNotInProgress);
    if(end.axisId()!=specification_.axisId())return Result<StageAxisReferenceSnapshot,StageAxisReferenceError>::failure(StageAxisReferenceError::AxisMismatch);
    if(end.unit()!=specification_.unit())return Result<StageAxisReferenceSnapshot,StageAxisReferenceError>::failure(StageAxisReferenceError::UnitMismatch);
    if(!isAt(end.value(),specification_.referencePosition()))
        return Result<StageAxisReferenceSnapshot,StageAxisReferenceError>::failure(StageAxisReferenceError::TargetBoundaryNotReached);
    if(!std::isfinite(uncertainty)||uncertainty<0||uncertainty>specification_.maximumReferenceUncertainty())
        return Result<StageAxisReferenceSnapshot,StageAxisReferenceError>::failure(StageAxisReferenceError::UncertaintyExceedsPolicy);
    status_=StageAxisReferenceStatus::ReferencedEstimatedFullStroke;
    evidence_=StageAxisReferenceEvidence::CalibratedCommandFullStroke;
    referencePosition_=end.value();uncertainty_=uncertainty;
    return Result<StageAxisReferenceSnapshot,StageAxisReferenceError>::success(makeSnapshot());
}
StageAxisReferenceSnapshot StageAxisReferenceState::snapshot()const noexcept{return makeSnapshot();}
const StageAxisReferenceSpecification& StageAxisReferenceState::specification()const noexcept{return specification_;}
void StageAxisReferenceState::invalidate()noexcept {
    status_=StageAxisReferenceStatus::Unreferenced;evidence_=StageAxisReferenceEvidence::None;
    referencePosition_.reset();uncertainty_.reset();traversalStartPosition_.reset();
}
void StageAxisReferenceState::abortFullStrokeTraversal()noexcept {
    if(status_==StageAxisReferenceStatus::TraversingFullStroke)invalidate();
}
} // namespace microscope::core
