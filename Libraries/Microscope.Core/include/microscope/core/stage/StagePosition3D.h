#pragma once
#include "microscope/core/motion/StageAxisPositionState.h"
#include "microscope/core/stage/StageWorkingSurface2D.h"

namespace microscope::core {

enum class StagePosition3DError {
    StageMismatch, AxisMismatch, AxisUnitMismatch, FocusAxisNotFound,
    FocusAxisMustBeDistinct, UnsupportedFocusAxis, PositionUnknown,
    PositionUncertaintyUnknown, PositionRejected, SlideProjectionRejected, SlideNotFound
};

struct SlidePositionEstimate2D final {
    SlideId slideId;
    CalibrationRevisionId registrationRevision;
    SlidePoint2D position;
    double xErrorBoundMicrometers;
    double yErrorBoundMicrometers;
    StagePositionEvidence xEvidence;
    StagePositionEvidence yEvidence;
    ObservationSource xSource;
    ObservationSource ySource;
    bool insideSlide;
    bool insideWorkingSurface;
};

// One installed machine coordinate aggregate:
// - X/Y are the stage working-surface coordinates and are projected into a
//   selected slide's 2D CAD rectangle.
// - Z is the microscope focus axis, independently referenced over its own
//   travel range. It is never part of slide geometry or slide-local XY.
class StagePosition3D final {
public:
    static Result<StagePosition3D, StagePosition3DError> unknown(
        const StageWorkingSurface2D&, const StageSpecification&, StageAxisId focusAxisId);

    Result<StagePosition3D, StagePosition3DError> afterPositionObservation(
        const AxisPositionObservation&, ObservationSource) const;
    Result<StagePosition3D, StagePosition3DError> afterVerifiedHomingObservation(
        const AxisPositionObservation&, ObservationSource) const;
    Result<StagePosition3D, StagePosition3DError> afterCalibratedReference(
        const StageAxisId&, double referencePosition, double uncertainty) const;
    Result<StagePosition3D, StagePosition3DError> afterCommandedPosition(
        const AxisPosition&, std::optional<double> additionalErrorBound) const;
    Result<StagePosition3D, StagePosition3DError> afterCommandedDisplacement(
        const AxisDisplacement&, std::optional<double> additionalErrorBound) const;
    // Invalidate only the XY working-plane estimate after an XY operation
    // fault. Focus Z has an independent reference lifecycle and is preserved.
    StagePosition3D invalidateXYReference() const;
    // A device/session feedback-loss event invalidates every machine axis.
    StagePosition3D invalidateMachineReference() const;

    const StageAxisPositionState& x() const noexcept;
    const StageAxisPositionState& y() const noexcept;
    const StageAxisPositionState& z() const noexcept;

    // Requires referenced X/Y only. Z focus position is exposed independently
    // through z(); an unreferenced focus axis does not invalidate XY mapping.
    Result<SlidePositionEstimate2D, StagePosition3DError> positionOnSlide(
        const StageWorkingSurface2D&, const SlideId&) const;

private:
    StagePosition3D(StageAxisPositionState, StageAxisPositionState, StageAxisPositionState);
    StageAxisPositionState x_;
    StageAxisPositionState y_;
    StageAxisPositionState z_;
};

} // namespace microscope::core
