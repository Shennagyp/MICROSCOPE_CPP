#pragma once

#include "microscope/core/equipment/StageSpecifications.h"

namespace microscope::core {

// Physical orientation of the two-slide carrier relative to the stage X axis.
enum class CarrierHorizontalDirection {
    PositiveStageXFromLeftToRight,
    NegativeStageXFromLeftToRight
};

enum class DualSlideCarrierSpecificationError {
    SamePlaneAxis,
    MissingHorizontalAxis,
    MissingTransverseAxis,
    NonLinearPlaneAxis,
    NonPhysicalPlaneAxisUnit,
    InvalidHorizontalDirection
};

// Immutable physical carrier-to-stage binding. It contains equipment facts
// only; navigation command construction belongs to the navigation layer.
class DualSlideCarrierSpecification final {
public:
    [[nodiscard]] static Result<DualSlideCarrierSpecification,
        DualSlideCarrierSpecificationError> create(
            const StageSpecification& stage,
            StageAxisId horizontalAxisId,
            StageAxisId transverseAxisId,
            CarrierHorizontalDirection direction);

    [[nodiscard]] const EquipmentComponentId& stageComponentId() const noexcept;
    [[nodiscard]] const StageAxisId& horizontalAxisId() const noexcept;
    [[nodiscard]] const StageAxisId& transverseAxisId() const noexcept;
    [[nodiscard]] AxisCoordinateUnit horizontalUnit() const noexcept;
    [[nodiscard]] AxisCoordinateUnit transverseUnit() const noexcept;
    [[nodiscard]] CarrierHorizontalDirection horizontalDirection() const noexcept;
    [[nodiscard]] const AxisOperatingRange& horizontalRange() const noexcept;
    [[nodiscard]] const AxisOperatingRange& transverseRange() const noexcept;

private:
    DualSlideCarrierSpecification(
        EquipmentComponentId stageComponentId,
        StageAxisId horizontalAxisId,
        StageAxisId transverseAxisId,
        CarrierHorizontalDirection direction,
        AxisCoordinateUnit horizontalUnit,
        AxisCoordinateUnit transverseUnit,
        AxisOperatingRange horizontalRange,
        AxisOperatingRange transverseRange);

    EquipmentComponentId stageComponentId_;
    StageAxisId horizontalAxisId_;
    StageAxisId transverseAxisId_;
    CarrierHorizontalDirection direction_;
    AxisCoordinateUnit horizontalUnit_;
    AxisCoordinateUnit transverseUnit_;
    AxisOperatingRange horizontalRange_;
    AxisOperatingRange transverseRange_;
};

} // namespace microscope::core
