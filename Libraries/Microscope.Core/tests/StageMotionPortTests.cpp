#include "microscope/core/motion/StageMotionPort.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
using namespace microscope::core;
template<class T,class E>T take(Result<T,E> r){if(!r)throw std::runtime_error("fixture rejected");return std::move(r).value();}
template<class T>T id(const char*s){return take(T::create(s));}
int main(){
 int failures=0;auto check=[&](bool v,const char*m){if(!v){++failures;std::cerr<<"FAIL: "<<m<<'\n';}};
 auto x=take(AxisPosition::create(id<StageAxisId>("x"),-100,AxisCoordinateUnit::Micrometers));
 auto y=take(AxisPosition::create(id<StageAxisId>("y"),5,AxisCoordinateUnit::Millimeters));
 auto absolute=MoveAxesTo::create({x,y});check(absolute&&absolute.value().targets().size()==2,"multi-axis absolute physical target");
 auto relative=MoveAxesBy::create({take(AxisDisplacement::create(id<StageAxisId>("x"),2,AxisCoordinateUnit::Micrometers))});
 check(bool(relative),"relative physical move");
 check(!MoveAxesTo::create({}),"empty move rejected");
 check(!MoveAxesTo::create({x,x}),"duplicate axis rejected");
 check(!AxisPosition::create(id<StageAxisId>("z"),1,AxisCoordinateUnit::DeviceDefined),"controller-native unit rejected");
 check(!AxisPosition::create(id<StageAxisId>("bad"),std::numeric_limits<double>::infinity(),AxisCoordinateUnit::Micrometers),"non-finite target rejected");
 auto axes=take(StageAxisSet::create({id<StageAxisId>("x"),id<StageAxisId>("y")}));
 auto home=HomeAxes::create(axes);auto stop=StopAxes::create(axes);
 check(home.axes().axes().size()==2&&stop.axes().axes().size()==2,"home and stop identify axes");
 auto acknowledgement=take(StageMotionAcknowledgement::create(StageMotionAction::MoveAxesTo,axes));
 check(acknowledgement.action()==StageMotionAction::MoveAxesTo&&
       acknowledgement.axes().axes().size()==2,
       "command acknowledgement carries its typed action and axis set");
 RuntimeDeviceCorrelation device{id<MicroscopeId>("m"),id<RuntimeId>("r"),take(RuntimeGeneration::create(1)),take(DeviceSessionGeneration::create(1)),id<DeviceId>("stage")};
 auto missing=StageMotionCommandContext::create(device,{id<OperationId>("op"),id<StepId>("step"),std::nullopt});
 check(!missing,"command identity required");
 auto context=StageMotionCommandContext::create(device,{id<OperationId>("op"),id<StepId>("step"),id<CommandId>("cmd")});
 check(bool(context),"complete command correlation");
 auto positionObservation=take(AxisPositionObservation::create(
     x,0.25,StagePositionEvidence::EncoderMeasured));
 StageMotionFeedbackPayload positionFeedback=positionObservation;
 check(std::holds_alternative<AxisPositionObservation>(positionFeedback),
       "position feedback carries measurement class and uncertainty");
 check(!AxisPositionObservation::create(
     x,-0.1,StagePositionEvidence::EncoderMeasured),
       "negative position uncertainty rejected");
 check(!AxisPositionObservation::create(
     x,0.1,StagePositionEvidence::CommandDerivedEstimate),
       "command-derived estimates are not mislabeled as device position observations");
 auto accepted=StageMotionSubmissionResult::accepted();
 auto rejected=StageMotionSubmissionResult::rejected(StageMotionSubmissionRejection::UnsupportedOperation);
 check(accepted.status()==StageMotionSubmissionStatus::Accepted&&!accepted.rejection()&&rejected.status()==StageMotionSubmissionStatus::Rejected,"submission acceptance differs from rejection");
 if(failures)return 1;std::cout<<"Stage motion port tests passed\n";return 0;
}
