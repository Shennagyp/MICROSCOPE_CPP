#include "microscope/core/stage/StageWorkingSurface3D.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace microscope::core {
namespace {
constexpr double tolerance=1.0e-6;
bool finite(const StagePoint3D&p)noexcept{return std::isfinite(p.xMicrometers)&&std::isfinite(p.yMicrometers)&&std::isfinite(p.zMicrometers);}
bool finite(const SlidePoint3D&p)noexcept{return std::isfinite(p.xMicrometers)&&std::isfinite(p.yMicrometers)&&std::isfinite(p.zMicrometers);}
bool finite(const Direction3D&v)noexcept{return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
double dot(const Direction3D&a,const Direction3D&b)noexcept{return a.x*b.x+a.y*b.y+a.z*b.z;}
double norm(const Direction3D&v)noexcept{return std::sqrt(dot(v,v));}
Direction3D cross(const Direction3D&a,const Direction3D&b)noexcept{return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
bool validBasis(const Direction3D&x,const Direction3D&y,const Direction3D&z)noexcept{
    if(!finite(x)||!finite(y)||!finite(z))return false;
    if(std::abs(norm(x)-1)>tolerance||std::abs(norm(y)-1)>tolerance||std::abs(norm(z)-1)>tolerance)return false;
    if(std::abs(dot(x,y))>tolerance||std::abs(dot(x,z))>tolerance||std::abs(dot(y,z))>tolerance)return false;
    const auto c=cross(x,y);return std::abs(c.x-z.x)<=tolerance&&std::abs(c.y-z.y)<=tolerance&&std::abs(c.z-z.z)<=tolerance;
}
bool inside(const StagePoint3D&p,const StagePoint3D&lo,const StagePoint3D&hi)noexcept{
    return p.xMicrometers>=lo.xMicrometers&&p.xMicrometers<=hi.xMicrometers&&
        p.yMicrometers>=lo.yMicrometers&&p.yMicrometers<=hi.yMicrometers&&
        p.zMicrometers>=lo.zMicrometers&&p.zMicrometers<=hi.zMicrometers;
}
const AxisSpecification* findAxis(const StageSpecification&s,const StageAxisId&id)noexcept{
    const auto it=std::find_if(s.axes().begin(),s.axes().end(),[&](const auto&a){return a.id()==id;});
    return it==s.axes().end()?nullptr:&*it;
}
bool linearUnit(AxisCoordinateUnit u)noexcept{return u==AxisCoordinateUnit::Micrometers||u==AxisCoordinateUnit::Millimeters;}
double toMicrometers(double v,AxisCoordinateUnit u)noexcept{return u==AxisCoordinateUnit::Millimeters?v*1000.0:v;}
}
SlideGeometry3D::SlideGeometry3D(double l,double w,double t)noexcept:length_(l),width_(w),thickness_(t){}
Result<SlideGeometry3D,SlideGeometry3DError> SlideGeometry3D::create(double l,double w,double t){
    if(!std::isfinite(l)||!std::isfinite(w)||!std::isfinite(t))return Result<SlideGeometry3D,SlideGeometry3DError>::failure(SlideGeometry3DError::NonFiniteDimension);
    if(l<=0||w<=0||t<=0)return Result<SlideGeometry3D,SlideGeometry3DError>::failure(SlideGeometry3DError::NonPositiveDimension);
    return Result<SlideGeometry3D,SlideGeometry3DError>::success(SlideGeometry3D(l,w,t));
}
double SlideGeometry3D::lengthMicrometers()const noexcept{return length_;}
double SlideGeometry3D::widthMicrometers()const noexcept{return width_;}
double SlideGeometry3D::thicknessMicrometers()const noexcept{return thickness_;}
bool SlideGeometry3D::contains(const SlidePoint3D&p)const noexcept{return finite(p)&&p.xMicrometers>=0&&p.xMicrometers<=length_&&p.yMicrometers>=0&&p.yMicrometers<=width_&&p.zMicrometers>=0&&p.zMicrometers<=thickness_;}

SlidePlacement3D::SlidePlacement3D(SlideId id,CalibrationRevisionId rev,SlideGeometry3D g,StagePoint3D o,
    Direction3D x,Direction3D y,Direction3D z,double u)noexcept
    :slideId_(std::move(id)),registrationRevision_(std::move(rev)),geometry_(g),stopPointOrigin_(o),x_(x),y_(y),z_(z),stopPointUncertainty_(u){}
Result<SlidePlacement3D,SlidePlacement3DError> SlidePlacement3D::create(
    SlideId id,CalibrationRevisionId rev,SlideGeometry3D g,StagePoint3D o,Direction3D x,Direction3D y,Direction3D z,double u){
    if(!finite(o))return Result<SlidePlacement3D,SlidePlacement3DError>::failure(SlidePlacement3DError::NonFiniteOrigin);
    if(!validBasis(x,y,z))return Result<SlidePlacement3D,SlidePlacement3DError>::failure(SlidePlacement3DError::InvalidBasis);
    if(!std::isfinite(u)||u<0)return Result<SlidePlacement3D,SlidePlacement3DError>::failure(SlidePlacement3DError::InvalidDatumUncertainty);
    return Result<SlidePlacement3D,SlidePlacement3DError>::success(SlidePlacement3D(std::move(id),std::move(rev),g,o,x,y,z,u));
}
const SlideId& SlidePlacement3D::slideId()const noexcept{return slideId_;}
const CalibrationRevisionId& SlidePlacement3D::registrationRevision()const noexcept{return registrationRevision_;}
const SlideGeometry3D& SlidePlacement3D::geometry()const noexcept{return geometry_;}
const StagePoint3D& SlidePlacement3D::stopPointOrigin()const noexcept{return stopPointOrigin_;}
const Direction3D& SlidePlacement3D::localXAxisInStage()const noexcept{return x_;}
const Direction3D& SlidePlacement3D::localYAxisInStage()const noexcept{return y_;}
const Direction3D& SlidePlacement3D::localZAxisInStage()const noexcept{return z_;}
double SlidePlacement3D::stopPointUncertaintyMicrometers()const noexcept{return stopPointUncertainty_;}
Result<StagePoint3D,SlidePlacement3DError> SlidePlacement3D::toStage(const SlidePoint3D&p)const{
    if(!finite(p))return Result<StagePoint3D,SlidePlacement3DError>::failure(SlidePlacement3DError::NonFiniteMappedPoint);
    if(!geometry_.contains(p))return Result<StagePoint3D,SlidePlacement3DError>::failure(SlidePlacement3DError::SlidePointOutsideGeometry);
    StagePoint3D m{stopPointOrigin_.xMicrometers+x_.x*p.xMicrometers+y_.x*p.yMicrometers+z_.x*p.zMicrometers,
        stopPointOrigin_.yMicrometers+x_.y*p.xMicrometers+y_.y*p.yMicrometers+z_.y*p.zMicrometers,
        stopPointOrigin_.zMicrometers+x_.z*p.xMicrometers+y_.z*p.yMicrometers+z_.z*p.zMicrometers};
    if(!finite(m))return Result<StagePoint3D,SlidePlacement3DError>::failure(SlidePlacement3DError::NonFiniteMappedPoint);
    return Result<StagePoint3D,SlidePlacement3DError>::success(m);
}
Result<SlidePoint3D,SlidePlacement3DError> SlidePlacement3D::toSlide(const StagePoint3D&p)const{
    if(!finite(p))return Result<SlidePoint3D,SlidePlacement3DError>::failure(SlidePlacement3DError::NonFiniteMappedPoint);
    const double dx=p.xMicrometers-stopPointOrigin_.xMicrometers,dy=p.yMicrometers-stopPointOrigin_.yMicrometers,dz=p.zMicrometers-stopPointOrigin_.zMicrometers;
    SlidePoint3D q{dx*x_.x+dy*x_.y+dz*x_.z,dx*y_.x+dy*y_.y+dz*y_.z,dx*z_.x+dy*z_.y+dz*z_.z};
    if(!finite(q))return Result<SlidePoint3D,SlidePlacement3DError>::failure(SlidePlacement3DError::NonFiniteMappedPoint);
    return Result<SlidePoint3D,SlidePlacement3DError>::success(q);
}

StageWorkingSurface3D::StageWorkingSurface3D(EquipmentComponentId stage,StageAxisId x,StageAxisId y,StageAxisId z,
    AxisCoordinateUnit xu,AxisCoordinateUnit yu,AxisCoordinateUnit zu,StagePoint3D lo,StagePoint3D hi,SlidePlacements p)noexcept
    :stageComponentId_(std::move(stage)),xAxisId_(std::move(x)),yAxisId_(std::move(y)),zAxisId_(std::move(z)),
     xUnit_(xu),yUnit_(yu),zUnit_(zu),minimumBoundary_(lo),maximumBoundary_(hi),placements_(std::move(p)){}
Result<StageWorkingSurface3D,StageWorkingSurface3DError> StageWorkingSurface3D::create(
    const StageSpecification&stage,StageAxisId x,StageAxisId y,StageAxisId z,StagePoint3D lo,StagePoint3D hi,SlidePlacements p){
    if(!finite(lo)||!finite(hi))return Result<StageWorkingSurface3D,StageWorkingSurface3DError>::failure(StageWorkingSurface3DError::NonFiniteBoundary);
    if(lo.xMicrometers>=hi.xMicrometers||lo.yMicrometers>=hi.yMicrometers||lo.zMicrometers>=hi.zMicrometers)
        return Result<StageWorkingSurface3D,StageWorkingSurface3DError>::failure(StageWorkingSurface3DError::InvalidBoundary);
    if(x==y||x==z||y==z)return Result<StageWorkingSurface3D,StageWorkingSurface3DError>::failure(StageWorkingSurface3DError::DuplicateStageAxis);
    const auto* xa=findAxis(stage,x);const auto* ya=findAxis(stage,y);const auto* za=findAxis(stage,z);
    if(!xa||!ya||!za)return Result<StageWorkingSurface3D,StageWorkingSurface3DError>::failure(StageWorkingSurface3DError::MissingStageAxis);
    if(xa->motionType()!=AxisMotionType::Linear||ya->motionType()!=AxisMotionType::Linear||za->motionType()!=AxisMotionType::Linear)
        return Result<StageWorkingSurface3D,StageWorkingSurface3DError>::failure(StageWorkingSurface3DError::NonLinearStageAxis);
    if(!linearUnit(xa->coordinateUnit())||!linearUnit(ya->coordinateUnit())||!linearUnit(za->coordinateUnit()))
        return Result<StageWorkingSurface3D,StageWorkingSurface3DError>::failure(StageWorkingSurface3DError::UnsupportedStageAxisUnit);
    const auto fits=[](const AxisSpecification&a,double lo,double hi){
        const auto& r=a.operatingRange();return lo>=toMicrometers(r.minimum(),a.coordinateUnit())&&hi<=toMicrometers(r.maximum(),a.coordinateUnit());
    };
    if(!fits(*xa,lo.xMicrometers,hi.xMicrometers)||!fits(*ya,lo.yMicrometers,hi.yMicrometers)||!fits(*za,lo.zMicrometers,hi.zMicrometers))
        return Result<StageWorkingSurface3D,StageWorkingSurface3DError>::failure(StageWorkingSurface3DError::WorkingVolumeOutsideAxisRange);
    for(std::size_t i=0;i<p.size();++i){
        for(std::size_t j=i+1;j<p.size();++j)if(p[i].slideId()==p[j].slideId())
            return Result<StageWorkingSurface3D,StageWorkingSurface3DError>::failure(StageWorkingSurface3DError::DuplicateSlideIdentity);
        const auto&g=p[i].geometry();
        for(unsigned mask=0;mask<8;++mask){
            SlidePoint3D c{(mask&1U)?g.lengthMicrometers():0.0,(mask&2U)?g.widthMicrometers():0.0,(mask&4U)?g.thicknessMicrometers():0.0};
            auto mapped=p[i].toStage(c);
            if(!mapped||!inside(mapped.value(),lo,hi))return Result<StageWorkingSurface3D,StageWorkingSurface3DError>::failure(StageWorkingSurface3DError::SlideOutsideWorkingVolume);
        }
    }
    return Result<StageWorkingSurface3D,StageWorkingSurface3DError>::success(
        StageWorkingSurface3D(stage.componentId(),std::move(x),std::move(y),std::move(z),
            xa->coordinateUnit(),ya->coordinateUnit(),za->coordinateUnit(),lo,hi,std::move(p)));
}
const EquipmentComponentId& StageWorkingSurface3D::stageComponentId()const noexcept{return stageComponentId_;}
const StageAxisId& StageWorkingSurface3D::xAxisId()const noexcept{return xAxisId_;}
const StageAxisId& StageWorkingSurface3D::yAxisId()const noexcept{return yAxisId_;}
const StageAxisId& StageWorkingSurface3D::zAxisId()const noexcept{return zAxisId_;}
AxisCoordinateUnit StageWorkingSurface3D::xAxisUnit()const noexcept{return xUnit_;}
AxisCoordinateUnit StageWorkingSurface3D::yAxisUnit()const noexcept{return yUnit_;}
AxisCoordinateUnit StageWorkingSurface3D::zAxisUnit()const noexcept{return zUnit_;}
const StagePoint3D& StageWorkingSurface3D::minimumBoundary()const noexcept{return minimumBoundary_;}
const StagePoint3D& StageWorkingSurface3D::maximumBoundary()const noexcept{return maximumBoundary_;}
const StageWorkingSurface3D::SlidePlacements& StageWorkingSurface3D::slidePlacements()const noexcept{return placements_;}
const SlidePlacement3D* StageWorkingSurface3D::placementFor(const SlideId&id)const noexcept{
    const auto it=std::find_if(placements_.begin(),placements_.end(),[&](const auto&p){return p.slideId()==id;});
    return it==placements_.end()?nullptr:&*it;
}
bool StageWorkingSurface3D::contains(const StagePoint3D&p)const noexcept{return finite(p)&&inside(p,minimumBoundary_,maximumBoundary_);}
Result<StagePoint3D,SlidePlacement3DError> StageWorkingSurface3D::toStage(const SlideId&id,const SlidePoint3D&p)const{
    const auto* placement=placementFor(id);if(!placement)return Result<StagePoint3D,SlidePlacement3DError>::failure(SlidePlacement3DError::SlideNotFound);
    return placement->toStage(p);
}
Result<SlidePosition3D,SlidePlacement3DError> StageWorkingSurface3D::toSlide(const SlideId&id,const StagePoint3D&p)const{
    const auto* placement=placementFor(id);if(!placement)return Result<SlidePosition3D,SlidePlacement3DError>::failure(SlidePlacement3DError::SlideNotFound);
    auto local=placement->toSlide(p);if(!local)return Result<SlidePosition3D,SlidePlacement3DError>::failure(local.error());
    return Result<SlidePosition3D,SlidePlacement3DError>::success(
        {placement->slideId(),placement->registrationRevision(),local.value(),placement->geometry().contains(local.value()),contains(p)});
}
} // namespace microscope::core
