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
            "Camera feature is not available on this device.",
            0, std::nullopt, std::nullopt
        };
    }
    if (!descriptor.isWritable) {
        return CameraFeatureAccessError{
            CameraFeatureAccessError::Code::FeatureNotWritable,
            descriptor.name,
            "Camera feature is not writable in the current state.",
            0, std::nullopt, std::nullopt
        };
    }

    const CameraFeatureKind actualKind = cameraFeatureKind(value);
    if (actualKind != descriptor.kind) {
        CameraFeatureAccessError error{
            CameraFeatureAccessError::Code::ValueTypeMismatch,
            descriptor.name,
            "Camera feature value has the wrong type.",
            0, descriptor.kind, actualKind
        };
        return error;
    }

    if (!descriptor.discreteIntegerValues.empty() &&
        descriptor.kind != CameraFeatureKind::Integer) {
        return CameraFeatureAccessError{
            CameraFeatureAccessError::Code::BackendFailure,
            descriptor.name,
            "Invalid descriptor: discreteIntegerValues requires an integer feature.",
            0, std::nullopt, std::nullopt
        };
    }

    if (descriptor.kind == CameraFeatureKind::Integer &&
        !descriptor.discreteIntegerValues.empty()) {
        const auto requested = std::get<std::int64_t>(value);
        if (std::find(descriptor.discreteIntegerValues.begin(),
                      descriptor.discreteIntegerValues.end(), requested) ==
            descriptor.discreteIntegerValues.end()) {
            return CameraFeatureAccessError{
                CameraFeatureAccessError::Code::InvalidValue,
                descriptor.name,
                "Integer value is not one of the supported discrete values.",
                0, std::nullopt, std::nullopt
            };
        }
    }

    if (descriptor.elementCount) {
        if (descriptor.kind != CameraFeatureKind::IntegerVector) {
            return CameraFeatureAccessError{
                CameraFeatureAccessError::Code::BackendFailure,
                descriptor.name,
                "Invalid descriptor: elementCount requires an integer-vector feature.",
                0, std::nullopt, std::nullopt
            };
        }
        const auto& values = std::get<std::vector<std::int64_t>>(value);
        if (values.size() != *descriptor.elementCount) {
            return CameraFeatureAccessError{
                CameraFeatureAccessError::Code::InvalidValue,
                descriptor.name,
                "Vector length does not match the feature descriptor.",
                0, std::nullopt, std::nullopt
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
                "Enumeration symbol is not supported by this camera.",
                0, std::nullopt, std::nullopt
            };
        }
    }

    if (descriptor.numericRange) {
        if (descriptor.kind == CameraFeatureKind::Integer) {
            const auto* range =
                std::get_if<CameraIntegerRange>(&*descriptor.numericRange);
            if (!range) {
                return CameraFeatureAccessError{
                    CameraFeatureAccessError::Code::BackendFailure,
                    descriptor.name,
                    "Invalid descriptor: integer features require an integer range.",
                    0, std::nullopt, std::nullopt
                };
            }

            if (range->minimum > range->maximum ||
                (range->increment && *range->increment <= 0)) {
                return CameraFeatureAccessError{
                    CameraFeatureAccessError::Code::BackendFailure,
                    descriptor.name,
                    "Camera returned an invalid integer range or increment.",
                    0, std::nullopt, std::nullopt
                };
            }

            const std::int64_t number = std::get<std::int64_t>(value);
            if (number < range->minimum || number > range->maximum) {
                return CameraFeatureAccessError{
                    CameraFeatureAccessError::Code::InvalidValue,
                    descriptor.name,
                    "Value is outside the current feature range.",
                    0, std::nullopt, std::nullopt
                };
            }

            if (range->increment) {
                // Unsigned subtraction preserves the exact mathematical
                // difference across the complete int64 domain without
                // overflowing signed arithmetic.
                const auto offset =
                    static_cast<std::uint64_t>(number) -
                    static_cast<std::uint64_t>(range->minimum);
                if (offset % static_cast<std::uint64_t>(*range->increment) != 0) {
                    return CameraFeatureAccessError{
                        CameraFeatureAccessError::Code::InvalidValue,
                        descriptor.name,
                        "Value does not satisfy the feature increment.",
                        0, std::nullopt, std::nullopt
                    };
                }
            }
        } else if (descriptor.kind == CameraFeatureKind::FloatingPoint) {
            const auto* range =
                std::get_if<CameraFloatingPointRange>(&*descriptor.numericRange);
            if (!range) {
                return CameraFeatureAccessError{
                    CameraFeatureAccessError::Code::BackendFailure,
                    descriptor.name,
                    "Invalid descriptor: floating-point features require a floating-point range.",
                    0, std::nullopt, std::nullopt
                };
            }

            if (!std::isfinite(range->minimum) ||
                !std::isfinite(range->maximum) ||
                range->minimum > range->maximum ||
                (range->increment &&
                 (!std::isfinite(*range->increment) || *range->increment <= 0.0))) {
                return CameraFeatureAccessError{
                    CameraFeatureAccessError::Code::BackendFailure,
                    descriptor.name,
                    "Camera returned an invalid floating-point range or increment.",
                    0, std::nullopt, std::nullopt
                };
            }

            const double number = std::get<double>(value);
            if (!std::isfinite(number) ||
                number < range->minimum || number > range->maximum) {
                return CameraFeatureAccessError{
                    CameraFeatureAccessError::Code::InvalidValue,
                    descriptor.name,
                    "Value is outside the current feature range.",
                    0, std::nullopt, std::nullopt
                };
            }

            if (range->increment) {
                const double steps =
                    (number - range->minimum) / *range->increment;
                if (!std::isfinite(steps) ||
                    std::abs(steps - std::round(steps)) > 1e-7) {
                    return CameraFeatureAccessError{
                        CameraFeatureAccessError::Code::InvalidValue,
                        descriptor.name,
                        "Value does not satisfy the feature increment.",
                        0, std::nullopt, std::nullopt
                    };
                }
            }
        } else {
            return CameraFeatureAccessError{
                CameraFeatureAccessError::Code::BackendFailure,
                descriptor.name,
                "Invalid descriptor: numericRange requires a scalar numeric feature.",
                0, std::nullopt, std::nullopt
            };
        }
    }
    return std::monostate{};
}

} // namespace camera
