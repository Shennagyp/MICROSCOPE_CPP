#include "microscope/core/motion/StageAxisPositionState.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>
using namespace microscope::core;
template<class T,class E>T take(Result<T,E> r){if(!r)throw std::runtime_error("fixture rejected");return std::move(r).value();}
template<class T>T id(const char*s){return take(T::create(s));}
int main(){
 int failures=0;auto check=[&](bool v,const char*m){if(!v){++failures;std::cerr<<"FAIL: "<<m<<'\n';}};
 auto x=id<StageAxisId>("x");
 auto stage=id<EquipmentComponentId>("stage");
 auto unknown=take(StageAxisPositionState::unknown(stage,x,AxisCoordinateUnit::Micrometers));
 check(unknown.requiresHoming(5)&&!unknown.positionIsUsable(),"unknown position requires homing");
 auto start=take(StageAxisPositionState::homed(
   stage,take(AxisPosition::create(x,0,AxisCoordinateUnit::Micrometers)),1.0,
   StagePositionEvidence::ControllerReported,ObservationSource::SdkStatus));
 check(start.positionIsUsable()&&!start.requiresHoming(5)&&start.stageComponentId()==stage,"homing establishes a referenced position scoped to its installed stage");
 auto first=take(start.afterCommandedDisplacement(
   take(AxisDisplacement::create(x,1000,AxisCoordinateUnit::Micrometers)),2.0));
 check(first.position()&&first.position()->value()==1000&&first.accumulatedPositionErrorBound().value()==3.0,
       "commanded motion updates estimate and accumulates its modeled error bound");
 auto second=take(first.afterCommandedDisplacement(
   take(AxisDisplacement::create(x,2000,AxisCoordinateUnit::Micrometers)),3.0));
 check(second.accumulatedPositionErrorBound().value()==6.0&&!second.requiresHoming(10),
       "error budget accumulates conservatively across moves");
 auto third=take(second.afterCommandedDisplacement(
   take(AxisDisplacement::create(x,1000,AxisCoordinateUnit::Micrometers)),5.0));
 check(third.requiresHoming(10),"threshold crossing requests homing");
 auto controller=take(third.afterPositionObservation(
   take(AxisPosition::create(x,4000,AxisCoordinateUnit::Micrometers)),0.2,
   StagePositionEvidence::ControllerReported,ObservationSource::SdkStatus));
 check(controller.accumulatedPositionErrorBound().value()>=11.0,
       "controller-reported coordinate does not erase accumulated open-loop error");
 auto encoder=take(third.afterPositionObservation(
   take(AxisPosition::create(x,4000,AxisCoordinateUnit::Micrometers)),0.2,
   StagePositionEvidence::EncoderMeasured,ObservationSource::HardwareReadback));
 check(encoder.accumulatedPositionErrorBound().value()==0.2,
       "independent encoder measurement replaces the accumulated position uncertainty");
 auto invalidated=encoder.invalidateMachineReference();
 check(!invalidated.positionIsUsable()&&invalidated.requiresHoming(10),
       "lost machine reference makes the retained coordinate unusable");
 auto unknownBound=take(start.afterCommandedDisplacement(
   take(AxisDisplacement::create(x,10,AxisCoordinateUnit::Micrometers)),std::nullopt));
 check(unknownBound.requiresHoming(100),"unknown move error bound cannot be treated as zero");
 if(failures)return 1;std::cout<<"Stage position state tests passed\n";return 0;
}
