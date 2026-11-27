#include "microscope/core/stage/StageWorkingSurface2D.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace microscope::core {
namespace {
bool finite(const StagePoint2D& p) noexcept {
    return std::isfinite(p.xMicrometers) && std::isfinite(p.yMicrometers);
}
bool finite(const SlidePoint2D& p) noexcept {
    return std::isfinite(p.xMicrometers) && std::isfinite(p.yMicrometers);
}
bool linearUnit(AxisCoordinateUnit u) noexcept {
    return u == AxisCoordinateUnit::Micrometers || u == AxisCoordinateUnit::Millimeters;
}
double toMicrometers(double value, AxisCoordinateUnit unit) noexcept {
    return unit == AxisCoordinateUnit::Millimeters ? value * 1000.0 : value;
}
const AxisSpecification* findAxis(const StageSpecification& stage, const StageAxisId& id) noexcept {
    const auto it = std::find_if(stage.axes().begin(), stage.axes().end(),
        [&](const auto& axis) { return axis.id() == id; });
    return it == stage.axes().end() ? nullptr : &*it;
}
bool inside(const StagePoint2D& point, const StagePoint2D& minimum,
    const StagePoint2D& maximum) noexcept {
    return point.xMicrometers >= minimum.xMicrometers &&
        point.xMicrometers <= maximum.xMicrometers &&
        point.yMicrometers >= minimum.yMicrometers &&
        point.yMicrometers <= maximum.yMicrometers;
}
bool rectangleInside(const SlideRectangleDefinition2D& rectangle,
    const StagePoint2D& minimum, const StagePoint2D& maximum) noexcept {
    const auto corners = rectangle.stageCorners();
    return inside(corners.stopPoint, minimum, maximum) &&
        inside(corners.lengthEdgeEnd, minimum, maximum) &&
        inside(corners.widthEdgeEnd, minimum, maximum) &&
        inside(corners.oppositeCorner, minimum, maximum);
}
struct RectangleBounds2D final {
    double minimumX;
    double maximumX;
    double minimumY;
    double maximumY;
};
RectangleBounds2D boundsOf(const SlideRectangleDefinition2D& rectangle) noexcept {
    const auto c = rectangle.stageCorners();
    return {
        std::min({c.stopPoint.xMicrometers, c.lengthEdgeEnd.xMicrometers,
            c.widthEdgeEnd.xMicrometers, c.oppositeCorner.xMicrometers}),
        std::max({c.stopPoint.xMicrometers, c.lengthEdgeEnd.xMicrometers,
            c.widthEdgeEnd.xMicrometers, c.oppositeCorner.xMicrometers}),
        std::min({c.stopPoint.yMicrometers, c.lengthEdgeEnd.yMicrometers,
            c.widthEdgeEnd.yMicrometers, c.oppositeCorner.yMicrometers}),
        std::max({c.stopPoint.yMicrometers, c.lengthEdgeEnd.yMicrometers,
            c.widthEdgeEnd.yMicrometers, c.oppositeCorner.yMicrometers})
    };
}
// Rectangles are stage-axis aligned by the current CAD contract. Strict
// inequalities reject positive-area overlap but permit exact edge/corner contact.
bool rectanglesOverlap(const SlideRectangleDefinition2D& left,
    const SlideRectangleDefinition2D& right) noexcept {
    const auto a = boundsOf(left);
    const auto b = boundsOf(right);
    return a.minimumX < b.maximumX && a.maximumX > b.minimumX &&
        a.minimumY < b.maximumY && a.maximumY > b.minimumY;
}
}

