#include "AmcamErrorMapping.h"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}
} // namespace

int main() {
    using camera::CameraErrorCode;
    using amscope::detail::commonCameraErrorCode;

    static_assert(commonCameraErrorCode(E_INVALIDARG) == CameraErrorCode::InvalidArgument);
    static_assert(commonCameraErrorCode(E_POINTER) == CameraErrorCode::InvalidArgument);
    static_assert(commonCameraErrorCode(E_NOTIMPL) == CameraErrorCode::Unsupported);
    static_assert(commonCameraErrorCode(E_NOINTERFACE) == CameraErrorCode::Unsupported);
    static_assert(commonCameraErrorCode(E_OUTOFMEMORY) == CameraErrorCode::ResourceExhausted);
    static_assert(commonCameraErrorCode(E_TIMEOUT) == CameraErrorCode::Timeout);
    static_assert(commonCameraErrorCode(E_BUSY) == CameraErrorCode::InvalidState);
    static_assert(commonCameraErrorCode(E_UNEXPECTED) == CameraErrorCode::InvalidState);
    static_assert(commonCameraErrorCode(E_WRONG_THREAD) == CameraErrorCode::InvalidState);
    static_assert(commonCameraErrorCode(E_ACCESSDENIED) == CameraErrorCode::PermissionDenied);
    static_assert(commonCameraErrorCode(E_GEN_FAILURE) == CameraErrorCode::TransportFailure);
    static_assert(commonCameraErrorCode(E_UNREACH) == CameraErrorCode::TransportFailure);
    static_assert(commonCameraErrorCode(E_PENDING) == CameraErrorCode::Unavailable);

    require(commonCameraErrorCode(static_cast<std::int32_t>(0x81234567U)) ==
                CameraErrorCode::BackendFailure,
            "unknown native HRESULTs must not be assigned speculative common semantics");
    require(commonCameraErrorCode(E_FAIL) == CameraErrorCode::BackendFailure,
            "generic SDK failure must remain BackendFailure");
    return EXIT_SUCCESS;
}
