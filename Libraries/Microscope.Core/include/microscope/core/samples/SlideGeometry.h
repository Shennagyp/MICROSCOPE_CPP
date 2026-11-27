#pragma once

#include "microscope/core/result/Result.h"

namespace microscope::core {

enum class SlideGeometryError {
    NonFiniteDimension,
    NonPositiveDimension
};

// Physical extents in a slide-local X/Y coordinate frame. The historical
// width/height accessors remain aliases for X/Y respectively; new code should
// use the axis-neutral extent names so orientation is owned by placement.
class SlideGeometry final {
public:
    [[nodiscard]] static Result<SlideGeometry, SlideGeometryError> create(
        double xExtentMicrometers, double yExtentMicrometers);
    [[nodiscard]] double xExtentMicrometers() const noexcept;
    [[nodiscard]] double yExtentMicrometers() const noexcept;
    [[nodiscard]] double widthMicrometers() const noexcept;
    [[nodiscard]] double heightMicrometers() const noexcept;
    [[nodiscard]] bool contains(double xMicrometers, double yMicrometers) const noexcept;

private:
    SlideGeometry(double xExtentMicrometers, double yExtentMicrometers);
    double xExtent_;
    double yExtent_;
};

} // namespace microscope::core
