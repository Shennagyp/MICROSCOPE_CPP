#pragma once

#include "microscope/core/navigation/DualSlideNavigation.h"

#include <array>

namespace microscope::application::navigation {

// A corner of the CAD slide rectangle. The order is cyclic:
// stop point -> length edge -> opposite corner -> width edge.
enum class SlideRectangleCorner2D {
    StopPoint,
    LengthEdgeEnd,
    OppositeCorner,
    WidthEdgeEnd
};

enum class SlideCornerCorrespondenceError {
    InvalidCorner,
    DuplicateCorner,
    NonAdjacentCorrespondence
};

// Explicitly states which CAD corner represents each measured physical corner.
// The input array order is TopLeft, TopRight, BottomRight, BottomLeft.
// Only cyclic rotations and reflections are accepted; an edge cannot map to
// a diagonal and the correspondence cannot silently fold the rectangle.
class SlideCornerCorrespondence final {
public:
    [[nodiscard]] static core::Result<SlideCornerCorrespondence,
        SlideCornerCorrespondenceError> create(
            std::array<SlideRectangleCorner2D, 4> workingSurfaceCorners);

    [[nodiscard]] SlideRectangleCorner2D workingSurfaceCornerFor(
        core::SlideCorner measuredCorner) const noexcept;
    [[nodiscard]] const std::array<SlideRectangleCorner2D, 4>&
    mappings() const noexcept;
    [[nodiscard]] bool measuredXMapsToWorkingSurfaceX() const noexcept;

private:
    explicit SlideCornerCorrespondence(
        std::array<SlideRectangleCorner2D, 4>, bool) noexcept;

    std::array<SlideRectangleCorner2D, 4> mappings_;
    bool measuredXMapsToWorkingSurfaceX_;
};

} // namespace microscope::application::navigation
