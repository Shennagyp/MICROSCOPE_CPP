#include "microscope/core/navigation/DualSlideNavigation.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace microscope::core {
namespace {
Result<MoveAxesTo,SlideNavigationCommandError> commandForStagePoint(
    const DualSlideCarrierSpecification& specification, const StagePlanePoint& point) {
    if (!std::isfinite(point.horizontalMicrometers) ||
        !std::isfinite(point.transverseMicrometers)) {
        return Result<MoveAxesTo,SlideNavigationCommandError>::failure(
            SlideNavigationCommandError::InvalidStageCoordinate);
    }
    const double horizontal = specification.horizontalUnit() == AxisCoordinateUnit::Millimeters
        ? point.horizontalMicrometers / 1000.0 : point.horizontalMicrometers;
    const double transverse = specification.transverseUnit() == AxisCoordinateUnit::Millimeters
        ? point.transverseMicrometers / 1000.0 : point.transverseMicrometers;
    const auto& horizontalRange = specification.horizontalRange();
    const auto& transverseRange = specification.transverseRange();
    if (horizontal < horizontalRange.minimum() || horizontal > horizontalRange.maximum() ||
        transverse < transverseRange.minimum() || transverse > transverseRange.maximum()) {
        return Result<MoveAxesTo,SlideNavigationCommandError>::failure(
            SlideNavigationCommandError::OutsideStageTravel);
    }
    auto x = AxisPosition::create(specification.horizontalAxisId(), horizontal,
        specification.horizontalUnit());
    auto y = AxisPosition::create(specification.transverseAxisId(), transverse,
        specification.transverseUnit());
    if (!x || !y) {
        return Result<MoveAxesTo,SlideNavigationCommandError>::failure(
            SlideNavigationCommandError::InvalidStageCoordinate);
    }
    auto command = MoveAxesTo::create({std::move(x).value(), std::move(y).value()});
    if (!command) {
        return Result<MoveAxesTo,SlideNavigationCommandError>::failure(
            SlideNavigationCommandError::InvalidStageCoordinate);
    }
    return Result<MoveAxesTo,SlideNavigationCommandError>::success(std::move(command).value());
}
}

namespace {
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
    SlideId id,CalibrationRevisionId revision,DualSlideSlot s,SlideGeometry g,
    std::array<SlideCornerMeasurement,4> c,std::array<double,6> t,
    std::array<double,4> residuals,double cornerUncertainty,double acceptedThreshold,double m,double r)
    :slideId_(std::move(id)),revision_(std::move(revision)),slot_(s),geometry_(g),corners_(std::move(c)),
     transform_(t),cornerResiduals_(residuals),maximumCornerUncertainty_(cornerUncertainty),
     acceptedMaximumFitResidualMicrometers_(acceptedThreshold),maxResidual_(m),rmsResidual_(r) {}
