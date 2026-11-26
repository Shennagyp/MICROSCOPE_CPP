#pragma once
#include "microscope/core/motion/StageAxisPositionState.h"
#include "microscope/core/stage/StageWorkingSurface3D.h"

namespace microscope::core {
enum class StagePosition3DError{
    StageMismatch,AxisMismatch,AxisUnitMismatch,PositionUnknown,
    PositionUncertaintyUnknown,PositionRejected,SlideProjectionRejected,SlideNotFound
};

struct SlidePositionEstimate3D final{
    SlideId slideId;
    CalibrationRevisionId registrationRevision;
    SlidePoint3D position;
    double xErrorBoundMicrometers;
    double yErrorBoundMicrometers;
    double zErrorBoundMicrometers;
    StagePositionEvidence xEvidence,yEvidence,zEvidence;
    ObservationSource xSource,ySource,zSource;
    bool insideSlide;
    bool insideWorkingVolume;
};

// Immutable three-axis position state. It is updated from controller/encoder
// observations on the serialized runtime owner and projected through one of
// the ten fixed slide transforms when a consumer selects a physical slide.
class StagePosition3D final{
public:
    static Result<StagePosition3D,StagePosition3DError> unknown(const StageWorkingSurface3D&);
    Result<StagePosition3D,StagePosition3DError> afterPositionObservation(
        const AxisPositionObservation&,ObservationSource)const;
    // Call only after the operation runtime has verified the complete reference
    // procedure for this axis.
    Result<StagePosition3D,StagePosition3DError> afterVerifiedHomingObservation(
        const AxisPositionObservation&,ObservationSource)const;
    Result<StagePosition3D,StagePosition3DError> afterCommandedPosition(
        const AxisPosition&,std::optional<double> additionalErrorBound)const;
    Result<StagePosition3D,StagePosition3DError> afterCommandedDisplacement(
        const AxisDisplacement&,std::optional<double> additionalErrorBound)const;
    StagePosition3D invalidateMachineReference()const;

    const StageAxisPositionState& x()const noexcept;
    const StageAxisPositionState& y()const noexcept;
    const StageAxisPositionState& z()const noexcept;

    Result<SlidePositionEstimate3D,StagePosition3DError> positionOnSlide(
        const StageWorkingSurface3D&,const SlideId&)const;

private:
    StagePosition3D(StageAxisPositionState,StageAxisPositionState,StageAxisPositionState);
    StageAxisPositionState x_,y_,z_;
};
} // namespace microscope::core
