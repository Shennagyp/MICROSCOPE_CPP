#pragma once

#ifndef AMCAM_HRESULT_ERRORCODE_NEEDED
#define AMCAM_HRESULT_ERRORCODE_NEEDED
#endif
#include "amcam.h"

#include "camera/CameraOperationError.h"

#include <cstdint>

namespace amscope::detail {

// Translate only HRESULT values whose meanings are explicitly documented by
// the bundled Amcam SDK. All other values remain backend failures; the exact
// signed HRESULT is retained separately as the native error identity.
[[nodiscard]] inline constexpr camera::CameraErrorCode commonCameraErrorCode(
    std::int32_t nativeCode) noexcept {
    switch (static_cast<HRESULT>(nativeCode)) {
    case E_INVALIDARG:
    case E_POINTER:
        return camera::CameraErrorCode::InvalidArgument;
    case E_NOTIMPL:
    case E_NOINTERFACE:
        return camera::CameraErrorCode::Unsupported;
    case E_OUTOFMEMORY:
        return camera::CameraErrorCode::ResourceExhausted;
    case E_TIMEOUT:
        return camera::CameraErrorCode::Timeout;
    case E_BUSY:
    case E_UNEXPECTED:
    case E_WRONG_THREAD:
        return camera::CameraErrorCode::InvalidState;
    case E_ACCESSDENIED:
        return camera::CameraErrorCode::PermissionDenied;
    case E_GEN_FAILURE:
    case E_UNREACH:
        return camera::CameraErrorCode::TransportFailure;
    case E_PENDING:
        return camera::CameraErrorCode::Unavailable;
    default:
        return camera::CameraErrorCode::BackendFailure;
    }
}

} // namespace amscope::detail