StageTravelRange2D::StageTravelRange2D(double x, double y) noexcept : x_(x), y_(y) {}
Result<StageTravelRange2D, StageTravelRange2DError> StageTravelRange2D::create(
    double x, double y) {
    if (!std::isfinite(x) || !std::isfinite(y))
        return Result<StageTravelRange2D, StageTravelRange2DError>::failure(
            StageTravelRange2DError::NonFiniteRange);
    if (x <= 0.0 || y <= 0.0)
        return Result<StageTravelRange2D, StageTravelRange2DError>::failure(
            StageTravelRange2DError::NonPositiveRange);
    return Result<StageTravelRange2D, StageTravelRange2DError>::success(
        StageTravelRange2D(x, y));
}
double StageTravelRange2D::xMicrometers() const noexcept { return x_; }
double StageTravelRange2D::yMicrometers() const noexcept { return y_; }

SlideGeometry2D::SlideGeometry2D(SlideGeometry geometry) noexcept
    : geometry_(std::move(geometry)) {}
Result<SlideGeometry2D, SlideGeometryError> SlideGeometry2D::create(
    double length, double width) {
    auto geometry = SlideGeometry::create(length, width);
    if (!geometry)
        return Result<SlideGeometry2D, SlideGeometryError>::failure(geometry.error());
    return Result<SlideGeometry2D, SlideGeometryError>::success(
        SlideGeometry2D(std::move(geometry).value()));
}
double SlideGeometry2D::lengthMicrometers() const noexcept {
    return geometry_.xExtentMicrometers();
}
double SlideGeometry2D::widthMicrometers() const noexcept {
    return geometry_.yExtentMicrometers();
}
bool SlideGeometry2D::contains(const SlidePoint2D& p) const noexcept {
    return finite(p) && geometry_.contains(p.xMicrometers, p.yMicrometers);
}
const SlideGeometry& SlideGeometry2D::canonicalGeometry() const noexcept {
    return geometry_;
}

SlidePlacement2D::SlidePlacement2D(SlideId id, CalibrationRevisionId revision,
    SlideGeometry2D geometry, StagePoint2D origin,
    double xx, double xy, double yx, double yy) noexcept
    : slideId_(std::move(id)), registrationRevision_(std::move(revision)),
      geometry_(geometry), stopPointOrigin_(origin), xX_(xx), xY_(xy), yX_(yx), yY_(yy) {}

Result<SlidePlacement2D, SlidePlacement2DError> SlidePlacement2D::create(
    SlideId id, CalibrationRevisionId revision, SlideGeometry2D geometry,
    StagePoint2D origin, double xx, double xy, double yx, double yy) {
    if (!finite(origin))
        return Result<SlidePlacement2D, SlidePlacement2DError>::failure(
            SlidePlacement2DError::NonFiniteOrigin);
    const auto unit = [](double x, double y) {
        return std::isfinite(x) && std::isfinite(y) &&
            std::abs(x*x + y*y - 1.0) <= 1.0e-6;
    };
    if (!unit(xx, xy) || !unit(yx, yy) ||
        std::abs(xx*yx + xy*yy) > 1.0e-6 ||
        std::abs(std::abs(xx*yy - xy*yx) - 1.0) > 1.0e-6) {
        return Result<SlidePlacement2D, SlidePlacement2DError>::failure(
            SlidePlacement2DError::InvalidBasis);
    }
    return Result<SlidePlacement2D, SlidePlacement2DError>::success(
        SlidePlacement2D(std::move(id), std::move(revision), geometry,
            origin, xx, xy, yx, yy));
}
const SlideId& SlidePlacement2D::slideId() const noexcept { return slideId_; }
const CalibrationRevisionId& SlidePlacement2D::registrationRevision() const noexcept {
    return registrationRevision_;
}
const SlideGeometry2D& SlidePlacement2D::geometry() const noexcept { return geometry_; }
const StagePoint2D& SlidePlacement2D::stopPointOrigin() const noexcept { return stopPointOrigin_; }
double SlidePlacement2D::localXAxisX() const noexcept { return xX_; }
double SlidePlacement2D::localXAxisY() const noexcept { return xY_; }
double SlidePlacement2D::localYAxisX() const noexcept { return yX_; }
double SlidePlacement2D::localYAxisY() const noexcept { return yY_; }

