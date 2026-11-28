#pragma once

#include "microscope/core/ids/Ids.h"
#include "microscope/core/result/Result.h"
#include "microscope/core/samples/SlideGeometry.h"
#include "microscope/core/runtime/Feedback.h"
#include "microscope/core/motion/StageMotionPort.h"

#include <array>
#include <cstdint>

namespace microscope::application::navigation {

// Coordinates in this registration are expressed in nanometres. Floating
// representation is retained for fitted/interpolated coordinates and error
// propagation; Stage motion targets remain integer nanometres.
struct SlideLocalPointNanometres final { double x=0.0, y=0.0; };
struct StagePlanePointNanometres final { double x=0.0, y=0.0; };

enum class StageSlotCornerMeasurementError {
    InvalidCorner, NonFiniteCoordinate, InvalidUncertainty,
    UnknownProvenance, InvalidProvenance
};

class StageSlotCornerMeasurement final {
public:
    [[nodiscard]] static core::Result<StageSlotCornerMeasurement,
        StageSlotCornerMeasurementError> create(
            core::SlideCorner corner,
            StagePlanePointNanometres stagePosition,
            core::ObservationSource source,
            double uncertaintyNanometres);

    [[nodiscard]] core::SlideCorner corner() const noexcept;
    [[nodiscard]] const StagePlanePointNanometres& stagePosition() const noexcept;
    [[nodiscard]] core::ObservationSource source() const noexcept;
    [[nodiscard]] double uncertaintyNanometres() const noexcept;

private:
    StageSlotCornerMeasurement(core::SlideCorner,StagePlanePointNanometres,
        core::ObservationSource,double);
    core::SlideCorner corner_;
    StagePlanePointNanometres stagePosition_;
    core::ObservationSource source_;
    double uncertaintyNanometres_;
};

struct SlidePositionEstimateNanometres final {
    SlideLocalPointNanometres position;
    double xErrorBoundNanometres=0.0;
    double yErrorBoundNanometres=0.0;
    core::StagePositionEvidence horizontalEvidence=core::StagePositionEvidence::Unknown;
    core::StagePositionEvidence transverseEvidence=core::StagePositionEvidence::Unknown;
    core::ObservationSource horizontalSource=core::ObservationSource::Unknown;
    core::ObservationSource transverseSource=core::ObservationSource::Unknown;
};

enum class StageSlotRegistrationError {
    InvalidSlideRevision, InvalidResidualLimit, DuplicateCorner, MissingCorner,
    DegenerateTransform, FitResidualExceeded, NonFiniteLocalPoint,
    LocalPointOutsideSlide, NonFiniteStagePoint, NonFiniteErrorBound
};

// Four-corner affine registration of one physical Slide in one generic
// StageSlot. The assignment is pinned to an immutable Slide revision and a
// separate calibration revision. It contains no left/right carrier semantics.
class StageSlotRegistration final {
public:
    [[nodiscard]] static core::Result<StageSlotRegistration,
        StageSlotRegistrationError> create(
            core::StageSlotId stageSlotId,
            core::SlideId slideId,
            std::uint64_t slideRevision,
            core::CalibrationRevisionId calibrationRevision,
            core::SlideGeometry slideGeometry,
            std::array<StageSlotCornerMeasurement,4> cornerMeasurements,
            double maximumFitResidualNanometres);

    [[nodiscard]] const core::StageSlotId& stageSlotId() const noexcept;
    [[nodiscard]] const core::SlideId& slideId() const noexcept;
    [[nodiscard]] std::uint64_t slideRevision() const noexcept;
    [[nodiscard]] const core::CalibrationRevisionId& calibrationRevision() const noexcept;
    [[nodiscard]] const core::SlideGeometry& slideGeometry() const noexcept;
    [[nodiscard]] const std::array<StageSlotCornerMeasurement,4>&
    cornerMeasurements() const noexcept;
    [[nodiscard]] double acceptedMaximumFitResidualNanometres() const noexcept;
    [[nodiscard]] double maximumFitResidualNanometres() const noexcept;
    [[nodiscard]] double rmsFitResidualNanometres() const noexcept;
    [[nodiscard]] const std::array<double,4>& cornerFitResidualsNanometres() const noexcept;

    [[nodiscard]] core::Result<StagePlanePointNanometres,
        StageSlotRegistrationError> toStage(SlideLocalPointNanometres) const;
    [[nodiscard]] core::Result<SlideLocalPointNanometres,
        StageSlotRegistrationError> toSlide(StagePlanePointNanometres) const;
    [[nodiscard]] core::Result<SlidePositionEstimateNanometres,
        StageSlotRegistrationError> estimateSlidePosition(
            StagePlanePointNanometres,
            double horizontalErrorBoundNanometres,
            double transverseErrorBoundNanometres,
            core::StagePositionEvidence horizontalEvidence,
            core::StagePositionEvidence transverseEvidence,
            core::ObservationSource horizontalSource,
            core::ObservationSource transverseSource) const;

private:
    StageSlotRegistration(core::StageSlotId,core::SlideId,std::uint64_t,
        core::CalibrationRevisionId,core::SlideGeometry,
        std::array<StageSlotCornerMeasurement,4>,std::array<double,6>,
        std::array<double,4>,double,double,double,double);

    core::StageSlotId stageSlotId_;
    core::SlideId slideId_;
    std::uint64_t slideRevision_;
    core::CalibrationRevisionId calibrationRevision_;
    core::SlideGeometry slideGeometry_;
    std::array<StageSlotCornerMeasurement,4> corners_;
    // X=a0+a1*x+a2*y; Y=b0+b1*x+b2*y, all coordinate values in nm.
    std::array<double,6> transform_;
    std::array<double,4> cornerResiduals_;
    double maximumCornerUncertaintyNanometres_;
    double acceptedMaximumFitResidualNanometres_;
    double maximumFitResidualNanometres_;
    double rmsFitResidualNanometres_;
};

} // namespace microscope::application::navigation
