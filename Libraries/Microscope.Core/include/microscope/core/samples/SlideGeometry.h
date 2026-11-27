#pragma once

#include "microscope/core/result/Result.h"

namespace microscope::core {

enum class SlideGeometryError {
    NonFiniteDimension,
    NonPositiveDimension
};

// Physical dimensions of a slide in its own local coordinate frame.
class SlideGeometry final {
public:
    [[nodiscard]] static Result<SlideGeometry, SlideGeometryError> create(
        double widthMicrometers, double heightMicrometers);
    [[nodiscard]] double widthMicrometers() const noexcept;
    [[nodiscard]] double heightMicrometers() const noexcept;
    [[nodiscard]] bool contains(double xMicrometers, double yMicrometers) const noexcept;

private:
    SlideGeometry(double widthMicrometers, double heightMicrometers);
    double width_;
    double height_;
};

} // namespace microscope::core