Result<StagePoint2D, SlidePlacement2DError> SlidePlacement2D::toStage(
    const SlidePoint2D& p) const {
    if (!finite(p))
        return Result<StagePoint2D, SlidePlacement2DError>::failure(
            SlidePlacement2DError::NonFiniteMappedPoint);
    if (!geometry_.contains(p))
        return Result<StagePoint2D, SlidePlacement2DError>::failure(
            SlidePlacement2DError::SlidePointOutsideGeometry);
    const StagePoint2D mapped{
        stopPointOrigin_.xMicrometers + xX_*p.xMicrometers + yX_*p.yMicrometers,
        stopPointOrigin_.yMicrometers + xY_*p.xMicrometers + yY_*p.yMicrometers
    };
    if (!finite(mapped))
        return Result<StagePoint2D, SlidePlacement2DError>::failure(
            SlidePlacement2DError::NonFiniteMappedPoint);
    return Result<StagePoint2D, SlidePlacement2DError>::success(mapped);
}
Result<SlidePoint2D, SlidePlacement2DError> SlidePlacement2D::toSlide(
    const StagePoint2D& p) const {
    if (!finite(p))
        return Result<SlidePoint2D, SlidePlacement2DError>::failure(
            SlidePlacement2DError::NonFiniteMappedPoint);
    const double dx = p.xMicrometers - stopPointOrigin_.xMicrometers;
    const double dy = p.yMicrometers - stopPointOrigin_.yMicrometers;
    const SlidePoint2D local{
        dx*xX_ + dy*xY_,
        dx*yX_ + dy*yY_
    };
    if (!finite(local))
        return Result<SlidePoint2D, SlidePlacement2DError>::failure(
            SlidePlacement2DError::NonFiniteMappedPoint);
    return Result<SlidePoint2D, SlidePlacement2DError>::success(local);
}

SlideRectangleDefinition2D::SlideRectangleDefinition2D(
    SlideId id, CalibrationRevisionId revision, StagePoint2D stop,
    StagePoint2D drawn, StagePoint2D solved, SlideRectangleLengthAxis axis,
    double length, double width, SlideGeometry2D geometry, SlidePlacement2D placement)
    : slideId_(std::move(id)), revision_(std::move(revision)), stopPoint_(stop),
      drawnOppositeCorner_(drawn), solvedOppositeCorner_(solved), lengthAxis_(axis),
      length_(length), width_(width), geometry_(geometry), placement_(std::move(placement)) {}

