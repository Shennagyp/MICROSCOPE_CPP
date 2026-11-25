#include "microscope/core/navigation/DualSlideNavigation.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace microscope::core {
namespace {
bool physicalLinear(AxisCoordinateUnit u) {
    return u==AxisCoordinateUnit::Micrometers || u==AxisCoordinateUnit::Millimeters;
}
std::size_t cornerIndex(SlideCorner c) {
    switch(c) {
    case SlideCorner::TopLeft:return 0;
    case SlideCorner::TopRight:return 1;
    case SlideCorner::BottomRight:return 2;
    case SlideCorner::BottomLeft:return 3;
    }
    return 4;
}
bool validSource(ObservationSource s) {
    return s==ObservationSource::HardwareReadback ||
           s==ObservationSource::DeviceEvent ||
           s==ObservationSource::SdkStatus ||
           s==ObservationSource::OperatorConfirmation ||
           s==ObservationSource::CalibratedEstimate;
}
}
DualSlideCarrierSpecification::DualSlideCarrierSpecification(
    EquipmentComponentId c,StageAxisId x,StageAxisId y,CarrierHorizontalDirection d,
    AxisCoordinateUnit xu,AxisCoordinateUnit yu,AxisOperatingRange xr,AxisOperatingRange yr)
    :stageComponentId_(std::move(c)),horizontalAxisId_(std::move(x)),
     transverseAxisId_(std::move(y)),direction_(d),horizontalUnit_(xu),transverseUnit_(yu),
     horizontalRange_(xr),transverseRange_(yr) {}
Result<DualSlideCarrierSpecification,DualSlideCarrierSpecificationError>
DualSlideCarrierSpecification::create(
    const StageSpecification& stage,StageAxisId x,StageAxisId y,CarrierHorizontalDirection d) {
    if(x==y)return Result<DualSlideCarrierSpecification,DualSlideCarrierSpecificationError>::failure(
        DualSlideCarrierSpecificationError::SamePlaneAxis);
    auto xi=std::find_if(stage.axes().begin(),stage.axes().end(),[&](const auto&a){return a.id()==x;});
    if(xi==stage.axes().end())return Result<DualSlideCarrierSpecification,DualSlideCarrierSpecificationError>::failure(
        DualSlideCarrierSpecificationError::MissingHorizontalAxis);
    auto yi=std::find_if(stage.axes().begin(),stage.axes().end(),[&](const auto&a){return a.id()==y;});
    if(yi==stage.axes().end())return Result<DualSlideCarrierSpecification,DualSlideCarrierSpecificationError>::failure(
        DualSlideCarrierSpecificationError::MissingTransverseAxis);
    if(xi->motionType()!=AxisMotionType::Linear || yi->motionType()!=AxisMotionType::Linear)
        return Result<DualSlideCarrierSpecification,DualSlideCarrierSpecificationError>::failure(
            DualSlideCarrierSpecificationError::NonLinearPlaneAxis);
    if(!physicalLinear(xi->coordinateUnit()) || !physicalLinear(yi->coordinateUnit()))
        return Result<DualSlideCarrierSpecification,DualSlideCarrierSpecificationError>::failure(
            DualSlideCarrierSpecificationError::NonPhysicalPlaneAxisUnit);
    if(d!=CarrierHorizontalDirection::PositiveStageXFromLeftToRight &&
       d!=CarrierHorizontalDirection::NegativeStageXFromLeftToRight)
        return Result<DualSlideCarrierSpecification,DualSlideCarrierSpecificationError>::failure(
            DualSlideCarrierSpecificationError::InvalidHorizontalDirection);
    return Result<DualSlideCarrierSpecification,DualSlideCarrierSpecificationError>::success(
        DualSlideCarrierSpecification(stage.componentId(),std::move(x),std::move(y),d,
            xi->coordinateUnit(),yi->coordinateUnit(),
            xi->operatingRange(),yi->operatingRange()));
}
const EquipmentComponentId& DualSlideCarrierSpecification::stageComponentId()const noexcept{return stageComponentId_;}
const StageAxisId& DualSlideCarrierSpecification::horizontalAxisId()const noexcept{return horizontalAxisId_;}
const StageAxisId& DualSlideCarrierSpecification::transverseAxisId()const noexcept{return transverseAxisId_;}
CarrierHorizontalDirection DualSlideCarrierSpecification::horizontalDirection()const noexcept{return direction_;}

