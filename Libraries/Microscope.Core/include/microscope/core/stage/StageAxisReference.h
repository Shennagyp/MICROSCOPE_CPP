#pragma once
#include "microscope/core/equipment/StageSpecifications.h"
#include "microscope/core/motion/StageMotionPort.h"
#include "microscope/core/result/Result.h"
#include <optional>

namespace microscope::core {

// For a focus mechanism without a limit-switch sensor, this mechanical
// capability permits the knob to continue rotating after the carriage reaches
// its travel boundary without collision or damage.
enum class StageAxisOvertravelCapability {
    NotSupported,
    ContinuousKnobRotationWithoutClash
};

enum class StageAxisReferenceMethod {
    PhysicalEndstop,
    FullRangeSweep
};

enum class StageAxisReferenceStatus {
    Unreferenced,
    SweepingFullRange,
    ReferencedPhysicalEndstop,
    ReferencedMeasuredFullRangeSweep,
    ReferencedEstimatedFullRangeSweep
};

enum class StageAxisReferenceEvidence {
    None,
    PhysicalLimitSwitch,
    EncoderFullRangeSweep,
    ExternalMeasurementFullRangeSweep,
    CalibratedCommandFullRangeSweep
};

enum class StageAxisReferenceError {
    InvalidMethod,
    InvalidEnd,
    InvalidOvertravelCapability,
    UnsafeOvertravelUnavailable,
    InvalidUncertainty,
    InvalidPositionTolerance,
    UnsupportedAxisUnit,
    AxisMismatch,
    UnitMismatch,
    WrongReferenceMethod,
    WrongEndstop,
    EndstopNotActive,
    SweepNotInProgress,
    SweepDirectionMismatch,
    SweepDistanceInsufficient,
    TargetBoundaryNotReached,
    UncertaintyExceedsPolicy
};

class StageAxisReferenceSpecification final {
public:
    static Result<StageAxisReferenceSpecification, StageAxisReferenceError> create(
        const AxisSpecification&, StageAxisReferenceMethod, AxisLimitEnd referenceEnd,
        StageAxisOvertravelCapability, double maximumReferenceUncertainty,
        double positionTolerance);

    const StageAxisId& axisId() const noexcept;
    AxisCoordinateUnit unit() const noexcept;
    StageAxisReferenceMethod method() const noexcept;
    AxisLimitEnd referenceEnd() const noexcept;
    StageAxisOvertravelCapability overtravelCapability() const noexcept;
    double minimumPosition() const noexcept;
    double maximumPosition() const noexcept;
    double referencePosition() const noexcept;
    double fullTravelDistance() const noexcept;
    double sweepDisplacement() const noexcept;
    double maximumReferenceUncertainty() const noexcept;
    double positionTolerance() const noexcept;

private:
    StageAxisReferenceSpecification(StageAxisId, AxisCoordinateUnit,
        StageAxisReferenceMethod, AxisLimitEnd, StageAxisOvertravelCapability,
        double, double, double, double);
    StageAxisId axisId_;
    AxisCoordinateUnit unit_;
    StageAxisReferenceMethod method_;
    AxisLimitEnd referenceEnd_;
    StageAxisOvertravelCapability overtravelCapability_;
    double minimumPosition_;
    double maximumPosition_;
    double maximumReferenceUncertainty_;
    double positionTolerance_;
};

struct StageAxisReferenceSnapshot final {
    StageAxisReferenceStatus status;
    StageAxisReferenceEvidence evidence;
    std::optional<double> referencePosition;
    std::optional<double> uncertainty;
    std::optional<double> sweepDisplacement;
};

// Owner-thread state. FullRangeSweep begins from an unknown carriage coordinate,
// commands one complete calibrated travel range toward the configured boundary,
// then assigns that boundary as a calibrated estimate. This is safe only for a
// mechanism with ContinuousKnobRotationWithoutClash. Completion must be called
// only after the exact motion operation/command has completed successfully.
class StageAxisReferenceState final {
public:
    static StageAxisReferenceState create(StageAxisReferenceSpecification);

    Result<StageAxisReferenceSnapshot, StageAxisReferenceError> recordPhysicalEndstop(
        const AxisLimitObservation&);
    Result<StageAxisReferenceSnapshot, StageAxisReferenceError> beginFullRangeSweep();
    Result<StageAxisReferenceSnapshot, StageAxisReferenceError> completeMeasuredFullRangeSweep(
        const AxisPositionObservation&);
    Result<StageAxisReferenceSnapshot, StageAxisReferenceError> completeCalibratedFullRangeSweep(
        const AxisDisplacement& completedDisplacement, double uncertainty);

    StageAxisReferenceSnapshot snapshot() const noexcept;
    const StageAxisReferenceSpecification& specification() const noexcept;
    void invalidate() noexcept;
    void abortFullRangeSweep() noexcept;

private:
    explicit StageAxisReferenceState(StageAxisReferenceSpecification);
    bool isAt(double position, double target) const noexcept;
    bool isMeasuredEvidence(StagePositionEvidence) const noexcept;
    StageAxisReferenceSnapshot makeSnapshot() const noexcept;

    StageAxisReferenceSpecification specification_;
    StageAxisReferenceStatus status_;
    StageAxisReferenceEvidence evidence_;
    std::optional<double> referencePosition_;
    std::optional<double> uncertainty_;
    std::optional<double> sweepDisplacement_;
};

} // namespace microscope::core
