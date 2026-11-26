#include "microscope/core/navigation/StageWorkingSurfaceNavigationPlanner.h"

#include <cmath>
#include <utility>

namespace microscope::core {
namespace {

double thresholdInAxisUnit(double micrometers, AxisCoordinateUnit unit) noexcept {
    return unit == AxisCoordinateUnit::Millimeters ? micrometers / 1000.0 : micrometers;
}

double fromMicrometers(double micrometers, AxisCoordinateUnit unit) noexcept {
    return unit == AxisCoordinateUnit::Millimeters ? micrometers / 1000.0 : micrometers;
}

StageWorkingSurfaceNavigationPlanningError mapTargetError(
    SlideNavigationTarget2DError error) noexcept {
    switch (error) {
    case SlideNavigationTarget2DError::SlideNotFound:
        return StageWorkingSurfaceNavigationPlanningError::SlideNotFound;
    case SlideNavigationTarget2DError::SlidePointOutsideGeometry:
        return StageWorkingSurfaceNavigationPlanningError::SlidePointOutsideGeometry;
    case SlideNavigationTarget2DError::RegistrationChanged:
        return StageWorkingSurfaceNavigationPlanningError::RegistrationChanged;
    case SlideNavigationTarget2DError::InvalidStageCoordinate:
        return StageWorkingSurfaceNavigationPlanningError::InvalidStageCoordinate;
    case SlideNavigationTarget2DError::OutsideWorkingSurface:
        return StageWorkingSurfaceNavigationPlanningError::OutsideWorkingSurface;
    }
    return StageWorkingSurfaceNavigationPlanningError::InvalidStageCoordinate;
}

Result<bool, StageWorkingSurfaceNavigationPlanningError> validatePosition(
    const StageWorkingSurface2D& surface, const StagePosition3D& position) {
    if (position.x().stageComponentId() != surface.stageComponentId() ||
        position.y().stageComponentId() != surface.stageComponentId() ||
        position.z().stageComponentId() != surface.stageComponentId())
        return Result<bool, StageWorkingSurfaceNavigationPlanningError>::failure(
            StageWorkingSurfaceNavigationPlanningError::StageComponentMismatch);
    if (position.x().axisId() != surface.xAxisId())
        return Result<bool, StageWorkingSurfaceNavigationPlanningError>::failure(
            StageWorkingSurfaceNavigationPlanningError::XAxisMismatch);
    if (position.y().axisId() != surface.yAxisId())
        return Result<bool, StageWorkingSurfaceNavigationPlanningError>::failure(
            StageWorkingSurfaceNavigationPlanningError::YAxisMismatch);
    if (position.x().unit() != surface.xAxisUnit())
        return Result<bool, StageWorkingSurfaceNavigationPlanningError>::failure(
            StageWorkingSurfaceNavigationPlanningError::XUnitMismatch);
    if (position.y().unit() != surface.yAxisUnit())
        return Result<bool, StageWorkingSurfaceNavigationPlanningError>::failure(
            StageWorkingSurfaceNavigationPlanningError::YUnitMismatch);
    return Result<bool, StageWorkingSurfaceNavigationPlanningError>::success(true);
}

Result<MoveAxesTo, StageWorkingSurfaceNavigationPlanningError> commandFor(
    const StageWorkingSurface2D& surface, const SlideNavigationTarget2D& target) {
    const auto resolved = surface.resolveNavigationTarget(target);
    if (!resolved)
        return Result<MoveAxesTo, StageWorkingSurfaceNavigationPlanningError>::failure(
            mapTargetError(resolved.error()));

    const double xValue = fromMicrometers(resolved.value().xMicrometers, surface.xAxisUnit());
    const double yValue = fromMicrometers(resolved.value().yMicrometers, surface.yAxisUnit());
    if (!std::isfinite(xValue) || !std::isfinite(yValue))
        return Result<MoveAxesTo, StageWorkingSurfaceNavigationPlanningError>::failure(
            StageWorkingSurfaceNavigationPlanningError::InvalidStageCoordinate);

    auto x = AxisPosition::create(surface.xAxisId(), xValue, surface.xAxisUnit());
    auto y = AxisPosition::create(surface.yAxisId(), yValue, surface.yAxisUnit());
    if (!x || !y)
        return Result<MoveAxesTo, StageWorkingSurfaceNavigationPlanningError>::failure(
            StageWorkingSurfaceNavigationPlanningError::InvalidStageCoordinate);
    auto command = MoveAxesTo::create({std::move(x).value(), std::move(y).value()});
    if (!command)
        return Result<MoveAxesTo, StageWorkingSurfaceNavigationPlanningError>::failure(
            StageWorkingSurfaceNavigationPlanningError::InvalidStageCoordinate);
    return Result<MoveAxesTo, StageWorkingSurfaceNavigationPlanningError>::success(
        std::move(command).value());
}

} // namespace

StageXYHomingPolicy::StageXYHomingPolicy(double x, double y) noexcept
    : xThreshold_(x), yThreshold_(y) {}

Result<StageXYHomingPolicy, StageXYHomingPolicyError>
StageXYHomingPolicy::create(double x, double y) {
    if (!std::isfinite(x) || x < 0.0)
        return Result<StageXYHomingPolicy, StageXYHomingPolicyError>::failure(
            StageXYHomingPolicyError::InvalidXThreshold);
    if (!std::isfinite(y) || y < 0.0)
        return Result<StageXYHomingPolicy, StageXYHomingPolicyError>::failure(
            StageXYHomingPolicyError::InvalidYThreshold);
    return Result<StageXYHomingPolicy, StageXYHomingPolicyError>::success(
        StageXYHomingPolicy(x, y));
}

double StageXYHomingPolicy::maximumXErrorMicrometers() const noexcept { return xThreshold_; }
double StageXYHomingPolicy::maximumYErrorMicrometers() const noexcept { return yThreshold_; }

Result<StageWorkingSurfaceNavigationPlan, StageWorkingSurfaceNavigationPlanningError>
StageWorkingSurfaceNavigationPlanner::plan(
    const StageWorkingSurface2D& surface, const SlideNavigationTarget2D& target,
    const StagePosition3D& position, const StageXYHomingPolicy& policy) {
    const auto valid = validatePosition(surface, position);
    if (!valid)
        return Result<StageWorkingSurfaceNavigationPlan,
            StageWorkingSurfaceNavigationPlanningError>::failure(valid.error());

    auto command = commandFor(surface, target);
    if (!command)
        return Result<StageWorkingSurfaceNavigationPlan,
            StageWorkingSurfaceNavigationPlanningError>::failure(command.error());

    const bool homeRequired =
        position.x().requiresHoming(thresholdInAxisUnit(
            policy.maximumXErrorMicrometers(), position.x().unit())) ||
        position.y().requiresHoming(thresholdInAxisUnit(
            policy.maximumYErrorMicrometers(), position.y().unit()));

    if (homeRequired) {
        auto axes = StageAxisSet::create({surface.xAxisId(), surface.yAxisId()});
        if (!axes)
            return Result<StageWorkingSurfaceNavigationPlan,
                StageWorkingSurfaceNavigationPlanningError>::failure(
                    StageWorkingSurfaceNavigationPlanningError::HomeAxisSetInvalid);
        return Result<StageWorkingSurfaceNavigationPlan,
            StageWorkingSurfaceNavigationPlanningError>::success(
                StageWorkingSurfaceNavigationPlan{StageWorkingSurfaceHomeThenMovePlan{
                    target, HomeAxes::create(std::move(axes).value())}});
    }

    return Result<StageWorkingSurfaceNavigationPlan,
        StageWorkingSurfaceNavigationPlanningError>::success(
            StageWorkingSurfaceNavigationPlan{StageWorkingSurfaceDirectMovePlan{
                target, std::move(command).value()}});
}

Result<StageWorkingSurfaceNavigationPlan, StageWorkingSurfaceNavigationPlanningError>
StageWorkingSurfaceNavigationPlanner::planToSlidePoint(
    const StageWorkingSurface2D& surface, const SlideId& slideId, SlidePoint2D point,
    const StagePosition3D& position, const StageXYHomingPolicy& policy) {
    const auto valid = validatePosition(surface, position);
    if (!valid)
        return Result<StageWorkingSurfaceNavigationPlan,
            StageWorkingSurfaceNavigationPlanningError>::failure(valid.error());

    const auto target = surface.createNavigationTarget(slideId, point);
    if (!target)
        return Result<StageWorkingSurfaceNavigationPlan,
            StageWorkingSurfaceNavigationPlanningError>::failure(mapTargetError(target.error()));
    return plan(surface, target.value(), position, policy);
}

Result<MoveAxesTo, StageWorkingSurfaceNavigationPlanningError>
StageWorkingSurfaceNavigationPlanner::resolveAfterHoming(
    const StageWorkingSurface2D& surface, const SlideNavigationTarget2D& target,
    const StagePosition3D& position, const StageXYHomingPolicy& policy) {
    const auto valid = validatePosition(surface, position);
    if (!valid)
        return Result<MoveAxesTo, StageWorkingSurfaceNavigationPlanningError>::failure(
            valid.error());

    if (!position.x().positionIsUsable() || !position.y().positionIsUsable())
        return Result<MoveAxesTo, StageWorkingSurfaceNavigationPlanningError>::failure(
            StageWorkingSurfaceNavigationPlanningError::HomingNotVerified);
    if (position.x().requiresHoming(thresholdInAxisUnit(
            policy.maximumXErrorMicrometers(), position.x().unit())) ||
        position.y().requiresHoming(thresholdInAxisUnit(
            policy.maximumYErrorMicrometers(), position.y().unit())))
        return Result<MoveAxesTo, StageWorkingSurfaceNavigationPlanningError>::failure(
            StageWorkingSurfaceNavigationPlanningError::PositionUncertaintyExceeded);

    return commandFor(surface, target);
}

} // namespace microscope::core
