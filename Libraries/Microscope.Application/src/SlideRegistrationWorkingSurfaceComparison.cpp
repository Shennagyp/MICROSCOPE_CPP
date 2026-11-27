#include "microscope/application/navigation/SlideRegistrationWorkingSurfaceComparison.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace microscope::application::navigation {
namespace {

core::StagePoint2D positionFor(
    const core::SlideRectangleCorners2D& corners,
    SlideRectangleCorner2D corner) noexcept {
    switch (corner) {
    case SlideRectangleCorner2D::StopPoint:
        return corners.stopPoint;
    case SlideRectangleCorner2D::LengthEdgeEnd:
        return corners.lengthEdgeEnd;
    case SlideRectangleCorner2D::OppositeCorner:
        return corners.oppositeCorner;
    case SlideRectangleCorner2D::WidthEdgeEnd:
        return corners.widthEdgeEnd;
    }
    return corners.stopPoint;
}

bool loadedInCarrier(
    const core::DualSlideCarrierState& carrier,
    const core::SlideId& slideId) noexcept {
    const auto& left = carrier.left().slideId();
    const auto& right = carrier.right().slideId();
    return (left && *left == slideId) || (right && *right == slideId);
}

} // namespace

SlideRegistrationWorkingSurfaceComparison::SlideRegistrationWorkingSurfaceComparison(
    core::SlideRegistration physicalRegistration,
    core::SlideRectangleDefinition2D workingSurfaceRectangle,
    bool horizontalMapsToX,
    bool physicalXMapsToWorkingSurfaceX,
    std::array<SlideCornerReconciliation, 4> corners,
    double maximumDiscrepancy,
    double rmsDiscrepancy) noexcept
    : physicalRegistration_(std::move(physicalRegistration)),
      workingSurfaceRectangle_(std::move(workingSurfaceRectangle)),
      horizontalMapsToWorkingSurfaceX_(horizontalMapsToX),
      physicalSlideXMapsToWorkingSurfaceX_(physicalXMapsToWorkingSurfaceX),
      corners_(std::move(corners)),
      maximumCornerDiscrepancy_(maximumDiscrepancy),
      rmsCornerDiscrepancy_(rmsDiscrepancy) {}

core::Result<SlideRegistrationWorkingSurfaceComparison,
    SlideRegistrationWorkingSurfaceComparisonError>
