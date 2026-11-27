#pragma once

#include "microscope/core/navigation/DualSlideNavigation.h"
#include "microscope/core/stage/StageWorkingSurface2D.h"

#include <array>

namespace microscope::application::navigation {

// A corner of the CAD slide rectangle. The order is cyclic:
// stop point -> length edge -> opposite corner -> width edge.
enum class SlideRectangleCorner2D {
    StopPoint,
    LengthEdgeEnd,
    OppositeCorner,
    WidthEdgeEnd
};

enum class SlideCornerCorrespondenceError {
    InvalidCorner,
    DuplicateCorner,
    NonAdjacentCorrespondence
};

// Explicitly states which CAD corner represents each measured physical corner.
// The input array order is TopLeft, TopRight, BottomRight, BottomLeft.
// Only cyclic rotations and reflections are accepted; an edge cannot map to
// a diagonal and the correspondence cannot silently fold the rectangle.
class SlideCornerCorrespondence final {
public:
    [[nodiscard]] static core::Result<SlideCornerCorrespondence,
        SlideCornerCorrespondenceError> create(
            std::array<SlideRectangleCorner2D, 4> workingSurfaceCorners);

    [[nodiscard]] SlideRectangleCorner2D workingSurfaceCornerFor(
        core::SlideCorner measuredCorner) const noexcept;
    [[nodiscard]] const std::array<SlideRectangleCorner2D, 4>&
    mappings() const noexcept;

private:
    explicit SlideCornerCorrespondence(
        std::array<SlideRectangleCorner2D, 4>) noexcept;

    std::array<SlideRectangleCorner2D, 4> mappings_;
};

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
    [[nodiscard]] const core::CalibrationRevisionId&
    physicalRegistrationRevision() const noexcept;
    [[nodiscard]] const core::CalibrationRevisionId&
    workingSurfaceRevision() const noexcept;
    [[nodiscard]] bool revisionsAligned() const noexcept;
    [[nodiscard]] bool carrierHorizontalAxisMapsToWorkingSurfaceX() const noexcept;

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
        core::SlideId,
        core::CalibrationRevisionId,
        core::CalibrationRevisionId,
        bool,
        double,
        double,
        double,
        double,
        std::array<SlideCornerReconciliation, 4>,
        double,
        double,
        double,
        double,
        double,
        std::array<double, 4>) noexcept;

    core::SlideId slideId_;
    core::CalibrationRevisionId physicalRegistrationRevision_;
    core::CalibrationRevisionId workingSurfaceRevision_;
    bool horizontalMapsToWorkingSurfaceX_;
    double physicalXExtent_;
    double physicalYExtent_;
    double workingSurfaceXExtent_;
    double workingSurfaceYExtent_;
    std::array<SlideCornerReconciliation, 4> corners_;
    double maximumCornerDiscrepancy_;
    double rmsCornerDiscrepancy_;
    double registrationMaximumFitResidual_;
    double registrationRmsFitResidual_;
    double registrationAcceptedMaximumFitResidual_;
    std::array<double, 4> registrationCornerFitResiduals_;
};

} // namespace microscope::application::navigation
