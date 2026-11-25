#include "microscope/core/navigation/DualSlideNavigation.h"
#include "microscope/core/equipment/OpticalTopology.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
using namespace microscope::core;
template<class T,class E>T take(Result<T,E> r){if(!r)throw std::runtime_error("fixture rejected");return std::move(r).value();}
template<class T>T id(const char*s){return take(T::create(s));}
AxisSpecification axis(const char*s){return take(AxisSpecification::create(id<StageAxisId>(s),AxisMotionType::Linear,AxisCoordinateUnit::Micrometers,take(AxisOperatingRange::create(-100000,150000,AxisCoordinateUnit::Micrometers)),take(PositiveAxisResolution::create(0.1)),std::nullopt,std::nullopt,std::nullopt,AxisPositionFeedbackCapability::DeviceReported));}
StageSpecification stage(){return take(StageSpecification::create(id<EquipmentComponentId>("stage"),{axis("x"),axis("y")}));}
SlideRegistration registration(const char*s,DualSlideSlot slot,double x){
 auto g=take(SlideGeometry::create(75000,25000));auto src=ObservationSource::HardwareReadback;
 auto tl=take(SlideCornerMeasurement::create(SlideCorner::TopLeft,{x,20000},src,0.5));
 auto tr=take(SlideCornerMeasurement::create(SlideCorner::TopRight,{x+75000,20000},src,0.5));
 auto br=take(SlideCornerMeasurement::create(SlideCorner::BottomRight,{x+75000,45000},src,0.5));
 auto bl=take(SlideCornerMeasurement::create(SlideCorner::BottomLeft,{x,45000},src,0.5));
 return take(SlideRegistration::create(id<SlideId>(s),id<CalibrationRevisionId>(std::string(s)+"-registration-v1"),slot,g,{tl,tr,br,bl},1.0));
}
void equipmentDefinitionOwnsCarrier() {
 auto s=stage();
 auto carrier=take(DualSlideCarrierSpecification::create(
     s,id<StageAxisId>("x"),id<StageAxisId>("y"),
     CarrierHorizontalDirection::PositiveStageXFromLeftToRight));
 auto component=take(EquipmentComponent::create(
     id<EquipmentComponentId>("stage"),EquipmentComponentKind::MechanicalAssembly,{}));
 auto definition=MicroscopeEquipmentDefinition::create(
     id<MicroscopeId>("dual-slide-scope"),{component},{},{},{},{},{},{},{s},{carrier});
 if(!definition||definition.value().dualSlideCarrierSpecifications().size()!=1)
     throw std::runtime_error("equipment definition did not retain carrier");
 auto otherStage=take(StageSpecification::create(
     id<EquipmentComponentId>("other-stage"),{axis("other-x"),axis("other-y")}));
 auto wrongCarrier=take(DualSlideCarrierSpecification::create(
     otherStage,id<StageAxisId>("other-x"),id<StageAxisId>("other-y"),
     CarrierHorizontalDirection::PositiveStageXFromLeftToRight));
 auto invalid=MicroscopeEquipmentDefinition::create(
     id<MicroscopeId>("dual-slide-scope"),{component},{},{},{},{},{},{},{s},{wrongCarrier});
 if(invalid||invalid.error()!=EquipmentDefinitionError::DualSlideCarrierReferencesUnknownStage)
     throw std::runtime_error("carrier bound to an absent stage was accepted");
}
void convertsMicrometersIntoDeclaredAxisUnits() {
 auto mmAxis=[](const char* name) {
    return take(AxisSpecification::create(id<StageAxisId>(name),AxisMotionType::Linear,
       AxisCoordinateUnit::Millimeters,
       take(AxisOperatingRange::create(-300.0,300.0,AxisCoordinateUnit::Millimeters)),
       take(PositiveAxisResolution::create(0.001)),std::nullopt,std::nullopt,std::nullopt,
       AxisPositionFeedbackCapability::DeviceReported));
 };
 auto mmStage=take(StageSpecification::create(
     id<EquipmentComponentId>("mm-stage"),{mmAxis("mm-x"),mmAxis("mm-y")}));
 auto spec=take(DualSlideCarrierSpecification::create(
     mmStage,id<StageAxisId>("mm-x"),id<StageAxisId>("mm-y"),
     CarrierHorizontalDirection::PositiveStageXFromLeftToRight));
 auto command=spec.moveTo({12500.0,22000.0});
 if(!command||command.value().targets()[0].unit()!=AxisCoordinateUnit::Millimeters||
    std::abs(command.value().targets()[0].value()-12.5)>1e-9||
    std::abs(command.value().targets()[1].value()-22.0)>1e-9)
     throw std::runtime_error("micrometer slide map was not converted to declared millimeter axes");
}
int main(){
 try { equipmentDefinitionOwnsCarrier(); convertsMicrometersIntoDeclaredAxisUnits(); }
 catch(const std::exception& e){std::cerr<<e.what()<<'\\n';return 1;}
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
 auto failFit=SlideRegistration::create(id<SlideId>("distorted"),id<CalibrationRevisionId>("distorted-registration-v1"),DualSlideSlot::Left,g,{a,b,c,d},5);
 check(!failFit&&failFit.error()==SlideRegistrationError::FitResidualExceeded,"fourth-corner residual gate rejects affine mismatch");
 auto right=registration("right",DualSlideSlot::Right,95000);
 auto lslot=take(SlideSlotState::create(DualSlideSlot::Left,id<SlideId>("left"),left));
 auto rslot=take(SlideSlotState::create(DualSlideSlot::Right,id<SlideId>("right"),right));
 auto carrier=take(DualSlideCarrierState::create(spec.value(),std::move(lslot),std::move(rslot)));
 check(carrier.registrationFor(id<SlideId>("left"))&&carrier.registrationFor(id<SlideId>("right")),"two slides retain separate simultaneous registrations");
 auto hx=take(StageAxisPositionState::homed(
     take(AxisPosition::create(id<StageAxisId>("x"),42500,AxisCoordinateUnit::Micrometers)),
     0.5,StagePositionEvidence::EncoderMeasured,ObservationSource::HardwareReadback));
 auto hy=take(StageAxisPositionState::homed(
     take(AxisPosition::create(id<StageAxisId>("y"),32500,AxisCoordinateUnit::Micrometers)),
     1.0,StagePositionEvidence::EncoderMeasured,ObservationSource::HardwareReadback));
 auto current=carrier.currentSlidePosition(id<SlideId>("left"),hx,hy);
 check(current&&std::abs(current.value().position.xMicrometers-37500)<1e-6&&
       std::abs(current.value().position.yMicrometers-12500)<1e-6,
       "current machine position maps back to the selected slide-local coordinate");
 check(current&&std::abs(current.value().xErrorBoundMicrometers-1.0)<1e-6&&
       std::abs(current.value().yErrorBoundMicrometers-1.5)<1e-6,
       "slide-local position carries propagated stage and registration uncertainty");
 auto unknownX=take(StageAxisPositionState::unknown(id<StageAxisId>("x"),AxisCoordinateUnit::Micrometers));
 auto unknownCurrent=carrier.currentSlidePosition(id<SlideId>("left"),unknownX,hy);
 check(!unknownCurrent&&unknownCurrent.error()==CurrentSlidePositionError::PositionUnknown,
       "unreferenced stage position cannot be presented as a slide coordinate");
 auto wrongRight=registration("wrong-right",DualSlideSlot::Right,-95000);
 auto wrongRightState=take(SlideSlotState::create(DualSlideSlot::Right,id<SlideId>("wrong-right"),wrongRight));
 auto wrongOrder=DualSlideCarrierState::create(spec.value(),
     take(SlideSlotState::create(DualSlideSlot::Left,id<SlideId>("left-order"),registration("left-order",DualSlideSlot::Left,5000))),
     std::move(wrongRightState));
 check(!wrongOrder&&wrongOrder.error()==DualSlideCarrierStateError::SlideSlotOrderMismatch,
       "validates left/right physical order against configured stage X direction");
 auto leftTarget=take(carrier.createTarget(id<SlideId>("left"),{10000,10000}));
 auto rightTarget=take(carrier.createTarget(id<SlideId>("right"),{10000,10000}));
 auto leftCommand=carrier.commandFor(leftTarget);
 auto rightCommand=carrier.commandFor(rightTarget);
 check(leftCommand&&rightCommand&&leftCommand.value().targets()[0].value()!=rightCommand.value().targets()[0].value(),"navigation resolves the selected SlideId through its own registration");
 auto changedLeft=registration("left",DualSlideSlot::Left,6000);
 auto changedCarrier=take(DualSlideCarrierState::create(spec,
     take(SlideSlotState::create(DualSlideSlot::Left,id<SlideId>("left"),changedLeft)),
     take(SlideSlotState::create(DualSlideSlot::Right,id<SlideId>("right"),right))));
 auto staleCommand=changedCarrier.commandFor(leftTarget);
 check(!staleCommand&&staleCommand.error()==SlideNavigationCommandError::RegistrationChanged,
       "saved navigation target rejects a changed slide registration");
 auto outsideTravel=carrier.commandFor(id<SlideId>("right"),{75000,12000});
 check(!outsideTravel&&outsideTravel.error()==SlideNavigationCommandError::OutsideStageTravel,
       "mapped slide target is rejected outside installed stage travel");
 if(failures)return 1;std::cout<<"Dual slide navigation tests passed\n";return 0;
}
