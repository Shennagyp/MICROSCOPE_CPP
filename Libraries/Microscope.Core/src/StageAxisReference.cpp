#include "microscope/core/stage/StageAxisReference.h"
#include <cmath>
#include <utility>

namespace microscope::core {
namespace {
bool validEnd(AxisLimitEnd e) noexcept {
    return e == AxisLimitEnd::Negative || e == AxisLimitEnd::Positive;
}
bool validMethod(StageAxisReferenceMethod method) noexcept {
    return method == StageAxisReferenceMethod::PhysicalEndstop ||
        method == StageAxisReferenceMethod::FullRangeSweep;
}
bool linearUnit(AxisCoordinateUnit unit) noexcept {
    return unit == AxisCoordinateUnit::Micrometers || unit == AxisCoordinateUnit::Millimeters;
}
}

StageAxisReferenceSpecification::StageAxisReferenceSpecification(
    StageAxisId id, AxisCoordinateUnit unit, StageAxisReferenceMethod method,
    AxisLimitEnd end, StageAxisOvertravelCapability overtravel, double minimum,
    double maximum, double uncertainty, double tolerance)
    : axisId_(std::move(id)), unit_(unit), method_(method), referenceEnd_(end),
      overtravelCapability_(overtravel), minimumPosition_(minimum), maximumPosition_(maximum),
      maximumReferenceUncertainty_(uncertainty), positionTolerance_(tolerance) {}

Result<StageAxisReferenceSpecification, StageAxisReferenceError>
StageAxisReferenceSpecification::create(
    const AxisSpecification& axis, StageAxisReferenceMethod method, AxisLimitEnd end,
    StageAxisOvertravelCapability overtravel, double uncertainty, double tolerance) {
    if (!validMethod(method))
        return Result<StageAxisReferenceSpecification, StageAxisReferenceError>::failure(
            StageAxisReferenceError::InvalidMethod);
    if (!validEnd(end))
        return Result<StageAxisReferenceSpecification, StageAxisReferenceError>::failure(
            StageAxisReferenceError::InvalidEnd);
    if (method == StageAxisReferenceMethod::FullRangeSweep &&
        overtravel != StageAxisOvertravelCapability::ContinuousKnobRotationWithoutClash)
        return Result<StageAxisReferenceSpecification, StageAxisReferenceError>::failure(
            StageAxisReferenceError::UnsafeOvertravelUnavailable);
    if (method == StageAxisReferenceMethod::PhysicalEndstop &&
        overtravel != StageAxisOvertravelCapability::NotSupported &&
        overtravel != StageAxisOvertravelCapability::ContinuousKnobRotationWithoutClash)
        return Result<StageAxisReferenceSpecification, StageAxisReferenceError>::failure(
            StageAxisReferenceError::InvalidOvertravelCapability);
    if (!std::isfinite(uncertainty) || uncertainty < 0.0)
        return Result<StageAxisReferenceSpecification, StageAxisReferenceError>::failure(
            StageAxisReferenceError::InvalidUncertainty);
    if (!std::isfinite(tolerance) || tolerance <= 0.0)
        return Result<StageAxisReferenceSpecification, StageAxisReferenceError>::failure(
            StageAxisReferenceError::InvalidPositionTolerance);
    if (axis.motionType() != AxisMotionType::Linear || !linearUnit(axis.coordinateUnit()))
        return Result<StageAxisReferenceSpecification, StageAxisReferenceError>::failure(
            StageAxisReferenceError::UnsupportedAxisUnit);
    const auto& range = axis.operatingRange();
    if (range.maximum() - range.minimum() <= 2.0 * tolerance)
        return Result<StageAxisReferenceSpecification, StageAxisReferenceError>::failure(
            StageAxisReferenceError::InvalidPositionTolerance);
    return Result<StageAxisReferenceSpecification, StageAxisReferenceError>::success(
        StageAxisReferenceSpecification(axis.id(), axis.coordinateUnit(), method, end,
            overtravel, range.minimum(), range.maximum(), uncertainty, tolerance));
}

const StageAxisId& StageAxisReferenceSpecification::axisId() const noexcept { return axisId_; }
AxisCoordinateUnit StageAxisReferenceSpecification::unit() const noexcept { return unit_; }
StageAxisReferenceMethod StageAxisReferenceSpecification::method() const noexcept { return method_; }
AxisLimitEnd StageAxisReferenceSpecification::referenceEnd() const noexcept { return referenceEnd_; }
StageAxisOvertravelCapability StageAxisReferenceSpecification::overtravelCapability() const noexcept { return overtravelCapability_; }
double StageAxisReferenceSpecification::minimumPosition() const noexcept { return minimumPosition_; }
double StageAxisReferenceSpecification::maximumPosition() const noexcept { return maximumPosition_; }
double StageAxisReferenceSpecification::referencePosition() const noexcept {
    return referenceEnd_ == AxisLimitEnd::Negative ? minimumPosition_ : maximumPosition_;
}
double StageAxisReferenceSpecification::fullTravelDistance() const noexcept {
    return maximumPosition_ - minimumPosition_;
}
double StageAxisReferenceSpecification::sweepDisplacement() const noexcept {
    return referenceEnd_ == AxisLimitEnd::Negative
        ? -fullTravelDistance() : fullTravelDistance();
}
double StageAxisReferenceSpecification::maximumReferenceUncertainty() const noexcept { return maximumReferenceUncertainty_; }
double StageAxisReferenceSpecification::positionTolerance() const noexcept { return positionTolerance_; }

StageAxisReferenceState::StageAxisReferenceState(StageAxisReferenceSpecification spec)
    : specification_(std::move(spec)), status_(StageAxisReferenceStatus::Unreferenced),
      evidence_(StageAxisReferenceEvidence::None) {}
StageAxisReferenceState StageAxisReferenceState::create(StageAxisReferenceSpecification spec) {
    return StageAxisReferenceState(std::move(spec));
}
bool StageAxisReferenceState::isAt(double position, double target) const noexcept {
    return std::isfinite(position) &&
        std::abs(position - target) <= specification_.positionTolerance();
}
bool StageAxisReferenceState::isMeasuredEvidence(StagePositionEvidence evidence) const noexcept {
    return evidence == StagePositionEvidence::EncoderMeasured ||
        evidence == StagePositionEvidence::ExternalMeasured;
}
StageAxisReferenceSnapshot StageAxisReferenceState::makeSnapshot() const noexcept {
    return {status_, evidence_, referencePosition_, uncertainty_, sweepDisplacement_};
}

Result<StageAxisReferenceSnapshot, StageAxisReferenceError>
StageAxisReferenceState::recordPhysicalEndstop(const AxisLimitObservation& observation) {
    if (specification_.method() != StageAxisReferenceMethod::PhysicalEndstop)
        return Result<StageAxisReferenceSnapshot, StageAxisReferenceError>::failure(
            StageAxisReferenceError::WrongReferenceMethod);
    if (observation.axisId() != specification_.axisId())
        return Result<StageAxisReferenceSnapshot, StageAxisReferenceError>::failure(
            StageAxisReferenceError::AxisMismatch);
    if (observation.end() != specification_.referenceEnd())
        return Result<StageAxisReferenceSnapshot, StageAxisReferenceError>::failure(
            StageAxisReferenceError::WrongEndstop);
    if (!observation.active())
        return Result<StageAxisReferenceSnapshot, StageAxisReferenceError>::failure(
            StageAxisReferenceError::EndstopNotActive);
    status_ = StageAxisReferenceStatus::ReferencedPhysicalEndstop;
    evidence_ = StageAxisReferenceEvidence::PhysicalLimitSwitch;
    referencePosition_ = specification_.referencePosition();
    uncertainty_ = specification_.maximumReferenceUncertainty();
    sweepDisplacement_.reset();
    return Result<StageAxisReferenceSnapshot, StageAxisReferenceError>::success(makeSnapshot());
}

Result<StageAxisReferenceSnapshot, StageAxisReferenceError>
StageAxisReferenceState::beginFullRangeSweep() {
    if (specification_.method() != StageAxisReferenceMethod::FullRangeSweep)
        return Result<StageAxisReferenceSnapshot, StageAxisReferenceError>::failure(
            StageAxisReferenceError::WrongReferenceMethod);
    if (specification_.overtravelCapability() !=
        StageAxisOvertravelCapability::ContinuousKnobRotationWithoutClash)
        return Result<StageAxisReferenceSnapshot, StageAxisReferenceError>::failure(
            StageAxisReferenceError::UnsafeOvertravelUnavailable);
    status_ = StageAxisReferenceStatus::SweepingFullRange;
    evidence_ = StageAxisReferenceEvidence::None;
    referencePosition_.reset();
    uncertainty_.reset();
    sweepDisplacement_ = specification_.sweepDisplacement();
    return Result<StageAxisReferenceSnapshot, StageAxisReferenceError>::success(makeSnapshot());
}

Result<StageAxisReferenceSnapshot, StageAxisReferenceError>
StageAxisReferenceState::completeMeasuredFullRangeSweep(
    const AxisPositionObservation& observation) {
    if (specification_.method() != StageAxisReferenceMethod::FullRangeSweep)
        return Result<StageAxisReferenceSnapshot, StageAxisReferenceError>::failure(
            StageAxisReferenceError::WrongReferenceMethod);
    if (status_ != StageAxisReferenceStatus::SweepingFullRange || !sweepDisplacement_)
        return Result<StageAxisReferenceSnapshot, StageAxisReferenceError>::failure(
            StageAxisReferenceError::SweepNotInProgress);
    const auto& position = observation.position();
    if (position.axisId() != specification_.axisId())
        return Result<StageAxisReferenceSnapshot, StageAxisReferenceError>::failure(
            StageAxisReferenceError::AxisMismatch);
    if (position.unit() != specification_.unit())
        return Result<StageAxisReferenceSnapshot, StageAxisReferenceError>::failure(
            StageAxisReferenceError::UnitMismatch);
    if (!isAt(position.value(), specification_.referencePosition()))
        return Result<StageAxisReferenceSnapshot, StageAxisReferenceError>::failure(
            StageAxisReferenceError::TargetBoundaryNotReached);
    if (!isMeasuredEvidence(observation.evidence()) ||
        observation.uncertaintyInAxisUnit() > specification_.maximumReferenceUncertainty())
        return Result<StageAxisReferenceSnapshot, StageAxisReferenceError>::failure(
            StageAxisReferenceError::UncertaintyExceedsPolicy);
    status_ = StageAxisReferenceStatus::ReferencedMeasuredFullRangeSweep;
    evidence_ = observation.evidence() == StagePositionEvidence::EncoderMeasured
        ? StageAxisReferenceEvidence::EncoderFullRangeSweep
        : StageAxisReferenceEvidence::ExternalMeasurementFullRangeSweep;
    referencePosition_ = specification_.referencePosition();
    uncertainty_ = observation.uncertaintyInAxisUnit();
    return Result<StageAxisReferenceSnapshot, StageAxisReferenceError>::success(makeSnapshot());
}

Result<StageAxisReferenceSnapshot, StageAxisReferenceError>
StageAxisReferenceState::completeCalibratedFullRangeSweep(
    const AxisDisplacement& displacement, double uncertainty) {
    if (specification_.method() != StageAxisReferenceMethod::FullRangeSweep)
        return Result<StageAxisReferenceSnapshot, StageAxisReferenceError>::failure(
            StageAxisReferenceError::WrongReferenceMethod);
    if (status_ != StageAxisReferenceStatus::SweepingFullRange || !sweepDisplacement_)
        return Result<StageAxisReferenceSnapshot, StageAxisReferenceError>::failure(
            StageAxisReferenceError::SweepNotInProgress);
    if (displacement.axisId() != specification_.axisId())
        return Result<StageAxisReferenceSnapshot, StageAxisReferenceError>::failure(
            StageAxisReferenceError::AxisMismatch);
    if (displacement.unit() != specification_.unit())
        return Result<StageAxisReferenceSnapshot, StageAxisReferenceError>::failure(
            StageAxisReferenceError::UnitMismatch);
    if (displacement.value() * specification_.sweepDisplacement() <= 0.0)
        return Result<StageAxisReferenceSnapshot, StageAxisReferenceError>::failure(
            StageAxisReferenceError::SweepDirectionMismatch);
    if (std::abs(displacement.value()) + specification_.positionTolerance() <
        specification_.fullTravelDistance())
        return Result<StageAxisReferenceSnapshot, StageAxisReferenceError>::failure(
            StageAxisReferenceError::SweepDistanceInsufficient);
    if (!std::isfinite(uncertainty) || uncertainty < 0.0 ||
        uncertainty > specification_.maximumReferenceUncertainty())
        return Result<StageAxisReferenceSnapshot, StageAxisReferenceError>::failure(
            StageAxisReferenceError::UncertaintyExceedsPolicy);
    status_ = StageAxisReferenceStatus::ReferencedEstimatedFullRangeSweep;
    evidence_ = StageAxisReferenceEvidence::CalibratedCommandFullRangeSweep;
    referencePosition_ = specification_.referencePosition();
    uncertainty_ = uncertainty;
    return Result<StageAxisReferenceSnapshot, StageAxisReferenceError>::success(makeSnapshot());
}

StageAxisReferenceSnapshot StageAxisReferenceState::snapshot() const noexcept { return makeSnapshot(); }
const StageAxisReferenceSpecification& StageAxisReferenceState::specification() const noexcept { return specification_; }
void StageAxisReferenceState::invalidate() noexcept {
    status_ = StageAxisReferenceStatus::Unreferenced;
    evidence_ = StageAxisReferenceEvidence::None;
    referencePosition_.reset();
    uncertainty_.reset();
    sweepDisplacement_.reset();
}
void StageAxisReferenceState::abortFullRangeSweep() noexcept {
    if (status_ == StageAxisReferenceStatus::SweepingFullRange)
        invalidate();
}
} // namespace microscope::core
