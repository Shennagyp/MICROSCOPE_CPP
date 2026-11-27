#include "microscope/application/navigation/SlideRegistrationWorkingSurfaceComparison.h"

#include "microscope/core/equipment/StageSpecifications.h"

#include <array>
#include <cmath>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace microscope::core;
using namespace microscope::application::navigation;

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

template <typename T, typename E>
T take(Result<T, E> result) {
    if (!result) throw std::runtime_error("fixture rejected");
    return std::move(result).value();
}

template <typename T>
T id(const char* value) {
    return take(T::create(value));
}

AxisSpecification axis(const char* name) {
    return take(AxisSpecification::create(
        id<StageAxisId>(name),
        AxisMotionType::Linear,
        AxisCoordinateUnit::Micrometers,
        take(AxisOperatingRange::create(
            -10'000.0, 150'000.0, AxisCoordinateUnit::Micrometers)),
        take(PositiveAxisResolution::create(0.1)),
        std::nullopt,
        std::nullopt,
        std::nullopt,
        AxisPositionFeedbackCapability::DeviceReported));
}

StageSpecification stage(
    const char* component = "scope-stage",
    bool includeZ = false) {
    std::vector<AxisSpecification> axes{axis("x"), axis("y")};
    if (includeZ) axes.push_back(axis("z"));
    return take(StageSpecification::create(
        id<EquipmentComponentId>(component), std::move(axes)));
}

SlideRegistration registration(
    const char* slideName,
    DualSlideSlot slot,
    const char* revision,
    bool horizontalMapsToX = true) {
    const auto geometry = take(SlideGeometry::create(75'000.0, 25'000.0));
    const auto source = ObservationSource::HardwareReadback;
    const auto point = [horizontalMapsToX](double x, double y) {
        return horizontalMapsToX
            ? StagePlanePoint{x, y}
            : StagePlanePoint{y, x};
    };
    const auto tl = take(SlideCornerMeasurement::create(
        SlideCorner::TopLeft, point(5'000.0, 20'000.0), source, 0.5));
    const auto tr = take(SlideCornerMeasurement::create(
        SlideCorner::TopRight, point(80'000.0, 20'000.0), source, 0.5));
    const auto br = take(SlideCornerMeasurement::create(
        SlideCorner::BottomRight, point(80'000.0, 45'000.0), source, 0.5));
    const auto bl = take(SlideCornerMeasurement::create(
        SlideCorner::BottomLeft, point(5'000.0, 45'000.0), source, 0.5));
    return take(SlideRegistration::create(
        id<SlideId>(slideName),
        id<CalibrationRevisionId>(revision),
        slot,
        geometry,
        {tl, tr, br, bl},
        1.0));
}

DualSlideCarrierState carrierWith(
    const StageSpecification& stageSpecification,
    const char* horizontalAxis,
    const char* transverseAxis,
    SlideRegistration slideRegistration) {
    const auto specification = take(DualSlideCarrierSpecification::create(
        stageSpecification,
        id<StageAxisId>(horizontalAxis),
        id<StageAxisId>(transverseAxis),
        CarrierHorizontalDirection::PositiveStageXFromLeftToRight));
    const auto slideId = slideRegistration.slideId();
    const auto left = take(SlideSlotState::create(
        DualSlideSlot::Left, slideId, std::move(slideRegistration)));
    const auto right = take(SlideSlotState::create(
        DualSlideSlot::Right, std::nullopt));
    return take(DualSlideCarrierState::create(
        specification, left, right));
}

StageWorkingSurface2D workingSurface(
    const StageSpecification& stageSpecification,
    const char* slideName,
    const char* revision,
    double originX = 5'000.0,
    double originY = 20'000.0,
    double length = 75'000.0,
    double width = 25'000.0,
    const char* xAxis = "x",
    const char* yAxis = "y") {
    auto rectangle = take(SlideRectangleDefinition2D::create(
        id<SlideId>(slideName),
        id<CalibrationRevisionId>(revision),
        {originX, originY},
        {originX + 1.0, originY + 1.0},
        SlideRectangleLengthAxis::StageX,
        length,
        width));
    return take(StageWorkingSurface2D::create(
        stageSpecification,
        id<StageAxisId>(xAxis),
        id<StageAxisId>(yAxis),
        {0.0, 0.0},
        {140'000.0, 70'000.0},
        {std::move(rectangle)}));
}

SlideCornerCorrespondence topLeftIsStopPoint() {
    return take(SlideCornerCorrespondence::create({
        SlideRectangleCorner2D::StopPoint,
        SlideRectangleCorner2D::LengthEdgeEnd,
        SlideRectangleCorner2D::OppositeCorner,
        SlideRectangleCorner2D::WidthEdgeEnd}));
}

