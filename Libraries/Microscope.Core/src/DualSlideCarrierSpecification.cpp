#include "microscope/core/equipment/DualSlideCarrierSpecification.h"

#include <algorithm>
#include <utility>

namespace microscope::core {
namespace {
bool physicalLinear(AxisCoordinateUnit unit) noexcept {
    return unit == AxisCoordinateUnit::Micrometers ||
        unit == AxisCoordinateUnit::Millimeters;
}
}

DualSlideCarrierSpecification::DualSlideCarrierSpecification(
    EquipmentComponentId componentId,
    StageAxisId horizontalAxisId,
    StageAxisId transverseAxisId,
    CarrierHorizontalDirection direction,
    AxisCoordinateUnit horizontalUnit,
    AxisCoordinateUnit transverseUnit,
    AxisOperatingRange horizontalRange,
    AxisOperatingRange transverseRange)
    : stageComponentId_(std::move(componentId)),
      horizontalAxisId_(std::move(horizontalAxisId)),
      transverseAxisId_(std::move(transverseAxisId)),
      direction_(direction),
      horizontalUnit_(horizontalUnit),
      transverseUnit_(transverseUnit),
      horizontalRange_(horizontalRange),
      transverseRange_(transverseRange) {}

Result<DualSlideCarrierSpecification, DualSlideCarrierSpecificationError>
DualSlideCarrierSpecification::create(
    const StageSpecification& stage,
    StageAxisId horizontalAxisId,
    StageAxisId transverseAxisId,
    CarrierHorizontalDirection direction) {
    if (horizontalAxisId == transverseAxisId) {
        return Result<DualSlideCarrierSpecification, DualSlideCarrierSpecificationError>::failure(
            DualSlideCarrierSpecificationError::SamePlaneAxis);
    }
    const auto horizontal = std::find_if(stage.axes().begin(), stage.axes().end(),
        [&](const auto& axis) { return axis.id() == horizontalAxisId; });
    if (horizontal == stage.axes().end()) {
        return Result<DualSlideCarrierSpecification, DualSlideCarrierSpecificationError>::failure(
            DualSlideCarrierSpecificationError::MissingHorizontalAxis);
    }
    const auto transverse = std::find_if(stage.axes().begin(), stage.axes().end(),
        [&](const auto& axis) { return axis.id() == transverseAxisId; });
    if (transverse == stage.axes().end()) {
        return Result<DualSlideCarrierSpecification, DualSlideCarrierSpecificationError>::failure(
            DualSlideCarrierSpecificationError::MissingTransverseAxis);
    }
    if (horizontal->motionType() != AxisMotionType::Linear ||
        transverse->motionType() != AxisMotionType::Linear) {
        return Result<DualSlideCarrierSpecification, DualSlideCarrierSpecificationError>::failure(
            DualSlideCarrierSpecificationError::NonLinearPlaneAxis);
    }
    if (!physicalLinear(horizontal->coordinateUnit()) ||
        !physicalLinear(transverse->coordinateUnit())) {
        return Result<DualSlideCarrierSpecification, DualSlideCarrierSpecificationError>::failure(
            DualSlideCarrierSpecificationError::NonPhysicalPlaneAxisUnit);
    }
    if (direction != CarrierHorizontalDirection::PositiveStageXFromLeftToRight &&
        direction != CarrierHorizontalDirection::NegativeStageXFromLeftToRight) {
        return Result<DualSlideCarrierSpecification, DualSlideCarrierSpecificationError>::failure(
            DualSlideCarrierSpecificationError::InvalidHorizontalDirection);
    }
    return Result<DualSlideCarrierSpecification, DualSlideCarrierSpecificationError>::success(
        DualSlideCarrierSpecification(
            stage.componentId(), std::move(horizontalAxisId), std::move(transverseAxisId),
            direction, horizontal->coordinateUnit(), transverse->coordinateUnit(),
            horizontal->operatingRange(), transverse->operatingRange()));
}

const EquipmentComponentId& DualSlideCarrierSpecification::stageComponentId() const noexcept {
    return stageComponentId_;
}
const StageAxisId& DualSlideCarrierSpecification::horizontalAxisId() const noexcept {
    return horizontalAxisId_;
}
const StageAxisId& DualSlideCarrierSpecification::transverseAxisId() const noexcept {
    return transverseAxisId_;
}
AxisCoordinateUnit DualSlideCarrierSpecification::horizontalUnit() const noexcept {
    return horizontalUnit_;
}
AxisCoordinateUnit DualSlideCarrierSpecification::transverseUnit() const noexcept {
    return transverseUnit_;
}
CarrierHorizontalDirection DualSlideCarrierSpecification::horizontalDirection() const noexcept {
    return direction_;
}
const AxisOperatingRange& DualSlideCarrierSpecification::horizontalRange() const noexcept {
    return horizontalRange_;
}
const AxisOperatingRange& DualSlideCarrierSpecification::transverseRange() const noexcept {
    return transverseRange_;
}

} // namespace microscope::core