Result<SlideRectangleDefinition2D, SlideRectangleDefinitionError>
SlideRectangleDefinition2D::create(
    SlideId id, CalibrationRevisionId revision, StagePoint2D stop,
    StagePoint2D handle, SlideRectangleLengthAxis axis, double length, double width) {
    if (!finite(stop) || !finite(handle))
        return Result<SlideRectangleDefinition2D, SlideRectangleDefinitionError>::failure(
            SlideRectangleDefinitionError::NonFiniteEndpoint);
    const double dx = handle.xMicrometers - stop.xMicrometers;
    const double dy = handle.yMicrometers - stop.yMicrometers;
    if (dx == 0.0 || dy == 0.0)
        return Result<SlideRectangleDefinition2D, SlideRectangleDefinitionError>::failure(
            SlideRectangleDefinitionError::DegenerateRectangle);
    if (!std::isfinite(length) || !std::isfinite(width) || length <= 0.0 || width <= 0.0)
        return Result<SlideRectangleDefinition2D, SlideRectangleDefinitionError>::failure(
            SlideRectangleDefinitionError::InvalidDimensionConstraint);

    const double signX = dx > 0.0 ? 1.0 : -1.0;
    const double signY = dy > 0.0 ? 1.0 : -1.0;
    double solvedX = 0.0;
    double solvedY = 0.0;
    double localXX = 0.0, localXY = 0.0, localYX = 0.0, localYY = 0.0;
    switch (axis) {
    case SlideRectangleLengthAxis::StageX:
        solvedX = signX * length;
        solvedY = signY * width;
        localXX = signX; localXY = 0.0;
        localYX = 0.0; localYY = signY;
        break;
    case SlideRectangleLengthAxis::StageY:
        solvedX = signX * width;
        solvedY = signY * length;
        localXX = 0.0; localXY = signY;
        localYX = signX; localYY = 0.0;
        break;
    default:
        return Result<SlideRectangleDefinition2D, SlideRectangleDefinitionError>::failure(
            SlideRectangleDefinitionError::InvalidAxisAssignment);
    }

    auto geometry = SlideGeometry2D::create(length, width);
    if (!geometry)
        return Result<SlideRectangleDefinition2D, SlideRectangleDefinitionError>::failure(
            SlideRectangleDefinitionError::GeometryRejected);
    auto placement = SlidePlacement2D::create(id, revision, geometry.value(), stop,
        localXX, localXY, localYX, localYY);
    if (!placement)
        return Result<SlideRectangleDefinition2D, SlideRectangleDefinitionError>::failure(
            SlideRectangleDefinitionError::PlacementRejected);

    const StagePoint2D lengthEdgeEnd{
        stop.xMicrometers + localXX * length,
        stop.yMicrometers + localXY * length
    };
    const StagePoint2D widthEdgeEnd{
        stop.xMicrometers + localYX * width,
        stop.yMicrometers + localYY * width
    };
    const StagePoint2D solved{stop.xMicrometers + solvedX, stop.yMicrometers + solvedY};
    if (!finite(lengthEdgeEnd) || !finite(widthEdgeEnd) || !finite(solved))
        return Result<SlideRectangleDefinition2D, SlideRectangleDefinitionError>::failure(
            SlideRectangleDefinitionError::NonFiniteEndpoint);
    return Result<SlideRectangleDefinition2D, SlideRectangleDefinitionError>::success(
        SlideRectangleDefinition2D(std::move(id), std::move(revision), stop, handle,
            solved, axis, length, width, geometry.value(), std::move(placement).value()));
}
const SlideId& SlideRectangleDefinition2D::slideId() const noexcept { return slideId_; }
const CalibrationRevisionId& SlideRectangleDefinition2D::registrationRevision() const noexcept { return revision_; }
const StagePoint2D& SlideRectangleDefinition2D::stopPointEndpoint() const noexcept { return stopPoint_; }
const StagePoint2D& SlideRectangleDefinition2D::drawnOppositeEndpoint() const noexcept { return drawnOppositeCorner_; }
const StagePoint2D& SlideRectangleDefinition2D::solvedOppositeEndpoint() const noexcept { return solvedOppositeCorner_; }
SlideRectangleCorners2D SlideRectangleDefinition2D::stageCorners() const noexcept {
    const auto& origin = placement_.stopPointOrigin();
    return {
        origin,
        {
            origin.xMicrometers + placement_.localXAxisX() * geometry_.lengthMicrometers(),
            origin.yMicrometers + placement_.localXAxisY() * geometry_.lengthMicrometers()
        },
        {
            origin.xMicrometers + placement_.localYAxisX() * geometry_.widthMicrometers(),
            origin.yMicrometers + placement_.localYAxisY() * geometry_.widthMicrometers()
        },
        solvedOppositeCorner_
    };
}
SlideRectangleLengthAxis SlideRectangleDefinition2D::lengthAxis() const noexcept { return lengthAxis_; }
double SlideRectangleDefinition2D::constrainedLengthMicrometers() const noexcept { return length_; }
double SlideRectangleDefinition2D::constrainedWidthMicrometers() const noexcept { return width_; }
const SlideGeometry2D& SlideRectangleDefinition2D::geometry() const noexcept { return geometry_; }
const SlidePlacement2D& SlideRectangleDefinition2D::placement() const noexcept { return placement_; }

SlideNavigationTarget2D::SlideNavigationTarget2D(
    SlideId slideId, SlidePoint2D point, CalibrationRevisionId revision)
    : slideId_(std::move(slideId)), point_(point),
      registrationRevision_(std::move(revision)) {}