void correspondenceRequiresOneCyclicBijection() {
    auto standard = SlideCornerCorrespondence::create({
        SlideRectangleCorner2D::StopPoint,
        SlideRectangleCorner2D::LengthEdgeEnd,
        SlideRectangleCorner2D::OppositeCorner,
        SlideRectangleCorner2D::WidthEdgeEnd});
    check(static_cast<bool>(standard),
        "accepts the direct TopLeft-to-stop-point correspondence");

    auto rotated = SlideCornerCorrespondence::create({
        SlideRectangleCorner2D::LengthEdgeEnd,
        SlideRectangleCorner2D::OppositeCorner,
        SlideRectangleCorner2D::WidthEdgeEnd,
        SlideRectangleCorner2D::StopPoint});
    check(static_cast<bool>(rotated),
        "accepts a cyclically rotated corner correspondence");

    auto reflected = SlideCornerCorrespondence::create({
        SlideRectangleCorner2D::StopPoint,
        SlideRectangleCorner2D::WidthEdgeEnd,
        SlideRectangleCorner2D::OppositeCorner,
        SlideRectangleCorner2D::LengthEdgeEnd});
    check(static_cast<bool>(reflected),
        "accepts a reflected corner correspondence");

    auto duplicate = SlideCornerCorrespondence::create({
        SlideRectangleCorner2D::StopPoint,
        SlideRectangleCorner2D::LengthEdgeEnd,
        SlideRectangleCorner2D::LengthEdgeEnd,
        SlideRectangleCorner2D::WidthEdgeEnd});
    check(!duplicate &&
        duplicate.error() == SlideCornerCorrespondenceError::DuplicateCorner,
        "rejects a correspondence that assigns two measured corners to one CAD corner");

    auto crossed = SlideCornerCorrespondence::create({
        SlideRectangleCorner2D::StopPoint,
        SlideRectangleCorner2D::OppositeCorner,
        SlideRectangleCorner2D::LengthEdgeEnd,
        SlideRectangleCorner2D::WidthEdgeEnd});
    check(!crossed &&
        crossed.error() ==
            SlideCornerCorrespondenceError::NonAdjacentCorrespondence,
        "rejects crossed mappings that would pair adjacent measured corners with CAD diagonals");
}

