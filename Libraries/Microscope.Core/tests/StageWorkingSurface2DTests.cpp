#include "microscope/core/stage/StageAxisReference.h"
#include "microscope/core/stage/StagePosition3D.h"
#include "microscope/core/stage/StageWorkingSurface2D.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using namespace microscope::core;
int failures = 0;
void check(bool ok, const char* message) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
template<class T, class E>
T take(Result<T,E> result) {
    if (!result) throw std::runtime_error("test fixture rejected");
    return std::move(result).value();
}
template<class T> T id(const char* value) { return take(T::create(value)); }

AxisSpecification axis(const char* name, double lo, double hi,
    AxisPositionFeedbackCapability feedback = AxisPositionFeedbackCapability::Encoder) {
    auto range = take(AxisOperatingRange::create(lo, hi, AxisCoordinateUnit::Micrometers));
    auto resolution = take(PositiveAxisResolution::create(0.1));
    const auto location = feedback == AxisPositionFeedbackCapability::None
        ? std::optional<AxisMeasurementLocation>{}
        : std::optional<AxisMeasurementLocation>{AxisMeasurementLocation::Axis};
    return take(AxisSpecification::create(id<StageAxisId>(name), AxisMotionType::Linear,
        AxisCoordinateUnit::Micrometers, range, resolution, std::nullopt, std::nullopt,
        std::nullopt, feedback, location));
}
StageSpecification stageSpecification() {
    return take(StageSpecification::create(id<EquipmentComponentId>("scope-stage"), {
        axis("x", -10'000, 410'000),
        axis("y", -10'000, 70'000),
        axis("z-focus", -5'000, 5'000, AxisPositionFeedbackCapability::None)
    }));
}
SlideRectangleDefinition2D rectangle(const char* name, double x, double y,
    double length = 75'000, double width = 25'000) {
    const auto revision = std::string(name) + "-v1";
    return take(SlideRectangleDefinition2D::create(id<SlideId>(name),
        id<CalibrationRevisionId>(revision.c_str()), {x,y}, {x+1.0,y+1.0},
        SlideRectangleLengthAxis::StageX, length, width));
}
StageWorkingSurface2D workingSurface(const StageSpecification& stage) {
    StageWorkingSurface2D::SlideRectangles slides{
        rectangle("slide-1", 0, 0),
        rectangle("slide-2", 80'000, 0),
        rectangle("slide-3", 160'000, 30'000, 50'000, 20'000)
    };
    return take(StageWorkingSurface2D::create(stage,
        id<StageAxisId>("x"), id<StageAxisId>("y"),
        {0,0}, {250'000,60'000}, std::move(slides)));
}
AxisPositionObservation observation(const char* name, double value, double error,
    StagePositionEvidence evidence = StagePositionEvidence::EncoderMeasured) {
    return take(AxisPositionObservation::create(
        take(AxisPosition::create(id<StageAxisId>(name), value, AxisCoordinateUnit::Micrometers)),
        error, evidence));
}

void cadRectanglesAreXYOnlyAndVariableCount() {
    const auto stage = stageSpecification();
    const auto surface = workingSurface(stage);
    check(surface.slideRectangles().size() == 3 &&
        surface.rectangleFor(id<SlideId>("slide-3")) != nullptr,
        "working surface stores a user-defined number of slide rectangles");
    const auto* slide3 = surface.rectangleFor(id<SlideId>("slide-3"));
    check(slide3->stopPointEndpoint().xMicrometers == 160'000 &&
        slide3->constrainedLengthMicrometers() == 50'000 &&
        slide3->constrainedWidthMicrometers() == 20'000,
        "slide drawing contains only an XY stop datum and length/width constraints");
    check(slide3->solvedOppositeEndpoint().xMicrometers == 210'000 &&
        slide3->solvedOppositeEndpoint().yMicrometers == 50'000,
        "the dimensions solve the opposite corner from the initial handle");
    const auto corners = slide3->stageCorners();
    check(corners.stopPoint.xMicrometers == 160'000 &&
        corners.stopPoint.yMicrometers == 30'000 &&
        corners.lengthEdgeEnd.xMicrometers == 210'000 &&
        corners.lengthEdgeEnd.yMicrometers == 30'000 &&
        corners.widthEdgeEnd.xMicrometers == 160'000 &&
        corners.widthEdgeEnd.yMicrometers == 50'000 &&
        corners.oppositeCorner.xMicrometers == 210'000 &&
        corners.oppositeCorner.yMicrometers == 50'000,
        "exposes all four stage-coordinate corners from the CAD constraints");

    auto vertical = take(SlideRectangleDefinition2D::create(
        id<SlideId>("vertical-slide"), id<CalibrationRevisionId>("vertical-v1"),
        {100'000, 0}, {100'001, 1}, SlideRectangleLengthAxis::StageY,
        70'000, 25'000));
    const auto verticalCorners = vertical.stageCorners();
    check(verticalCorners.lengthEdgeEnd.xMicrometers == 100'000 &&
        verticalCorners.lengthEdgeEnd.yMicrometers == 70'000 &&
        verticalCorners.widthEdgeEnd.xMicrometers == 125'000 &&
        verticalCorners.widthEdgeEnd.yMicrometers == 0 &&
        verticalCorners.oppositeCorner.xMicrometers == 125'000 &&
        verticalCorners.oppositeCorner.yMicrometers == 70'000,
        "maps the four corners correctly when slide length is assigned to stage Y");

    auto local = take(surface.toSlide(id<SlideId>("slide-2"), {90'000, 5'000}));
    check(std::abs(local.position.xMicrometers - 10'000) < 1e-9 &&
        std::abs(local.position.yMicrometers - 5'000) < 1e-9,
        "maps stage XY into the selected slide-local XY frame");

    auto gap = take(surface.toSlide(id<SlideId>("slide-1"), {79'000, 5'000}));
    check(!gap.insideSlide && gap.insideWorkingSurface &&
        gap.position.xMicrometers > 75'000,
        "keeps slide-relative XY coordinates while the stage is between slide rectangles");

    auto machine = take(surface.toStage(id<SlideId>("slide-2"), {10'000, 5'000}));
    check(machine.xMicrometers == 90'000 && machine.yMicrometers == 5'000,
        "maps slide-local XY back into stage coordinates");
}

void emptyCanvasUsesUserOriginAndTravelRange() {
    const auto stage = stageSpecification();
    const auto range = take(StageTravelRange2D::create(420'000, 80'000));
    const auto empty = take(StageWorkingSurface2D::createEmpty(
        stage, id<StageAxisId>("x"), id<StageAxisId>("y"),
        {-10'000, -10'000}, range));

    check(empty.slideRectangles().empty(),
        "new working surface is an empty canvas with no implicit slide rectangles");
    check(empty.origin().xMicrometers == -10'000 &&
        empty.origin().yMicrometers == -10'000,
        "retains the user-defined starting machine coordinate");
    check(empty.travelRange().xMicrometers() == 420'000 &&
        empty.travelRange().yMicrometers() == 80'000,
        "retains the user-defined positive X/Y travel extents");
    check(empty.maximumBoundary().xMicrometers == 410'000 &&
        empty.maximumBoundary().yMicrometers == 70'000,
        "derives the far canvas boundary from origin plus travel extents");
    check(empty.contains({-10'000, -10'000}) && empty.contains({410'000, 70'000}) &&
        !empty.contains({410'000.1, 70'000}),
        "uses a closed workspace boundary and rejects points beyond travel extents");

    auto position = take(StagePosition3D::unknown(
        empty, stage, id<StageAxisId>("z-focus")));
    auto noSlide = position.positionOnSlide(empty, id<SlideId>("not-drawn-yet"));
    check(!noSlide && noSlide.error() == StagePosition3DError::SlideNotFound,
        "an empty workspace has machine-axis coordinates but no slide-local projection");

    auto invalidZero = StageTravelRange2D::create(0.0, 10.0);
    auto invalidNegative = StageTravelRange2D::create(10.0, -1.0);
    auto invalidInfinite = StageTravelRange2D::create(
        std::numeric_limits<double>::infinity(), 10.0);
    check(!invalidZero && !invalidNegative && !invalidInfinite,
        "rejects zero, negative and non-finite travel extents");

    auto invalidOrigin = StageWorkingSurface2D::createEmpty(
        stage, id<StageAxisId>("x"), id<StageAxisId>("y"),
        {std::numeric_limits<double>::quiet_NaN(), 0.0}, range);
    check(!invalidOrigin && invalidOrigin.error() ==
        StageWorkingSurface2DError::NonFiniteOrigin,
        "rejects a non-finite user-defined workspace origin");

    auto outsideAxisRange = StageWorkingSurface2D::createEmpty(
        stage, id<StageAxisId>("x"), id<StageAxisId>("y"),
        {-10'000, -10'000}, take(StageTravelRange2D::create(420'001, 80'000)));
    check(!outsideAxisRange && outsideAxisRange.error() ==
        StageWorkingSurface2DError::WorkingSurfaceOutsideAxisRange,
        "rejects a user workspace that exceeds the configured physical axis ranges");

    auto overflow = StageWorkingSurface2D::createEmpty(
        stage, id<StageAxisId>("x"), id<StageAxisId>("y"),
        {1.0e308, 0.0}, take(StageTravelRange2D::create(1.0e308, 10.0)));
    check(!overflow && overflow.error() ==
        StageWorkingSurface2DError::TravelRangeNotRepresentable,
        "rejects an overflowing or unrepresentable workspace boundary");
}

void emptyCanvasSupportsImmutableSlideRectangleEdits() {
    const auto stage = stageSpecification();
    const auto empty = take(StageWorkingSurface2D::createEmpty(
        stage, id<StageAxisId>("x"), id<StageAxisId>("y"), {0,0},
        take(StageTravelRange2D::create(250'000, 60'000))));

    const auto first = rectangle("slide-1", 40'000, 10'000);
    const auto oneSlide = take(empty.withSlideRectangle(first));
    const auto* firstStored = oneSlide.rectangleFor(id<SlideId>("slide-1"));
    check(empty.slideRectangles().empty() && oneSlide.slideRectangles().size() == 1,
        "adding a drawn slide creates a new snapshot without mutating the blank canvas");
    check(firstStored && firstStored->stopPointEndpoint().xMicrometers == 40'000 &&
        firstStored->stopPointEndpoint().yMicrometers == 10'000 &&
        empty.origin().xMicrometers == 0 && empty.origin().yMicrometers == 0,
        "keeps the slide's user-defined stop point separate from the workspace origin");

    auto duplicate = oneSlide.withSlideRectangle(first);
    check(!duplicate && duplicate.error() ==
        StageWorkingSurface2DError::DuplicateSlideIdentity,
        "rejects adding a slide identity already present on the canvas");

    const auto revised = take(SlideRectangleDefinition2D::create(
        id<SlideId>("slide-1"), id<CalibrationRevisionId>("slide-1-v2"),
        {40'000,10'000}, {40'001,10'001}, SlideRectangleLengthAxis::StageX,
        80'000, 20'000));
    const auto replaced = take(oneSlide.replacingSlideRectangle(revised));
    check(oneSlide.rectangleFor(id<SlideId>("slide-1"))->constrainedLengthMicrometers() == 75'000 &&
        replaced.rectangleFor(id<SlideId>("slide-1"))->constrainedLengthMicrometers() == 80'000 &&
        replaced.rectangleFor(id<SlideId>("slide-1"))->registrationRevision() ==
            id<CalibrationRevisionId>("slide-1-v2"),
        "replaces one slide registration as a new immutable layout revision");

    auto wrongIdentity = oneSlide.replacingSlideRectangle(rectangle("slide-2", 80'000, 0));
    check(!wrongIdentity && wrongIdentity.error() == StageWorkingSurface2DError::SlideNotFound,
        "replacement requires an existing slide identity");

    auto outside = empty.withSlideRectangle(rectangle("slide-outside", 250'000, 0));
    check(!outside && outside.error() == StageWorkingSurface2DError::SlideOutsideWorkingSurface,
        "rejects a slide footprint that extends beyond the user-defined travel range");

    const auto overlapRect = rectangle("slide-overlap", 80'000, 20'000);
    auto overlap = oneSlide.withSlideRectangle(overlapRect);
    check(!overlap && overlap.error() == StageWorkingSurface2DError::SlideRectanglesOverlap,
        "rejects positive-area overlap between two slide footprints");

    auto loadedOverlap = StageWorkingSurface2D::create(
        stage, id<StageAxisId>("x"), id<StageAxisId>("y"),
        {0,0}, {250'000,60'000}, {first, overlapRect});
    check(!loadedOverlap && loadedOverlap.error() ==
        StageWorkingSurface2DError::SlideRectanglesOverlap,
        "rejects positive-area overlap when validating a loaded full layout");

    auto edgeContact = oneSlide.withSlideRectangle(
        rectangle("slide-edge-contact", 115'000, 10'000));
    check(edgeContact && edgeContact.value().slideRectangles().size() == 2,
        "allows slide footprints to share an exact edge without area overlap");

    const auto withSecond = take(oneSlide.withSlideRectangle(
        rectangle("slide-2", 150'000, 10'000)));
    auto overlappingReplacement = withSecond.replacingSlideRectangle(
        rectangle("slide-1", 100'000, 15'000));
    check(!overlappingReplacement &&
        overlappingReplacement.error() == StageWorkingSurface2DError::SlideRectanglesOverlap,
        "rejects replacing a slide with a footprint overlapping another slide");

    const auto removed = take(oneSlide.withoutSlideRectangle(id<SlideId>("slide-1")));
    check(removed.slideRectangles().empty() && oneSlide.slideRectangles().size() == 1,
        "removes a slide into another immutable snapshot without changing earlier snapshots");
    auto missing = empty.withoutSlideRectangle(id<SlideId>("slide-1"));
    check(!missing && missing.error() == StageWorkingSurface2DError::SlideNotFound,
        "rejects removal of a slide that is not on the canvas");
}

void xyReferenceInvalidationPreservesIndependentFocusReference() {
    const auto stage = stageSpecification();
    const auto surface = take(StageWorkingSurface2D::createEmpty(
        stage, id<StageAxisId>("x"), id<StageAxisId>("y"), {0,0},
        take(StageTravelRange2D::create(200'000, 60'000))));
    auto position = take(StagePosition3D::unknown(
        surface, stage, id<StageAxisId>("z-focus")));
    position = take(position.afterCalibratedReference(
        id<StageAxisId>("z-focus"), 12'500.0, 2.0));

    const auto xyInvalidated = position.invalidateXYReference();
    check(!xyInvalidated.x().positionIsUsable() &&
        !xyInvalidated.y().positionIsUsable(),
        "an XY operation fault invalidates both in-plane position references");
    check(xyInvalidated.z().positionIsUsable() &&
        xyInvalidated.z().position() &&
        xyInvalidated.z().position()->value() == 12'500.0 &&
        xyInvalidated.z().evidence() == StagePositionEvidence::CalibratedEstimate,
        "an XY operation fault preserves the independent focus-Z calibration");

    const auto allInvalidated = position.invalidateMachineReference();
    check(!allInvalidated.x().positionIsUsable() &&
        !allInvalidated.y().positionIsUsable() &&
        !allInvalidated.z().positionIsUsable(),
        "a whole-device feedback-loss event still invalidates every machine axis");
}

void zFocusIsIndependentFromSlidePlane() {
    const auto stage = stageSpecification();
    const auto surface = workingSurface(stage);
    auto position = take(StagePosition3D::unknown(
        surface, stage, id<StageAxisId>("z-focus")));

    position = take(position.afterVerifiedHomingObservation(
        observation("x", 10'000, 0.5), ObservationSource::HardwareReadback));
    position = take(position.afterVerifiedHomingObservation(
        observation("y", 5'000, 0.75), ObservationSource::HardwareReadback));

    auto slidePosition = take(position.positionOnSlide(surface, id<SlideId>("slide-1")));
    check(std::abs(slidePosition.position.xMicrometers - 10'000) < 1e-9 &&
        std::abs(slidePosition.position.yMicrometers - 5'000) < 1e-9,
        "publishes valid slide XY after X and Y reference");
    check(!position.z().positionIsUsable(),
        "unreferenced microscope focus Z does not invalidate slide XY mapping");

    position = take(position.afterCalibratedReference(
        id<StageAxisId>("z-focus"), 5'000, 2.0));
    check(position.z().positionIsUsable() &&
        position.z().evidence() == StagePositionEvidence::CommandDerivedEstimate &&
        position.z().position()->value() == 5'000,
        "stores a no-endstop focus reference as an estimated independent Z coordinate");
}

void physicalEndstopStillUsesItsSwitch() {
    auto z = axis("z-switch", -5'000, 5'000);
    auto spec = take(StageAxisReferenceSpecification::create(
        z, StageAxisReferenceMethod::PhysicalEndstop, AxisLimitEnd::Negative,
        StageAxisOvertravelCapability::NotSupported, 1.0, 0.5));
    auto state = StageAxisReferenceState::create(spec);
    auto inactive = take(AxisLimitObservation::create(
        id<StageAxisId>("z-switch"), AxisLimitEnd::Negative, false));
    auto rejected = state.recordPhysicalEndstop(inactive);
    check(!rejected && rejected.error() == StageAxisReferenceError::EndstopNotActive,
        "does not reference from an inactive physical endstop");
    auto active = take(AxisLimitObservation::create(
        id<StageAxisId>("z-switch"), AxisLimitEnd::Negative, true));
    auto result = take(state.recordPhysicalEndstop(active));
    check(result.status == StageAxisReferenceStatus::ReferencedPhysicalEndstop &&
        result.referencePosition == std::optional<double>(-5'000) &&
        result.evidence == StageAxisReferenceEvidence::PhysicalLimitSwitch,
        "references the configured travel boundary from the active switch");
}

void zWithoutEndstopUsesSafeFullRangeSweepFromUnknownPosition() {
    auto z = axis("z-focus", -5'000, 5'000, AxisPositionFeedbackCapability::None);
    auto unsafe = StageAxisReferenceSpecification::create(
        z, StageAxisReferenceMethod::FullRangeSweep, AxisLimitEnd::Positive,
        StageAxisOvertravelCapability::NotSupported, 3.0, 0.5);
    check(!unsafe && unsafe.error() == StageAxisReferenceError::UnsafeOvertravelUnavailable,
        "rejects full-range sweeping unless continuous knob rotation is mechanically safe");

    auto spec = take(StageAxisReferenceSpecification::create(
        z, StageAxisReferenceMethod::FullRangeSweep, AxisLimitEnd::Positive,
        StageAxisOvertravelCapability::ContinuousKnobRotationWithoutClash, 3.0, 0.5));
    check(spec.fullTravelDistance() == 10'000 &&
        spec.sweepDisplacement() == 10'000 &&
        spec.referencePosition() == 5'000,
        "defines one complete calibrated Z travel in the direction of the selected upper boundary");

    auto state = StageAxisReferenceState::create(spec);
    auto begun = take(state.beginFullRangeSweep());
    check(begun.status == StageAxisReferenceStatus::SweepingFullRange &&
        begun.sweepDisplacement == std::optional<double>(10'000) &&
        !begun.referencePosition,
        "begins the sweep without requiring a known starting Z coordinate");

    auto wrongDirection = take(AxisDisplacement::create(
        id<StageAxisId>("z-focus"), -10'000, AxisCoordinateUnit::Micrometers));
    auto wrong = state.completeCalibratedFullRangeSweep(wrongDirection, 2.0);
    check(!wrong && wrong.error() == StageAxisReferenceError::SweepDirectionMismatch,
        "rejects completed travel in the wrong direction");

    auto shortTravel = take(AxisDisplacement::create(
        id<StageAxisId>("z-focus"), 9'000, AxisCoordinateUnit::Micrometers));
    auto shortResult = state.completeCalibratedFullRangeSweep(shortTravel, 2.0);
    check(!shortResult && shortResult.error() == StageAxisReferenceError::SweepDistanceInsufficient,
        "rejects a sweep shorter than the configured full Z travel");

    auto fullTravel = take(AxisDisplacement::create(
        id<StageAxisId>("z-focus"), 10'000, AxisCoordinateUnit::Micrometers));
    auto completed = take(state.completeCalibratedFullRangeSweep(fullTravel, 2.0));
    check(completed.status == StageAxisReferenceStatus::ReferencedEstimatedFullRangeSweep &&
        completed.referencePosition == std::optional<double>(5'000) &&
        completed.evidence == StageAxisReferenceEvidence::CalibratedCommandFullRangeSweep,
        "assigns the top boundary after successful calibrated full-range movement while preserving estimated evidence");

    state.invalidate();
    auto negativeSpec = take(StageAxisReferenceSpecification::create(
        z, StageAxisReferenceMethod::FullRangeSweep, AxisLimitEnd::Negative,
        StageAxisOvertravelCapability::ContinuousKnobRotationWithoutClash, 3.0, 0.5));
    auto negativeState = StageAxisReferenceState::create(negativeSpec);
    auto negativeBegin = take(negativeState.beginFullRangeSweep());
    check(negativeBegin.sweepDisplacement == std::optional<double>(-10'000),
        "sweeps in the opposite direction when the lower/bottom boundary is selected");
}

void measuredFullRangeSweepCanUseIndependentFeedback() {
    auto z = axis("z-encoder", -5'000, 5'000);
    auto spec = take(StageAxisReferenceSpecification::create(
        z, StageAxisReferenceMethod::FullRangeSweep, AxisLimitEnd::Positive,
        StageAxisOvertravelCapability::ContinuousKnobRotationWithoutClash, 1.0, 0.5));
    auto state = StageAxisReferenceState::create(spec);
    take(state.beginFullRangeSweep());
    auto end = observation("z-encoder", 5'000, 0.25);
    auto result = take(state.completeMeasuredFullRangeSweep(end));
    check(result.status == StageAxisReferenceStatus::ReferencedMeasuredFullRangeSweep &&
        result.evidence == StageAxisReferenceEvidence::EncoderFullRangeSweep,
        "uses an independent encoder observation as measured full-range reference evidence");
}
}

int main() {
    cadRectanglesAreXYOnlyAndVariableCount();
    emptyCanvasUsesUserOriginAndTravelRange();
    emptyCanvasSupportsImmutableSlideRectangleEdits();
    xyReferenceInvalidationPreservesIndependentFocusReference();
    zFocusIsIndependentFromSlidePlane();
    physicalEndstopStillUsesItsSwitch();
    zWithoutEndstopUsesSafeFullRangeSweepFromUnknownPosition();
    measuredFullRangeSweepCanUseIndependentFeedback();
    if (failures != 0) return 1;
    std::cout << "Stage XY working-surface and microscope Z-reference tests passed\n";
    return 0;
}
