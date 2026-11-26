#include "microscope/core/stage/StagePosition3D.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace microscope::core {
namespace {
double toMicrometers(double value, AxisCoordinateUnit unit) noexcept {
    return unit == AxisCoordinateUnit::Millimeters ? value * 1000.0 : value;
}
bool linearUnit(AxisCoordinateUnit unit) noexcept {
    return unit == AxisCoordinateUnit::Micrometers || unit == AxisCoordinateUnit::Millimeters;
}
StagePosition3DError mapPositionError(StageAxisPositionStateError error) {
    if (error == StageAxisPositionStateError::AxisMismatch)
        return StagePosition3DError::AxisMismatch;
    if (error == StageAxisPositionStateError::UnitMismatch)
        return StagePosition3DError::AxisUnitMismatch;
    return StagePosition3DError::PositionRejected;
}
const AxisSpecification* findAxis(const StageSpecification& stage, const StageAxisId& id) {
    const auto it = std::find_if(stage.axes().begin(), stage.axes().end(),
        [&](const auto& axis) { return axis.id() == id; });
    return it == stage.axes().end() ? nullptr : &*it;
}
}

StagePosition3D::StagePosition3D(
    StageAxisPositionState x, StageAxisPositionState y, StageAxisPositionState z)
    : x_(std::move(x)), y_(std::move(y)), z_(std::move(z)) {}

Result<StagePosition3D, StagePosition3DError> StagePosition3D::unknown(
    const StageWorkingSurface2D& surface,
    const StageSpecification& stage,
    StageAxisId focusAxisId) {
    if (surface.stageComponentId() != stage.componentId())
        return Result<StagePosition3D, StagePosition3DError>::failure(
            StagePosition3DError::StageMismatch);
    if (focusAxisId == surface.xAxisId() || focusAxisId == surface.yAxisId())
        return Result<StagePosition3D, StagePosition3DError>::failure(
            StagePosition3DError::FocusAxisMustBeDistinct);
    const auto* focus = findAxis(stage, focusAxisId);
    if (!focus)
        return Result<StagePosition3D, StagePosition3DError>::failure(
            StagePosition3DError::FocusAxisNotFound);
    if (focus->motionType() != AxisMotionType::Linear || !linearUnit(focus->coordinateUnit()))
        return Result<StagePosition3D, StagePosition3DError>::failure(
            StagePosition3DError::UnsupportedFocusAxis);

    auto x = StageAxisPositionState::unknown(
        surface.stageComponentId(), surface.xAxisId(), surface.xAxisUnit());
    auto y = StageAxisPositionState::unknown(
        surface.stageComponentId(), surface.yAxisId(), surface.yAxisUnit());
    auto z = StageAxisPositionState::unknown(
        surface.stageComponentId(), focus->id(), focus->coordinateUnit());
    if (!x || !y || !z)
        return Result<StagePosition3D, StagePosition3DError>::failure(
            StagePosition3DError::AxisUnitMismatch);
    return Result<StagePosition3D, StagePosition3DError>::success(
        StagePosition3D(std::move(x).value(), std::move(y).value(), std::move(z).value()));
}

Result<StagePosition3D, StagePosition3DError>
StagePosition3D::afterPositionObservation(
    const AxisPositionObservation& observation, ObservationSource source) const {
    const auto& id = observation.position().axisId();
    if (id == x_.axisId()) {
        auto next = x_.afterPositionObservation(observation, source);
        if (!next) return Result<StagePosition3D, StagePosition3DError>::failure(mapPositionError(next.error()));
        return Result<StagePosition3D, StagePosition3DError>::success(
            StagePosition3D(std::move(next).value(), y_, z_));
    }
    if (id == y_.axisId()) {
        auto next = y_.afterPositionObservation(observation, source);
        if (!next) return Result<StagePosition3D, StagePosition3DError>::failure(mapPositionError(next.error()));
        return Result<StagePosition3D, StagePosition3DError>::success(
            StagePosition3D(x_, std::move(next).value(), z_));
    }
    if (id == z_.axisId()) {
        auto next = z_.afterPositionObservation(observation, source);
        if (!next) return Result<StagePosition3D, StagePosition3DError>::failure(mapPositionError(next.error()));
        return Result<StagePosition3D, StagePosition3DError>::success(
            StagePosition3D(x_, y_, std::move(next).value()));
    }
    return Result<StagePosition3D, StagePosition3DError>::failure(StagePosition3DError::AxisMismatch);
}

