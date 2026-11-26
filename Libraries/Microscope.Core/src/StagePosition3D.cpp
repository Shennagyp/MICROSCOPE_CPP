#include "microscope/core/stage/StagePosition3D.h"
#include <cmath>
#include <utility>

namespace microscope::core{
namespace{
double toMicrometers(double value,AxisCoordinateUnit unit)noexcept{return unit==AxisCoordinateUnit::Millimeters?value*1000.0:value;}
double errorToMicrometers(double value,AxisCoordinateUnit unit)noexcept{return toMicrometers(value,unit);}
bool linearUnit(AxisCoordinateUnit unit)noexcept{return unit==AxisCoordinateUnit::Micrometers||unit==AxisCoordinateUnit::Millimeters;}
StagePosition3DError mapPositionError(StageAxisPositionStateError e){
    if(e==StageAxisPositionStateError::AxisMismatch)return StagePosition3DError::AxisMismatch;
    if(e==StageAxisPositionStateError::UnitMismatch)return StagePosition3DError::AxisUnitMismatch;
    return StagePosition3DError::PositionRejected;
}
}
StagePosition3D::StagePosition3D(StageAxisPositionState x,StageAxisPositionState y,StageAxisPositionState z)
    :x_(std::move(x)),y_(std::move(y)),z_(std::move(z)){}
Result<StagePosition3D,StagePosition3DError> StagePosition3D::unknown(const StageWorkingSurface3D&s){
    auto x=StageAxisPositionState::unknown(s.stageComponentId(),s.xAxisId(),s.xAxisUnit());
    auto y=StageAxisPositionState::unknown(s.stageComponentId(),s.yAxisId(),s.yAxisUnit());
    auto z=StageAxisPositionState::unknown(s.stageComponentId(),s.zAxisId(),s.zAxisUnit());
    if(!x||!y||!z)return Result<StagePosition3D,StagePosition3DError>::failure(StagePosition3DError::AxisUnitMismatch);
    return Result<StagePosition3D,StagePosition3DError>::success(StagePosition3D(std::move(x).value(),std::move(y).value(),std::move(z).value()));
}
Result<StagePosition3D,StagePosition3DError> StagePosition3D::afterPositionObservation(const AxisPositionObservation&o,ObservationSource source)const{
    const auto& id=o.position().axisId();
    if(id==x_.axisId()){auto n=x_.afterPositionObservation(o,source);if(!n)return Result<StagePosition3D,StagePosition3DError>::failure(mapPositionError(n.error()));return Result<StagePosition3D,StagePosition3DError>::success(StagePosition3D(std::move(n).value(),y_,z_));}
    if(id==y_.axisId()){auto n=y_.afterPositionObservation(o,source);if(!n)return Result<StagePosition3D,StagePosition3DError>::failure(mapPositionError(n.error()));return Result<StagePosition3D,StagePosition3DError>::success(StagePosition3D(x_,std::move(n).value(),z_));}
    if(id==z_.axisId()){auto n=z_.afterPositionObservation(o,source);if(!n)return Result<StagePosition3D,StagePosition3DError>::failure(mapPositionError(n.error()));return Result<StagePosition3D,StagePosition3DError>::success(StagePosition3D(x_,y_,std::move(n).value()));}
    return Result<StagePosition3D,StagePosition3DError>::failure(StagePosition3DError::AxisMismatch);
}
Result<StagePosition3D,StagePosition3DError> StagePosition3D::afterVerifiedHomingObservation(const AxisPositionObservation&o,ObservationSource source)const{
    const auto& id=o.position().axisId();
    if(id==x_.axisId()){if(o.position().unit()!=x_.unit())return Result<StagePosition3D,StagePosition3DError>::failure(StagePosition3DError::AxisUnitMismatch);auto n=StageAxisPositionState::homed(x_.stageComponentId(),o,source);if(!n)return Result<StagePosition3D,StagePosition3DError>::failure(mapPositionError(n.error()));return Result<StagePosition3D,StagePosition3DError>::success(StagePosition3D(std::move(n).value(),y_,z_));}
    if(id==y_.axisId()){if(o.position().unit()!=y_.unit())return Result<StagePosition3D,StagePosition3DError>::failure(StagePosition3DError::AxisUnitMismatch);auto n=StageAxisPositionState::homed(y_.stageComponentId(),o,source);if(!n)return Result<StagePosition3D,StagePosition3DError>::failure(mapPositionError(n.error()));return Result<StagePosition3D,StagePosition3DError>::success(StagePosition3D(x_,std::move(n).value(),z_));}
    if(id==z_.axisId()){if(o.position().unit()!=z_.unit())return Result<StagePosition3D,StagePosition3DError>::failure(StagePosition3DError::AxisUnitMismatch);auto n=StageAxisPositionState::homed(z_.stageComponentId(),o,source);if(!n)return Result<StagePosition3D,StagePosition3DError>::failure(mapPositionError(n.error()));return Result<StagePosition3D,StagePosition3DError>::success(StagePosition3D(x_,y_,std::move(n).value()));}
    return Result<StagePosition3D,StagePosition3DError>::failure(StagePosition3DError::AxisMismatch);
}
Result<StagePosition3D,StagePosition3DError> StagePosition3D::afterCommandedPosition(const AxisPosition&p,std::optional<double> e)const{
    if(p.axisId()==x_.axisId()){auto n=x_.afterCommandedPosition(p,e);if(!n)return Result<StagePosition3D,StagePosition3DError>::failure(mapPositionError(n.error()));return Result<StagePosition3D,StagePosition3DError>::success(StagePosition3D(std::move(n).value(),y_,z_));}
    if(p.axisId()==y_.axisId()){auto n=y_.afterCommandedPosition(p,e);if(!n)return Result<StagePosition3D,StagePosition3DError>::failure(mapPositionError(n.error()));return Result<StagePosition3D,StagePosition3DError>::success(StagePosition3D(x_,std::move(n).value(),z_));}
    if(p.axisId()==z_.axisId()){auto n=z_.afterCommandedPosition(p,e);if(!n)return Result<StagePosition3D,StagePosition3DError>::failure(mapPositionError(n.error()));return Result<StagePosition3D,StagePosition3DError>::success(StagePosition3D(x_,y_,std::move(n).value()));}
    return Result<StagePosition3D,StagePosition3DError>::failure(StagePosition3DError::AxisMismatch);
}
Result<StagePosition3D,StagePosition3DError> StagePosition3D::afterCommandedDisplacement(const AxisDisplacement&p,std::optional<double> e)const{
    if(p.axisId()==x_.axisId()){auto n=x_.afterCommandedDisplacement(p,e);if(!n)return Result<StagePosition3D,StagePosition3DError>::failure(mapPositionError(n.error()));return Result<StagePosition3D,StagePosition3DError>::success(StagePosition3D(std::move(n).value(),y_,z_));}
    if(p.axisId()==y_.axisId()){auto n=y_.afterCommandedDisplacement(p,e);if(!n)return Result<StagePosition3D,StagePosition3DError>::failure(mapPositionError(n.error()));return Result<StagePosition3D,StagePosition3DError>::success(StagePosition3D(x_,std::move(n).value(),z_));}
    if(p.axisId()==z_.axisId()){auto n=z_.afterCommandedDisplacement(p,e);if(!n)return Result<StagePosition3D,StagePosition3DError>::failure(mapPositionError(n.error()));return Result<StagePosition3D,StagePosition3DError>::success(StagePosition3D(x_,y_,std::move(n).value()));}
    return Result<StagePosition3D,StagePosition3DError>::failure(StagePosition3DError::AxisMismatch);
}
StagePosition3D StagePosition3D::invalidateMachineReference()const{return StagePosition3D(x_.invalidateMachineReference(),y_.invalidateMachineReference(),z_.invalidateMachineReference());}
const StageAxisPositionState& StagePosition3D::x()const noexcept{return x_;}
const StageAxisPositionState& StagePosition3D::y()const noexcept{return y_;}
const StageAxisPositionState& StagePosition3D::z()const noexcept{return z_;}

Result<SlidePositionEstimate3D,StagePosition3DError> StagePosition3D::positionOnSlide(const StageWorkingSurface3D&s,const SlideId&id)const{
    if(s.stageComponentId()!=x_.stageComponentId()||s.stageComponentId()!=y_.stageComponentId()||s.stageComponentId()!=z_.stageComponentId())
        return Result<SlidePositionEstimate3D,StagePosition3DError>::failure(StagePosition3DError::StageMismatch);
    if(x_.axisId()!=s.xAxisId()||y_.axisId()!=s.yAxisId()||z_.axisId()!=s.zAxisId())
        return Result<SlidePositionEstimate3D,StagePosition3DError>::failure(StagePosition3DError::AxisMismatch);
    if(x_.unit()!=s.xAxisUnit()||y_.unit()!=s.yAxisUnit()||z_.unit()!=s.zAxisUnit())
        return Result<SlidePositionEstimate3D,StagePosition3DError>::failure(StagePosition3DError::AxisUnitMismatch);
    if(!x_.positionIsUsable()||!y_.positionIsUsable()||!z_.positionIsUsable()||
        !x_.position()||!y_.position()||!z_.position())
        return Result<SlidePositionEstimate3D,StagePosition3DError>::failure(StagePosition3DError::PositionUnknown);
    if(!x_.accumulatedPositionErrorBound()||!y_.accumulatedPositionErrorBound()||!z_.accumulatedPositionErrorBound())
        return Result<SlidePositionEstimate3D,StagePosition3DError>::failure(StagePosition3DError::PositionUncertaintyUnknown);
    if(!linearUnit(x_.unit())||!linearUnit(y_.unit())||!linearUnit(z_.unit()))
        return Result<SlidePositionEstimate3D,StagePosition3DError>::failure(StagePosition3DError::AxisUnitMismatch);
    StagePoint3D machine{
        toMicrometers(x_.position()->value(),x_.unit()),
        toMicrometers(y_.position()->value(),y_.unit()),
        toMicrometers(z_.position()->value(),z_.unit())};
    if(!s.placementFor(id))return Result<SlidePositionEstimate3D,StagePosition3DError>::failure(StagePosition3DError::SlideNotFound);
    auto projected=s.toSlide(id,machine);
    if(!projected)return Result<SlidePositionEstimate3D,StagePosition3DError>::failure(StagePosition3DError::SlideProjectionRejected);
    const auto* placement=s.placementFor(id);
    if(!placement)return Result<SlidePositionEstimate3D,StagePosition3DError>::failure(StagePosition3DError::SlideProjectionRejected);
    const double ex=errorToMicrometers(*x_.accumulatedPositionErrorBound(),x_.unit());
    const double ey=errorToMicrometers(*y_.accumulatedPositionErrorBound(),y_.unit());
    const double ez=errorToMicrometers(*z_.accumulatedPositionErrorBound(),z_.unit());
    const auto& bx=placement->localXAxisInStage();const auto& by=placement->localYAxisInStage();const auto& bz=placement->localZAxisInStage();
    const double datum=placement->stopPointUncertaintyMicrometers();
    const double lx=std::abs(bx.x)*ex+std::abs(bx.y)*ey+std::abs(bx.z)*ez+datum;
    const double ly=std::abs(by.x)*ex+std::abs(by.y)*ey+std::abs(by.z)*ez+datum;
    const double lz=std::abs(bz.x)*ex+std::abs(bz.y)*ey+std::abs(bz.z)*ez+datum;
    if(!std::isfinite(lx)||!std::isfinite(ly)||!std::isfinite(lz))
        return Result<SlidePositionEstimate3D,StagePosition3DError>::failure(StagePosition3DError::PositionUncertaintyUnknown);
    return Result<SlidePositionEstimate3D,StagePosition3DError>::success({
        projected.value().slideId,projected.value().registrationRevision,projected.value().position,
        lx,ly,lz,x_.evidence(),y_.evidence(),z_.evidence(),x_.source(),y_.source(),z_.source(),
        projected.value().insideSlide,projected.value().insideWorkingVolume});
}
} // namespace microscope::core
