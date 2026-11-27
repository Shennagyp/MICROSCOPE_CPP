#include "microscope/application/navigation/SlideCornerCorrespondence.h"

#include <cstddef>
#include <utility>

namespace microscope::application::navigation {
namespace {

bool validWorkingSurfaceCorner(SlideRectangleCorner2D corner) noexcept {
    switch (corner) {
    case SlideRectangleCorner2D::StopPoint:
    case SlideRectangleCorner2D::LengthEdgeEnd:
    case SlideRectangleCorner2D::OppositeCorner:
    case SlideRectangleCorner2D::WidthEdgeEnd:
        return true;
    }
    return false;
}

std::size_t workingSurfaceCornerIndex(SlideRectangleCorner2D corner) noexcept {
    switch (corner) {
    case SlideRectangleCorner2D::StopPoint: return 0;
    case SlideRectangleCorner2D::LengthEdgeEnd: return 1;
    case SlideRectangleCorner2D::OppositeCorner: return 2;
    case SlideRectangleCorner2D::WidthEdgeEnd: return 3;
    }
    return 0;
}

} // namespace

SlideCornerCorrespondence::SlideCornerCorrespondence(
    std::array<SlideRectangleCorner2D, 4> mappings,
    bool measuredXMapsToWorkingSurfaceX) noexcept
    : mappings_(mappings),
      measuredXMapsToWorkingSurfaceX_(measuredXMapsToWorkingSurfaceX) {}

core::Result<SlideCornerCorrespondence, SlideCornerCorrespondenceError>
SlideCornerCorrespondence::create(
    std::array<SlideRectangleCorner2D, 4> mappings) {
    std::array<bool, 4> seen{{false, false, false, false}};
    std::array<std::size_t, 4> indices{};

    for (std::size_t i = 0; i < mappings.size(); ++i) {
        if (!validWorkingSurfaceCorner(mappings[i])) {
            return core::Result<SlideCornerCorrespondence,
                SlideCornerCorrespondenceError>::failure(
                    SlideCornerCorrespondenceError::InvalidCorner);
        }
        const auto index = workingSurfaceCornerIndex(mappings[i]);
        if (seen[index]) {
            return core::Result<SlideCornerCorrespondence,
                SlideCornerCorrespondenceError>::failure(
                    SlideCornerCorrespondenceError::DuplicateCorner);
        }
        seen[index] = true;
        indices[i] = index;
    }

    // A valid correspondence preserves the rectangle's cyclic adjacency.
    // Either winding is accepted, including all four cyclic rotations.
    const auto firstStep = (indices[1] + 4U - indices[0]) % 4U;
    if (firstStep != 1U && firstStep != 3U) {
        return core::Result<SlideCornerCorrespondence,
            SlideCornerCorrespondenceError>::failure(
                SlideCornerCorrespondenceError::NonAdjacentCorrespondence);
    }
    for (std::size_t i = 1; i < indices.size(); ++i) {
        const auto step = (indices[(i + 1U) % 4U] + 4U - indices[i]) % 4U;
        if (step != firstStep) {
            return core::Result<SlideCornerCorrespondence,
                SlideCornerCorrespondenceError>::failure(
                    SlideCornerCorrespondenceError::NonAdjacentCorrespondence);
        }
    }

    const auto first = indices[0];
    const auto second = indices[1];
    const bool measuredXMapsToWorkingSurfaceX =
        (first == 0U && second == 1U) ||
        (first == 1U && second == 0U) ||
        (first == 2U && second == 3U) ||
        (first == 3U && second == 2U);

    return core::Result<SlideCornerCorrespondence,
        SlideCornerCorrespondenceError>::success(
            SlideCornerCorrespondence(
                std::move(mappings), measuredXMapsToWorkingSurfaceX));
}

SlideRectangleCorner2D SlideCornerCorrespondence::workingSurfaceCornerFor(
    core::SlideCorner measuredCorner) const noexcept {
    switch (measuredCorner) {
    case core::SlideCorner::TopLeft: return mappings_[0];
    case core::SlideCorner::TopRight: return mappings_[1];
    case core::SlideCorner::BottomRight: return mappings_[2];
    case core::SlideCorner::BottomLeft: return mappings_[3];
    }
    return SlideRectangleCorner2D::StopPoint;
}

const std::array<SlideRectangleCorner2D, 4>&
SlideCornerCorrespondence::mappings() const noexcept {
    return mappings_;
}

bool SlideCornerCorrespondence::measuredXMapsToWorkingSurfaceX() const noexcept {
    return measuredXMapsToWorkingSurfaceX_;
}

} // namespace microscope::application::navigation