const SlideId& SlideNavigationTarget2D::slideId() const noexcept { return slideId_; }
const SlidePoint2D& SlideNavigationTarget2D::point() const noexcept { return point_; }
const CalibrationRevisionId& SlideNavigationTarget2D::registrationRevision() const noexcept {
    return registrationRevision_;
}

StageWorkingSurface2D::StageWorkingSurface2D(EquipmentComponentId stage, StageAxisId x,
    StageAxisId y, AxisCoordinateUnit xu, AxisCoordinateUnit yu,
    StagePoint2D minimum, StagePoint2D maximum, StageTravelRange2D travelRange,
    SlideRectangles rectangles) noexcept
    : stageComponentId_(std::move(stage)), xAxisId_(std::move(x)), yAxisId_(std::move(y)),
      xUnit_(xu), yUnit_(yu), minimumBoundary_(minimum), maximumBoundary_(maximum),
      travelRange_(travelRange), rectangles_(std::move(rectangles)) {}

Result<StageWorkingSurface2D, StageWorkingSurface2DError> StageWorkingSurface2D::create(
    const StageSpecification& stage, StageAxisId x, StageAxisId y,
    StagePoint2D minimum, StagePoint2D maximum, SlideRectangles rectangles) {
    if (!finite(minimum) || !finite(maximum))
        return Result<StageWorkingSurface2D, StageWorkingSurface2DError>::failure(
            StageWorkingSurface2DError::NonFiniteBoundary);
    if (minimum.xMicrometers >= maximum.xMicrometers ||
        minimum.yMicrometers >= maximum.yMicrometers)
        return Result<StageWorkingSurface2D, StageWorkingSurface2DError>::failure(
            StageWorkingSurface2DError::InvalidBoundary);
    auto travelRange = StageTravelRange2D::create(
        maximum.xMicrometers - minimum.xMicrometers,
        maximum.yMicrometers - minimum.yMicrometers);
    if (!travelRange)
        return Result<StageWorkingSurface2D, StageWorkingSurface2DError>::failure(
            StageWorkingSurface2DError::TravelRangeNotRepresentable);
    if (x == y)
        return Result<StageWorkingSurface2D, StageWorkingSurface2DError>::failure(
            StageWorkingSurface2DError::DuplicateStageAxis);
    const auto* xa = findAxis(stage, x);
    const auto* ya = findAxis(stage, y);
    if (!xa || !ya)
        return Result<StageWorkingSurface2D, StageWorkingSurface2DError>::failure(
            StageWorkingSurface2DError::MissingStageAxis);
    if (xa->motionType() != AxisMotionType::Linear || ya->motionType() != AxisMotionType::Linear)
        return Result<StageWorkingSurface2D, StageWorkingSurface2DError>::failure(
            StageWorkingSurface2DError::NonLinearStageAxis);
    if (!linearUnit(xa->coordinateUnit()) || !linearUnit(ya->coordinateUnit()))
        return Result<StageWorkingSurface2D, StageWorkingSurface2DError>::failure(
            StageWorkingSurface2DError::UnsupportedStageAxisUnit);
    const auto fits = [](const AxisSpecification& axis, double lo, double hi) {
        const auto& range = axis.operatingRange();
        const double axisMinimum = toMicrometers(range.minimum(), axis.coordinateUnit());
        const double axisMaximum = toMicrometers(range.maximum(), axis.coordinateUnit());
        return std::isfinite(axisMinimum) && std::isfinite(axisMaximum) &&
            lo >= axisMinimum && hi <= axisMaximum;
    };
    if (!fits(*xa, minimum.xMicrometers, maximum.xMicrometers) ||
        !fits(*ya, minimum.yMicrometers, maximum.yMicrometers))
        return Result<StageWorkingSurface2D, StageWorkingSurface2DError>::failure(
            StageWorkingSurface2DError::WorkingSurfaceOutsideAxisRange);

    for (std::size_t i = 0; i < rectangles.size(); ++i) {
        for (std::size_t j = i + 1; j < rectangles.size(); ++j) {
            if (rectangles[i].slideId() == rectangles[j].slideId())
                return Result<StageWorkingSurface2D, StageWorkingSurface2DError>::failure(
                    StageWorkingSurface2DError::DuplicateSlideIdentity);
            if (rectanglesOverlap(rectangles[i], rectangles[j]))
                return Result<StageWorkingSurface2D, StageWorkingSurface2DError>::failure(
                    StageWorkingSurface2DError::SlideRectanglesOverlap);
        }
        if (!rectangleInside(rectangles[i], minimum, maximum))
            return Result<StageWorkingSurface2D, StageWorkingSurface2DError>::failure(
                StageWorkingSurface2DError::SlideOutsideWorkingSurface);
    }

    return Result<StageWorkingSurface2D, StageWorkingSurface2DError>::success(
        StageWorkingSurface2D(stage.componentId(), std::move(x), std::move(y),
            xa->coordinateUnit(), ya->coordinateUnit(), minimum, maximum,
            travelRange.value(), std::move(rectangles)));
}
Result<StageWorkingSurface2D, StageWorkingSurface2DError> StageWorkingSurface2D::createEmpty(
    const StageSpecification& stage, StageAxisId x, StageAxisId y,
    StagePoint2D origin, StageTravelRange2D travelRange) {
    if (!finite(origin))
        return Result<StageWorkingSurface2D, StageWorkingSurface2DError>::failure(
            StageWorkingSurface2DError::NonFiniteOrigin);
    const StagePoint2D maximum{
        origin.xMicrometers + travelRange.xMicrometers(),
        origin.yMicrometers + travelRange.yMicrometers()
    };
    if (!finite(maximum) ||
        maximum.xMicrometers <= origin.xMicrometers ||
        maximum.yMicrometers <= origin.yMicrometers)
        return Result<StageWorkingSurface2D, StageWorkingSurface2DError>::failure(
            StageWorkingSurface2DError::TravelRangeNotRepresentable);

    auto validated = create(stage, x, y, origin, maximum, {});
    if (!validated)
        return Result<StageWorkingSurface2D, StageWorkingSurface2DError>::failure(
            validated.error());
    const auto& surface = validated.value();
    return Result<StageWorkingSurface2D, StageWorkingSurface2DError>::success(
        StageWorkingSurface2D(surface.stageComponentId(), surface.xAxisId(), surface.yAxisId(),
            surface.xAxisUnit(), surface.yAxisUnit(), origin, maximum,
            travelRange, {}));
}

