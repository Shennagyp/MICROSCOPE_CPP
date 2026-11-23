#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace camera {

// Operation-level failures shared by camera backends. Feature-specific
// validation/access failures remain CameraFeatureAccessError because they
// carry feature identity and expected/actual value kinds.
enum class CameraErrorCode : std::uint8_t {
    InvalidArgument,
    NotFound,
    Unsupported,
    Unavailable,
    InvalidState,
    Timeout,
    Cancelled,
    ResourceExhausted,
    DeviceDisconnected,
    TransportFailure,
    BackendFailure,
    InternalFailure,
    PermissionDenied
};

// Backend details are preserved rather than translated into a fabricated
// common code. A native code is meaningful only within its declared domain.
struct CameraNativeError {
    std::string domain;
    std::int64_t code = 0;
};

struct CameraOperationError {
    CameraErrorCode code = CameraErrorCode::InternalFailure;
    std::string operation;
    std::string message;
    std::optional<CameraNativeError> native;
};

template <typename T>
using CameraOperationResult = std::variant<T, CameraOperationError>;

} // namespace camera