Result<SlideRegistration,SlideRegistrationError> SlideRegistration::create(
    SlideId id,CalibrationRevisionId revision,DualSlideSlot slot,SlideGeometry g,
    std::array<SlideCornerMeasurement,4> input,double maxAllowed) {
    if(slot!=DualSlideSlot::Left&&slot!=DualSlideSlot::Right)
        return Result<SlideRegistration,SlideRegistrationError>::failure(SlideRegistrationError::InvalidSlot);
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
    double sum=0,maxResidual=0,maximumCornerUncertainty=0;
    std::array<double,4> residuals{};
    for(const auto& corner:corners)
        maximumCornerUncertainty=std::max(maximumCornerUncertainty,corner.uncertaintyMicrometers());
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
        SlideRegistration(std::move(id),std::move(revision),slot,g,std::move(corners),
            {tx,ax,bx,ty,ay,by},residuals,maximumCornerUncertainty,maxAllowed,maxResidual,std::sqrt(sum/4.0)));
}
const SlideId& SlideRegistration::slideId()const noexcept{return slideId_;}
const CalibrationRevisionId& SlideRegistration::revision()const noexcept{return revision_;}
DualSlideSlot SlideRegistration::slot()const noexcept{return slot_;}
const SlideGeometry& SlideRegistration::geometry()const noexcept{return geometry_;}
const std::array<SlideCornerMeasurement,4>& SlideRegistration::cornerMeasurements()const noexcept{return corners_;}
double SlideRegistration::maximumFitResidualMicrometers()const noexcept{return maxResidual_;}
double SlideRegistration::acceptedMaximumFitResidualMicrometers()const noexcept{return acceptedMaximumFitResidualMicrometers_;}
double SlideRegistration::rmsFitResidualMicrometers()const noexcept{return rmsResidual_;}
const std::array<double,4>& SlideRegistration::cornerFitResidualsMicrometers()const noexcept{return cornerResiduals_;}
Result<StagePlanePoint,SlideRegistrationError> SlideRegistration::toStage(SlideLocalPoint p)const {
    if(!std::isfinite(p.xMicrometers)||!std::isfinite(p.yMicrometers))
        return Result<StagePlanePoint,SlideRegistrationError>::failure(SlideRegistrationError::NonFiniteLocalPoint);
    if(!geometry_.contains(p.xMicrometers,p.yMicrometers))
        return Result<StagePlanePoint,SlideRegistrationError>::failure(SlideRegistrationError::LocalPointOutsideSlide);
    const StagePlanePoint mapped{
        transform_[0]+transform_[1]*p.xMicrometers+transform_[2]*p.yMicrometers,
        transform_[3]+transform_[4]*p.xMicrometers+transform_[5]*p.yMicrometers};
    if(!std::isfinite(mapped.horizontalMicrometers)||!std::isfinite(mapped.transverseMicrometers))
        return Result<StagePlanePoint,SlideRegistrationError>::failure(SlideRegistrationError::NonFiniteStagePoint);
    return Result<StagePlanePoint,SlideRegistrationError>::success(mapped);
}
Result<SlideLocalPoint,SlideRegistrationError> SlideRegistration::toSlide(StagePlanePoint p)const {
    if(!std::isfinite(p.horizontalMicrometers)||!std::isfinite(p.transverseMicrometers))
        return Result<SlideLocalPoint,SlideRegistrationError>::failure(SlideRegistrationError::NonFiniteStagePoint);
    const double dx=p.horizontalMicrometers-transform_[0],dy=p.transverseMicrometers-transform_[3];
    const double det=transform_[1]*transform_[5]-transform_[2]*transform_[4];
    const SlideLocalPoint mapped{
        (dx*transform_[5]-transform_[2]*dy)/det,
        (transform_[1]*dy-dx*transform_[4])/det};
    if(!std::isfinite(mapped.xMicrometers)||!std::isfinite(mapped.yMicrometers))
        return Result<SlideLocalPoint,SlideRegistrationError>::failure(SlideRegistrationError::NonFiniteLocalPoint);
    return Result<SlideLocalPoint,SlideRegistrationError>::success(mapped);
}