Result<StageWorkingSurface2D, StageWorkingSurface2DError>
StageWorkingSurface2D::withSlideRectangle(const SlideRectangleDefinition2D& rectangle) const {
    for (const auto& existing : rectangles_) {
        if (existing.slideId() == rectangle.slideId())
            return Result<StageWorkingSurface2D, StageWorkingSurface2DError>::failure(
                StageWorkingSurface2DError::DuplicateSlideIdentity);
    }
    if (!rectangleInside(rectangle, minimumBoundary_, maximumBoundary_))
        return Result<StageWorkingSurface2D, StageWorkingSurface2DError>::failure(
            StageWorkingSurface2DError::SlideOutsideWorkingSurface);
    for (const auto& existing : rectangles_) {
        if (rectanglesOverlap(existing, rectangle))
            return Result<StageWorkingSurface2D, StageWorkingSurface2DError>::failure(
                StageWorkingSurface2DError::SlideRectanglesOverlap);
    }
    auto rectangles = rectangles_;
    rectangles.push_back(rectangle);
    return Result<StageWorkingSurface2D, StageWorkingSurface2DError>::success(
        StageWorkingSurface2D(stageComponentId_, xAxisId_, yAxisId_, xUnit_, yUnit_,
            minimumBoundary_, maximumBoundary_, travelRange_, std::move(rectangles)));
}

