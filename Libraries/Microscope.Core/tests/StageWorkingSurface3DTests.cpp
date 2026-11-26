#include "microscope/core/stage/StageAxisReference.h"
#include "microscope/core/stage/StagePosition3D.h"
#include "microscope/core/stage/StageWorkingSurface3D.h"
#include "microscope/core/stage/SlideRectangleDefinition2D.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace microscope::core;
int failures=0;
void check(bool ok,const char* message){if(!ok){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}
template<class T,class E>T take(Result<T,E> r){if(!r)throw std::runtime_error("invalid test fixture");return std::move(r).value();}
template<class T>T id(const char* v){return take(T::create(v));}

AxisSpecification linearAxis(const char* name,double lo,double hi,
    AxisPositionFeedbackCapability feedback=AxisPositionFeedbackCapability::Encoder){
    auto range=take(AxisOperatingRange::create(lo,hi,AxisCoordinateUnit::Micrometers));
    auto resolution=take(PositiveAxisResolution::create(0.1));
    auto location=feedback==AxisPositionFeedbackCapability::None
        ? std::optional<AxisMeasurementLocation>{}
        : std::optional<AxisMeasurementLocation>{AxisMeasurementLocation::Axis};
    return take(AxisSpecification::create(id<StageAxisId>(name),AxisMotionType::Linear,
        AxisCoordinateUnit::Micrometers,range,resolution,std::nullopt,std::nullopt,std::nullopt,
        feedback,location));
}
StageSpecification stageSpecification(){
    return take(StageSpecification::create(id<EquipmentComponentId>("stage"),{
        linearAxis("x",-10'000,410'000),linearAxis("y",-10'000,70'000),
        linearAxis("z",-100,2'000,AxisPositionFeedbackCapability::None)}));
}
SlideRectangleDefinition2D placement(const char* name,double x,double y,double z,double datumError=0.5){
    const auto rev=std::string(name)+"-registration-v1";
    return take(SlideRectangleDefinition2D::create(id<SlideId>(name),id<CalibrationRevisionId>(rev.c_str()),
        {x,y},{x+75'000,y+25'000},SlideRectangleLengthAxis::StageX,
        75'000,25'000,z,1'000,0.01,datumError));
}
StageWorkingSurface3D workingSurface(){
    StageWorkingSurface3D::SlideRectangles slides{
        placement("s01",0,0,0),placement("s02",80'000,0,0),placement("s03",160'000,0,0)};
    return take(StageWorkingSurface3D::create(stageSpecification(),
        id<StageAxisId>("x"),id<StageAxisId>("y"),id<StageAxisId>("z"),
        {0,0,0},{250'000,60'000,2'000},std::move(slides)));
}

void userDefinedSlideMap(){
    auto surface=workingSurface();
    check(surface.slideRectangles().size()==3&&surface.placementFor(id<SlideId>("s03"))!=nullptr,
        "stores a user-defined number of slide rectangles rather than a fixed slot count");
    check(surface.xAxisId()==id<StageAxisId>("x")&&surface.yAxisId()==id<StageAxisId>("y")&&
        surface.zAxisId()==id<StageAxisId>("z"),"binds the working volume to explicit installed X/Y/Z axis identities");
    auto p=take(surface.toSlide(id<SlideId>("s01"),{10'000,5'000,500}));
    check(std::abs(p.position.xMicrometers-10'000)<1e-9&&std::abs(p.position.yMicrometers-5'000)<1e-9,
        "maps a stage coordinate into the selected slide frame");
    auto other=take(surface.toSlide(id<SlideId>("s02"),{90'000,5'000,500}));
    check(std::abs(other.position.xMicrometers-10'000)<1e-9&&std::abs(other.position.yMicrometers-5'000)<1e-9,
        "uses an independent transform for the adjacent physical slide");
    auto gap=take(surface.toSlide(id<SlideId>("s01"),{79'000,5'000,500}));
    check(!gap.insideSlide&&gap.insideWorkingVolume,
        "retains slide-relative coordinates when the stage is in the gap between slides");
    auto stage=take(surface.toStage(id<SlideId>("s02"),{10'000,5'000,500}));
    check(std::abs(stage.xMicrometers-90'000)<1e-9,
        "maps a slide-local point from its mechanical stop datum to the stage frame");
}

void cadRectangleConstraints(){
    auto rectangle=take(SlideRectangleDefinition2D::create(
        id<SlideId>("cad-slide"),id<CalibrationRevisionId>("cad-v1"),
        {1000,2000},{76000,27000},SlideRectangleLengthAxis::StageX,
        75000,25000,300,1000,0.01,0.4));
    check(rectangle.stopPointEndpoint().xMicrometers==1000&&
        rectangle.stopPointEndpoint().yMicrometers==2000&&
        rectangle.drawnOppositeEndpoint().xMicrometers==76000&&
        rectangle.oppositeEndpoint().xMicrometers==76000&&rectangle.oppositeEndpoint().yMicrometers==27000,
        "retains the original CAD handle and the constrained opposite corner with the first endpoint as stop datum");
    check(rectangle.constrainedLengthMicrometers()==75000&&
        rectangle.constrainedWidthMicrometers()==25000,
        "retains explicit parametric length and width constraints");
    auto mapped=take(rectangle.placement().toStage({75000,25000,1000}));
    check(std::abs(mapped.xMicrometers-76000)<1e-9&&
        std::abs(mapped.yMicrometers-27000)<1e-9&&
        std::abs(mapped.zMicrometers-1300)<1e-9,
        "derives the 3D slide transform from the constrained CAD rectangle");
    auto mismatch=SlideRectangleDefinition2D::create(
        id<SlideId>("bad-cad"),id<CalibrationRevisionId>("bad-v1"),
        {1000,2000},{76000,27000},SlideRectangleLengthAxis::StageX,
        74000,25000,300,1000,0.01);
    check(mismatch&&std::abs(mismatch.value().oppositeEndpoint().xMicrometers-75000)<1e-9,
        "solves the opposite corner from the fixed stop datum and constrained dimensions rather than locking the cursor point");
    auto lengthOnY=take(SlideRectangleDefinition2D::create(
        id<SlideId>("vertical-cad"),id<CalibrationRevisionId>("vertical-v1"),
        {10000,10000},{20000,85000},SlideRectangleLengthAxis::StageY,
        75000,10000,0,1000,0.01));
    auto opposite=take(lengthOnY.placement().toStage({75000,10000,100}));
    check(std::abs(opposite.xMicrometers-20000)<1e-9&&
        std::abs(opposite.yMicrometers-85000)<1e-9,
        "supports slide length aligned with stage Y");
}

void rigidThreeDimensionalMapping(){
    auto g=take(SlideGeometry3D::create(75'000,25'000,1'000));
    auto rotated=take(SlidePlacement3D::create(id<SlideId>("rotated"),id<CalibrationRevisionId>("r1"),g,
        {100'000,100'000,0},{0,1,0},{-1,0,0},{0,0,1},0.25));
    auto stage=take(rotated.toStage({10'000,5'000,250}));
    auto local=take(rotated.toSlide(stage));
    check(std::abs(local.xMicrometers-10'000)<1e-9&&std::abs(local.yMicrometers-5'000)<1e-9&&
        std::abs(local.zMicrometers-250)<1e-9,"round-trips XYZ coordinates through a rotated slide pose");
    auto invalid=SlidePlacement3D::create(id<SlideId>("bad"),id<CalibrationRevisionId>("bad1"),g,
        {0,0,0},{2,0,0},{0,1,0},{0,0,1});
    check(!invalid&&invalid.error()==SlidePlacement3DError::InvalidBasis,"rejects non-orthonormal slide frames");
}
void physicalEndstopReference(){
    auto axis=linearAxis("x",-5'000,5'000);
    auto spec=take(StageAxisReferenceSpecification::create(axis,StageAxisReferenceMethod::PhysicalEndstop,
        AxisLimitEnd::Negative,2.0,0.5));
    auto state=StageAxisReferenceState::create(spec);
    auto off=take(AxisLimitObservation::create(id<StageAxisId>("x"),AxisLimitEnd::Negative,false));
    auto rejected=state.recordPhysicalEndstop(off);
    check(!rejected&&rejected.error()==StageAxisReferenceError::EndstopNotActive,
        "does not reference an axis from an inactive limit input");
    auto on=take(AxisLimitObservation::create(id<StageAxisId>("x"),AxisLimitEnd::Negative,true));
    auto result=take(state.recordPhysicalEndstop(on));
    check(result.status==StageAxisReferenceStatus::ReferencedPhysicalEndstop&&
        result.referencePosition==std::optional<double>(-5'000)&&
        result.evidence==StageAxisReferenceEvidence::PhysicalLimitSwitch,
        "records the active physical endstop as the axis datum");
}
void zFullStrokeReference(){
    auto spec=take(StageAxisReferenceSpecification::create(linearAxis("z",-5'000,5'000,
        AxisPositionFeedbackCapability::None),StageAxisReferenceMethod::FullStrokeTraversal,
        AxisLimitEnd::Positive,3.0,0.5));
    check(spec.fullStrokeDistance()==10'000&&spec.startingPosition()==-5'000&&spec.referencePosition()==5'000,
        "defines the software endstop at the opposite end of the configured full travel");
    auto state=StageAxisReferenceState::create(spec);
    auto wrong=take(AxisPosition::create(id<StageAxisId>("z"),-4'000,AxisCoordinateUnit::Micrometers));
    auto rejected=state.beginFullStrokeTraversal(wrong,StagePositionEvidence::ExternalMeasured,0.5);
    check(!rejected&&rejected.error()==StageAxisReferenceError::WrongStartingBoundary,
        "requires a known opposite boundary before full-stroke traversal");
    auto start=take(AxisPosition::create(id<StageAxisId>("z"),-5'000,AxisCoordinateUnit::Micrometers));
    auto begun=take(state.beginFullStrokeTraversal(start,StagePositionEvidence::ExternalMeasured,0.5));
    check(begun.status==StageAxisReferenceStatus::TraversingFullStroke,"starts full stroke from measured datum");
    auto end=take(AxisPosition::create(id<StageAxisId>("z"),5'000,AxisCoordinateUnit::Micrometers));
    auto estimated=take(state.completeCalibratedFullStrokeTraversal(end,2.0));
    check(estimated.status==StageAxisReferenceStatus::ReferencedEstimatedFullStroke&&
        estimated.evidence==StageAxisReferenceEvidence::CalibratedCommandFullStroke,
        "records an open-loop virtual endstop as an estimate, not measured evidence");
    state.invalidate();
    take(state.beginFullStrokeTraversal(start,StagePositionEvidence::ExternalMeasured,0.5));
    auto observed=take(AxisPositionObservation::create(end,0.25,StagePositionEvidence::EncoderMeasured));
    auto measured=take(state.completeMeasuredFullStrokeTraversal(observed));
    check(measured.status==StageAxisReferenceStatus::ReferencedMeasuredFullStroke&&
        measured.evidence==StageAxisReferenceEvidence::EncoderFullStroke,
        "records an encoder-verified software endstop with measured evidence");
    state.abortFullStrokeTraversal();
    check(state.snapshot().status==StageAxisReferenceStatus::ReferencedMeasuredFullStroke,
        "does not invalidate completed reference when traversal abort is called");
}
void measuredStagePositionProjectsIntoSlideCoordinates(){
    const auto surface=workingSurface();
    auto position=take(StagePosition3D::unknown(surface));
    auto unknown=position.positionOnSlide(surface,id<SlideId>("s01"));
    check(!unknown&&unknown.error()==StagePosition3DError::PositionUnknown,
        "does not publish a slide position before all three machine axes are referenced");

    auto px=take(AxisPosition::create(id<StageAxisId>("x"),10'000,AxisCoordinateUnit::Micrometers));
    auto py=take(AxisPosition::create(id<StageAxisId>("y"),5'000,AxisCoordinateUnit::Micrometers));
    auto pz=take(AxisPosition::create(id<StageAxisId>("z"),500,AxisCoordinateUnit::Micrometers));
    auto ox=take(AxisPositionObservation::create(px,1.0,StagePositionEvidence::EncoderMeasured));
    auto oy=take(AxisPositionObservation::create(py,2.0,StagePositionEvidence::EncoderMeasured));
    auto oz=take(AxisPositionObservation::create(pz,3.0,StagePositionEvidence::EncoderMeasured));
    position=take(position.afterVerifiedHomingObservation(ox,ObservationSource::HardwareReadback));
    position=take(position.afterVerifiedHomingObservation(oy,ObservationSource::HardwareReadback));
    position=take(position.afterVerifiedHomingObservation(oz,ObservationSource::HardwareReadback));

    auto estimate=take(position.positionOnSlide(surface,id<SlideId>("s01")));
    check(std::abs(estimate.position.xMicrometers-10'000)<1e-9&&
        std::abs(estimate.position.yMicrometers-5'000)<1e-9&&
        std::abs(estimate.position.zMicrometers-500)<1e-9,
        "projects measured XYZ machine position into the selected physical slide frame");
    check(std::abs(estimate.xErrorBoundMicrometers-1.5)<1e-9&&
        std::abs(estimate.yErrorBoundMicrometers-2.5)<1e-9&&
        std::abs(estimate.zErrorBoundMicrometers-3.5)<1e-9,
        "propagates independent axis uncertainties and slide stop-point datum uncertainty");
    check(estimate.insideSlide&&estimate.insideWorkingVolume&&
        estimate.xEvidence==StagePositionEvidence::EncoderMeasured,
        "publishes geometry membership and evidence alongside slide-relative coordinates");

    auto pxGap=take(AxisPosition::create(id<StageAxisId>("x"),79'000,AxisCoordinateUnit::Micrometers));
    auto oxGap=take(AxisPositionObservation::create(pxGap,1.0,StagePositionEvidence::EncoderMeasured));
    position=take(position.afterPositionObservation(oxGap,ObservationSource::HardwareReadback));
    auto gap=take(position.positionOnSlide(surface,id<SlideId>("s01")));
    check(!gap.insideSlide&&gap.insideWorkingVolume&&gap.position.xMicrometers>75'000,
        "continues tracking slide-relative coordinates while physically between slide footprints");
}
}
int main(){
    userDefinedSlideMap();cadRectangleConstraints();rigidThreeDimensionalMapping();physicalEndstopReference();
    zFullStrokeReference();measuredStagePositionProjectsIntoSlideCoordinates();
    if(failures)return 1;
    std::cout<<"3D working-surface and axis-reference tests passed\n";
    return 0;
}
