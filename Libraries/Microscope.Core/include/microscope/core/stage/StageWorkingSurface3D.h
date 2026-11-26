#pragma once
#include "microscope/core/equipment/StageSpecifications.h"
#include "microscope/core/ids/Ids.h"
#include "microscope/core/result/Result.h"
#include <vector>

namespace microscope::core {
struct StagePoint3D final{double xMicrometers;double yMicrometers;double zMicrometers;};
struct SlidePoint3D final{double xMicrometers;double yMicrometers;double zMicrometers;};
struct Direction3D final{double x;double y;double z;};
struct StagePoint2D final{double xMicrometers;double yMicrometers;};

enum class SlideGeometry3DError{NonFiniteDimension,NonPositiveDimension};
class SlideGeometry3D final{
public:
    static Result<SlideGeometry3D,SlideGeometry3DError> create(double length,double width,double thickness);
    double lengthMicrometers()const noexcept;double widthMicrometers()const noexcept;
    double thicknessMicrometers()const noexcept;bool contains(const SlidePoint3D&)const noexcept;
private:
    SlideGeometry3D(double,double,double)noexcept;
    double length_,width_,thickness_;
};

enum class SlidePlacement3DError{
    NonFiniteOrigin,InvalidBasis,InvalidDatumUncertainty,SlidePointOutsideGeometry,
    NonFiniteMappedPoint,SlideNotFound
};
class SlidePlacement3D final{
public:
    static Result<SlidePlacement3D,SlidePlacement3DError> create(
        SlideId,CalibrationRevisionId,SlideGeometry3D,StagePoint3D,
        Direction3D,Direction3D,Direction3D,double stopPointUncertaintyMicrometers=0.0);
    const SlideId& slideId()const noexcept;
    const CalibrationRevisionId& registrationRevision()const noexcept;
    const SlideGeometry3D& geometry()const noexcept;
    const StagePoint3D& stopPointOrigin()const noexcept;
    const Direction3D& localXAxisInStage()const noexcept;
    const Direction3D& localYAxisInStage()const noexcept;
    const Direction3D& localZAxisInStage()const noexcept;
    double stopPointUncertaintyMicrometers()const noexcept;
    Result<StagePoint3D,SlidePlacement3DError> toStage(const SlidePoint3D&)const;
    Result<SlidePoint3D,SlidePlacement3DError> toSlide(const StagePoint3D&)const;
private:
    SlidePlacement3D(SlideId,CalibrationRevisionId,SlideGeometry3D,StagePoint3D,
        Direction3D,Direction3D,Direction3D,double)noexcept;
    SlideId slideId_;CalibrationRevisionId registrationRevision_;SlideGeometry3D geometry_;
    StagePoint3D stopPointOrigin_;Direction3D x_,y_,z_;double stopPointUncertainty_;
};

enum class SlideRectangleLengthAxis{StageX,StageY};
enum class SlideRectangleDefinitionError{
    NonFiniteEndpoint,DegenerateRectangle,InvalidConstraintTolerance,
    InvalidDimensionConstraint,DimensionConstraintMismatch,InvalidZDatum,
    InvalidStopPointUncertainty,InvalidAxisAssignment,GeometryRejected,PlacementRejected
};

// CAD-authored, axis-aligned rectangle. Both original endpoints and explicit
// dimension constraints are retained as authoritative layout data. The first
// endpoint is the physical stop point and local origin; the second is opposite.
class SlideRectangleDefinition2D final{
public:
    static Result<SlideRectangleDefinition2D,SlideRectangleDefinitionError> create(
        SlideId,CalibrationRevisionId,StagePoint2D stopPoint,StagePoint2D oppositeCorner,
        SlideRectangleLengthAxis lengthAxis,double constrainedLengthMicrometers,
        double constrainedWidthMicrometers,double zDatumMicrometers,
        double thicknessMicrometers,double constraintToleranceMicrometers,
        double stopPointUncertaintyMicrometers=0.0);
    const SlideId& slideId()const noexcept;
    const CalibrationRevisionId& registrationRevision()const noexcept;
    const StagePoint2D& stopPointEndpoint()const noexcept;
    const StagePoint2D& oppositeEndpoint()const noexcept;
    SlideRectangleLengthAxis lengthAxis()const noexcept;
    double constrainedLengthMicrometers()const noexcept;
    double constrainedWidthMicrometers()const noexcept;
    double zDatumMicrometers()const noexcept;
    double thicknessMicrometers()const noexcept;
    double constraintToleranceMicrometers()const noexcept;
    const SlidePlacement3D& placement()const noexcept;
private:
    SlideRectangleDefinition2D(SlideId,CalibrationRevisionId,StagePoint2D,StagePoint2D,
        SlideRectangleLengthAxis,double,double,double,double,double,SlidePlacement3D);
    SlideId slideId_;CalibrationRevisionId revision_;StagePoint2D stopPoint_,oppositeCorner_;
    SlideRectangleLengthAxis lengthAxis_;double length_,width_,zDatum_,thickness_,tolerance_;
    SlidePlacement3D placement_;
};

enum class StageWorkingSurface3DError{
    NonFiniteBoundary,InvalidBoundary,DuplicateSlideIdentity,SlideOutsideWorkingVolume,
    DuplicateStageAxis,MissingStageAxis,NonLinearStageAxis,UnsupportedStageAxisUnit,
    WorkingVolumeOutsideAxisRange
};
struct SlidePosition3D final{
    SlideId slideId;CalibrationRevisionId registrationRevision;SlidePoint3D position;
    bool insideSlide;bool insideWorkingVolume;
};

// One fixed physical stage volume with a user-authored, variable-size CAD slide
// layout. The collection is saved design data, not auto-discovered hardware.
class StageWorkingSurface3D final{
public:
    using SlideRectangles=std::vector<SlideRectangleDefinition2D>;
    using SlidePlacements=SlideRectangles;
    static Result<StageWorkingSurface3D,StageWorkingSurface3DError> create(
        const StageSpecification&,StageAxisId xAxisId,StageAxisId yAxisId,StageAxisId zAxisId,
        StagePoint3D minimumBoundary,StagePoint3D maximumBoundary,SlideRectangles);
    const EquipmentComponentId& stageComponentId()const noexcept;
    const StageAxisId& xAxisId()const noexcept;const StageAxisId& yAxisId()const noexcept;
    const StageAxisId& zAxisId()const noexcept;
    AxisCoordinateUnit xAxisUnit()const noexcept;AxisCoordinateUnit yAxisUnit()const noexcept;
    AxisCoordinateUnit zAxisUnit()const noexcept;
    const StagePoint3D& minimumBoundary()const noexcept;const StagePoint3D& maximumBoundary()const noexcept;
    const SlideRectangles& slideRectangles()const noexcept;
    const SlidePlacement3D* placementFor(const SlideId&)const noexcept;
    const SlideRectangleDefinition2D* rectangleFor(const SlideId&)const noexcept;
    bool contains(const StagePoint3D&)const noexcept;
    Result<StagePoint3D,SlidePlacement3DError> toStage(const SlideId&,const SlidePoint3D&)const;
    Result<SlidePosition3D,SlidePlacement3DError> toSlide(const SlideId&,const StagePoint3D&)const;
private:
    StageWorkingSurface3D(EquipmentComponentId,StageAxisId,StageAxisId,StageAxisId,
        AxisCoordinateUnit,AxisCoordinateUnit,AxisCoordinateUnit,StagePoint3D,StagePoint3D,
        SlideRectangles)noexcept;
    EquipmentComponentId stageComponentId_;StageAxisId xAxisId_,yAxisId_,zAxisId_;
    AxisCoordinateUnit xUnit_,yUnit_,zUnit_;StagePoint3D minimumBoundary_,maximumBoundary_;
    SlideRectangles rectangles_;
};
} // namespace microscope::core
