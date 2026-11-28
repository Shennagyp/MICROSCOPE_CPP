#include "microscope/application/navigation/StageSlotRegistration.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace microscope::application::navigation {
namespace {

std::size_t cornerIndex(StageSlotRegistrationCorner corner) noexcept {
    switch(corner) {
    case StageSlotRegistrationCorner::TopLeft:return 0;
    case StageSlotRegistrationCorner::TopRight:return 1;
    case StageSlotRegistrationCorner::BottomRight:return 2;
    case StageSlotRegistrationCorner::BottomLeft:return 3;
    }
    return 4;
}

bool validSource(core::ObservationSource source) noexcept {
    switch(source) {
    case core::ObservationSource::HardwareReadback:
    case core::ObservationSource::DeviceEvent:
    case core::ObservationSource::SdkStatus:
    case core::ObservationSource::OperatorConfirmation:
    case core::ObservationSource::CalibratedEstimate:
    case core::ObservationSource::AdapterReport:
        return true;
    case core::ObservationSource::Unknown:
        return false;
    }
    return false;
}

double slideXExtentNanometres(const core::SlideGeometry& geometry) noexcept {
    return geometry.xExtentMicrometers()*1'000.0;
}
double slideYExtentNanometres(const core::SlideGeometry& geometry) noexcept {
    return geometry.yExtentMicrometers()*1'000.0;
}

} // namespace

StageSlotCornerMeasurement::StageSlotCornerMeasurement(
    StageSlotRegistrationCorner corner,StagePlanePointNanometres stagePosition,
    core::ObservationSource source,double uncertainty)
    :corner_(corner),stagePosition_(stagePosition),source_(source),
     uncertaintyNanometres_(uncertainty) {}

core::Result<StageSlotCornerMeasurement,StageSlotCornerMeasurementError>
StageSlotCornerMeasurement::create(
    StageSlotRegistrationCorner corner,StagePlanePointNanometres position,
    core::ObservationSource source,double uncertainty) {
    if(cornerIndex(corner)>=4)
        return core::Result<StageSlotCornerMeasurement,
            StageSlotCornerMeasurementError>::failure(
                StageSlotCornerMeasurementError::InvalidCorner);
    if(!std::isfinite(position.x)||!std::isfinite(position.y))
        return core::Result<StageSlotCornerMeasurement,
            StageSlotCornerMeasurementError>::failure(
                StageSlotCornerMeasurementError::NonFiniteCoordinate);
    if(!std::isfinite(uncertainty)||uncertainty<0.0)
        return core::Result<StageSlotCornerMeasurement,
            StageSlotCornerMeasurementError>::failure(
                StageSlotCornerMeasurementError::InvalidUncertainty);
    if(source==core::ObservationSource::Unknown)
        return core::Result<StageSlotCornerMeasurement,
            StageSlotCornerMeasurementError>::failure(
                StageSlotCornerMeasurementError::UnknownProvenance);
    if(!validSource(source))
        return core::Result<StageSlotCornerMeasurement,
            StageSlotCornerMeasurementError>::failure(
                StageSlotCornerMeasurementError::InvalidProvenance);
    return core::Result<StageSlotCornerMeasurement,
        StageSlotCornerMeasurementError>::success(
            StageSlotCornerMeasurement(corner,position,source,uncertainty));
}

StageSlotRegistrationCorner StageSlotCornerMeasurement::corner() const noexcept { return corner_; }
const StagePlanePointNanometres&
StageSlotCornerMeasurement::stagePosition() const noexcept { return stagePosition_; }
core::ObservationSource StageSlotCornerMeasurement::source() const noexcept { return source_; }
double StageSlotCornerMeasurement::uncertaintyNanometres() const noexcept {
    return uncertaintyNanometres_;
}

