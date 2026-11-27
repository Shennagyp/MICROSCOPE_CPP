#include "microscope/core/samples/SlideGeometry.h"

#include <cmath>

namespace microscope::core {

SlideGeometry::SlideGeometry(double width, double height)
    : width_(width), height_(height) {}

Result<SlideGeometry, SlideGeometryError> SlideGeometry::create(
    double width, double height) {
    if (!std::isfinite(width) || !std::isfinite(height)) {
        return Result<SlideGeometry, SlideGeometryError>::failure(
            SlideGeometryError::NonFiniteDimension);
    }
    if (width <= 0.0 || height <= 0.0) {
        return Result<SlideGeometry, SlideGeometryError>::failure(
            SlideGeometryError::NonPositiveDimension);
    }
    return Result<SlideGeometry, SlideGeometryError>::success(
        SlideGeometry(width, height));
}

double SlideGeometry::widthMicrometers() const noexcept { return width_; }
double SlideGeometry::heightMicrometers() const noexcept { return height_; }

bool SlideGeometry::contains(double x, double y) const noexcept {
    return std::isfinite(x) && std::isfinite(y) &&
        x >= 0.0 && y >= 0.0 && x <= width_ && y <= height_;
}

} // namespace microscope::core