Result<StagePosition3D, StagePosition3DError>
StagePosition3D::afterVerifiedHomingObservation(
    const AxisPositionObservation& observation, ObservationSource source) const {
    const auto& id = observation.position().axisId();
    const auto apply = [&](const StageAxisPositionState& current)
        -> Result<StageAxisPositionState, StageAxisPositionStateError> {
        if (observation.position().unit() != current.unit())
            return Result<StageAxisPositionState, StageAxisPositionStateError>::failure(
                StageAxisPositionStateError::UnitMismatch);
        return StageAxisPositionState::homed(current.stageComponentId(), observation, source);
    };
    if (id == x_.axisId()) {
        auto next = apply(x_);
        if (!next) return Result<StagePosition3D, StagePosition3DError>::failure(mapPositionError(next.error()));
        return Result<StagePosition3D, StagePosition3DError>::success(
            StagePosition3D(std::move(next).value(), y_, z_));
    }
    if (id == y_.axisId()) {
        auto next = apply(y_);
        if (!next) return Result<StagePosition3D, StagePosition3DError>::failure(mapPositionError(next.error()));
        return Result<StagePosition3D, StagePosition3DError>::success(
            StagePosition3D(x_, std::move(next).value(), z_));
    }
    if (id == z_.axisId()) {
        auto next = apply(z_);
        if (!next) return Result<StagePosition3D, StagePosition3DError>::failure(mapPositionError(next.error()));
        return Result<StagePosition3D, StagePosition3DError>::success(
            StagePosition3D(x_, y_, std::move(next).value()));
    }
    return Result<StagePosition3D, StagePosition3DError>::failure(StagePosition3DError::AxisMismatch);
}

Result<StagePosition3D, StagePosition3DError>
StagePosition3D::afterCalibratedReference(
    const StageAxisId& axisId, double referencePosition, double uncertainty) const {
    const auto apply = [&](const StageAxisPositionState& current)
        -> Result<StageAxisPositionState, StageAxisPositionStateError> {
        return StageAxisPositionState::referencedEstimate(
            current.stageComponentId(), current.axisId(), current.unit(),
            referencePosition, uncertainty);
    };
    if (axisId == x_.axisId()) {
        auto next = apply(x_);
        if (!next) return Result<StagePosition3D, StagePosition3DError>::failure(mapPositionError(next.error()));
        return Result<StagePosition3D, StagePosition3DError>::success(
            StagePosition3D(std::move(next).value(), y_, z_));
    }
    if (axisId == y_.axisId()) {
        auto next = apply(y_);
        if (!next) return Result<StagePosition3D, StagePosition3DError>::failure(mapPositionError(next.error()));
        return Result<StagePosition3D, StagePosition3DError>::success(
            StagePosition3D(x_, std::move(next).value(), z_));
    }
    if (axisId == z_.axisId()) {
        auto next = apply(z_);
        if (!next) return Result<StagePosition3D, StagePosition3DError>::failure(mapPositionError(next.error()));
        return Result<StagePosition3D, StagePosition3DError>::success(
            StagePosition3D(x_, y_, std::move(next).value()));
    }
    return Result<StagePosition3D, StagePosition3DError>::failure(StagePosition3DError::AxisMismatch);
}

Result<StagePosition3D, StagePosition3DError>
StagePosition3D::afterCommandedPosition(const AxisPosition& position,
    std::optional<double> additionalError) const {
    if (position.axisId() == x_.axisId()) {
        auto next = x_.afterCommandedPosition(position, additionalError);
        if (!next) return Result<StagePosition3D, StagePosition3DError>::failure(mapPositionError(next.error()));
        return Result<StagePosition3D, StagePosition3DError>::success(
            StagePosition3D(std::move(next).value(), y_, z_));
    }
    if (position.axisId() == y_.axisId()) {
        auto next = y_.afterCommandedPosition(position, additionalError);
        if (!next) return Result<StagePosition3D, StagePosition3DError>::failure(mapPositionError(next.error()));
        return Result<StagePosition3D, StagePosition3DError>::success(
            StagePosition3D(x_, std::move(next).value(), z_));
    }
    if (position.axisId() == z_.axisId()) {
        auto next = z_.afterCommandedPosition(position, additionalError);
        if (!next) return Result<StagePosition3D, StagePosition3DError>::failure(mapPositionError(next.error()));
        return Result<StagePosition3D, StagePosition3DError>::success(
            StagePosition3D(x_, y_, std::move(next).value()));
    }
    return Result<StagePosition3D, StagePosition3DError>::failure(StagePosition3DError::AxisMismatch);
}