Result<MoveAxesTo,SlideNavigationCommandError>
DualSlideCarrierSpecification::moveTo(const StagePlanePoint& p) const {
    if(!std::isfinite(p.horizontalMicrometers)||!std::isfinite(p.transverseMicrometers))
        return Result<MoveAxesTo,SlideNavigationCommandError>::failure(
            SlideNavigationCommandError::InvalidStageCoordinate);
    const double x=horizontalUnit_==AxisCoordinateUnit::Millimeters
        ? p.horizontalMicrometers/1000.0 : p.horizontalMicrometers;
    const double y=transverseUnit_==AxisCoordinateUnit::Millimeters
        ? p.transverseMicrometers/1000.0 : p.transverseMicrometers;
    if(x<horizontalRange_.minimum()||x>horizontalRange_.maximum()||
       y<transverseRange_.minimum()||y>transverseRange_.maximum())
        return Result<MoveAxesTo,SlideNavigationCommandError>::failure(
            SlideNavigationCommandError::OutsideStageTravel);
    auto xt=AxisPosition::create(horizontalAxisId_,x,horizontalUnit_);
    auto yt=AxisPosition::create(transverseAxisId_,y,transverseUnit_);
    if(!xt||!yt)return Result<MoveAxesTo,SlideNavigationCommandError>::failure(
        SlideNavigationCommandError::InvalidStageCoordinate);
    auto command=MoveAxesTo::create({std::move(xt).value(),std::move(yt).value()});
    if(!command)return Result<MoveAxesTo,SlideNavigationCommandError>::failure(
        SlideNavigationCommandError::InvalidStageCoordinate);
    return Result<MoveAxesTo,SlideNavigationCommandError>::success(std::move(command).value());
}


SlideGeometry::SlideGeometry(double w,double h):width_(w),height_(h){}
Result<SlideGeometry,SlideGeometryError> SlideGeometry::create(double w,double h) {
    if(!std::isfinite(w)||!std::isfinite(h))
        return Result<SlideGeometry,SlideGeometryError>::failure(SlideGeometryError::NonFiniteDimension);
    if(w<=0||h<=0)
        return Result<SlideGeometry,SlideGeometryError>::failure(SlideGeometryError::NonPositiveDimension);
    return Result<SlideGeometry,SlideGeometryError>::success(SlideGeometry(w,h));
}
double SlideGeometry::widthMicrometers()const noexcept{return width_;}
double SlideGeometry::heightMicrometers()const noexcept{return height_;}
bool SlideGeometry::contains(double x,double y)const noexcept {
    return std::isfinite(x)&&std::isfinite(y)&&x>=0&&y>=0&&x<=width_&&y<=height_;
}

SlideCornerMeasurement::SlideCornerMeasurement(SlideCorner c,StagePlanePoint p,ObservationSource s,double u)
    :corner_(c),stage_(p),source_(s),uncertainty_(u){}
