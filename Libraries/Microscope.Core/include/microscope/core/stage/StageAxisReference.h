#pragma once
#include "microscope/core/equipment/StageSpecifications.h"
#include "microscope/core/motion/StageMotionPort.h"
#include "microscope/core/result/Result.h"
#include <optional>

namespace microscope::core {

enum class StageAxisReferenceMethod { PhysicalEndstop, FullStrokeTraversal };
enum class StageAxisReferenceStatus {
    Unreferenced, TraversingFullStroke, ReferencedPhysicalEndstop,
    ReferencedMeasuredFullStroke, ReferencedEstimatedFullStroke
};
enum class StageAxisReferenceEvidence {
    None, PhysicalLimitSwitch, EncoderFullStroke,
    ExternalMeasurementFullStroke, CalibratedCommandFullStroke
};
enum class StageAxisReferenceError {
    InvalidMethod, InvalidEnd, InvalidUncertainty, InvalidPositionTolerance,
    UnsupportedAxisUnit, AxisMismatch, UnitMismatch, WrongReferenceMethod,
    WrongEndstop, EndstopNotActive, WrongStartingBoundary,
    StartingEvidenceInsufficient, StrokeNotInProgress,
    TargetBoundaryNotReached, UncertaintyExceedsPolicy
};

class StageAxisReferenceSpecification final {
public:
    static Result<StageAxisReferenceSpecification,StageAxisReferenceError> create(
        const AxisSpecification&,StageAxisReferenceMethod,AxisLimitEnd,
        double maximumReferenceUncertainty,double positionTolerance);
    const StageAxisId& axisId()const noexcept;
    AxisCoordinateUnit unit()const noexcept;
    StageAxisReferenceMethod method()const noexcept;
    AxisLimitEnd referenceEnd()const noexcept;
    AxisLimitEnd startingEnd()const noexcept;
    double minimumPosition()const noexcept;
    double maximumPosition()const noexcept;
    double referencePosition()const noexcept;
    double startingPosition()const noexcept;
    double fullStrokeDistance()const noexcept;
    double maximumReferenceUncertainty()const noexcept;
    double positionTolerance()const noexcept;
private:
    StageAxisReferenceSpecification(StageAxisId,AxisCoordinateUnit,StageAxisReferenceMethod,
        AxisLimitEnd,double,double,double,double);
    StageAxisId axisId_;
    AxisCoordinateUnit unit_;
    StageAxisReferenceMethod method_;
    AxisLimitEnd referenceEnd_;
    double minimumPosition_,maximumPosition_,maximumReferenceUncertainty_,positionTolerance_;
};

struct StageAxisReferenceSnapshot final {
    StageAxisReferenceStatus status;
    StageAxisReferenceEvidence evidence;
    std::optional<double> referencePosition;
    std::optional<double> uncertainty;
    std::optional<double> traversalStartPosition;
};

// Owner-thread reference state. Full-stroke completion must be called only
// after the matching full-range motion action has completed. Open-loop
// completion remains an estimated soft-endstop, never measured evidence.
class StageAxisReferenceState final {
public:
    static StageAxisReferenceState create(StageAxisReferenceSpecification);
    Result<StageAxisReferenceSnapshot,StageAxisReferenceError> recordPhysicalEndstop(
        const AxisLimitObservation&);
    Result<StageAxisReferenceSnapshot,StageAxisReferenceError> beginFullStrokeTraversal(
        const AxisPosition&,StagePositionEvidence startEvidence,double startUncertainty);
    Result<StageAxisReferenceSnapshot,StageAxisReferenceError> completeMeasuredFullStrokeTraversal(
        const AxisPositionObservation&);
    Result<StageAxisReferenceSnapshot,StageAxisReferenceError> completeCalibratedFullStrokeTraversal(
        const AxisPosition&,double uncertainty);
    StageAxisReferenceSnapshot snapshot()const noexcept;
    const StageAxisReferenceSpecification& specification()const noexcept;
    void invalidate()noexcept;
    void abortFullStrokeTraversal()noexcept;
private:
    explicit StageAxisReferenceState(StageAxisReferenceSpecification);
    bool isAt(double,double)const noexcept;
    bool isMeasuredEvidence(StagePositionEvidence)const noexcept;
    StageAxisReferenceSnapshot makeSnapshot()const noexcept;
    StageAxisReferenceSpecification specification_;
    StageAxisReferenceStatus status_;
    StageAxisReferenceEvidence evidence_;
    std::optional<double> referencePosition_,uncertainty_,traversalStartPosition_;
};

} // namespace microscope::core
