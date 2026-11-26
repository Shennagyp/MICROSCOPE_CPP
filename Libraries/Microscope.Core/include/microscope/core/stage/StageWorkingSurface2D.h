#pragma once
#include "microscope/core/equipment/StageSpecifications.h"
#include "microscope/core/ids/Ids.h"
#include "microscope/core/result/Result.h"
#include <vector>

namespace microscope::core {

struct StagePoint2D final { double xMicrometers; double yMicrometers; };
struct SlidePoint2D final { double xMicrometers; double yMicrometers; };

enum class SlideRectangleLengthAxis { StageX, StageY };

enum class SlideGeometry2DError { NonFiniteDimension, NonPositiveDimension };
class SlideGeometry2D final {
public:
    static Result<SlideGeometry2D, SlideGeometry2DError> create(double length, double width);
    double lengthMicrometers() const noexcept;
    double widthMicrometers() const noexcept;
    bool contains(const SlidePoint2D&) const noexcept;
private:
    SlideGeometry2D(double, double) noexcept;
    double length_;
    double width_;
};

enum class SlidePlacement2DError {
    NonFiniteOrigin, InvalidBasis, SlidePointOutsideGeometry, NonFiniteMappedPoint, SlideNotFound
};

// Pure XY transform. The local origin is the first CAD endpoint: the slide's
// mechanical stop point. No Z, thickness, drawing tolerance or datum uncertainty
// is part of a slide rectangle.
class SlidePlacement2D final {
public:
    static Result<SlidePlacement2D, SlidePlacement2DError> create(
        SlideId, CalibrationRevisionId, SlideGeometry2D, StagePoint2D stopPoint,
        double localXAxisX, double localXAxisY,
        double localYAxisX, double localYAxisY);

    const SlideId& slideId() const noexcept;
    const CalibrationRevisionId& registrationRevision() const noexcept;
    const SlideGeometry2D& geometry() const noexcept;
    const StagePoint2D& stopPointOrigin() const noexcept;
    double localXAxisX() const noexcept;
    double localXAxisY() const noexcept;
    double localYAxisX() const noexcept;
    double localYAxisY() const noexcept;
    Result<StagePoint2D, SlidePlacement2DError> toStage(const SlidePoint2D&) const;
    // Inverse projection is valid even when the stage is outside this rectangle.
    Result<SlidePoint2D, SlidePlacement2DError> toSlide(const StagePoint2D&) const;

private:
    SlidePlacement2D(SlideId, CalibrationRevisionId, SlideGeometry2D, StagePoint2D,
        double, double, double, double) noexcept;
    SlideId slideId_;
    CalibrationRevisionId registrationRevision_;
    SlideGeometry2D geometry_;
    StagePoint2D stopPointOrigin_;
    double xX_, xY_, yX_, yY_;
};

enum class SlideRectangleDefinitionError {
    NonFiniteEndpoint, DegenerateRectangle, InvalidDimensionConstraint,
    InvalidAxisAssignment, GeometryRejected, PlacementRejected
};

// CAD rectangle semantics:
// - stopPoint: anchored first endpoint / mechanical stop datum.
// - oppositeCornerHandle: initial point that chooses quadrant and direction.
// - constrained length/width: solve the final opposite corner.
// Original handle and solved corner are both retained as CAD state.
class SlideRectangleDefinition2D final {
public:
    static Result<SlideRectangleDefinition2D, SlideRectangleDefinitionError> create(
        SlideId, CalibrationRevisionId,
        StagePoint2D stopPoint, StagePoint2D oppositeCornerHandle,
        SlideRectangleLengthAxis lengthAxis,
        double constrainedLengthMicrometers,
        double constrainedWidthMicrometers);

    const SlideId& slideId() const noexcept;
    const CalibrationRevisionId& registrationRevision() const noexcept;
    const StagePoint2D& stopPointEndpoint() const noexcept;
    const StagePoint2D& drawnOppositeEndpoint() const noexcept;
    const StagePoint2D& solvedOppositeEndpoint() const noexcept;
    SlideRectangleLengthAxis lengthAxis() const noexcept;
    double constrainedLengthMicrometers() const noexcept;
    double constrainedWidthMicrometers() const noexcept;
    const SlideGeometry2D& geometry() const noexcept;
    const SlidePlacement2D& placement() const noexcept;

private:
    SlideRectangleDefinition2D(SlideId, CalibrationRevisionId,
        StagePoint2D, StagePoint2D, StagePoint2D, SlideRectangleLengthAxis,
        double, double, SlideGeometry2D, SlidePlacement2D);
    SlideId slideId_;
    CalibrationRevisionId revision_;
    StagePoint2D stopPoint_;
    StagePoint2D drawnOppositeCorner_;
    StagePoint2D solvedOppositeCorner_;
    SlideRectangleLengthAxis lengthAxis_;
    double length_;
    double width_;
    SlideGeometry2D geometry_;
    SlidePlacement2D placement_;
};

enum class StageWorkingSurface2DError {
    NonFiniteBoundary, InvalidBoundary, DuplicateSlideIdentity,
    SlideOutsideWorkingSurface, DuplicateStageAxis, MissingStageAxis,
    NonLinearStageAxis, UnsupportedStageAxisUnit, WorkingSurfaceOutsideAxisRange
};

struct SlidePosition2D final {
    SlideId slideId;
    CalibrationRevisionId registrationRevision;
    SlidePoint2D position;
    bool insideSlide;
    bool insideWorkingSurface;
};

// The working surface is an XY plane. Z focus travel belongs to the microscope
// axis model and is deliberately not stored here.
class StageWorkingSurface2D final {
public:
    using SlideRectangles = std::vector<SlideRectangleDefinition2D>;

    static Result<StageWorkingSurface2D, StageWorkingSurface2DError> create(
        const StageSpecification&, StageAxisId xAxisId, StageAxisId yAxisId,
        StagePoint2D minimumBoundary, StagePoint2D maximumBoundary,
        SlideRectangles rectangles);

    const EquipmentComponentId& stageComponentId() const noexcept;
    const StageAxisId& xAxisId() const noexcept;
    const StageAxisId& yAxisId() const noexcept;
    AxisCoordinateUnit xAxisUnit() const noexcept;
    AxisCoordinateUnit yAxisUnit() const noexcept;
    const StagePoint2D& minimumBoundary() const noexcept;
    const StagePoint2D& maximumBoundary() const noexcept;
    const SlideRectangles& slideRectangles() const noexcept;
    const SlideRectangleDefinition2D* rectangleFor(const SlideId&) const noexcept;
    const SlidePlacement2D* placementFor(const SlideId&) const noexcept;
    bool contains(const StagePoint2D&) const noexcept;

    Result<StagePoint2D, SlidePlacement2DError> toStage(
        const SlideId&, const SlidePoint2D&) const;
    Result<SlidePosition2D, SlidePlacement2DError> toSlide(
        const SlideId&, const StagePoint2D&) const;

private:
    StageWorkingSurface2D(EquipmentComponentId, StageAxisId, StageAxisId,
        AxisCoordinateUnit, AxisCoordinateUnit, StagePoint2D, StagePoint2D,
        SlideRectangles) noexcept;
    EquipmentComponentId stageComponentId_;
    StageAxisId xAxisId_;
    StageAxisId yAxisId_;
    AxisCoordinateUnit xUnit_;
    AxisCoordinateUnit yUnit_;
    StagePoint2D minimumBoundary_;
    StagePoint2D maximumBoundary_;
    SlideRectangles rectangles_;
};

} // namespace microscope::core