Result<SlideCornerMeasurement,SlideCornerMeasurementError>
SlideCornerMeasurement::create(SlideCorner c,StagePlanePoint p,ObservationSource s,double u) {
    if(cornerIndex(c)>=4)return Result<SlideCornerMeasurement,SlideCornerMeasurementError>::failure(
        SlideCornerMeasurementError::InvalidCorner);
    if(!std::isfinite(p.horizontalMicrometers)||!std::isfinite(p.transverseMicrometers))
        return Result<SlideCornerMeasurement,SlideCornerMeasurementError>::failure(
            SlideCornerMeasurementError::NonFiniteCoordinate);
    if(!std::isfinite(u)||u<0)return Result<SlideCornerMeasurement,SlideCornerMeasurementError>::failure(
        SlideCornerMeasurementError::InvalidUncertainty);
    if(s==ObservationSource::Unknown)return Result<SlideCornerMeasurement,SlideCornerMeasurementError>::failure(
        SlideCornerMeasurementError::UnknownProvenance);
    if(!validSource(s))return Result<SlideCornerMeasurement,SlideCornerMeasurementError>::failure(
        SlideCornerMeasurementError::InvalidProvenance);
    return Result<SlideCornerMeasurement,SlideCornerMeasurementError>::success(
        SlideCornerMeasurement(c,p,s,u));
}
SlideCorner SlideCornerMeasurement::corner()const noexcept{return corner_;}
const StagePlanePoint& SlideCornerMeasurement::stagePosition()const noexcept{return stage_;}
ObservationSource SlideCornerMeasurement::source()const noexcept{return source_;}
double SlideCornerMeasurement::uncertaintyMicrometers()const noexcept{return uncertainty_;}

SlideRegistration::SlideRegistration(
    SlideId id,DualSlideSlot s,SlideGeometry g,std::array<SlideCornerMeasurement,4> c,
    std::array<double,6> t,std::array<double,4> residuals,double m,double r)
    :slideId_(std::move(id)),slot_(s),geometry_(g),corners_(std::move(c)),
     transform_(t),cornerResiduals_(residuals),maxResidual_(m),rmsResidual_(r) {}
