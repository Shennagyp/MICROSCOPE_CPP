#include "microscope/core/navigation/StageNavigationPlanner.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using namespace microscope::core;

template<class T,class E>
T take(Result<T,E> result) {
    if(!result)throw std::runtime_error("test fixture factory rejected a valid value");
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
StageSpecification stage() {
    return take(StageSpecification::create(id<EquipmentComponentId>("stage"),{axis("x"),axis("y")}));
}
SlideRegistration registration(
    const char* slideId,DualSlideSlot slot,double originX,const char* revision) {
    const auto geometry=take(SlideGeometry::create(75000,25000));
    const auto source=ObservationSource::HardwareReadback;
    const auto tl=take(SlideCornerMeasurement::create(SlideCorner::TopLeft,{originX,20000},source,0.25));
    const auto tr=take(SlideCornerMeasurement::create(SlideCorner::TopRight,{originX+75000,20000},source,0.25));
    const auto br=take(SlideCornerMeasurement::create(SlideCorner::BottomRight,{originX+75000,45000},source,0.25));
    const auto bl=take(SlideCornerMeasurement::create(SlideCorner::BottomLeft,{originX,45000},source,0.25));
    return take(SlideRegistration::create(
        id<SlideId>(slideId),id<CalibrationRevisionId>(revision),slot,geometry,{tl,tr,br,bl},0.5));
}
StageAxisPositionState homed(const char* axisId,double position,double error,const char* stageId="stage") {
    auto positionValue=take(AxisPosition::create(
        id<StageAxisId>(axisId),position,AxisCoordinateUnit::Micrometers));
    auto observation=take(AxisPositionObservation::create(
        positionValue,error,StagePositionEvidence::EncoderMeasured));
    return take(StageAxisPositionState::homed(
        id<EquipmentComponentId>(stageId),observation,ObservationSource::HardwareReadback));
}
}

int main() {
    using namespace microscope::core;
    int failures=0;
    const auto check=[&](bool value,const char* message) {
        if(!value){++failures;std::cerr<<"FAIL: "<<message<<'\n';}
    };

    const auto stageDefinition=stage();
    const auto specification=take(DualSlideCarrierSpecification::create(
        stageDefinition,id<StageAxisId>("x"),id<StageAxisId>("y"),
        CarrierHorizontalDirection::PositiveStageXFromLeftToRight));
    const auto leftRegistration=registration(
        "slide-left",DualSlideSlot::Left,5000,"slide-left-registration-v1");
    const auto rightRegistration=registration(
        "slide-right",DualSlideSlot::Right,95000,"slide-right-registration-v1");
    const auto carrier=take(DualSlideCarrierState::create(
        specification,
        take(SlideSlotState::create(
            DualSlideSlot::Left,id<SlideId>("slide-left"),leftRegistration)),
        take(SlideSlotState::create(
            DualSlideSlot::Right,id<SlideId>("slide-right"),rightRegistration))));
    const auto target=take(carrier.createTarget(id<SlideId>("slide-left"),{20000,10000}));
    const auto policy=take(StageHomingPolicy::create(5.0,5.0));

    const auto direct=StageNavigationPlanner::plan(
        carrier,target,homed("x",25000,1.0),homed("y",30000,1.0),policy);
    check(direct&&std::holds_alternative<StageDirectMovePlan>(direct.value()),
          "direct movement is planned while both axis errors are within policy");
    if(direct) {
        const auto& move=std::get<StageDirectMovePlan>(direct.value());
        check(move.command.targets().size()==2&&
              move.target.registrationRevision()==leftRegistration.revision(),
              "direct plan preserves the target registration and coordinated X/Y command");
    }

    auto driftedX=take(homed("x",25000,1.0).afterCommandedDisplacement(
        take(AxisDisplacement::create(id<StageAxisId>("x"),500,AxisCoordinateUnit::Micrometers)),
        6.0));
    const auto recovery=StageNavigationPlanner::plan(
        carrier,target,driftedX,homed("y",30000,1.0),policy);
    check(recovery&&std::holds_alternative<StageHomeThenMovePlan>(recovery.value()),
          "crossing either axis threshold requires homing before navigation");
    if(recovery) {
        const auto& home=std::get<StageHomeThenMovePlan>(recovery.value());
        check(home.home.axes().axes().size()==2&&home.target.slideId()==target.slideId(),
              "recovery homes the common plane and preserves SlideId/local destination");
    }

    const auto restored=StageNavigationPlanner::resolveAfterHoming(
        carrier,target,homed("x",0,0.5),homed("y",0,0.5),policy);
    check(restored&&restored.value().targets().size()==2,
          "verified home resolves the saved local destination to an absolute command");
    if(restored) {
        check(std::abs(restored.value().targets()[0].value()-25000)<1e-6&&
              std::abs(restored.value().targets()[1].value()-30000)<1e-6,
              "restored destination is recomputed from slide-local coordinates");
    }

    const auto unverified=StageNavigationPlanner::resolveAfterHoming(
        carrier,target,
        take(StageAxisPositionState::unknown(id<EquipmentComponentId>("stage"),id<StageAxisId>("x"),AxisCoordinateUnit::Micrometers)),
        homed("y",0,0.1),policy);
    check(!unverified&&unverified.error()==StageNavigationPlanningError::HomingNotVerified,
          "does not dispatch the saved target before homing/reference is verified");

    const auto movedRegistration=registration(
        "slide-left",DualSlideSlot::Left,6000,"slide-left-registration-v2");
    const auto movedCarrier=take(DualSlideCarrierState::create(
        specification,
        take(SlideSlotState::create(
            DualSlideSlot::Left,id<SlideId>("slide-left"),movedRegistration)),
        take(SlideSlotState::create(
            DualSlideSlot::Right,id<SlideId>("slide-right"),rightRegistration))));
    const auto stale=StageNavigationPlanner::resolveAfterHoming(
        movedCarrier,target,homed("x",0,0.1),homed("y",0,0.1),policy);
    check(!stale&&stale.error()==StageNavigationPlanningError::RegistrationChanged,
          "does not restore a target through a changed physical slide placement");

    const auto unknown=StageNavigationPlanner::plan(
        carrier,target,
        take(StageAxisPositionState::unknown(id<EquipmentComponentId>("stage"),id<StageAxisId>("x"),AxisCoordinateUnit::Micrometers)),
        homed("y",30000,0.1),policy);
    check(unknown&&std::holds_alternative<StageHomeThenMovePlan>(unknown.value()),
          "unknown machine reference plans homing instead of inventing a coordinate");
    const auto foreignStage=StageNavigationPlanner::plan(
        carrier,target,homed("x",25000,0.1,"another-stage"),homed("y",30000,0.1),policy);
    check(!foreignStage&&foreignStage.error()==StageNavigationPlanningError::StageComponentMismatch,
          "rejects same-named axes from a different installed stage");

    if(failures!=0) {
        std::cerr<<failures<<" stage navigation planner test(s) failed\n";
        return 1;
    }
    std::cout<<"All stage navigation planner tests passed\n";
    return 0;
}
