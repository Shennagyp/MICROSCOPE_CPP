#include "microscope/core/samples/SlideGeometry.h"

#include <cmath>

namespace microscope::core {

SlideGeometry::SlideGeometry(double xExtent, double yExtent)
    : xExtent_(xExtent), yExtent_(yExtent) {}

Result<SlideGeometry, SlideGeometryError> SlideGeometry::create(
    double xExtent, double yExtent) {
    if (!std::isfinite(xExtent) || !std::isfinite(yExtent)) {
        return Result<SlideGeometry, SlideGeometryError>::failure(
            SlideGeometryError::NonFiniteDimension);
    }
    if (xExtent <= 0.0 || yExtent <= 0.0) {
        return Result<SlideGeometry, SlideGeometryError>::failure(
            SlideGeometryError::NonPositiveDimension);
    }
    return Result<SlideGeometry, SlideGeometryError>::success(
        SlideGeometry(xExtent, yExtent));
}

double SlideGeometry::xExtentMicrometers() const noexcept { return xExtent_; }
double SlideGeometry::yExtentMicrometers() const noexcept { return yExtent_; }
double SlideGeometry::widthMicrometers() const noexcept { return xExtent_; }
double SlideGeometry::heightMicrometers() const noexcept { return yExtent_; }

bool SlideGeometry::contains(double x, double y) const noexcept {
    return std::isfinite(x) && std::isfinite(y) &&
        x >= 0.0 && y >= 0.0 && x <= xExtent_ && y <= yExtent_;
}

} // namespace microscope::core