Result<SlideRegistration,SlideRegistrationError> SlideRegistration::create(
    SlideId id,DualSlideSlot slot,SlideGeometry g,
    std::array<SlideCornerMeasurement,4> input,double maxAllowed) {
    if(!std::isfinite(maxAllowed)||maxAllowed<0)
        return Result<SlideRegistration,SlideRegistrationError>::failure(SlideRegistrationError::InvalidResidualLimit);
    std::array<std::optional<SlideCornerMeasurement>,4> ordered;
    for(const auto&m:input) {
        const auto i=cornerIndex(m.corner());
        if(i>=4)return Result<SlideRegistration,SlideRegistrationError>::failure(SlideRegistrationError::MissingCorner);
        if(ordered[i])return Result<SlideRegistration,SlideRegistrationError>::failure(SlideRegistrationError::DuplicateCorner);
        ordered[i]=m;
    }
    for(const auto&m:ordered)if(!m)
        return Result<SlideRegistration,SlideRegistrationError>::failure(SlideRegistrationError::MissingCorner);
    std::array<SlideCornerMeasurement,4> corners={*ordered[0],*ordered[1],*ordered[2],*ordered[3]};
    const double hx=g.widthMicrometers()/2.0,hy=g.heightMicrometers()/2.0;
    const double lx[4]={0,g.widthMicrometers(),g.widthMicrometers(),0};
    const double ly[4]={0,0,g.heightMicrometers(),g.heightMicrometers()};
    double mx=0,my=0;
    for(const auto&m:corners) {
        mx+=m.stagePosition().horizontalMicrometers/4.0;
        my+=m.stagePosition().transverseMicrometers/4.0;
    }
    double sx=0,skx=0,sy=0,sky=0;
    for(int i=0;i<4;++i) {
        const double dx=lx[i]-hx,dy=ly[i]-hy;
        const double px=corners[i].stagePosition().horizontalMicrometers-mx;
        const double py=corners[i].stagePosition().transverseMicrometers-my;
        sx+=dx*px; skx+=dy*px; sy+=dx*py; sky+=dy*py;
    }
    const double ax=sx/(4*hx*hx),bx=skx/(4*hy*hy);
    const double ay=sy/(4*hx*hx),by=sky/(4*hy*hy);
    const double tx=mx-ax*hx-bx*hy,ty=my-ay*hx-by*hy;
    const double determinant=ax*by-bx*ay;
    const double scale=std::abs(ax*by)+std::abs(bx*ay);
    if(!std::isfinite(determinant) ||
       std::abs(determinant)<=std::numeric_limits<double>::epsilon()*std::max(1.0,scale)*64)
        return Result<SlideRegistration,SlideRegistrationError>::failure(SlideRegistrationError::DegenerateTransform);
    double sum=0,maxResidual=0;
    std::array<double,4> residuals{};
    for(int i=0;i<4;++i) {
        const double x=tx+ax*lx[i]+bx*ly[i];
        const double y=ty+ay*lx[i]+by*ly[i];
        const double dx=x-corners[i].stagePosition().horizontalMicrometers;
        const double dy=y-corners[i].stagePosition().transverseMicrometers;
        const double residual=std::hypot(dx,dy);
        residuals[static_cast<std::size_t>(i)]=residual;
        sum+=residual*residual;
        maxResidual=std::max(maxResidual,residual);
    }
    if(!std::isfinite(maxResidual)||maxResidual>maxAllowed)
        return Result<SlideRegistration,SlideRegistrationError>::failure(SlideRegistrationError::FitResidualExceeded);
    return Result<SlideRegistration,SlideRegistrationError>::success(
        SlideRegistration(std::move(id),slot,g,std::move(corners),
            {tx,ax,bx,ty,ay,by},residuals,maxResidual,std::sqrt(sum/4.0)));
}
const SlideId& SlideRegistration::slideId()const noexcept{return slideId_;}
DualSlideSlot SlideRegistration::slot()const noexcept{return slot_;}
const SlideGeometry& SlideRegistration::geometry()const noexcept{return geometry_;}
const std::array<SlideCornerMeasurement,4>& SlideRegistration::cornerMeasurements()const noexcept{return corners_;}
double SlideRegistration::maximumFitResidualMicrometers()const noexcept{return maxResidual_;}
double SlideRegistration::rmsFitResidualMicrometers()const noexcept{return rmsResidual_;}
const std::array<double,4>& SlideRegistration::cornerFitResidualsMicrometers()const noexcept{return cornerResiduals_;}
Result<StagePlanePoint,SlideRegistrationError> SlideRegistration::toStage(SlideLocalPoint p)const {
    if(!std::isfinite(p.xMicrometers)||!std::isfinite(p.yMicrometers))
        return Result<StagePlanePoint,SlideRegistrationError>::failure(SlideRegistrationError::NonFiniteLocalPoint);
    if(!geometry_.contains(p.xMicrometers,p.yMicrometers))
        return Result<StagePlanePoint,SlideRegistrationError>::failure(SlideRegistrationError::LocalPointOutsideSlide);
    return Result<StagePlanePoint,SlideRegistrationError>::success({
        transform_[0]+transform_[1]*p.xMicrometers+transform_[2]*p.yMicrometers,
        transform_[3]+transform_[4]*p.xMicrometers+transform_[5]*p.yMicrometers});
}
Result<SlideLocalPoint,SlideRegistrationError> SlideRegistration::toSlide(StagePlanePoint p)const {
    if(!std::isfinite(p.horizontalMicrometers)||!std::isfinite(p.transverseMicrometers))
        return Result<SlideLocalPoint,SlideRegistrationError>::failure(SlideRegistrationError::NonFiniteStagePoint);
    const double dx=p.horizontalMicrometers-transform_[0],dy=p.transverseMicrometers-transform_[3];
    const double det=transform_[1]*transform_[5]-transform_[2]*transform_[4];
    return Result<SlideLocalPoint,SlideRegistrationError>::success({
        (dx*transform_[5]-transform_[2]*dy)/det,
        (transform_[1]*dy-dx*transform_[4])/det});
}

SlideSlotState::SlideSlotState(DualSlideSlot s,std::optional<SlideId> id,std::optional<SlideRegistration> r)
    :slot_(s),slideId_(std::move(id)),registration_(std::move(r)){}
