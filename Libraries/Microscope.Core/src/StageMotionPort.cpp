#include "microscope/core/motion/StageMotionPort.h"
#include <cmath>
#include <set>
#include <utility>

namespace microscope::core {
namespace {
bool physicalUnit(AxisCoordinateUnit u) noexcept {
    return u==AxisCoordinateUnit::Micrometers || u==AxisCoordinateUnit::Millimeters ||
           u==AxisCoordinateUnit::Degrees || u==AxisCoordinateUnit::Radians;
}
template<class T> bool uniqueAxes(const std::vector<T>& values) {
    std::set<StageAxisId> ids;
    for (const auto& v:values) if (!ids.insert(v.axisId()).second) return false;
    return true;
}
bool known(StageAxisMotionState s) noexcept {
    switch(s) {
    case StageAxisMotionState::Idle: case StageAxisMotionState::Moving:
    case StageAxisMotionState::Homing: case StageAxisMotionState::Stopping:
    case StageAxisMotionState::Faulted: case StageAxisMotionState::Unknown: return true;
    }
    return false;
}
bool known(AxisLimitEnd e) noexcept { return e==AxisLimitEnd::Negative || e==AxisLimitEnd::Positive; }
bool known(StageMotionAction a) noexcept {
    switch(a) {
    case StageMotionAction::MoveAxesTo: case StageMotionAction::MoveAxesBy:
    case StageMotionAction::HomeAxes: case StageMotionAction::StopAxes: return true;
    }
    return false;
}
bool known(StageMotionFaultKind f) noexcept {
    switch(f) {
    case StageMotionFaultKind::LimitReached: case StageMotionFaultKind::FollowingError:
    case StageMotionFaultKind::ControllerFault: case StageMotionFaultKind::CommunicationLost:
    case StageMotionFaultKind::EmergencyStop: case StageMotionFaultKind::Unknown: return true;
    }
    return false;
}
}
namespace {
bool isMeasuredEvidence(StagePositionEvidence e) noexcept {
    return e==StagePositionEvidence::ControllerReported||
           e==StagePositionEvidence::EncoderMeasured||
           e==StagePositionEvidence::ExternalMeasured;
}
}
AxisPosition::AxisPosition(StageAxisId a,double v,AxisCoordinateUnit u):axisId_(std::move(a)),value_(v),unit_(u){}
Result<AxisPosition,MotionValueError> AxisPosition::create(StageAxisId a,double v,AxisCoordinateUnit u) {
    if(!std::isfinite(v)) return Result<AxisPosition,MotionValueError>::failure(MotionValueError::NonFiniteValue);
    if(!physicalUnit(u)) return Result<AxisPosition,MotionValueError>::failure(MotionValueError::UnsupportedCoordinateUnit);
    return Result<AxisPosition,MotionValueError>::success(AxisPosition(std::move(a),v,u));
}
const StageAxisId& AxisPosition::axisId()const noexcept{return axisId_;}
double AxisPosition::value()const noexcept{return value_;}
AxisCoordinateUnit AxisPosition::unit()const noexcept{return unit_;}

AxisPositionObservation::AxisPositionObservation(
    AxisPosition position,double uncertainty,StagePositionEvidence evidence)
    :position_(std::move(position)),uncertaintyInAxisUnit_(uncertainty),evidence_(evidence) {}
Result<AxisPositionObservation,AxisPositionObservationError>
AxisPositionObservation::create(
    AxisPosition position,double uncertainty,StagePositionEvidence evidence) {
    if(!std::isfinite(uncertainty)||uncertainty<0.0)
        return Result<AxisPositionObservation,AxisPositionObservationError>::failure(
            AxisPositionObservationError::InvalidUncertainty);
    if(!isMeasuredEvidence(evidence))
        return Result<AxisPositionObservation,AxisPositionObservationError>::failure(
            AxisPositionObservationError::InvalidEvidence);
    return Result<AxisPositionObservation,AxisPositionObservationError>::success(
        AxisPositionObservation(std::move(position),uncertainty,evidence));
}
const AxisPosition& AxisPositionObservation::position()const noexcept{return position_;}
double AxisPositionObservation::uncertaintyInAxisUnit()const noexcept{return uncertaintyInAxisUnit_;}
StagePositionEvidence AxisPositionObservation::evidence()const noexcept{return evidence_;}

AxisDisplacement::AxisDisplacement(StageAxisId a,double v,AxisCoordinateUnit u):axisId_(std::move(a)),value_(v),unit_(u){}
Result<AxisDisplacement,MotionValueError> AxisDisplacement::create(StageAxisId a,double v,AxisCoordinateUnit u) {
    if(!std::isfinite(v)) return Result<AxisDisplacement,MotionValueError>::failure(MotionValueError::NonFiniteValue);
    if(!physicalUnit(u)) return Result<AxisDisplacement,MotionValueError>::failure(MotionValueError::UnsupportedCoordinateUnit);
    return Result<AxisDisplacement,MotionValueError>::success(AxisDisplacement(std::move(a),v,u));
}
const StageAxisId& AxisDisplacement::axisId()const noexcept{return axisId_;}
double AxisDisplacement::value()const noexcept{return value_;}
AxisCoordinateUnit AxisDisplacement::unit()const noexcept{return unit_;}

StageAxisSet::StageAxisSet(std::vector<StageAxisId> a):axes_(std::move(a)){}
Result<StageAxisSet,StageAxisSetError> StageAxisSet::create(std::vector<StageAxisId> a) {
    if(a.empty()) return Result<StageAxisSet,StageAxisSetError>::failure(StageAxisSetError::Empty);
    std::set<StageAxisId> ids;
    for(const auto& x:a) if(!ids.insert(x).second) return Result<StageAxisSet,StageAxisSetError>::failure(StageAxisSetError::DuplicateAxisId);
    return Result<StageAxisSet,StageAxisSetError>::success(StageAxisSet(std::move(a)));
}
const std::vector<StageAxisId>& StageAxisSet::axes()const noexcept{return axes_;}

MoveAxesTo::MoveAxesTo(std::vector<AxisPosition> v):targets_(std::move(v)){}
Result<MoveAxesTo,StageMotionCommandError> MoveAxesTo::create(std::vector<AxisPosition> v) {
    if(v.empty()) return Result<MoveAxesTo,StageMotionCommandError>::failure(StageMotionCommandError::EmptyAxisValues);
    if(!uniqueAxes(v)) return Result<MoveAxesTo,StageMotionCommandError>::failure(StageMotionCommandError::DuplicateAxisId);
    return Result<MoveAxesTo,StageMotionCommandError>::success(MoveAxesTo(std::move(v)));
}
const std::vector<AxisPosition>& MoveAxesTo::targets()const noexcept{return targets_;}

MoveAxesBy::MoveAxesBy(std::vector<AxisDisplacement> v):displacements_(std::move(v)){}
Result<MoveAxesBy,StageMotionCommandError> MoveAxesBy::create(std::vector<AxisDisplacement> v) {
    if(v.empty()) return Result<MoveAxesBy,StageMotionCommandError>::failure(StageMotionCommandError::EmptyAxisValues);
    if(!uniqueAxes(v)) return Result<MoveAxesBy,StageMotionCommandError>::failure(StageMotionCommandError::DuplicateAxisId);
    return Result<MoveAxesBy,StageMotionCommandError>::success(MoveAxesBy(std::move(v)));
}
const std::vector<AxisDisplacement>& MoveAxesBy::displacements()const noexcept{return displacements_;}

HomeAxes::HomeAxes(StageAxisSet a):axes_(std::move(a)){}
HomeAxes HomeAxes::create(StageAxisSet a){return HomeAxes(std::move(a));}
const StageAxisSet& HomeAxes::axes()const noexcept{return axes_;}
StopAxes::StopAxes(StageAxisSet a):axes_(std::move(a)){}
StopAxes StopAxes::create(StageAxisSet a){return StopAxes(std::move(a));}
const StageAxisSet& StopAxes::axes()const noexcept{return axes_;}

StageMotionCommandContext::StageMotionCommandContext(RuntimeDeviceCorrelation d,OperationCorrelation o):runtimeDevice_(std::move(d)),operation_(std::move(o)){}
Result<StageMotionCommandContext,StageMotionCommandContextError> StageMotionCommandContext::create(RuntimeDeviceCorrelation d,OperationCorrelation o) {
    if(!o.commandId) return Result<StageMotionCommandContext,StageMotionCommandContextError>::failure(StageMotionCommandContextError::MissingCommandId);
    return Result<StageMotionCommandContext,StageMotionCommandContextError>::success(StageMotionCommandContext(std::move(d),std::move(o)));
}
const RuntimeDeviceCorrelation& StageMotionCommandContext::runtimeDevice()const noexcept{return runtimeDevice_;}
const OperationCorrelation& StageMotionCommandContext::operation()const noexcept{return operation_;}

StageMotionSubmissionResult::StageMotionSubmissionResult(StageMotionSubmissionStatus s,std::optional<StageMotionSubmissionRejection> r):status_(s),rejection_(r){}
StageMotionSubmissionResult StageMotionSubmissionResult::accepted()noexcept{return {StageMotionSubmissionStatus::Accepted,std::nullopt};}
StageMotionSubmissionResult StageMotionSubmissionResult::rejected(StageMotionSubmissionRejection r)noexcept{return {StageMotionSubmissionStatus::Rejected,r};}
StageMotionSubmissionStatus StageMotionSubmissionResult::status()const noexcept{return status_;}
const std::optional<StageMotionSubmissionRejection>& StageMotionSubmissionResult::rejection()const noexcept{return rejection_;}

AxisMotionStateObservation::AxisMotionStateObservation(StageAxisId a,StageAxisMotionState s):axisId_(std::move(a)),state_(s){}
Result<AxisMotionStateObservation,StageMotionObservationError> AxisMotionStateObservation::create(StageAxisId a,StageAxisMotionState s) {
    if(!known(s)) return Result<AxisMotionStateObservation,StageMotionObservationError>::failure(StageMotionObservationError::InvalidMotionState);
    return Result<AxisMotionStateObservation,StageMotionObservationError>::success(AxisMotionStateObservation(std::move(a),s));
}
const StageAxisId& AxisMotionStateObservation::axisId()const noexcept{return axisId_;}
StageAxisMotionState AxisMotionStateObservation::state()const noexcept{return state_;}

AxisLimitObservation::AxisLimitObservation(StageAxisId a,AxisLimitEnd e,bool v):axisId_(std::move(a)),end_(e),active_(v){}
Result<AxisLimitObservation,StageMotionObservationError> AxisLimitObservation::create(StageAxisId a,AxisLimitEnd e,bool v) {
    if(!known(e)) return Result<AxisLimitObservation,StageMotionObservationError>::failure(StageMotionObservationError::InvalidLimitEnd);
    return Result<AxisLimitObservation,StageMotionObservationError>::success(AxisLimitObservation(std::move(a),e,v));
}
const StageAxisId& AxisLimitObservation::axisId()const noexcept{return axisId_;}
AxisLimitEnd AxisLimitObservation::end()const noexcept{return end_;}
bool AxisLimitObservation::active()const noexcept{return active_;}

StageMotionAcknowledgement::StageMotionAcknowledgement(
    StageMotionAction a,StageAxisSet axes)
    :action_(a),axes_(std::move(axes)) {}
Result<StageMotionAcknowledgement,StageMotionAcknowledgementError>
StageMotionAcknowledgement::create(StageMotionAction action,StageAxisSet axes) {
    if(!known(action))
        return Result<StageMotionAcknowledgement,StageMotionAcknowledgementError>::failure(
            StageMotionAcknowledgementError::InvalidAction);
    return Result<StageMotionAcknowledgement,StageMotionAcknowledgementError>::success(
        StageMotionAcknowledgement(action,std::move(axes)));
}
StageMotionAction StageMotionAcknowledgement::action()const noexcept{return action_;}
const StageAxisSet& StageMotionAcknowledgement::axes()const noexcept{return axes_;}

StageMotionActionCompletion::StageMotionActionCompletion(StageMotionAction a,StageAxisSet s):action_(a),axes_(std::move(s)){}
Result<StageMotionActionCompletion,StageMotionActionCompletionError> StageMotionActionCompletion::create(StageMotionAction a,StageAxisSet s) {
    if(!known(a)) return Result<StageMotionActionCompletion,StageMotionActionCompletionError>::failure(StageMotionActionCompletionError::InvalidAction);
    return Result<StageMotionActionCompletion,StageMotionActionCompletionError>::success(StageMotionActionCompletion(a,std::move(s)));
}
StageMotionAction StageMotionActionCompletion::action()const noexcept{return action_;}
const StageAxisSet& StageMotionActionCompletion::axes()const noexcept{return axes_;}

StageMotionFaultObservation::StageMotionFaultObservation(StageMotionFaultKind k,std::optional<StageAxisId> a):kind_(k),axisId_(std::move(a)){}
Result<StageMotionFaultObservation,StageMotionFaultError> StageMotionFaultObservation::create(StageMotionFaultKind k,std::optional<StageAxisId> a) {
    if(!known(k)) return Result<StageMotionFaultObservation,StageMotionFaultError>::failure(StageMotionFaultError::InvalidFaultKind);
    return Result<StageMotionFaultObservation,StageMotionFaultError>::success(StageMotionFaultObservation(k,std::move(a)));
}
StageMotionFaultKind StageMotionFaultObservation::kind()const noexcept{return kind_;}
const std::optional<StageAxisId>& StageMotionFaultObservation::axisId()const noexcept{return axisId_;}
} // namespace microscope::core