SlideRegistrationWorkingSurfaceComparison::compare(
    const core::DualSlideCarrierState& carrier,
    const core::StageWorkingSurface2D& workingSurface,
    const core::SlideId& slideId,
    const SlideCornerCorrespondence& correspondence) {
    if (!loadedInCarrier(carrier, slideId)) {
        return core::Result<SlideRegistrationWorkingSurfaceComparison,
            SlideRegistrationWorkingSurfaceComparisonError>::failure(
                SlideRegistrationWorkingSurfaceComparisonError::SlideNotInCarrier);
    }

    const auto* registration = carrier.registrationFor(slideId);
    if (registration == nullptr) {
        return core::Result<SlideRegistrationWorkingSurfaceComparison,
            SlideRegistrationWorkingSurfaceComparisonError>::failure(
                SlideRegistrationWorkingSurfaceComparisonError::SlideNotRegistered);
    }

    const auto& carrierSpecification = carrier.specification();
    if (carrierSpecification.stageComponentId() != workingSurface.stageComponentId()) {
        return core::Result<SlideRegistrationWorkingSurfaceComparison,
            SlideRegistrationWorkingSurfaceComparisonError>::failure(
                SlideRegistrationWorkingSurfaceComparisonError::StageComponentMismatch);
    }

    const bool horizontalMapsToX =
        carrierSpecification.horizontalAxisId() == workingSurface.xAxisId() &&
        carrierSpecification.transverseAxisId() == workingSurface.yAxisId();
    const bool horizontalMapsToY =
        carrierSpecification.horizontalAxisId() == workingSurface.yAxisId() &&
        carrierSpecification.transverseAxisId() == workingSurface.xAxisId();
    if (!horizontalMapsToX && !horizontalMapsToY) {
        return core::Result<SlideRegistrationWorkingSurfaceComparison,
            SlideRegistrationWorkingSurfaceComparisonError>::failure(
                SlideRegistrationWorkingSurfaceComparisonError::StageAxisMismatch);
    }

    const auto* rectangle = workingSurface.rectangleFor(slideId);
    if (rectangle == nullptr) {
        return core::Result<SlideRegistrationWorkingSurfaceComparison,
            SlideRegistrationWorkingSurfaceComparisonError>::failure(
                SlideRegistrationWorkingSurfaceComparisonError::SlideNotInWorkingSurface);
    }

    const auto& measuredCorners = registration->cornerMeasurements();
    const auto cadCorners = rectangle->stageCorners();
    const auto makeCorner = [&](const core::SlideCornerMeasurement& measurement)
        -> core::Result<SlideCornerReconciliation,
            SlideRegistrationWorkingSurfaceComparisonError> {
        const auto& measured = measurement.stagePosition();
        const core::StagePoint2D measuredInWorkingAxes = horizontalMapsToX
            ? core::StagePoint2D{
                measured.horizontalMicrometers, measured.transverseMicrometers}
            : core::StagePoint2D{
                measured.transverseMicrometers, measured.horizontalMicrometers};
        const auto cad = positionFor(
            cadCorners, correspondence.workingSurfaceCornerFor(measurement.corner()));
        const double deltaX =
            cad.xMicrometers - measuredInWorkingAxes.xMicrometers;
        const double deltaY =
            cad.yMicrometers - measuredInWorkingAxes.yMicrometers;
        const double distance = std::hypot(deltaX, deltaY);
        if (!std::isfinite(deltaX) || !std::isfinite(deltaY) ||
            !std::isfinite(distance)) {
            return core::Result<SlideCornerReconciliation,
                SlideRegistrationWorkingSurfaceComparisonError>::failure(
                    SlideRegistrationWorkingSurfaceComparisonError::NonFiniteDiscrepancy);
        }
        return core::Result<SlideCornerReconciliation,
            SlideRegistrationWorkingSurfaceComparisonError>::success(
                SlideCornerReconciliation{
                    measurement,
                    correspondence.workingSurfaceCornerFor(measurement.corner()),
                    measuredInWorkingAxes,
                    cad,
                    deltaX,
                    deltaY,
                    distance});
    };

    // SlideRegistration normalizes and stores its four measurements in this
    // canonical cyclic order, independent of the caller's input array order.
    auto topLeft = makeCorner(measuredCorners[0]);
    if (!topLeft) {
        return core::Result<SlideRegistrationWorkingSurfaceComparison,
            SlideRegistrationWorkingSurfaceComparisonError>::failure(topLeft.error());
    }
    auto topRight = makeCorner(measuredCorners[1]);
    if (!topRight) {
        return core::Result<SlideRegistrationWorkingSurfaceComparison,
            SlideRegistrationWorkingSurfaceComparisonError>::failure(topRight.error());
    }
    auto bottomRight = makeCorner(measuredCorners[2]);
    if (!bottomRight) {
        return core::Result<SlideRegistrationWorkingSurfaceComparison,
            SlideRegistrationWorkingSurfaceComparisonError>::failure(bottomRight.error());
    }
    auto bottomLeft = makeCorner(measuredCorners[3]);
    if (!bottomLeft) {
        return core::Result<SlideRegistrationWorkingSurfaceComparison,
            SlideRegistrationWorkingSurfaceComparisonError>::failure(bottomLeft.error());
    }
    std::array<SlideCornerReconciliation, 4> reconciledCorners{
        std::move(topLeft).value(),
        std::move(topRight).value(),
        std::move(bottomRight).value(),
        std::move(bottomLeft).value()};

    for (const auto& corner : reconciledCorners) {
        if (!std::isfinite(corner.discrepancyMicrometers)) {
            return core::Result<SlideRegistrationWorkingSurfaceComparison,
                SlideRegistrationWorkingSurfaceComparisonError>::failure(
                    SlideRegistrationWorkingSurfaceComparisonError::NonFiniteDiscrepancy);
        }
    }

    const double maximumDiscrepancy = std::max({
        reconciledCorners[0].discrepancyMicrometers,
        reconciledCorners[1].discrepancyMicrometers,
        reconciledCorners[2].discrepancyMicrometers,
        reconciledCorners[3].discrepancyMicrometers});
    const double rmsDiscrepancy = std::hypot(
        std::hypot(reconciledCorners[0].discrepancyMicrometers,
                   reconciledCorners[1].discrepancyMicrometers),
        std::hypot(reconciledCorners[2].discrepancyMicrometers,
                   reconciledCorners[3].discrepancyMicrometers)) / 2.0;
    if (!std::isfinite(rmsDiscrepancy)) {
        return core::Result<SlideRegistrationWorkingSurfaceComparison,
            SlideRegistrationWorkingSurfaceComparisonError>::failure(
                SlideRegistrationWorkingSurfaceComparisonError::NonFiniteDiscrepancy);
    }

    const auto& physicalGeometry = registration->geometry();
    const auto& workingGeometry = rectangle->geometry().canonicalGeometry();
    return core::Result<SlideRegistrationWorkingSurfaceComparison,
        SlideRegistrationWorkingSurfaceComparisonError>::success(
            SlideRegistrationWorkingSurfaceComparison(
                *registration,
                *rectangle,
                horizontalMapsToX,
                correspondence.measuredXMapsToWorkingSurfaceX(),
                std::move(reconciledCorners),
                maximumDiscrepancy,
                rmsDiscrepancy));
}

