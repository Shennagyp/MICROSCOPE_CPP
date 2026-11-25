#include "microscope/core/navigation/DualSlideNavigation.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>
using namespace microscope::core;
template<class T,class E>T take(Result<T,E> r){if(!r)throw std::runtime_error("fixture rejected");return std::move(r).value();}
template<class T>T id(const char*s){return take(T::create(s));}
AxisSpecification axis(const char*s){return take(AxisSpecification::create(id<StageAxisId>(s),AxisMotionType::Linear,AxisCoordinateUnit::Micrometers,take(AxisOperatingRange::create(-300000,300000,AxisCoordinateUnit::Micrometers)),take(PositiveAxisResolution::create(0.1)),std::nullopt,std::nullopt,std::nullopt,AxisPositionFeedbackCapability::DeviceReported));}
StageSpecification stage(){return take(StageSpecification::create(id<EquipmentComponentId>("stage"),{axis("x"),axis("y")}));}
SlideRegistration registration(const char*s,DualSlideSlot slot,double x){
 auto g=take(SlideGeometry::create(75000,25000));auto src=ObservationSource::HardwareReadback;
 auto tl=take(SlideCornerMeasurement::create(SlideCorner::TopLeft,{x,20000},src,0.5));
 auto tr=take(SlideCornerMeasurement::create(SlideCorner::TopRight,{x+75000,20000},src,0.5));
 auto br=take(SlideCornerMeasurement::create(SlideCorner::BottomRight,{x+75000,45000},src,0.5));
 auto bl=take(SlideCornerMeasurement::create(SlideCorner::BottomLeft,{x,45000},src,0.5));
 return take(SlideRegistration::create(id<SlideId>(s),slot,g,{tl,tr,br,bl},1.0));
}
int main(){
 int failures=0;auto check=[&](bool v,const char*m){if(!v){++failures;std::cerr<<"FAIL: "<<m<<'\n';}};
 auto st=stage();
 auto spec=DualSlideCarrierSpecification::create(st,id<StageAxisId>("x"),id<StageAxisId>("y"),CarrierHorizontalDirection::PositiveStageXFromLeftToRight);
 check(bool(spec),"carrier uses two distinct linear in-plane axes");
 auto left=registration("left",DualSlideSlot::Left,5000);
 auto mapped=left.toStage({37500,12500});
 check(mapped&&std::abs(mapped.value().horizontalMicrometers-42500)<1e-6&&std::abs(mapped.value().transverseMicrometers-32500)<1e-6,"four-corner affine transform maps slide center");
 check(left.cornerFitResidualsMicrometers()[0]<1e-6&&left.cornerFitResidualsMicrometers()[3]<1e-6,"retains a residual for each measured corner");
 if(mapped){auto back=left.toSlide(mapped.value());check(back&&std::abs(back.value().xMicrometers-37500)<1e-6&&std::abs(back.value().yMicrometers-12500)<1e-6,"inverse transform round trips");}
 check(!left.toStage({75001,1}),"outside slide target rejected");
 auto g=take(SlideGeometry::create(75000,25000));auto src=ObservationSource::OperatorConfirmation;
 auto a=take(SlideCornerMeasurement::create(SlideCorner::TopLeft,{0,0},src,1));
 auto b=take(SlideCornerMeasurement::create(SlideCorner::TopRight,{75000,0},src,1));
 auto c=take(SlideCornerMeasurement::create(SlideCorner::BottomRight,{75500,25000},src,1));
 auto d=take(SlideCornerMeasurement::create(SlideCorner::BottomLeft,{0,25000},src,1));
 auto failFit=SlideRegistration::create(id<SlideId>("distorted"),DualSlideSlot::Left,g,{a,b,c,d},5);
 check(!failFit&&failFit.error()==SlideRegistrationError::FitResidualExceeded,"fourth-corner residual gate rejects affine mismatch");
 auto right=registration("right",DualSlideSlot::Right,95000);
 auto lslot=take(SlideSlotState::create(DualSlideSlot::Left,id<SlideId>("left"),left));
 auto rslot=take(SlideSlotState::create(DualSlideSlot::Right,id<SlideId>("right"),right));
 auto carrier=take(DualSlideCarrierState::create(spec.value(),std::move(lslot),std::move(rslot)));
 check(carrier.registrationFor(id<SlideId>("left"))&&carrier.registrationFor(id<SlideId>("right")),"two slides retain separate simultaneous registrations");
 auto wrongRight=registration("wrong-right",DualSlideSlot::Right,-95000);
 auto wrongRightState=take(SlideSlotState::create(DualSlideSlot::Right,id<SlideId>("wrong-right"),wrongRight));
 auto wrongOrder=DualSlideCarrierState::create(spec.value(),
     take(SlideSlotState::create(DualSlideSlot::Left,id<SlideId>("left-order"),registration("left-order",DualSlideSlot::Left,5000))),
     std::move(wrongRightState));
 check(!wrongOrder&&wrongOrder.error()==DualSlideCarrierStateError::SlideSlotOrderMismatch,
       "validates left/right physical order against configured stage X direction");
 auto leftCommand=carrier.commandFor(id<SlideId>("left"),{10000,10000});
 auto rightCommand=carrier.commandFor(id<SlideId>("right"),{10000,10000});
 check(leftCommand&&rightCommand&&leftCommand.value().targets()[0].value()!=rightCommand.value().targets()[0].value(),"navigation resolves the selected SlideId through its own registration");
 auto outsideTravel=carrier.commandFor(id<SlideId>("right"),{75000,12000});
 check(!outsideTravel&&outsideTravel.error()==SlideNavigationCommandError::OutsideStageTravel,
       "mapped slide target is rejected outside installed stage travel");
 if(failures)return 1;std::cout<<"Dual slide navigation tests passed\n";return 0;
}
