#include "microscope/application/motion/StagePosition3DFeedbackReducer.h"
#include "microscope/application/runtime/RuntimeWakeSignal.h"
#include "microscope/core/stage/StageWorkingSurface2D.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using namespace microscope;
int failures = 0;
void check(bool ok, const char* message) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
template<class T, class E> T take(core::Result<T,E> result) {
    if (!result) throw std::runtime_error("XYZ reducer test fixture rejected");
    return std::move(result).value();
}
template<class T> T id(const char* value) { return take(T::create(value)); }

core::AxisSpecification axis(const char* name, double lo, double hi) {
    return take(core::AxisSpecification::create(id<core::StageAxisId>(name),
        core::AxisMotionType::Linear, core::AxisCoordinateUnit::Micrometers,
        take(core::AxisOperatingRange::create(lo, hi, core::AxisCoordinateUnit::Micrometers)),
        take(core::PositiveAxisResolution::create(0.1)), std::nullopt, std::nullopt,
        std::nullopt, core::AxisPositionFeedbackCapability::Encoder,
        core::AxisMeasurementLocation::Axis));
}
core::StageSpecification stageSpecification() {
    return take(core::StageSpecification::create(id<core::EquipmentComponentId>("stage"), {
        axis("x", -10'000, 410'000), axis("y", -10'000, 70'000),
        axis("z", -5'000, 5'000)
    }));
}
core::StageWorkingSurface2D surface(const core::StageSpecification& spec) {
    auto makeSlide = [](const char* name, double x, double y) {
        const auto revision = std::string(name) + "-v1";
        return take(core::SlideRectangleDefinition2D::create(id<core::SlideId>(name),
            id<core::CalibrationRevisionId>(revision.c_str()), {x,y}, {x+1,y+1},
            core::SlideRectangleLengthAxis::StageX, 75'000, 25'000));
    };
    core::StageWorkingSurface2D::SlideRectangles rectangles{
        makeSlide("slide-1",0,0), makeSlide("slide-2",80'000,0),
        makeSlide("slide-3",160'000,30'000)};
    return take(core::StageWorkingSurface2D::create(spec,
        id<core::StageAxisId>("x"), id<core::StageAxisId>("y"),
        {0,0}, {250'000,60'000}, std::move(rectangles)));
}
core::AxisPositionObservation observation(const char* name,double value,double uncertainty,
    core::StagePositionEvidence evidence) {
    return take(core::AxisPositionObservation::create(
        take(core::AxisPosition::create(id<core::StageAxisId>(name),value,
            core::AxisCoordinateUnit::Micrometers)), uncertainty, evidence));
}
core::FeedbackEnvelope<core::StageMotionFeedbackPayload> positionFeedback(
    const core::RuntimeDeviceCorrelation& device,const char* axis,double value) {
    auto payload=core::StageMotionFeedbackPayload{
        observation(axis,value,0.2,core::StagePositionEvidence::EncoderMeasured)};
    auto metadata=take(core::FeedbackMetadata::create({device,std::nullopt},
        core::FeedbackKind::DeviceStateObserved,core::ObservationSource::HardwareReadback));
    return core::FeedbackEnvelope<core::StageMotionFeedbackPayload>::create(
        metadata,std::move(payload));
}
core::StagePosition3D referencedPosition(const core::StageWorkingSurface2D& working,
    const core::StageSpecification& stage) {
    auto position=take(core::StagePosition3D::unknown(
        working,stage,id<core::StageAxisId>("z")));
    position=take(position.afterVerifiedHomingObservation(
        observation("x",10'000,0.5,core::StagePositionEvidence::EncoderMeasured),
        core::ObservationSource::HardwareReadback));
    position=take(position.afterVerifiedHomingObservation(
        observation("y",5'000,0.5,core::StagePositionEvidence::EncoderMeasured),
        core::ObservationSource::HardwareReadback));
    // Z deliberately remains unreferenced; slide XY must still be available.
    return position;
}
}

int main() {
    const auto stage=stageSpecification();
    const auto working=surface(stage);
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

    auto state=referencedPosition(working,stage);
    check(!state.z().positionIsUsable(),
        "focus Z starts independently unreferenced");
    auto projected=take(state.positionOnSlide(working,id<core::SlideId>("slide-1")));
    check(std::abs(projected.position.xMicrometers-10'000)<1e-9 &&
        std::abs(projected.position.yMicrometers-5'000)<1e-9,
        "slide-relative XY projection succeeds while focus Z remains unreferenced");

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
    check(reduced.disposition==application::motion::StagePosition3DFeedbackDisposition::PositionObservationApplied &&
        std::abs(reduced.position.z().position()->value()-750)<1e-9,
        "fresh focus-axis telemetry updates the independent Z coordinate");
    state=std::move(reduced.position);
    auto newSlidePosition=take(state.positionOnSlide(working,id<core::SlideId>("slide-1")));
    check(std::abs(newSlidePosition.position.xMicrometers-10'000)<1e-9 &&
        std::abs(newSlidePosition.position.yMicrometers-5'000)<1e-9,
        "focus Z telemetry does not change slide-relative XY coordinates");

    auto outside=submitAndProcess(positionFeedback(device,"other",1));
    auto ignored=take(application::motion::StagePosition3DFeedbackReducer::reduce(
        state,outside,runtime->feedbackLossLatched()));
    check(ignored.disposition==application::motion::StagePosition3DFeedbackDisposition::IgnoredAxisOutsideVolume,
        "ignores telemetry for axes outside this microscope's X/Y/Z position state");

    auto first=positionFeedback(device,"x",20'000);
    auto overflow=positionFeedback(device,"y",6'000);
    check(runtime->feedbackIngress().submit(std::move(first))==core::FeedbackSubmitStatus::Enqueued,
        "queues a first receipt before feedback overflow");
    check(runtime->feedbackIngress().submit(std::move(overflow))==core::FeedbackSubmitStatus::QueueFull &&
        runtime->feedbackLossLatched(),"overflow latches uncertain feedback ordering");
    auto preserved=take(runtime->processNextFeedback());
    check(preserved.has_value(),"preserves an already queued receipt for ordered draining");
    auto invalidated=take(application::motion::StagePosition3DFeedbackReducer::reduce(
        state,*preserved,runtime->feedbackLossLatched()));
    check(invalidated.disposition==application::motion::StagePosition3DFeedbackDisposition::InvalidatedAfterFeedbackLoss &&
        !invalidated.position.x().positionIsUsable() &&
        !invalidated.position.y().positionIsUsable() &&
        !invalidated.position.z().positionIsUsable(),
        "feedback loss invalidates the complete three-axis machine reference");
    if(failures) return 1;
    std::cout<<"3D machine position and independent XY slide projection reducer tests passed\n";
    return 0;
}
