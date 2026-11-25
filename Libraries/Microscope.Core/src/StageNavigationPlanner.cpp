#include "microscope/core/navigation/StageNavigationPlanner.h"
#include <cmath>
#include <utility>

namespace microscope::core {
namespace {

double thresholdInAxisUnit(double micrometers,AxisCoordinateUnit unit) noexcept {
    return unit==AxisCoordinateUnit::Millimeters?micrometers/1000.0:micrometers;
}

Result<bool,StageNavigationPlanningError> validateAxes(
    const DualSlideCarrierSpecification& specification,
    const StageAxisPositionState& horizontal,
    const StageAxisPositionState& transverse) {
    if(horizontal.stageComponentId()!=specification.stageComponentId()||
       transverse.stageComponentId()!=specification.stageComponentId())
        return Result<bool,StageNavigationPlanningError>::failure(
            StageNavigationPlanningError::StageComponentMismatch);
    if(horizontal.axisId()!=specification.horizontalAxisId())
        return Result<bool,StageNavigationPlanningError>::failure(
            StageNavigationPlanningError::HorizontalAxisMismatch);
    if(transverse.axisId()!=specification.transverseAxisId())
        return Result<bool,StageNavigationPlanningError>::failure(
            StageNavigationPlanningError::TransverseAxisMismatch);
    if(horizontal.unit()!=specification.horizontalUnit())
        return Result<bool,StageNavigationPlanningError>::failure(
            StageNavigationPlanningError::HorizontalUnitMismatch);
    if(transverse.unit()!=specification.transverseUnit())
        return Result<bool,StageNavigationPlanningError>::failure(
            StageNavigationPlanningError::TransverseUnitMismatch);
    return Result<bool,StageNavigationPlanningError>::success(true);
}

StageNavigationPlanningError mapCommandError(SlideNavigationCommandError error) {
    switch(error) {
    case SlideNavigationCommandError::InvalidStageCoordinate:
        return StageNavigationPlanningError::InvalidStageCoordinate;
    case SlideNavigationCommandError::OutsideStageTravel:
        return StageNavigationPlanningError::OutsideStageTravel;
    case SlideNavigationCommandError::SlideNotInCarrier:
        return StageNavigationPlanningError::SlideNotInCarrier;
    case SlideNavigationCommandError::SlideNotRegistered:
        return StageNavigationPlanningError::SlideNotRegistered;
    case SlideNavigationCommandError::RegistrationChanged:
        return StageNavigationPlanningError::RegistrationChanged;
    case SlideNavigationCommandError::RegistrationChanged:
        return StageNavigationPlanningError::RegistrationChanged;
    }
    return StageNavigationPlanningError::InvalidStageCoordinate;
}

StageNavigationPlanningError mapTargetError(SlideNavigationTargetError error) {
    switch(error) {
    case SlideNavigationTargetError::SlideNotInCarrier:
        return StageNavigationPlanningError::SlideNotInCarrier;
    case SlideNavigationTargetError::SlideNotRegistered:
        return StageNavigationPlanningError::SlideNotRegistered;
    case SlideNavigationTargetError::PointOutsideSlide:
        return StageNavigationPlanningError::SlidePointOutsideGeometry;
    }
    return StageNavigationPlanningError::InvalidStageCoordinate;
}

} // namespace

StageHomingPolicy::StageHomingPolicy(double horizontal,double transverse)
    :horizontalThreshold_(horizontal),transverseThreshold_(transverse) {}

Result<StageHomingPolicy,StageHomingPolicyError> StageHomingPolicy::create(
    double horizontal,double transverse) {
    if(!std::isfinite(horizontal)||horizontal<0)
        return Result<StageHomingPolicy,StageHomingPolicyError>::failure(
            StageHomingPolicyError::InvalidHorizontalThreshold);
    if(!std::isfinite(transverse)||transverse<0)
        return Result<StageHomingPolicy,StageHomingPolicyError>::failure(
            StageHomingPolicyError::InvalidTransverseThreshold);
    return Result<StageHomingPolicy,StageHomingPolicyError>::success(
        StageHomingPolicy(horizontal,transverse));
}

double StageHomingPolicy::maximumHorizontalErrorMicrometers()const noexcept {
    return horizontalThreshold_;
}
double StageHomingPolicy::maximumTransverseErrorMicrometers()const noexcept {
    return transverseThreshold_;
}

Result<StageNavigationPlan,StageNavigationPlanningError> StageNavigationPlanner::plan(
    const DualSlideCarrierState& carrier,const SlideNavigationTarget& target,
    const StageAxisPositionState& horizontal,const StageAxisPositionState& transverse,
    const StageHomingPolicy& policy) {
    auto axes=validateAxes(carrier.specification(),horizontal,transverse);
    if(!axes)
        return Result<StageNavigationPlan,StageNavigationPlanningError>::failure(axes.error());

    // Validate slide identity, registration revision, physical slide bounds and
    // installed stage travel before deciding whether any home command is needed.
    auto destination=carrier.commandFor(target);
    if(!destination)
        return Result<StageNavigationPlan,StageNavigationPlanningError>::failure(
            mapCommandError(destination.error()));

    const bool homeRequired=
        horizontal.requiresHoming(thresholdInAxisUnit(
            policy.maximumHorizontalErrorMicrometers(),horizontal.unit()))||
        transverse.requiresHoming(thresholdInAxisUnit(
            policy.maximumTransverseErrorMicrometers(),transverse.unit()));

    if(homeRequired) {
        auto axisSet=StageAxisSet::create({
            carrier.specification().horizontalAxisId(),
            carrier.specification().transverseAxisId()});
        if(!axisSet)
            return Result<StageNavigationPlan,StageNavigationPlanningError>::failure(
                StageNavigationPlanningError::HomeAxisSetInvalid);
        return Result<StageNavigationPlan,StageNavigationPlanningError>::success(
            StageNavigationPlan{StageHomeThenMovePlan{
                target,HomeAxes::create(std::move(axisSet).value())}});
    }

    return Result<StageNavigationPlan,StageNavigationPlanningError>::success(
        StageNavigationPlan{StageDirectMovePlan{target,std::move(destination).value()}});
}

Result<StageNavigationPlan,StageNavigationPlanningError>
StageNavigationPlanner::planToSlidePoint(
    const DualSlideCarrierState& carrier,const SlideId& slideId,
    SlideLocalPoint point,const StagePositionState& position,
    const StageHomingPolicy& policy) {
    if(position.stageComponentId()!=carrier.specification().stageComponentId())
        return Result<StageNavigationPlan,StageNavigationPlanningError>::failure(
            StageNavigationPlanningError::StageComponentMismatch);

    auto target=carrier.createTarget(slideId,point);
    if(!target)
        return Result<StageNavigationPlan,StageNavigationPlanningError>::failure(
            mapTargetError(target.error()));

    return plan(carrier,target.value(),position.horizontal(),position.transverse(),policy);
}

Result<MoveAxesTo,StageNavigationPlanningError>
StageNavigationPlanner::resolveAfterHoming(
    const DualSlideCarrierState& carrier,const SlideNavigationTarget& target,
    const StagePositionState& position,const StageHomingPolicy& policy) {
    if(position.stageComponentId()!=carrier.specification().stageComponentId())
        return Result<MoveAxesTo,StageNavigationPlanningError>::failure(
            StageNavigationPlanningError::StageComponentMismatch);
    return resolveAfterHoming(
        carrier,target,position.horizontal(),position.transverse(),policy);
}

Result<MoveAxesTo,StageNavigationPlanningError>
StageNavigationPlanner::resolveAfterHoming(
    const DualSlideCarrierState& carrier,const SlideNavigationTarget& target,
    const StageAxisPositionState& horizontal,const StageAxisPositionState& transverse,
    const StageHomingPolicy& policy) {
    auto axes=validateAxes(carrier.specification(),horizontal,transverse);
    if(!axes)
        return Result<MoveAxesTo,StageNavigationPlanningError>::failure(axes.error());

    if(!horizontal.positionIsUsable()||!transverse.positionIsUsable())
        return Result<MoveAxesTo,StageNavigationPlanningError>::failure(
            StageNavigationPlanningError::HomingNotVerified);

    if(horizontal.requiresHoming(thresholdInAxisUnit(
           policy.maximumHorizontalErrorMicrometers(),horizontal.unit()))||
       transverse.requiresHoming(thresholdInAxisUnit(
           policy.maximumTransverseErrorMicrometers(),transverse.unit())))
        return Result<MoveAxesTo,StageNavigationPlanningError>::failure(
            StageNavigationPlanningError::PositionUncertaintyExceeded);

    auto command=carrier.commandFor(target);
    if(!command)
        return Result<MoveAxesTo,StageNavigationPlanningError>::failure(
            mapCommandError(command.error()));
    return Result<MoveAxesTo,StageNavigationPlanningError>::success(
        std::move(command).value());
}

} // namespace microscope::core
