#include "microscope/core/motion/StagePositionState.h"
#include "microscope/core/navigation/StageNavigationPlanner.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using namespace microscope::core;
template<class T,class E>T take(Result<T,E> result) {
    if(!result)throw std::runtime_error("stage position fixture rejected");
    return std::move(result).value();
}
template<class T>T id(const char* value){return take(T::create(value));}
AxisSpecification axis(const char* name) {
    return take(AxisSpecification::create(
        id<StageAxisId>(name),AxisMotionType::Linear,AxisCoordinateUnit::Micrometers,
        take(AxisOperatingRange::create(-100000,150000,AxisCoordinateUnit::Micrometers)),
        take(PositiveAxisResolution::create(0.1)),std::nullopt,std::nullopt,std::nullopt,
        AxisPositionFeedbackCapability::DeviceReported));
}
SlideRegistration registration(const char* slide,DualSlideSlot slot,double originX) {
    auto geometry=take(SlideGeometry::create(75000,25000));
    const auto source=ObservationSource::HardwareReadback;
    auto tl=take(SlideCornerMeasurement::create(SlideCorner::TopLeft,{originX,20000},source,0.25));
    auto tr=take(SlideCornerMeasurement::create(SlideCorner::TopRight,{originX+75000,20000},source,0.25));
    auto br=take(SlideCornerMeasurement::create(SlideCorner::BottomRight,{originX+75000,45000},source,0.25));
    auto bl=take(SlideCornerMeasurement::create(SlideCorner::BottomLeft,{originX,45000},source,0.25));
    return take(SlideRegistration::create(
        id<SlideId>(slide),id<CalibrationRevisionId>("position-test-registration"),slot,
        geometry,{tl,tr,br,bl},0.5));
}
AxisPositionObservation observation(const char* axis,double position,double uncertainty,
                                    StagePositionEvidence evidence) {
    return take(AxisPositionObservation::create(
        take(AxisPosition::create(id<StageAxisId>(axis),position,AxisCoordinateUnit::Micrometers)),
        uncertainty,evidence));
}
}

int main() {
    int failures=0;
    const auto check=[&](bool value,const char* message) {
        if(!value){++failures;std::cerr<<"FAIL: "<<message<<'\n';}
    };

    auto spec=take(DualSlideCarrierSpecification::create(
        take(StageSpecification::create(id<EquipmentComponentId>("stage"),{axis("x"),axis("y")})),
        id<StageAxisId>("x"),id<StageAxisId>("y"),
        CarrierHorizontalDirection::PositiveStageXFromLeftToRight));
    auto state=take(StagePositionState::create(spec));
    check(!state.horizontal().positionIsUsable()&&!state.transverse().positionIsUsable(),
          "new stage position aggregate starts with both axes unreferenced");

    auto hx=observation("x",0,0.5,StagePositionEvidence::ControllerReported);
    auto hy=observation("y",0,0.5,StagePositionEvidence::ControllerReported);
    state=take(state.afterVerifiedHomingObservation(hx,ObservationSource::SdkStatus));
    state=take(state.afterVerifiedHomingObservation(hy,ObservationSource::SdkStatus));
    check(state.horizontal().positionIsUsable()&&state.transverse().positionIsUsable(),
          "verified homing establishes the two in-plane references independently");

    auto moved=take(state.afterCommandedDisplacement(
        take(AxisDisplacement::create(id<StageAxisId>("x"),25000,AxisCoordinateUnit::Micrometers)),
        3.0));
    check(moved.horizontal().position()->value()==25000&&
          moved.horizontal().accumulatedPositionErrorBound().value()==3.5,
          "completed movement updates one axis and accumulates its calibrated error");
    check(moved.transverse().position()->value()==0,
          "movement of one axis preserves the other axis state");
    auto absoluteX=take(moved.afterCommandedPosition(
        take(AxisPosition::create(id<StageAxisId>("x"),30000,AxisCoordinateUnit::Micrometers)),
        2.0));
    check(absoluteX.horizontal().position()->value()==30000&&
          absoluteX.horizontal().accumulatedPositionErrorBound().value()==5.5,
          "absolute MoveAxesTo completion updates the target coordinate and accumulates its error");
    check(absoluteX.transverse().position()->value()==0,
          "absolute movement of X preserves the Y state");

    auto encoder=observation("x",25010,0.2,StagePositionEvidence::EncoderMeasured);
    auto measured=take(moved.afterPositionObservation(encoder,ObservationSource::HardwareReadback));
    check(measured.horizontal().position()->value()==25010&&
          measured.horizontal().accumulatedPositionErrorBound().value()==0.2,
          "independent encoder evidence replaces the open-loop axis error");

    auto badAxis=observation("z",1,0.1,StagePositionEvidence::EncoderMeasured);
    auto rejected=measured.afterPositionObservation(badAxis,ObservationSource::HardwareReadback);
    check(!rejected&&rejected.error()==StageAxisPositionStateError::AxisMismatch,
          "position feedback for an axis outside the stage is rejected");

    auto invalidated=measured.invalidateMachineReference();
    check(!invalidated.horizontal().positionIsUsable()&&
          !invalidated.transverse().positionIsUsable(),
          "reference invalidation applies to the complete stage plane");

    auto left=registration("left-slide",DualSlideSlot::Left,5000);
    auto right=registration("right-slide",DualSlideSlot::Right,95000);
    auto carrier=take(DualSlideCarrierState::create(
        spec,
        take(SlideSlotState::create(DualSlideSlot::Left,id<SlideId>("left-slide"),left)),
        take(SlideSlotState::create(DualSlideSlot::Right,id<SlideId>("right-slide"),right))));
    auto target=take(carrier.createTarget(id<SlideId>("left-slide"),{20000,10000}));
    auto targetState=take(StagePositionState::create(spec));
    targetState=take(targetState.afterVerifiedHomingObservation(
        observation("x",0,0.5,StagePositionEvidence::ControllerReported),ObservationSource::SdkStatus));
    targetState=take(targetState.afterVerifiedHomingObservation(
        observation("y",0,0.5,StagePositionEvidence::ControllerReported),ObservationSource::SdkStatus));
    auto current=targetState.currentSlidePosition(carrier,id<SlideId>("left-slide"));
    check(!current&&current.error()==CurrentSlidePositionError::PositionOutsideSlide,
          "selected slide position is not reported when machine reference is at its home origin outside the slide");

    const auto foreignSpec=take(DualSlideCarrierSpecification::create(
        take(StageSpecification::create(id<EquipmentComponentId>("other-stage"),{axis("x"),axis("y")})),
        id<StageAxisId>("x"),id<StageAxisId>("y"),
        CarrierHorizontalDirection::PositiveStageXFromLeftToRight));
    const auto foreignState=take(StagePositionState::create(foreignSpec));
    auto wrongCarrier=carrier;
    (void)wrongCarrier;
    auto wrongStage=foreignState.currentSlidePosition(carrier,id<SlideId>("left-slide"));
    check(!wrongStage&&wrongStage.error()==CurrentSlidePositionError::StageComponentMismatch,
          "one stage aggregate cannot be mapped through another stage's slide registration");

    if(failures) {
        std::cerr<<failures<<" stage position state test(s) failed\n";
        return 1;
    }
    std::cout<<"All stage position state tests passed\n";
    return 0;
}
