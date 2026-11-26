#include "microscope/core/stage/SlideRectangleDefinition2D.h"
#include <cmath>
#include <utility>

namespace microscope::core{
SlideRectangleDefinition2D::SlideRectangleDefinition2D(
    SlideId id,CalibrationRevisionId revision,StagePoint2D stop,StagePoint2D drawnOpposite,StagePoint2D opposite,
    SlideRectangleLengthAxis axis,double length,double width,double z,double thickness,
    double tolerance,SlidePlacement3D placement)
    :slideId_(std::move(id)),revision_(std::move(revision)),stopPoint_(stop),drawnOppositeCorner_(drawnOpposite),oppositeCorner_(opposite),
     lengthAxis_(axis),length_(length),width_(width),zDatum_(z),thickness_(thickness),
     tolerance_(tolerance),placement_(std::move(placement)){}
Result<SlideRectangleDefinition2D,SlideRectangleDefinitionError>
SlideRectangleDefinition2D::create(SlideId id,CalibrationRevisionId revision,StagePoint2D stop,
    StagePoint2D opposite,SlideRectangleLengthAxis axis,double length,double width,double z,
    double thickness,double tolerance,double stopUncertainty){
    if(!std::isfinite(stop.xMicrometers)||!std::isfinite(stop.yMicrometers)||
       !std::isfinite(opposite.xMicrometers)||!std::isfinite(opposite.yMicrometers))
        return Result<SlideRectangleDefinition2D,SlideRectangleDefinitionError>::failure(SlideRectangleDefinitionError::NonFiniteEndpoint);
    const double dx=opposite.xMicrometers-stop.xMicrometers,dy=opposite.yMicrometers-stop.yMicrometers;
    const double ax=std::abs(dx),ay=std::abs(dy);
    if(ax<=0||ay<=0)return Result<SlideRectangleDefinition2D,SlideRectangleDefinitionError>::failure(SlideRectangleDefinitionError::DegenerateRectangle);
    if(!std::isfinite(tolerance)||tolerance<0)
        return Result<SlideRectangleDefinition2D,SlideRectangleDefinitionError>::failure(SlideRectangleDefinitionError::InvalidConstraintTolerance);
    if(!std::isfinite(length)||!std::isfinite(width)||length<=0||width<=0)
        return Result<SlideRectangleDefinition2D,SlideRectangleDefinitionError>::failure(SlideRectangleDefinitionError::InvalidDimensionConstraint);
    if(!std::isfinite(z))return Result<SlideRectangleDefinition2D,SlideRectangleDefinitionError>::failure(SlideRectangleDefinitionError::InvalidZDatum);
    if(!std::isfinite(stopUncertainty)||stopUncertainty<0)
        return Result<SlideRectangleDefinition2D,SlideRectangleDefinitionError>::failure(SlideRectangleDefinitionError::InvalidStopPointUncertainty);
    Direction3D x{},y{},normal{};
    double resolvedX=0.0,resolvedY=0.0;
    const double sx=dx>0?1.0:-1.0,sy=dy>0?1.0:-1.0;
    switch(axis){
    case SlideRectangleLengthAxis::StageX:
        resolvedX=length;resolvedY=width;x={sx,0,0};y={0,sy,0};normal={0,0,sx*sy};break;
    case SlideRectangleLengthAxis::StageY:
        resolvedX=width;resolvedY=length;x={0,sy,0};y={sx,0,0};normal={0,0,-sx*sy};break;
    default:
        return Result<SlideRectangleDefinition2D,SlideRectangleDefinitionError>::failure(SlideRectangleDefinitionError::InvalidAxisAssignment);
    }
    const StagePoint2D constrainedOpposite{stop.xMicrometers+sx*resolvedX,stop.yMicrometers+sy*resolvedY};
    auto geometry=SlideGeometry3D::create(length,width,thickness);
    if(!geometry)return Result<SlideRectangleDefinition2D,SlideRectangleDefinitionError>::failure(SlideRectangleDefinitionError::GeometryRejected);
    auto placement=SlidePlacement3D::create(id,revision,std::move(geometry).value(),
        {stop.xMicrometers,stop.yMicrometers,z},x,y,normal,stopUncertainty);
    if(!placement)return Result<SlideRectangleDefinition2D,SlideRectangleDefinitionError>::failure(SlideRectangleDefinitionError::PlacementRejected);
    return Result<SlideRectangleDefinition2D,SlideRectangleDefinitionError>::success(
        SlideRectangleDefinition2D(std::move(id),std::move(revision),stop,opposite,constrainedOpposite,axis,length,width,z,thickness,tolerance,std::move(placement).value()));
}
const SlideId& SlideRectangleDefinition2D::slideId()const noexcept{return slideId_;}
const CalibrationRevisionId& SlideRectangleDefinition2D::registrationRevision()const noexcept{return revision_;}
const StagePoint2D& SlideRectangleDefinition2D::stopPointEndpoint()const noexcept{return stopPoint_;}
const StagePoint2D& SlideRectangleDefinition2D::drawnOppositeEndpoint()const noexcept{return drawnOppositeCorner_;}
const StagePoint2D& SlideRectangleDefinition2D::oppositeEndpoint()const noexcept{return oppositeCorner_;}
SlideRectangleLengthAxis SlideRectangleDefinition2D::lengthAxis()const noexcept{return lengthAxis_;}
double SlideRectangleDefinition2D::constrainedLengthMicrometers()const noexcept{return length_;}
double SlideRectangleDefinition2D::constrainedWidthMicrometers()const noexcept{return width_;}
double SlideRectangleDefinition2D::zDatumMicrometers()const noexcept{return zDatum_;}
double SlideRectangleDefinition2D::thicknessMicrometers()const noexcept{return thickness_;}
double SlideRectangleDefinition2D::constraintToleranceMicrometers()const noexcept{return tolerance_;}
const SlidePlacement3D& SlideRectangleDefinition2D::placement()const noexcept{return placement_;}
} // namespace microscope::core
