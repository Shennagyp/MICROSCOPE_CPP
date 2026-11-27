#pragma once

#include "microscope/application/navigation/SlideCornerCorrespondence.h"
#include "microscope/core/stage/StageWorkingSurface2D.h"

#include <array>

namespace microscope::application::navigation {

enum class SlideRegistrationWorkingSurfaceComparisonError {
    SlideNotInCarrier,
    SlideNotRegistered,
    SlideNotInWorkingSurface,
    StageComponentMismatch,
    StageAxisMismatch,
    NonFiniteDiscrepancy
};

// One raw physical corner measurement compared with its explicitly paired CAD
// corner. The original measurement (including source and uncertainty) remains
// intact; the measured position is also expressed in the working-surface X/Y
// axis order so that deltas have an unambiguous coordinate frame.
struct SlideCornerReconciliation final {
    core::SlideCornerMeasurement measurement;
    SlideRectangleCorner2D workingSurfaceCorner;
    core::StagePoint2D measuredPositionInWorkingSurfaceAxes;
    core::StagePoint2D workingSurfacePosition;
    double workingSurfaceMinusMeasuredXMicrometers;
    double workingSurfaceMinusMeasuredYMicrometers;
    double discrepancyMicrometers;
};

// A comparison snapshot, not a replacement calibration and not a navigation
// transform. It retains both revision identities and reports whether they are
// aligned, along with raw corner deviations and the registration fit evidence.
class SlideRegistrationWorkingSurfaceComparison final {
public:
    [[nodiscard]] static core::Result<
        SlideRegistrationWorkingSurfaceComparison,
        SlideRegistrationWorkingSurfaceComparisonError> compare(
            const core::DualSlideCarrierState& carrier,
            const core::StageWorkingSurface2D& workingSurface,
            const core::SlideId& slideId,
            const SlideCornerCorrespondence& correspondence);

    [[nodiscard]] const core::SlideId& slideId() const noexcept;
    [[nodiscard]] const core::SlideRegistration&
    physicalRegistration() const noexcept;
    [[nodiscard]] const core::SlideRectangleDefinition2D&
    workingSurfaceRectangle() const noexcept;
    [[nodiscard]] const core::CalibrationRevisionId&
    physicalRegistrationRevision() const noexcept;
    [[nodiscard]] const core::CalibrationRevisionId&
    workingSurfaceRevision() const noexcept;
    [[nodiscard]] bool revisionsAligned() const noexcept;
    [[nodiscard]] bool carrierHorizontalAxisMapsToWorkingSurfaceX() const noexcept;
    [[nodiscard]] bool physicalSlideXMapsToWorkingSurfaceX() const noexcept;

    [[nodiscard]] double physicalXExtentMicrometers() const noexcept;
    [[nodiscard]] double physicalYExtentMicrometers() const noexcept;
    [[nodiscard]] double workingSurfaceXExtentMicrometers() const noexcept;
    [[nodiscard]] double workingSurfaceYExtentMicrometers() const noexcept;
    [[nodiscard]] double workingSurfaceMinusPhysicalXExtentMicrometers() const noexcept;
    [[nodiscard]] double workingSurfaceMinusPhysicalYExtentMicrometers() const noexcept;

    // Ordered TopLeft, TopRight, BottomRight, BottomLeft by measured corner.
    [[nodiscard]] const std::array<SlideCornerReconciliation, 4>&
    corners() const noexcept;
    [[nodiscard]] double maximumCornerDiscrepancyMicrometers() const noexcept;
    [[nodiscard]] double rmsCornerDiscrepancyMicrometers() const noexcept;

    [[nodiscard]] double registrationMaximumFitResidualMicrometers() const noexcept;
    [[nodiscard]] double registrationRmsFitResidualMicrometers() const noexcept;
    [[nodiscard]] double registrationAcceptedMaximumFitResidualMicrometers() const noexcept;
    [[nodiscard]] const std::array<double, 4>&
    registrationCornerFitResidualsMicrometers() const noexcept;

private:
    SlideRegistrationWorkingSurfaceComparison(
        core::SlideRegistration,
        core::SlideRectangleDefinition2D,
        bool,
        bool,
        std::array<SlideCornerReconciliation, 4>,
        double,
        double) noexcept;

    // Keep the exact immutable source models. In particular, the affine
    // registration remains available through its original toStage/toSlide API.
    core::SlideRegistration physicalRegistration_;
    core::SlideRectangleDefinition2D workingSurfaceRectangle_;
    bool horizontalMapsToWorkingSurfaceX_;
    bool physicalSlideXMapsToWorkingSurfaceX_;
    std::array<SlideCornerReconciliation, 4> corners_;
    double maximumCornerDiscrepancy_;
    double rmsCornerDiscrepancy_;
};

} // namespace microscope::application::navigation