Result<SlideSlotState,SlideSlotStateError> SlideSlotState::create(
    DualSlideSlot s,std::optional<SlideId> id,std::optional<SlideRegistration> r) {
    if(s!=DualSlideSlot::Left&&s!=DualSlideSlot::Right)
        return Result<SlideSlotState,SlideSlotStateError>::failure(SlideSlotStateError::InvalidSlot);
    if(r&&!id)return Result<SlideSlotState,SlideSlotStateError>::failure(SlideSlotStateError::RegistrationWithoutSlide);
    if(r&&r->slideId()!=*id)return Result<SlideSlotState,SlideSlotStateError>::failure(SlideSlotStateError::RegistrationSlideMismatch);
    if(r&&r->slot()!=s)return Result<SlideSlotState,SlideSlotStateError>::failure(SlideSlotStateError::RegistrationSlotMismatch);
    return Result<SlideSlotState,SlideSlotStateError>::success(SlideSlotState(s,std::move(id),std::move(r)));
}
DualSlideSlot SlideSlotState::slot()const noexcept{return slot_;}
const std::optional<SlideId>& SlideSlotState::slideId()const noexcept{return slideId_;}
const std::optional<SlideRegistration>& SlideSlotState::registration()const noexcept{return registration_;}

DualSlideCarrierState::DualSlideCarrierState(DualSlideCarrierSpecification s,SlideSlotState l,SlideSlotState r)
    :specification_(std::move(s)),left_(std::move(l)),right_(std::move(r)){}
Result<DualSlideCarrierState,DualSlideCarrierStateError> DualSlideCarrierState::create(
    DualSlideCarrierSpecification s,SlideSlotState l,SlideSlotState r) {
    if(l.slot()!=DualSlideSlot::Left)return Result<DualSlideCarrierState,DualSlideCarrierStateError>::failure(DualSlideCarrierStateError::WrongLeftSlot);
    if(r.slot()!=DualSlideSlot::Right)return Result<DualSlideCarrierState,DualSlideCarrierStateError>::failure(DualSlideCarrierStateError::WrongRightSlot);
    if(l.slideId()&&r.slideId()&&*l.slideId()==*r.slideId())
        return Result<DualSlideCarrierState,DualSlideCarrierStateError>::failure(DualSlideCarrierStateError::DuplicateSlideId);
    return Result<DualSlideCarrierState,DualSlideCarrierStateError>::success(
        DualSlideCarrierState(std::move(s),std::move(l),std::move(r)));
}
const DualSlideCarrierSpecification& DualSlideCarrierState::specification()const noexcept{return specification_;}
const SlideSlotState& DualSlideCarrierState::left()const noexcept{return left_;}
const SlideSlotState& DualSlideCarrierState::right()const noexcept{return right_;}
const SlideRegistration* DualSlideCarrierState::registrationFor(const SlideId&id)const noexcept {
    if(left_.slideId()&&*left_.slideId()==id&&left_.registration())return &*left_.registration();
    if(right_.slideId()&&*right_.slideId()==id&&right_.registration())return &*right_.registration();
    return nullptr;
}

Result<MoveAxesTo,SlideNavigationCommandError> DualSlideCarrierState::commandFor(
    const SlideId& slideId,SlideLocalPoint localPoint) const {
    const auto* registration=registrationFor(slideId);
    if(!registration) {
        const bool loaded=(left_.slideId()&&*left_.slideId()==slideId)||
                          (right_.slideId()&&*right_.slideId()==slideId);
        return Result<MoveAxesTo,SlideNavigationCommandError>::failure(
            loaded?SlideNavigationCommandError::SlideNotRegistered:
                   SlideNavigationCommandError::SlideNotInCarrier);
    }
    auto stagePoint=registration->toStage(localPoint);
    if(!stagePoint)return Result<MoveAxesTo,SlideNavigationCommandError>::failure(
        SlideNavigationCommandError::InvalidStageCoordinate);
    return specification_.moveTo(stagePoint.value());
}

} // namespace microscope::core