const core::SlideId& SlideRegistrationWorkingSurfaceComparison::slideId() const noexcept {
    return physicalRegistration_.slideId();
}
const core::SlideRegistration&
SlideRegistrationWorkingSurfaceComparison::physicalRegistration() const noexcept {
    return physicalRegistration_;
}
const core::SlideRectangleDefinition2D&
SlideRegistrationWorkingSurfaceComparison::workingSurfaceRectangle() const noexcept {
    return workingSurfaceRectangle_;
}
const core::CalibrationRevisionId&
SlideRegistrationWorkingSurfaceComparison::physicalRegistrationRevision() const noexcept {
    return physicalRegistration_.revision();
}
const core::CalibrationRevisionId&
SlideRegistrationWorkingSurfaceComparison::workingSurfaceRevision() const noexcept {
    return workingSurfaceRectangle_.registrationRevision();
}
bool SlideRegistrationWorkingSurfaceComparison::revisionsAligned() const noexcept {
    return physicalRegistration_.revision() ==
        workingSurfaceRectangle_.registrationRevision();
}
bool SlideRegistrationWorkingSurfaceComparison::
carrierHorizontalAxisMapsToWorkingSurfaceX() const noexcept {
    return horizontalMapsToWorkingSurfaceX_;
}
bool SlideRegistrationWorkingSurfaceComparison::
physicalSlideXMapsToWorkingSurfaceX() const noexcept {
    return physicalSlideXMapsToWorkingSurfaceX_;
}
double SlideRegistrationWorkingSurfaceComparison::physicalXExtentMicrometers() const noexcept {
    return physicalRegistration_.geometry().xExtentMicrometers();
}
double SlideRegistrationWorkingSurfaceComparison::physicalYExtentMicrometers() const noexcept {
    return physicalRegistration_.geometry().yExtentMicrometers();
}
double SlideRegistrationWorkingSurfaceComparison::workingSurfaceXExtentMicrometers() const noexcept {
    return workingSurfaceRectangle_.geometry().canonicalGeometry().xExtentMicrometers();
}
double SlideRegistrationWorkingSurfaceComparison::workingSurfaceYExtentMicrometers() const noexcept {
    return workingSurfaceRectangle_.geometry().canonicalGeometry().yExtentMicrometers();
}
double SlideRegistrationWorkingSurfaceComparison::
workingSurfaceMinusPhysicalXExtentMicrometers() const noexcept {
    const double correspondingWorkingSurfaceExtent =
        physicalSlideXMapsToWorkingSurfaceX_
            ? workingSurfaceXExtentMicrometers()
            : workingSurfaceYExtentMicrometers();
    return correspondingWorkingSurfaceExtent - physicalXExtentMicrometers();
}
double SlideRegistrationWorkingSurfaceComparison::
workingSurfaceMinusPhysicalYExtentMicrometers() const noexcept {
    const double correspondingWorkingSurfaceExtent =
        physicalSlideXMapsToWorkingSurfaceX_
            ? workingSurfaceYExtentMicrometers()
            : workingSurfaceXExtentMicrometers();
    return correspondingWorkingSurfaceExtent - physicalYExtentMicrometers();
}
const std::array<SlideCornerReconciliation, 4>&
SlideRegistrationWorkingSurfaceComparison::corners() const noexcept {
    return corners_;
}
double SlideRegistrationWorkingSurfaceComparison::
maximumCornerDiscrepancyMicrometers() const noexcept {
    return maximumCornerDiscrepancy_;
}
double SlideRegistrationWorkingSurfaceComparison::
rmsCornerDiscrepancyMicrometers() const noexcept {
    return rmsCornerDiscrepancy_;
}
double SlideRegistrationWorkingSurfaceComparison::
registrationMaximumFitResidualMicrometers() const noexcept {
    return physicalRegistration_.maximumFitResidualMicrometers();
}
double SlideRegistrationWorkingSurfaceComparison::
registrationRmsFitResidualMicrometers() const noexcept {
    return physicalRegistration_.rmsFitResidualMicrometers();
}
double SlideRegistrationWorkingSurfaceComparison::
registrationAcceptedMaximumFitResidualMicrometers() const noexcept {
    return physicalRegistration_.acceptedMaximumFitResidualMicrometers();
}
const std::array<double, 4>&
SlideRegistrationWorkingSurfaceComparison::
registrationCornerFitResidualsMicrometers() const noexcept {
    return physicalRegistration_.cornerFitResidualsMicrometers();
}

} // namespace microscope::application::navigation
