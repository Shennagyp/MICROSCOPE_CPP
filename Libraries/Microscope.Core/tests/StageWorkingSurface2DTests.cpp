#include "microscope/core/stage/StageAxisReference.h"
#include "microscope/core/stage/StagePosition3D.h"
#include "microscope/core/stage/StageWorkingSurface2D.h"
#include <cmath>
#include <iostream>
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
        slide3->solvedOppositeEndpoint().yMicrometers == 20'000,
        "the dimensions solve the opposite corner from the initial handle");

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
    zFocusIsIndependentFromSlidePlane();
    physicalEndstopStillUsesItsSwitch();
    zWithoutEndstopUsesSafeFullRangeSweepFromUnknownPosition();
    measuredFullRangeSweepCanUseIndependentFeedback();
    if (failures != 0) return 1;
    std::cout << "Stage XY working-surface and microscope Z-reference tests passed\n";
    return 0;
}