Result<StageWorkingSurface2D, StageWorkingSurface2DError>
StageWorkingSurface2D::replacingSlideRectangle(
    const SlideRectangleDefinition2D& rectangle) const {
    auto it = std::find_if(rectangles_.begin(), rectangles_.end(),
        [&](const auto& existing) { return existing.slideId() == rectangle.slideId(); });
    if (it == rectangles_.end())
        return Result<StageWorkingSurface2D, StageWorkingSurface2DError>::failure(
            StageWorkingSurface2DError::SlideNotFound);
    if (rectangle.registrationRevision() == it->registrationRevision())
        return Result<StageWorkingSurface2D, StageWorkingSurface2DError>::failure(
            StageWorkingSurface2DError::RegistrationRevisionMustChange);
    if (!rectangleInside(rectangle, minimumBoundary_, maximumBoundary_))
        return Result<StageWorkingSurface2D, StageWorkingSurface2DError>::failure(
            StageWorkingSurface2DError::SlideOutsideWorkingSurface);
    const auto index = static_cast<std::size_t>(std::distance(rectangles_.begin(), it));
    for (std::size_t i = 0; i < rectangles_.size(); ++i) {
        if (i != index && rectanglesOverlap(rectangles_[i], rectangle))
            return Result<StageWorkingSurface2D, StageWorkingSurface2DError>::failure(
                StageWorkingSurface2DError::SlideRectanglesOverlap);
    }
    auto rectangles = rectangles_;
    rectangles[index] = rectangle;
    return Result<StageWorkingSurface2D, StageWorkingSurface2DError>::success(
        StageWorkingSurface2D(stageComponentId_, xAxisId_, yAxisId_, xUnit_, yUnit_,
            minimumBoundary_, maximumBoundary_, travelRange_, std::move(rectangles)));
}

Result<StageWorkingSurface2D, StageWorkingSurface2DError>
StageWorkingSurface2D::withoutSlideRectangle(const SlideId& slideId) const {
    auto rectangles = rectangles_;
    const auto it = std::find_if(rectangles.begin(), rectangles.end(),
        [&](const auto& rectangle) { return rectangle.slideId() == slideId; });
    if (it == rectangles.end())
        return Result<StageWorkingSurface2D, StageWorkingSurface2DError>::failure(
            StageWorkingSurface2DError::SlideNotFound);
    rectangles.erase(it);
    return Result<StageWorkingSurface2D, StageWorkingSurface2DError>::success(
        StageWorkingSurface2D(stageComponentId_, xAxisId_, yAxisId_, xUnit_, yUnit_,
            minimumBoundary_, maximumBoundary_, travelRange_, std::move(rectangles)));
}

const EquipmentComponentId& StageWorkingSurface2D::stageComponentId() const noexcept { return stageComponentId_; }
const StageAxisId& StageWorkingSurface2D::xAxisId() const noexcept { return xAxisId_; }
const StageAxisId& StageWorkingSurface2D::yAxisId() const noexcept { return yAxisId_; }
AxisCoordinateUnit StageWorkingSurface2D::xAxisUnit() const noexcept { return xUnit_; }
AxisCoordinateUnit StageWorkingSurface2D::yAxisUnit() const noexcept { return yUnit_; }
const StagePoint2D& StageWorkingSurface2D::origin() const noexcept { return minimumBoundary_; }
const StageTravelRange2D& StageWorkingSurface2D::travelRange() const noexcept { return travelRange_; }
const StagePoint2D& StageWorkingSurface2D::minimumBoundary() const noexcept { return minimumBoundary_; }
const StagePoint2D& StageWorkingSurface2D::maximumBoundary() const noexcept { return maximumBoundary_; }
const StageWorkingSurface2D::SlideRectangles& StageWorkingSurface2D::slideRectangles() const noexcept { return rectangles_; }
const SlideRectangleDefinition2D* StageWorkingSurface2D::rectangleFor(const SlideId& id) const noexcept {
    const auto it = std::find_if(rectangles_.begin(), rectangles_.end(),
        [&](const auto& rectangle) { return rectangle.slideId() == id; });
    return it == rectangles_.end() ? nullptr : &*it;
}
const SlidePlacement2D* StageWorkingSurface2D::placementFor(const SlideId& id) const noexcept {
    const auto* rectangle = rectangleFor(id);
    return rectangle ? &rectangle->placement() : nullptr;
}
bool StageWorkingSurface2D::contains(const StagePoint2D& p) const noexcept {
    return finite(p) && inside(p, minimumBoundary_, maximumBoundary_);
}
Result<SlideNavigationTarget2D, SlideNavigationTarget2DError>
StageWorkingSurface2D::createNavigationTarget(
    const SlideId& slideId, const SlidePoint2D& point) const {
    const auto* rectangle = rectangleFor(slideId);
    if (rectangle == nullptr)
        return Result<SlideNavigationTarget2D, SlideNavigationTarget2DError>::failure(
            SlideNavigationTarget2DError::SlideNotFound);
    if (!rectangle->geometry().contains(point))
        return Result<SlideNavigationTarget2D, SlideNavigationTarget2DError>::failure(
            SlideNavigationTarget2DError::SlidePointOutsideGeometry);
    return Result<SlideNavigationTarget2D, SlideNavigationTarget2DError>::success(
        SlideNavigationTarget2D(slideId, point, rectangle->registrationRevision()));
}

