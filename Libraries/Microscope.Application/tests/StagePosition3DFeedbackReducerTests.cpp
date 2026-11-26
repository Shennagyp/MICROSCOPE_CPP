#include "microscope/application/motion/StagePosition3DFeedbackReducer.h"
#include "microscope/application/runtime/RuntimeWakeSignal.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace{
using namespace microscope;
int failures=0;
void check(bool ok,const char* message){if(!ok){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}
template<class T,class E>T take(core::Result<T,E> r){if(!r)throw std::runtime_error("3D feedback fixture rejected");return std::move(r).value();}
template<class T>T id(const char*v){return take(T::create(v));}
core::AxisSpecification axis(const char* name,double lo,double hi){
    return take(core::AxisSpecification::create(id<core::StageAxisId>(name),core::AxisMotionType::Linear,
        core::AxisCoordinateUnit::Micrometers,
        take(core::AxisOperatingRange::create(lo,hi,core::AxisCoordinateUnit::Micrometers)),
        take(core::PositiveAxisResolution::create(0.1)),std::nullopt,std::nullopt,std::nullopt,
        core::AxisPositionFeedbackCapability::Encoder,core::AxisMeasurementLocation::Axis));
}
core::StageWorkingSurface3D surface(){
    auto spec=take(core::StageSpecification::create(id<core::EquipmentComponentId>("stage"),
        {axis("x",-10'000,410'000),axis("y",-10'000,70'000),axis("z",-100,2'000)}));
    auto makeSlide=[](const char* name,double x,double y){
        auto geometry=take(core::SlideGeometry3D::create(75'000,25'000,1'000));
        const auto revision=std::string(name)+"-v1";
        return take(core::SlidePlacement3D::create(id<core::SlideId>(name),id<core::CalibrationRevisionId>(revision.c_str()),
            geometry,{x,y,0},{1,0,0},{0,1,0},{0,0,1},0.5));
    };
    core::StageWorkingSurface3D::SlidePlacements slides{
        makeSlide("s01",0,0),makeSlide("s02",80'000,0),makeSlide("s03",160'000,0),
        makeSlide("s04",240'000,0),makeSlide("s05",320'000,0),makeSlide("s06",0,30'000),
        makeSlide("s07",80'000,30'000),makeSlide("s08",160'000,30'000),
        makeSlide("s09",240'000,30'000),makeSlide("s10",320'000,30'000)};
    return take(core::StageWorkingSurface3D::create(spec,id<core::StageAxisId>("x"),
        id<core::StageAxisId>("y"),id<core::StageAxisId>("z"),
        {0,0,0},{400'000,60'000,2'000},std::move(slides)));
}
core::AxisPositionObservation observation(const char* axis,double value,double uncertainty,
    core::StagePositionEvidence evidence){
    return take(core::AxisPositionObservation::create(take(core::AxisPosition::create(
        id<core::StageAxisId>(axis),value,core::AxisCoordinateUnit::Micrometers)),uncertainty,evidence));
}
core::FeedbackEnvelope<core::StageMotionFeedbackPayload> positionFeedback(
    const core::RuntimeDeviceCorrelation& device,const char* axis,double value){
    auto payload=core::StageMotionFeedbackPayload{observation(axis,value,0.2,core::StagePositionEvidence::EncoderMeasured)};
    auto metadata=take(core::FeedbackMetadata::create({device,std::nullopt},core::FeedbackKind::DeviceStateObserved,
        core::ObservationSource::HardwareReadback));
    return core::FeedbackEnvelope<core::StageMotionFeedbackPayload>::create(metadata,std::move(payload));
}
core::StagePosition3D referencedPosition(const core::StageWorkingSurface3D& s){
    auto p=take(core::StagePosition3D::unknown(s));
    p=take(p.afterVerifiedHomingObservation(observation("x",10'000,0.5,core::StagePositionEvidence::EncoderMeasured),core::ObservationSource::HardwareReadback));
    p=take(p.afterVerifiedHomingObservation(observation("y",5'000,0.5,core::StagePositionEvidence::EncoderMeasured),core::ObservationSource::HardwareReadback));
    p=take(p.afterVerifiedHomingObservation(observation("z",500,0.5,core::StagePositionEvidence::EncoderMeasured),core::ObservationSource::HardwareReadback));
    return p;
}
}
int main(){
    const auto s=surface();
    const auto microscopeId=id<core::MicroscopeId>("scope");
    const auto runtimeId=id<core::RuntimeId>("stage-runtime");
    const auto generation=take(core::RuntimeGeneration::create(1));
    const core::RuntimeDeviceCorrelation device{microscopeId,runtimeId,generation,
        take(core::DeviceSessionGeneration::create(1)),id<core::DeviceId>("stage-device")};
    application::runtime::RuntimeWakeSignal wake;
    auto runtime=take(core::SerializedOperationRuntime<core::StageMotionFeedbackPayload>::create(
        microscopeId,runtimeId,generation,1,wake));
    take(runtime->bindOwnerThread());
    take(runtime->openDeviceSession(device));

    auto state=referencedPosition(s);
    auto submitAndProcess=[&](core::FeedbackEnvelope<core::StageMotionFeedbackPayload> event){
        if(runtime->feedbackIngress().submit(std::move(event))!=core::FeedbackSubmitStatus::Enqueued)
            throw std::runtime_error("feedback did not enqueue");
        auto next=take(runtime->processNextFeedback());
        if(!next)throw std::runtime_error("feedback queue unexpectedly empty");
        return std::move(*next);
    };
    auto update=submitAndProcess(positionFeedback(device,"z",750));
    auto reduced=take(application::motion::StagePosition3DFeedbackReducer::reduce(
        state,update,runtime->feedbackLossLatched()));
    check(reduced.disposition==application::motion::StagePosition3DFeedbackDisposition::PositionObservationApplied&&
        std::abs(reduced.position.z().position()->value()-750)<1e-9,
        "fresh Z encoder telemetry updates the three-axis position aggregate");
    state=std::move(reduced.position);
    auto projected=take(state.positionOnSlide(s,id<core::SlideId>("s01")));
    check(std::abs(projected.position.zMicrometers-750)<1e-9&&
        projected.zEvidence==core::StagePositionEvidence::EncoderMeasured,
        "updated actual machine position is projected into the selected physical slide");

    auto outside=submitAndProcess(positionFeedback(device,"other",1));
    auto ignored=take(application::motion::StagePosition3DFeedbackReducer::reduce(
        state,outside,runtime->feedbackLossLatched()));
    check(ignored.disposition==application::motion::StagePosition3DFeedbackDisposition::IgnoredAxisOutsideVolume,
        "ignores telemetry from axes outside the installed XYZ working volume");

    auto first=positionFeedback(device,"x",20'000);
    auto overflow=positionFeedback(device,"y",6'000);
    check(runtime->feedbackIngress().submit(std::move(first))==core::FeedbackSubmitStatus::Enqueued,
        "queues a first receipt before feedback overflow");
    check(runtime->feedbackIngress().submit(std::move(overflow))==core::FeedbackSubmitStatus::QueueFull&&
        runtime->feedbackLossLatched(),"overflow latches uncertain feedback ordering");
    auto preserved=take(runtime->processNextFeedback());
    check(preserved.has_value(),"preserves an already queued receipt for ordered draining");
    auto invalidated=take(application::motion::StagePosition3DFeedbackReducer::reduce(
        state,*preserved,runtime->feedbackLossLatched()));
    check(invalidated.disposition==application::motion::StagePosition3DFeedbackDisposition::InvalidatedAfterFeedbackLoss&&
        !invalidated.position.x().positionIsUsable()&&!invalidated.position.y().positionIsUsable()&&
        !invalidated.position.z().positionIsUsable(),
        "feedback loss invalidates the full XYZ absolute reference, not just the axis whose update was dropped");

    if(failures)return 1;
    std::cout<<"All 3D stage-position feedback reducer tests passed\n";
    return 0;
}
