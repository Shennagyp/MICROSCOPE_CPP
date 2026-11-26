#pragma once

#ifndef AMCAM_HRESULT_ERRORCODE_NEEDED
#define AMCAM_HRESULT_ERRORCODE_NEEDED
#endif
#include "amcam.h"
#include "camera/CameraFeatureAccess.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace amscope::detail {

// Classifies the first PixelFormatSupport query before its count is consumed.
// E_NOTIMPL is an explicit per-model unsupported result in the vendor SDK;
// other HRESULT failures are operational errors and must retain their code.
inline std::optional<camera::CameraFeatureAccessError>
pixelFormatEnumerationError(
    std::string feature, HRESULT result, int count) {
    using Error = camera::CameraFeatureAccessError;
    if (FAILED(result)) {
        if (result == E_NOTIMPL) {
            return Error{
                Error::Code::FeatureUnavailable,
                std::move(feature),
                "The camera SDK does not implement pixel-format enumeration.",
                static_cast<std::int32_t>(result),
                std::nullopt,
                std::nullopt
            };
        }
        return Error{
            Error::Code::BackendFailure,
            std::move(feature),
            "Amcam_get_PixelFormatSupport(count) failed.",
            static_cast<std::int32_t>(result),
            std::nullopt,
            std::nullopt
        };
    }

    if (count == 0) {
        return Error{
            Error::Code::FeatureUnavailable,
            std::move(feature),
            "The camera reports no enumerable pixel formats.",
            0,
            std::nullopt,
            std::nullopt
        };
    }

    if (count < 0 || count > 64) {
        return Error{
            Error::Code::BackendFailure,
            std::move(feature),
            "The SDK returned an invalid pixel-format count.",
            0,
            std::nullopt,
            std::nullopt
        };
    }

    return std::nullopt;
}

} // namespace amscope::detail