StageSlotRegistration::StageSlotRegistration(
    core::StageSlotId slotId,core::SlideId slideId,std::uint64_t slideRevision,
    core::CalibrationRevisionId calibrationRevision,core::SlideGeometry geometry,
    std::array<StageSlotCornerMeasurement,4> corners,std::array<double,6> transform,
    std::array<double,4> residuals,double cornerUncertainty,double acceptedThreshold,
    double maximumResidual,double rmsResidual)
    :stageSlotId_(std::move(slotId)),slideId_(std::move(slideId)),
     slideRevision_(slideRevision),calibrationRevision_(std::move(calibrationRevision)),
     slideGeometry_(geometry),corners_(std::move(corners)),transform_(transform),
     cornerResiduals_(residuals),maximumCornerUncertaintyNanometres_(cornerUncertainty),
     acceptedMaximumFitResidualNanometres_(acceptedThreshold),
     maximumFitResidualNanometres_(maximumResidual),rmsFitResidualNanometres_(rmsResidual) {}

core::Result<StageSlotRegistration,StageSlotRegistrationError>
StageSlotRegistration::create(
    core::StageSlotId slotId,core::SlideId slideId,std::uint64_t slideRevision,
    core::CalibrationRevisionId calibrationRevision,core::SlideGeometry geometry,
    std::array<StageSlotCornerMeasurement,4> input,double maximumAllowedResidual) {
    if(slideRevision==0)
        return core::Result<StageSlotRegistration,StageSlotRegistrationError>::failure(
            StageSlotRegistrationError::InvalidSlideRevision);
    if(!std::isfinite(maximumAllowedResidual)||maximumAllowedResidual<0.0)
        return core::Result<StageSlotRegistration,StageSlotRegistrationError>::failure(
            StageSlotRegistrationError::InvalidResidualLimit);
    const double width=slideXExtentNanometres(geometry);
    const double height=slideYExtentNanometres(geometry);
    if(!std::isfinite(width)||!std::isfinite(height)||width<=0.0||height<=0.0)
        return core::Result<StageSlotRegistration,StageSlotRegistrationError>::failure(
            StageSlotRegistrationError::NonFiniteLocalPoint);

    std::array<std::optional<StageSlotCornerMeasurement>,4> ordered;
    for(const auto& measurement:input) {
        const auto index=cornerIndex(measurement.corner());
        if(index>=4)
            return core::Result<StageSlotRegistration,StageSlotRegistrationError>::failure(
                StageSlotRegistrationError::MissingCorner);
        if(ordered[index])
            return core::Result<StageSlotRegistration,StageSlotRegistrationError>::failure(
                StageSlotRegistrationError::DuplicateCorner);
        ordered[index]=measurement;
    }
    for(const auto& measurement:ordered)
        if(!measurement)
            return core::Result<StageSlotRegistration,StageSlotRegistrationError>::failure(
                StageSlotRegistrationError::MissingCorner);

    std::array<StageSlotCornerMeasurement,4> corners{
        *ordered[0],*ordered[1],*ordered[2],*ordered[3]};
    const double hx=width/2.0,hy=height/2.0;
    const double localX[4]={0.0,width,width,0.0};
    const double localY[4]={0.0,0.0,height,height};
    double meanX=0.0,meanY=0.0;
    for(const auto& corner:corners) {
        meanX+=corner.stagePosition().x/4.0;
        meanY+=corner.stagePosition().y/4.0;
    }
    double sx=0.0,skx=0.0,sy=0.0,sky=0.0;
    for(int i=0;i<4;++i) {
        const double dx=localX[i]-hx,dy=localY[i]-hy;
        const double px=corners[static_cast<std::size_t>(i)].stagePosition().x-meanX;
        const double py=corners[static_cast<std::size_t>(i)].stagePosition().y-meanY;
        sx+=dx*px; skx+=dy*px; sy+=dx*py; sky+=dy*py;
    }
    const double ax=sx/(4.0*hx*hx),bx=skx/(4.0*hy*hy);
    const double ay=sy/(4.0*hx*hx),by=sky/(4.0*hy*hy);
    const double tx=meanX-ax*hx-bx*hy;
    const double ty=meanY-ay*hx-by*hy;
    const double determinant=ax*by-bx*ay;
    const double scale=std::abs(ax*by)+std::abs(bx*ay);
    if(!std::isfinite(determinant)||
       std::abs(determinant)<=std::numeric_limits<double>::epsilon()*
           std::max(1.0,scale)*64.0)
        return core::Result<StageSlotRegistration,StageSlotRegistrationError>::failure(
            StageSlotRegistrationError::DegenerateTransform);

    double maximumCornerUncertainty=0.0,sumSquares=0.0,maximumResidual=0.0;
    std::array<double,4> residuals{};
    for(const auto& corner:corners)
        maximumCornerUncertainty=std::max(
            maximumCornerUncertainty,corner.uncertaintyNanometres());
    for(int i=0;i<4;++i) {
        const double predictedX=tx+ax*localX[i]+bx*localY[i];
        const double predictedY=ty+ay*localX[i]+by*localY[i];
        const double dx=predictedX-corners[static_cast<std::size_t>(i)].stagePosition().x;
        const double dy=predictedY-corners[static_cast<std::size_t>(i)].stagePosition().y;
        const double residual=std::hypot(dx,dy);
        residuals[static_cast<std::size_t>(i)]=residual;
        sumSquares+=residual*residual;
        maximumResidual=std::max(maximumResidual,residual);
    }
    if(!std::isfinite(maximumResidual)||maximumResidual>maximumAllowedResidual)
        return core::Result<StageSlotRegistration,StageSlotRegistrationError>::failure(
            StageSlotRegistrationError::FitResidualExceeded);

    return core::Result<StageSlotRegistration,StageSlotRegistrationError>::success(
        StageSlotRegistration(std::move(slotId),std::move(slideId),slideRevision,
            std::move(calibrationRevision),geometry,std::move(corners),
            {tx,ax,bx,ty,ay,by},residuals,maximumCornerUncertainty,
            maximumAllowedResidual,maximumResidual,std::sqrt(sumSquares/4.0)));
}