void exactDirectAxisComparisonPreservesCalibrationEvidence() {
    const auto stageSpecification = stage();
    const auto measured = registration(
        "slide-direct", DualSlideSlot::Left, "slide-direct-cal-v1");
    const auto carrier = carrierWith(
        stageSpecification, "x", "y", measured);
    const auto surface = workingSurface(
        stageSpecification, "slide-direct", "slide-direct-cal-v1");

    const auto comparison = SlideRegistrationWorkingSurfaceComparison::compare(
        carrier, surface, id<SlideId>("slide-direct"), topLeftIsStopPoint());
    check(static_cast<bool>(comparison),
        "compares a registered slide to its CAD rectangle when stage axes and identity agree");
    if (!comparison) return;

    const auto& value = comparison.value();
    check(value.revisionsAligned(),
        "reports aligned physical-registration and working-surface revisions");
    check(value.carrierHorizontalAxisMapsToWorkingSurfaceX(),
        "records the direct horizontal-to-X stage-axis mapping");
    check(value.corners().size() == 4 &&
        value.maximumCornerDiscrepancyMicrometers() < 1e-9 &&
        value.rmsCornerDiscrepancyMicrometers() < 1e-9,
        "reports zero corner discrepancy when the CAD rectangle matches the measurements");
    check(value.physicalXExtentMicrometers() == 75'000.0 &&
        value.physicalYExtentMicrometers() == 25'000.0 &&
        value.workingSurfaceXExtentMicrometers() == 75'000.0 &&
        value.workingSurfaceYExtentMicrometers() == 25'000.0,
        "reports both physical and CAD geometry without replacing either model");
    check(value.corners()[0].measurement.corner() == SlideCorner::TopLeft &&
        value.corners()[0].measurement.source() == ObservationSource::HardwareReadback &&
        value.corners()[0].measurement.uncertaintyMicrometers() == 0.5,
        "retains the original corner identity, provenance and uncertainty");
    check(value.registrationMaximumFitResidualMicrometers() < 1e-9 &&
        value.registrationAcceptedMaximumFitResidualMicrometers() == 1.0 &&
        value.registrationCornerFitResidualsMicrometers()[0] < 1e-9,
        "retains the registration fit residuals and the accepted residual limit");

    const auto mappedFromRetainedRegistration =
        value.physicalRegistration().toStage({37'500.0, 12'500.0});
    check(mappedFromRetainedRegistration &&
        std::abs(mappedFromRetainedRegistration.value().horizontalMicrometers - 42'500.0) < 1e-9 &&
        std::abs(mappedFromRetainedRegistration.value().transverseMicrometers - 32'500.0) < 1e-9,
        "retains the original affine registration and its slide-to-stage mapping methods");
    check(value.workingSurfaceRectangle().drawnOppositeEndpoint().xMicrometers == 75'001.0 &&
        value.workingSurfaceRectangle().drawnOppositeEndpoint().yMicrometers == 20'001.0,
        "retains the original CAD drawing handle and constrained rectangle model");
}

void reportsDifferencesAndIndependentRevisionIdentity() {
    const auto stageSpecification = stage();
    const auto measured = registration(
        "slide-shifted", DualSlideSlot::Left, "physical-cal-v3");
    const auto carrier = carrierWith(
        stageSpecification, "x", "y", measured);
    const auto surface = workingSurface(
        stageSpecification,
        "slide-shifted",
        "cad-layout-v4",
        5'100.0,
        19'800.0);

    const auto comparison = SlideRegistrationWorkingSurfaceComparison::compare(
        carrier, surface, id<SlideId>("slide-shifted"), topLeftIsStopPoint());
    check(static_cast<bool>(comparison),
        "compares independently revised physical and CAD snapshots");
    if (!comparison) return;

    const auto& value = comparison.value();
    check(!value.revisionsAligned(),
        "flags different physical-registration and CAD-layout revisions instead of conflating them");
    check(std::abs(value.corners()[0].workingSurfaceMinusMeasuredXMicrometers() - 100.0) < 1e-9 &&
        std::abs(value.corners()[0].workingSurfaceMinusMeasuredYMicrometers() + 200.0) < 1e-9,
        "reports signed CAD-minus-measured corner deltas in the working-surface coordinate frame");
    check(std::abs(value.maximumCornerDiscrepancyMicrometers() - std::hypot(100.0, 200.0)) < 1e-9 &&
        std::abs(value.rmsCornerDiscrepancyMicrometers() - std::hypot(100.0, 200.0)) < 1e-9,
        "reports maximum and RMS discrepancy without modifying physical calibration");
}

void reportsGeometryDifferencesSeparatelyFromCornerRegistration() {
    const auto stageSpecification = stage();
    const auto measured = registration(
        "slide-geometry", DualSlideSlot::Left, "slide-geometry-cal-v1");
    const auto carrier = carrierWith(
        stageSpecification, "x", "y", measured);
    const auto surface = workingSurface(
        stageSpecification,
        "slide-geometry",
        "slide-geometry-layout-v2",
        5'000.0,
        20'000.0,
        75'020.0,
        24'980.0);

    const auto comparison = SlideRegistrationWorkingSurfaceComparison::compare(
        carrier, surface, id<SlideId>("slide-geometry"), topLeftIsStopPoint());
    check(static_cast<bool>(comparison),
        "returns a comparison when CAD dimensions differ from the physical slide geometry");
    if (!comparison) return;

    check(std::abs(comparison.value().workingSurfaceMinusPhysicalXExtentMicrometers() - 20.0) < 1e-9 &&
        std::abs(comparison.value().workingSurfaceMinusPhysicalYExtentMicrometers() + 20.0) < 1e-9,
        "reports CAD-minus-physical extent deltas instead of rejecting or rewriting either geometry");
    check(std::abs(comparison.value().maximumCornerDiscrepancyMicrometers() - std::hypot(20.0, 20.0)) < 1e-9,
        "reports the geometric corner mismatch independently from the registration fit residual");
}

void distinguishesLoadedButUnregisteredSlide() {
    const auto stageSpecification = stage();
    const auto specification = take(DualSlideCarrierSpecification::create(
        stageSpecification,
        id<StageAxisId>("x"),
        id<StageAxisId>("y"),
        CarrierHorizontalDirection::PositiveStageXFromLeftToRight));
    const auto left = take(SlideSlotState::create(
        DualSlideSlot::Left, id<SlideId>("slide-unregistered")));
    const auto right = take(SlideSlotState::create(
        DualSlideSlot::Right, std::nullopt));
    const auto carrier = take(DualSlideCarrierState::create(
        specification, left, right));
    const auto surface = workingSurface(
        stageSpecification, "slide-unregistered", "slide-unregistered-layout-v1");

    const auto comparison = SlideRegistrationWorkingSurfaceComparison::compare(
        carrier, surface, id<SlideId>("slide-unregistered"), topLeftIsStopPoint());
    check(!comparison &&
        comparison.error() ==
            SlideRegistrationWorkingSurfaceComparisonError::SlideNotRegistered,
        "distinguishes a physically loaded slide with no four-corner registration");
}

void mapsSwappedCarrierAxesIntoWorkingSurfaceCoordinates() {
    const auto stageSpecification = stage();
    const auto measured = registration(
        "slide-swapped", DualSlideSlot::Left, "slide-swapped-cal-v1", false);
    const auto carrier = carrierWith(
        stageSpecification, "y", "x", measured);
    const auto surface = workingSurface(
        stageSpecification, "slide-swapped", "slide-swapped-cal-v1");

    const auto comparison = SlideRegistrationWorkingSurfaceComparison::compare(
        carrier, surface, id<SlideId>("slide-swapped"), topLeftIsStopPoint());
    check(static_cast<bool>(comparison),
        "supports a carrier whose horizontal/transverse axes are the reverse of working-surface X/Y");
    if (!comparison) return;

    check(!comparison.value().carrierHorizontalAxisMapsToWorkingSurfaceX() &&
        comparison.value().maximumCornerDiscrepancyMicrometers() < 1e-9,
        "reorders stage-plane coordinates by axis identity before comparing corners");
}

void rejectsMismatchedPhysicalIdentitiesAndMissingGeometry() {
    const auto stageSpecification = stage();
    const auto measured = registration(
        "slide-errors", DualSlideSlot::Left, "slide-errors-cal-v1");
    const auto carrier = carrierWith(
        stageSpecification, "x", "y", measured);
    const auto correspondence = topLeftIsStopPoint();

    const auto foreignSlide = SlideRegistrationWorkingSurfaceComparison::compare(
        carrier,
        workingSurface(stageSpecification, "another-slide", "another-rev"),
        id<SlideId>("another-slide"),
        correspondence);
    check(!foreignSlide &&
        foreignSlide.error() ==
            SlideRegistrationWorkingSurfaceComparisonError::SlideNotInCarrier,
        "rejects a physical slide identity absent from the carrier");

    const auto missingRectangle = SlideRegistrationWorkingSurfaceComparison::compare(
        carrier,
        StageWorkingSurface2D::createEmpty(
            stageSpecification,
            id<StageAxisId>("x"),
            id<StageAxisId>("y"),
            {0.0, 0.0},
            take(StageTravelRange2D::create(140'000.0, 70'000.0))).value(),
        id<SlideId>("slide-errors"),
        correspondence);
    check(!missingRectangle &&
        missingRectangle.error() ==
            SlideRegistrationWorkingSurfaceComparisonError::SlideNotInWorkingSurface,
        "rejects a slide that has registration but no CAD placement");

    const auto otherStage = stage("other-stage");
    const auto foreignSurface = workingSurface(
        otherStage, "slide-errors", "slide-errors-cal-v1");
    const auto stageMismatch = SlideRegistrationWorkingSurfaceComparison::compare(
        carrier, foreignSurface, id<SlideId>("slide-errors"), correspondence);
    check(!stageMismatch &&
        stageMismatch.error() ==
            SlideRegistrationWorkingSurfaceComparisonError::StageComponentMismatch,
        "rejects measurements and CAD coordinates belonging to different physical stages");

    const auto stageWithZ = stage("scope-stage", true);
    const auto carrierWithWrongAxes = carrierWith(
        stageWithZ, "x", "z",
        registration("slide-errors", DualSlideSlot::Left, "slide-errors-cal-v1"));
    const auto axisMismatch = SlideRegistrationWorkingSurfaceComparison::compare(
        carrierWithWrongAxes,
        workingSurface(stageWithZ, "slide-errors", "slide-errors-cal-v1"),
        id<SlideId>("slide-errors"),
        correspondence);
    check(!axisMismatch &&
        axisMismatch.error() ==
            SlideRegistrationWorkingSurfaceComparisonError::StageAxisMismatch,
        "rejects a carrier whose stage axes do not match the working-surface axes");
}

} // namespace

int main() {
    try {
        correspondenceRequiresOneCyclicBijection();
        exactDirectAxisComparisonPreservesCalibrationEvidence();
        reportsDifferencesAndIndependentRevisionIdentity();
        reportsGeometryDifferencesSeparatelyFromCornerRegistration();
        distinguishesLoadedButUnregisteredSlide();
        mapsSwappedCarrierAxesIntoWorkingSurfaceCoordinates();
        rejectsMismatchedPhysicalIdentitiesAndMissingGeometry();
    } catch (const std::exception& error) {
        std::cerr << "UNCAUGHT: " << error.what() << '\n';
        return 1;
    }

    if (failures != 0) return 1;
    std::cout << "Slide registration / working-surface comparison tests passed\n";
    return 0;
}