Result<StagePoint2D, SlideNavigationTarget2DError>
StageWorkingSurface2D::resolveNavigationTarget(
    const SlideNavigationTarget2D& target) const {
    const auto* rectangle = rectangleFor(target.slideId());
    if (rectangle == nullptr)
        return Result<StagePoint2D, SlideNavigationTarget2DError>::failure(
            SlideNavigationTarget2DError::SlideNotFound);
    if (rectangle->registrationRevision() != target.registrationRevision())
        return Result<StagePoint2D, SlideNavigationTarget2DError>::failure(
            SlideNavigationTarget2DError::RegistrationChanged);
    if (!rectangle->geometry().contains(target.point()))
        return Result<StagePoint2D, SlideNavigationTarget2DError>::failure(
            SlideNavigationTarget2DError::SlidePointOutsideGeometry);
    const auto mapped = rectangle->placement().toStage(target.point());
    if (!mapped)
        return Result<StagePoint2D, SlideNavigationTarget2DError>::failure(
            mapped.error() == SlidePlacement2DError::SlidePointOutsideGeometry
                ? SlideNavigationTarget2DError::SlidePointOutsideGeometry
                : SlideNavigationTarget2DError::InvalidStageCoordinate);
    if (!contains(mapped.value()))
        return Result<StagePoint2D, SlideNavigationTarget2DError>::failure(
            SlideNavigationTarget2DError::OutsideWorkingSurface);
    return Result<StagePoint2D, SlideNavigationTarget2DError>::success(mapped.value());
}

Result<StagePoint2D, SlidePlacement2DError> StageWorkingSurface2D::toStage(
    const SlideId& id, const SlidePoint2D& p) const {
    const auto* placement = placementFor(id);
    if (!placement)
        return Result<StagePoint2D, SlidePlacement2DError>::failure(
            SlidePlacement2DError::SlideNotFound);
    return placement->toStage(p);
}
Result<SlidePosition2D, SlidePlacement2DError> StageWorkingSurface2D::toSlide(
    const SlideId& id, const StagePoint2D& p) const {
    const auto* placement = placementFor(id);
    if (!placement)
        return Result<SlidePosition2D, SlidePlacement2DError>::failure(
            SlidePlacement2DError::SlideNotFound);
    auto local = placement->toSlide(p);
    if (!local)
        return Result<SlidePosition2D, SlidePlacement2DError>::failure(local.error());
    return Result<SlidePosition2D, SlidePlacement2DError>::success({
        placement->slideId(), placement->registrationRevision(), local.value(),
        placement->geometry().contains(local.value()), contains(p)
    });
}

} // namespace microscope::core
