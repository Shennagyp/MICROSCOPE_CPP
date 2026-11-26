#pragma once
#include "microscope/core/equipment/StageSpecifications.h"
#include "microscope/core/ids/Ids.h"
#include "microscope/core/result/Result.h"
#include <array>
#include <cstddef>

namespace microscope::core {
inline constexpr std::size_t StageWorkingSurfaceSlideCount=10;
struct StagePoint3D final{double xMicrometers;double yMicrometers;double zMicrometers;};
struct SlidePoint3D final{double xMicrometers;double yMicrometers;double zMicrometers;};
struct Direction3D final{double x;double y;double z;};

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
// Rigid placement. Local (0,0,0) is the mechanical stop-point datum.
// Local axes form a right-handed orthonormal basis in stage coordinates.
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
    // Inverse projection remains valid outside the slide footprint.
    Result<SlidePoint3D,SlidePlacement3DError> toSlide(const StagePoint3D&)const;
private:
    SlidePlacement3D(SlideId,CalibrationRevisionId,SlideGeometry3D,StagePoint3D,
        Direction3D,Direction3D,Direction3D,double)noexcept;
    SlideId slideId_;CalibrationRevisionId registrationRevision_;SlideGeometry3D geometry_;
    StagePoint3D stopPointOrigin_;Direction3D x_,y_,z_;double stopPointUncertainty_;
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

class StageWorkingSurface3D final{
public:
    using SlidePlacements=std::array<SlidePlacement3D,StageWorkingSurfaceSlideCount>;
    static Result<StageWorkingSurface3D,StageWorkingSurface3DError> create(
        const StageSpecification&,StageAxisId xAxisId,StageAxisId yAxisId,StageAxisId zAxisId,
        StagePoint3D minimumBoundary,StagePoint3D maximumBoundary,SlidePlacements);
    const EquipmentComponentId& stageComponentId()const noexcept;
    const StageAxisId& xAxisId()const noexcept;const StageAxisId& yAxisId()const noexcept;
    const StageAxisId& zAxisId()const noexcept;
    AxisCoordinateUnit xAxisUnit()const noexcept;AxisCoordinateUnit yAxisUnit()const noexcept;
    AxisCoordinateUnit zAxisUnit()const noexcept;
    const StagePoint3D& minimumBoundary()const noexcept;const StagePoint3D& maximumBoundary()const noexcept;
    const SlidePlacements& slidePlacements()const noexcept;
    const SlidePlacement3D* placementFor(const SlideId&)const noexcept;
    bool contains(const StagePoint3D&)const noexcept;
    Result<StagePoint3D,SlidePlacement3DError> toStage(const SlideId&,const SlidePoint3D&)const;
    Result<SlidePosition3D,SlidePlacement3DError> toSlide(const SlideId&,const StagePoint3D&)const;
private:
    StageWorkingSurface3D(EquipmentComponentId,StageAxisId,StageAxisId,StageAxisId,
        AxisCoordinateUnit,AxisCoordinateUnit,AxisCoordinateUnit,StagePoint3D,StagePoint3D,
        SlidePlacements)noexcept;
    EquipmentComponentId stageComponentId_;
    StageAxisId xAxisId_,yAxisId_,zAxisId_;
    AxisCoordinateUnit xUnit_,yUnit_,zUnit_;
    StagePoint3D minimumBoundary_,maximumBoundary_;
    SlidePlacements placements_;
};
} // namespace microscope::core
