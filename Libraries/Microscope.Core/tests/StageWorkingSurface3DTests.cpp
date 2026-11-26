#include "microscope/core/stage/StageAxisReference.h"
#include "microscope/core/stage/StageWorkingSurface3D.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using namespace microscope::core;
int failures=0;
void check(bool ok,const char* message){if(!ok){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}
template<class T,class E>T take(Result<T,E> r){if(!r)throw std::runtime_error("invalid test fixture");return std::move(r).value();}
template<class T>T id(const char* v){return take(T::create(v));}
SlidePlacement3D placement(const char* name,double x,double y,double z,
    Direction3D ax={1,0,0},Direction3D ay={0,1,0},Direction3D az={0,0,1}) {
    auto g=take(SlideGeometry3D::create(75'000,25'000,1'000));
    const auto revision=std::string(name)+"-registration-v1";
    return take(SlidePlacement3D::create(id<SlideId>(name),id<CalibrationRevisionId>(revision.c_str()),g,{x,y,z},ax,ay,az));
}
AxisSpecification zAxis() {
    auto r=take(AxisOperatingRange::create(-5'000,5'000,AxisCoordinateUnit::Micrometers));
    auto resolution=take(PositiveAxisResolution::create(0.1));
    return take(AxisSpecification::create(id<StageAxisId>("z"),AxisMotionType::Linear,
        AxisCoordinateUnit::Micrometers,r,resolution,std::nullopt,std::nullopt,std::nullopt,
        AxisPositionFeedbackCapability::None,std::nullopt));
}
void fixedTenSlideMap() {
    StageWorkingSurface3D::SlidePlacements slides{
        placement("s01",0,0,0),placement("s02",80'000,0,0),placement("s03",160'000,0,0),
        placement("s04",240'000,0,0),placement("s05",320'000,0,0),
        placement("s06",0,30'000,0),placement("s07",80'000,30'000,0),placement("s08",160'000,30'000,0),
        placement("s09",240'000,30'000,0),placement("s10",320'000,30'000,0)};
    auto surface=take(StageWorkingSurface3D::create(id<EquipmentComponentId>("stage"),
        {0,0,0},{400'000,60'000,2'000},std::move(slides)));
    static_assert(StageWorkingSurfaceSlideCount==10,"fixed ten-slide mechanical layout");
    check(surface.slidePlacements().size()==10&&surface.placementFor(id<SlideId>("s10"))!=nullptr,
        "stores and resolves ten fixed physical slide placements");
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
void rigidThreeDimensionalMapping() {
    auto g=take(SlideGeometry3D::create(75'000,25'000,1'000));
    auto rotated=take(SlidePlacement3D::create(id<SlideId>("rotated"),id<CalibrationRevisionId>("r1"),g,
        {100'000,100'000,0},{0,1,0},{-1,0,0},{0,0,1}));
    auto stage=take(rotated.toStage({10'000,5'000,250}));
    auto local=take(rotated.toSlide(stage));
    check(std::abs(local.xMicrometers-10'000)<1e-9&&std::abs(local.yMicrometers-5'000)<1e-9&&
        std::abs(local.zMicrometers-250)<1e-9,"round-trips XYZ coordinates through a rotated slide pose");
    auto invalid=SlidePlacement3D::create(id<SlideId>("bad"),id<CalibrationRevisionId>("bad1"),g,
        {0,0,0},{2,0,0},{0,1,0},{0,0,1});
    check(!invalid&&invalid.error()==SlidePlacement3DError::InvalidBasis,"rejects non-orthonormal slide frames");
}
void physicalEndstopReference() {
    auto range=take(AxisOperatingRange::create(-5'000,5'000,AxisCoordinateUnit::Micrometers));
    auto resolution=take(PositiveAxisResolution::create(0.1));
    auto axis=take(AxisSpecification::create(id<StageAxisId>("x"),AxisMotionType::Linear,
        AxisCoordinateUnit::Micrometers,range,resolution,std::nullopt,std::nullopt,std::nullopt,
        AxisPositionFeedbackCapability::Encoder,AxisMeasurementLocation::Axis));
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
void zFullStrokeReference() {
    auto spec=take(StageAxisReferenceSpecification::create(zAxis(),StageAxisReferenceMethod::FullStrokeTraversal,
        AxisLimitEnd::Positive,3.0,0.5));
    check(spec.fullStrokeDistance()==10'000&&spec.startingPosition()==-5'000&&spec.referencePosition()==5'000,
        "defines the software endstop at the opposite end of the configured full travel");
    auto state=StageAxisReferenceState::create(spec);
    auto wrong=take(AxisPosition::create(id<StageAxisId>("z"),-4'000,AxisCoordinateUnit::Micrometers));
    auto startRejected=state.beginFullStrokeTraversal(wrong,StagePositionEvidence::ExternalMeasured,0.5);
    check(!startRejected&&startRejected.error()==StageAxisReferenceError::WrongStartingBoundary,
        "requires a known opposite boundary before full-stroke traversal");
    auto start=take(AxisPosition::create(id<StageAxisId>("z"),-5'000,AxisCoordinateUnit::Micrometers));
    auto begun=take(state.beginFullStrokeTraversal(start,StagePositionEvidence::ExternalMeasured,0.5));
    check(begun.status==StageAxisReferenceStatus::TraversingFullStroke,
        "starts the full-stroke procedure from a measured datum");
    auto end=take(AxisPosition::create(id<StageAxisId>("z"),5'000,AxisCoordinateUnit::Micrometers));
    auto estimated=take(state.completeCalibratedFullStrokeTraversal(end,2.0));
    check(estimated.status==StageAxisReferenceStatus::ReferencedEstimatedFullStroke&&
        estimated.evidence==StageAxisReferenceEvidence::CalibratedCommandFullStroke,
        "records an open-loop virtual endstop as an estimate, not a physical measurement");

    state.invalidate();
    take(state.beginFullStrokeTraversal(start,StagePositionEvidence::ExternalMeasured,0.5));
    auto observed=take(AxisPositionObservation::create(end,0.25,StagePositionEvidence::EncoderMeasured));
    auto measured=take(state.completeMeasuredFullStrokeTraversal(observed));
    check(measured.status==StageAxisReferenceStatus::ReferencedMeasuredFullStroke&&
        measured.evidence==StageAxisReferenceEvidence::EncoderFullStroke,
        "records an encoder-verified software endstop with measured evidence");
    state.abortFullStrokeTraversal();
    check(state.snapshot().status==StageAxisReferenceStatus::ReferencedMeasuredFullStroke,
        "does not invalidate an already completed reference on traversal abort");
}
}
int main(){
    fixedTenSlideMap();rigidThreeDimensionalMapping();physicalEndstopReference();zFullStrokeReference();
    if(failures)return 1;
    std::cout<<"3D working-surface and axis-reference tests passed\n";
    return 0;
}