Result<StagePosition3D, StagePosition3DError>
StagePosition3D::afterCommandedDisplacement(const AxisDisplacement& displacement,
    std::optional<double> additionalError) const {
    if (displacement.axisId() == x_.axisId()) {
        auto next = x_.afterCommandedDisplacement(displacement, additionalError);
        if (!next) return Result<StagePosition3D, StagePosition3DError>::failure(mapPositionError(next.error()));
        return Result<StagePosition3D, StagePosition3DError>::success(
            StagePosition3D(std::move(next).value(), y_, z_));
    }
    if (displacement.axisId() == y_.axisId()) {
        auto next = y_.afterCommandedDisplacement(displacement, additionalError);
        if (!next) return Result<StagePosition3D, StagePosition3DError>::failure(mapPositionError(next.error()));
        return Result<StagePosition3D, StagePosition3DError>::success(
            StagePosition3D(x_, std::move(next).value(), z_));
    }
    if (displacement.axisId() == z_.axisId()) {
        auto next = z_.afterCommandedDisplacement(displacement, additionalError);
        if (!next) return Result<StagePosition3D, StagePosition3DError>::failure(mapPositionError(next.error()));
        return Result<StagePosition3D, StagePosition3DError>::success(
            StagePosition3D(x_, y_, std::move(next).value()));
    }
    return Result<StagePosition3D, StagePosition3DError>::failure(StagePosition3DError::AxisMismatch);
}

StagePosition3D StagePosition3D::invalidateMachineReference() const {
    return StagePosition3D(x_.invalidateMachineReference(),
        y_.invalidateMachineReference(), z_.invalidateMachineReference());
}
const StageAxisPositionState& StagePosition3D::x() const noexcept { return x_; }
const StageAxisPositionState& StagePosition3D::y() const noexcept { return y_; }
const StageAxisPositionState& StagePosition3D::z() const noexcept { return z_; }

Result<SlidePositionEstimate2D, StagePosition3DError>
StagePosition3D::positionOnSlide(const StageWorkingSurface2D& surface, const SlideId& id) const {
    if (surface.stageComponentId() != x_.stageComponentId() ||
        surface.stageComponentId() != y_.stageComponentId() ||
        surface.stageComponentId() != z_.stageComponentId())
        return Result<SlidePositionEstimate2D, StagePosition3DError>::failure(
            StagePosition3DError::StageMismatch);
    if (x_.axisId() != surface.xAxisId() || y_.axisId() != surface.yAxisId())
        return Result<SlidePositionEstimate2D, StagePosition3DError>::failure(
            StagePosition3DError::AxisMismatch);
    if (x_.unit() != surface.xAxisUnit() || y_.unit() != surface.yAxisUnit())
        return Result<SlidePositionEstimate2D, StagePosition3DError>::failure(
            StagePosition3DError::AxisUnitMismatch);
    if (!linearUnit(x_.unit()) || !linearUnit(y_.unit()))
        return Result<SlidePositionEstimate2D, StagePosition3DError>::failure(
            StagePosition3DError::AxisUnitMismatch);
    if (!x_.positionIsUsable() || !y_.positionIsUsable() ||
        !x_.position() || !y_.position())
        return Result<SlidePositionEstimate2D, StagePosition3DError>::failure(
            StagePosition3DError::PositionUnknown);
    if (!x_.accumulatedPositionErrorBound() || !y_.accumulatedPositionErrorBound())
        return Result<SlidePositionEstimate2D, StagePosition3DError>::failure(
            StagePosition3DError::PositionUncertaintyUnknown);
    if (!surface.placementFor(id))
        return Result<SlidePositionEstimate2D, StagePosition3DError>::failure(
            StagePosition3DError::SlideNotFound);

    const StagePoint2D machine{
        toMicrometers(x_.position()->value(), x_.unit()),
        toMicrometers(y_.position()->value(), y_.unit())
    };
    auto projected = surface.toSlide(id, machine);
    if (!projected)
        return Result<SlidePositionEstimate2D, StagePosition3DError>::failure(
            StagePosition3DError::SlideProjectionRejected);

    const double ex = toMicrometers(*x_.accumulatedPositionErrorBound(), x_.unit());
    const double ey = toMicrometers(*y_.accumulatedPositionErrorBound(), y_.unit());
    const auto* placement = surface.placementFor(id);
    const double localXError =
        std::abs(placement->localXAxisX()) * ex +
        std::abs(placement->localXAxisY()) * ey;
    const double localYError =
        std::abs(placement->localYAxisX()) * ex +
        std::abs(placement->localYAxisY()) * ey;
    if (!std::isfinite(localXError) || !std::isfinite(localYError))
        return Result<SlidePositionEstimate2D, StagePosition3DError>::failure(
            StagePosition3DError::PositionUncertaintyUnknown);

    return Result<SlidePositionEstimate2D, StagePosition3DError>::success({
        projected.value().slideId, projected.value().registrationRevision,
        projected.value().position, localXError, localYError,
        x_.evidence(), y_.evidence(), x_.source(), y_.source(),
        projected.value().insideSlide, projected.value().insideWorkingSurface
    });
}
} // namespace microscope::core
