#pragma once

#include "camera/CameraFeatureAccess.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace camera {

inline CameraFeatureKind cameraFeatureKind(const CameraFeatureValue& value) noexcept {
    return std::visit([](const auto& item) noexcept {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, bool>) {
            return CameraFeatureKind::Boolean;
        } else if constexpr (std::is_same_v<T, std::int64_t>) {
            return CameraFeatureKind::Integer;
        } else if constexpr (std::is_same_v<T, std::vector<std::int64_t>>) {
            return CameraFeatureKind::IntegerVector;
        } else if constexpr (std::is_same_v<T, double>) {
            return CameraFeatureKind::FloatingPoint;
        } else if constexpr (std::is_same_v<T, FeatureEnumeration>) {
            return CameraFeatureKind::Enumeration;
        } else {
            return CameraFeatureKind::String;
        }
    }, value);
}

inline CameraFeatureResult<std::monostate> validateFeatureWrite(
    const CameraFeatureDescriptor& descriptor,
    const CameraFeatureValue& value) {

    if (!descriptor.isAvailable) {
        return CameraFeatureAccessError{
            CameraFeatureAccessError::Code::FeatureUnavailable,
            descriptor.name,
            "Camera feature is not available on this device."
        };
    }
    if (!descriptor.isWritable) {
        return CameraFeatureAccessError{
            CameraFeatureAccessError::Code::FeatureNotWritable,
            descriptor.name,
            "Camera feature is not writable in the current state."
        };
    }

    const CameraFeatureKind actualKind = cameraFeatureKind(value);
    if (actualKind != descriptor.kind) {
        CameraFeatureAccessError error{
            CameraFeatureAccessError::Code::ValueTypeMismatch,
            descriptor.name,
            "Camera feature value has the wrong type."
        };
        error.expectedKind = descriptor.kind;
        error.actualKind = actualKind;
        return error;
    }

    if (descriptor.elementCount) {
        if (descriptor.kind != CameraFeatureKind::IntegerVector) {
            return CameraFeatureAccessError{
                CameraFeatureAccessError::Code::BackendFailure,
                descriptor.name,
                "Invalid descriptor: elementCount requires an integer-vector feature."
            };
        }
        const auto& values = std::get<std::vector<std::int64_t>>(value);
        if (values.size() != *descriptor.elementCount) {
            return CameraFeatureAccessError{
                CameraFeatureAccessError::Code::InvalidValue,
                descriptor.name,
                "Vector length does not match the feature descriptor."
            };
        }
    }

    if (descriptor.kind == CameraFeatureKind::Enumeration) {
        const auto& symbol = std::get<FeatureEnumeration>(value).symbol;
        if (std::find(descriptor.enumerationValues.begin(),
                      descriptor.enumerationValues.end(), symbol) ==
            descriptor.enumerationValues.end()) {
            return CameraFeatureAccessError{
                CameraFeatureAccessError::Code::InvalidValue,
                descriptor.name,
                "Enumeration symbol is not supported by this camera."
            };
        }
    }

    if (descriptor.numericRange) {
        if (descriptor.kind != CameraFeatureKind::Integer &&
            descriptor.kind != CameraFeatureKind::FloatingPoint) {
            return CameraFeatureAccessError{
                CameraFeatureAccessError::Code::BackendFailure,
                descriptor.name,
                "Invalid descriptor: numericRange requires a scalar numeric feature."
            };
        }

        const auto& range = *descriptor.numericRange;
        if (!std::isfinite(range.minimum) ||
            !std::isfinite(range.maximum) ||
            range.minimum > range.maximum ||
            (range.increment &&
             (!std::isfinite(*range.increment) || *range.increment <= 0.0))) {
            return CameraFeatureAccessError{
                CameraFeatureAccessError::Code::BackendFailure,
                descriptor.name,
                "Camera returned an invalid numeric range or increment."
            };
        }

        const double number = descriptor.kind == CameraFeatureKind::Integer
            ? static_cast<double>(std::get<std::int64_t>(value))
            : std::get<double>(value);

        if (!std::isfinite(number) ||
            number < range.minimum || number > range.maximum) {
            return CameraFeatureAccessError{
                CameraFeatureAccessError::Code::InvalidValue,
                descriptor.name,
                "Value is outside the current feature range."
            };
        }

        if (range.increment) {
            const double steps = (number - range.minimum) / *range.increment;
            if (!std::isfinite(steps) ||
                std::abs(steps - std::round(steps)) > 1e-7) {
                return CameraFeatureAccessError{
                    CameraFeatureAccessError::Code::InvalidValue,
                    descriptor.name,
                    "Value does not satisfy the feature increment."
                };
            }
        }
    }

    return std::monostate{};
}

} // namespace camera