Result<SlidePositionEstimate,SlideRegistrationError>
SlideRegistration::estimateSlidePosition(
    StagePlanePoint p,double ex,double ey,
    StagePositionEvidence hx,StagePositionEvidence hy,
    ObservationSource sx,ObservationSource sy) const {
    if(!std::isfinite(ex)||!std::isfinite(ey)||ex<0||ey<0)
        return Result<SlidePositionEstimate,SlideRegistrationError>::failure(
            SlideRegistrationError::NonFiniteStagePoint);
    auto local=toSlide(p);
    if(!local)return Result<SlidePositionEstimate,SlideRegistrationError>::failure(local.error());
    // For an affine rectangle map the four-corner interpolation weights have
    // an L1 norm no greater than 1.5 anywhere inside the slide. This scales the
    // maximum corner uncertainty/residual into a conservative stage-plane
    // registration envelope before the inverse-transform propagation below.
    if(!geometry_.contains(local.value().xMicrometers,local.value().yMicrometers))
        return Result<SlidePositionEstimate,SlideRegistrationError>::failure(
            SlideRegistrationError::LocalPointOutsideSlide);
    const double det=std::abs(transform_[1]*transform_[5]-transform_[2]*transform_[4]);
    const double registrationBound=1.5*(maximumCornerUncertainty_+maxResidual_);
    const double stageXBound=ex+registrationBound;
    const double stageYBound=ey+registrationBound;
    const double xBound=(std::abs(transform_[5])*stageXBound+
                         std::abs(transform_[2])*stageYBound)/det;
    const double yBound=(std::abs(transform_[4])*stageXBound+
                         std::abs(transform_[1])*stageYBound)/det;
    if(!std::isfinite(xBound)||!std::isfinite(yBound))
        return Result<SlidePositionEstimate,SlideRegistrationError>::failure(
            SlideRegistrationError::NonFiniteLocalPoint);
    return Result<SlidePositionEstimate,SlideRegistrationError>::success(
        SlidePositionEstimate{local.value(),xBound,yBound,hx,hy,sx,sy});
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
    if(l.registration()&&r.registration()) {
        const auto centerX=[](const SlideRegistration& registration) {
            double sum=0.0;
            for(const auto& corner:registration.cornerMeasurements())
                sum+=corner.stagePosition().horizontalMicrometers/4.0;
            return sum;
        };
        const double leftX=centerX(*l.registration());
        const double rightX=centerX(*r.registration());
        const bool ordered=s.horizontalDirection()==CarrierHorizontalDirection::PositiveStageXFromLeftToRight
            ? rightX>leftX : rightX<leftX;
        if(!ordered)
            return Result<DualSlideCarrierState,DualSlideCarrierStateError>::failure(
                DualSlideCarrierStateError::SlideSlotOrderMismatch);
    }
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

SlideNavigationTarget::SlideNavigationTarget(
    SlideId slideId,SlideLocalPoint point,CalibrationRevisionId revision)
    :slideId_(std::move(slideId)),point_(point),registrationRevision_(std::move(revision)) {}
const SlideId& SlideNavigationTarget::slideId()const noexcept{return slideId_;}
const SlideLocalPoint& SlideNavigationTarget::point()const noexcept{return point_;}
const CalibrationRevisionId& SlideNavigationTarget::registrationRevision()const noexcept{return registrationRevision_;}

Result<SlideNavigationTarget,SlideNavigationTargetError>
DualSlideCarrierState::createTarget(const SlideId& slideId,SlideLocalPoint point) const {
    const auto* registration=registrationFor(slideId);
    if(!registration) {
        const bool loaded=(left_.slideId()&&*left_.slideId()==slideId)||
                          (right_.slideId()&&*right_.slideId()==slideId);
        return Result<SlideNavigationTarget,SlideNavigationTargetError>::failure(
            loaded?SlideNavigationTargetError::SlideNotRegistered:
                   SlideNavigationTargetError::SlideNotInCarrier);
    }
    if(!registration->toStage(point))
        return Result<SlideNavigationTarget,SlideNavigationTargetError>::failure(
            SlideNavigationTargetError::PointOutsideSlide);
    return Result<SlideNavigationTarget,SlideNavigationTargetError>::success(
        SlideNavigationTarget(slideId,point,registration->revision()));
}

Result<MoveAxesTo,SlideNavigationCommandError> DualSlideCarrierState::commandFor(
    const SlideNavigationTarget& target) const {
    const auto* registration=registrationFor(target.slideId());
    if(!registration) {
        const bool loaded=(left_.slideId()&&*left_.slideId()==target.slideId())||
                          (right_.slideId()&&*right_.slideId()==target.slideId());
        return Result<MoveAxesTo,SlideNavigationCommandError>::failure(
            loaded?SlideNavigationCommandError::SlideNotRegistered:
                   SlideNavigationCommandError::SlideNotInCarrier);
    }
    if(registration->revision()!=target.registrationRevision())
        return Result<MoveAxesTo,SlideNavigationCommandError>::failure(
            SlideNavigationCommandError::RegistrationChanged);
    auto stagePoint=registration->toStage(target.point());
    if(!stagePoint)
        return Result<MoveAxesTo,SlideNavigationCommandError>::failure(
            SlideNavigationCommandError::InvalidStageCoordinate);
    return commandForStagePoint(specification_, stagePoint.value());
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

namespace {
double toMicrometers(double value,AxisCoordinateUnit unit) {
    return unit==AxisCoordinateUnit::Millimeters?value*1000.0:value;
}
double errorToMicrometers(double value,AxisCoordinateUnit unit) {
    return unit==AxisCoordinateUnit::Millimeters?value*1000.0:value;
}
}
Result<SlidePositionEstimate,CurrentSlidePositionError>
DualSlideCarrierState::currentSlidePosition(
    const SlideId& slideId,const StageAxisPositionState& horizontal,
    const StageAxisPositionState& transverse) const {
    const auto* registration=registrationFor(slideId);
    if(!registration) {
        const bool loaded=(left_.slideId()&&*left_.slideId()==slideId)||
                          (right_.slideId()&&*right_.slideId()==slideId);
        return Result<SlidePositionEstimate,CurrentSlidePositionError>::failure(
            loaded?CurrentSlidePositionError::SlideNotRegistered:
                   CurrentSlidePositionError::SlideNotInCarrier);
    }
    if(horizontal.stageComponentId()!=specification_.stageComponentId()||
       transverse.stageComponentId()!=specification_.stageComponentId())
        return Result<SlidePositionEstimate,CurrentSlidePositionError>::failure(
            CurrentSlidePositionError::StageComponentMismatch);
    if(horizontal.axisId()!=specification_.horizontalAxisId())
        return Result<SlidePositionEstimate,CurrentSlidePositionError>::failure(
            CurrentSlidePositionError::HorizontalAxisMismatch);
    if(transverse.axisId()!=specification_.transverseAxisId())
        return Result<SlidePositionEstimate,CurrentSlidePositionError>::failure(
            CurrentSlidePositionError::TransverseAxisMismatch);
    if(horizontal.unit()!=specification_.horizontalUnit()||
       transverse.unit()!=specification_.transverseUnit())
        return Result<SlidePositionEstimate,CurrentSlidePositionError>::failure(
            CurrentSlidePositionError::AxisUnitMismatch);
    if(!horizontal.positionIsUsable()||!transverse.positionIsUsable()||
       !horizontal.position()||!transverse.position())
        return Result<SlidePositionEstimate,CurrentSlidePositionError>::failure(
            CurrentSlidePositionError::PositionUnknown);
    if(!horizontal.accumulatedPositionErrorBound()||
       !transverse.accumulatedPositionErrorBound())
        return Result<SlidePositionEstimate,CurrentSlidePositionError>::failure(
            CurrentSlidePositionError::PositionUncertaintyUnknown);
    const StagePlanePoint machinePoint{
        toMicrometers(horizontal.position()->value(),horizontal.unit()),
        toMicrometers(transverse.position()->value(),transverse.unit())};
    auto estimate=registration->estimateSlidePosition(
        machinePoint,
        errorToMicrometers(*horizontal.accumulatedPositionErrorBound(),horizontal.unit()),
        errorToMicrometers(*transverse.accumulatedPositionErrorBound(),transverse.unit()),
        horizontal.evidence(),transverse.evidence(),horizontal.source(),transverse.source());
    if(!estimate) {
        const auto error=estimate.error()==SlideRegistrationError::LocalPointOutsideSlide
            ? CurrentSlidePositionError::PositionOutsideSlide
            : CurrentSlidePositionError::InvalidEstimate;
        return Result<SlidePositionEstimate,CurrentSlidePositionError>::failure(error);
    }
    return Result<SlidePositionEstimate,CurrentSlidePositionError>::success(
        std::move(estimate).value());
}

} // namespace microscope::core