const core::StageSlotId& StageSlotRegistration::stageSlotId() const noexcept { return stageSlotId_; }
const core::SlideId& StageSlotRegistration::slideId() const noexcept { return slideId_; }
std::uint64_t StageSlotRegistration::slideRevision() const noexcept { return slideRevision_; }
const core::CalibrationRevisionId& StageSlotRegistration::calibrationRevision() const noexcept {
    return calibrationRevision_;
}
const core::SlideGeometry& StageSlotRegistration::slideGeometry() const noexcept {
    return slideGeometry_;
}
const std::array<StageSlotCornerMeasurement,4>&
StageSlotRegistration::cornerMeasurements() const noexcept { return corners_; }
double StageSlotRegistration::acceptedMaximumFitResidualNanometres() const noexcept {
    return acceptedMaximumFitResidualNanometres_;
}
double StageSlotRegistration::maximumFitResidualNanometres() const noexcept {
    return maximumFitResidualNanometres_;
}
double StageSlotRegistration::rmsFitResidualNanometres() const noexcept {
    return rmsFitResidualNanometres_;
}
const std::array<double,4>& StageSlotRegistration::cornerFitResidualsNanometres() const noexcept {
    return cornerResiduals_;
}

core::Result<StagePlanePointNanometres,StageSlotRegistrationError>
StageSlotRegistration::toStage(SlideLocalPointNanometres point) const {
    if(!std::isfinite(point.x)||!std::isfinite(point.y))
        return core::Result<StagePlanePointNanometres,StageSlotRegistrationError>::failure(
            StageSlotRegistrationError::NonFiniteLocalPoint);
    if(point.x<0.0||point.y<0.0||
       point.x>slideXExtentNanometres(slideGeometry_)||
       point.y>slideYExtentNanometres(slideGeometry_))
        return core::Result<StagePlanePointNanometres,StageSlotRegistrationError>::failure(
            StageSlotRegistrationError::LocalPointOutsideSlide);
    const StagePlanePointNanometres mapped{
        transform_[0]+transform_[1]*point.x+transform_[2]*point.y,
        transform_[3]+transform_[4]*point.x+transform_[5]*point.y};
    if(!std::isfinite(mapped.x)||!std::isfinite(mapped.y))
        return core::Result<StagePlanePointNanometres,StageSlotRegistrationError>::failure(
            StageSlotRegistrationError::NonFiniteStagePoint);
    return core::Result<StagePlanePointNanometres,StageSlotRegistrationError>::success(mapped);
}

core::Result<SlideLocalPointNanometres,StageSlotRegistrationError>
StageSlotRegistration::toSlide(StagePlanePointNanometres point) const {
    if(!std::isfinite(point.x)||!std::isfinite(point.y))
        return core::Result<SlideLocalPointNanometres,StageSlotRegistrationError>::failure(
            StageSlotRegistrationError::NonFiniteStagePoint);
    const double dx=point.x-transform_[0],dy=point.y-transform_[3];
    const double determinant=transform_[1]*transform_[5]-transform_[2]*transform_[4];
    const SlideLocalPointNanometres mapped{
        (dx*transform_[5]-transform_[2]*dy)/determinant,
        (transform_[1]*dy-dx*transform_[4])/determinant};
    if(!std::isfinite(mapped.x)||!std::isfinite(mapped.y))
        return core::Result<SlideLocalPointNanometres,StageSlotRegistrationError>::failure(
            StageSlotRegistrationError::NonFiniteLocalPoint);
    return core::Result<SlideLocalPointNanometres,StageSlotRegistrationError>::success(mapped);
}

core::Result<SlidePositionEstimateNanometres,StageSlotRegistrationError>
StageSlotRegistration::estimateSlidePosition(
    StagePlanePointNanometres point,double horizontalError,double transverseError,
    core::StagePositionEvidence horizontalEvidence,
    core::StagePositionEvidence transverseEvidence,
    core::ObservationSource horizontalSource,
    core::ObservationSource transverseSource) const {
    if(!std::isfinite(horizontalError)||!std::isfinite(transverseError)||
       horizontalError<0.0||transverseError<0.0)
        return core::Result<SlidePositionEstimateNanometres,StageSlotRegistrationError>::failure(
            StageSlotRegistrationError::NonFiniteErrorBound);
    auto local=toSlide(point);
    if(!local)
        return core::Result<SlidePositionEstimateNanometres,StageSlotRegistrationError>::failure(
            local.error());
    if(local.value().x<0.0||local.value().y<0.0||
       local.value().x>slideXExtentNanometres(slideGeometry_)||
       local.value().y>slideYExtentNanometres(slideGeometry_))
        return core::Result<SlidePositionEstimateNanometres,StageSlotRegistrationError>::failure(
            StageSlotRegistrationError::LocalPointOutsideSlide);

    const double determinant=std::abs(
        transform_[1]*transform_[5]-transform_[2]*transform_[4]);
    // For the affine rectangle map, the four-corner interpolation weights
    // have L1 norm <= 1.5 over the slide. This produces a conservative
    // stage-plane registration uncertainty envelope.
    const double registrationBound=1.5*
        (maximumCornerUncertaintyNanometres_+maximumFitResidualNanometres_);
    const double stageXBound=horizontalError+registrationBound;
    const double stageYBound=transverseError+registrationBound;
    const double xBound=(std::abs(transform_[5])*stageXBound+
                         std::abs(transform_[2])*stageYBound)/determinant;
    const double yBound=(std::abs(transform_[4])*stageXBound+
                         std::abs(transform_[1])*stageYBound)/determinant;
    if(!std::isfinite(xBound)||!std::isfinite(yBound))
        return core::Result<SlidePositionEstimateNanometres,StageSlotRegistrationError>::failure(
            StageSlotRegistrationError::NonFiniteErrorBound);
    return core::Result<SlidePositionEstimateNanometres,StageSlotRegistrationError>::success(
        SlidePositionEstimateNanometres{local.value(),xBound,yBound,
            horizontalEvidence,transverseEvidence,horizontalSource,transverseSource});
}

} // namespace microscope::application::navigation
