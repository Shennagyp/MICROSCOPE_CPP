#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace camera {
inline constexpr std::size_t DefaultCameraRegisterTransferLimitBytes =
    16U * 1024U * 1024U;

struct CameraRegisterDescriptor {
    std::string name;
    std::string displayName;
    std::size_t sizeBytes = 0;
    bool isAvailable = false;
    bool isReadable = false;
    bool isWritable = false;
};

struct CameraRegisterAccessError {
    enum class Code : std::uint8_t {
        Unsupported, RegisterNotFound, RegisterUnavailable, RegisterNotReadable,
        RegisterNotWritable, InvalidSize, TransferLimitExceeded, BackendFailure
    };
    Code code = Code::BackendFailure;
    std::string registerName;
    std::string message;
};

template <typename T>
using CameraRegisterResult = std::variant<T, CameraRegisterAccessError>;

// Raw register values are opaque bytes, separate from scalar features.
// No integer width, byte order or bit-field meaning is inferred.
class ICameraRegisterAccess {
public:
    virtual ~ICameraRegisterAccess() = default;
    virtual CameraRegisterResult<std::vector<CameraRegisterDescriptor>>
    registerDescriptors() = 0;
    virtual CameraRegisterResult<std::vector<std::uint8_t>>
    readRegister(std::string_view name) = 0;
    virtual CameraRegisterResult<std::monostate>
    writeRegister(std::string_view name,
                  const std::vector<std::uint8_t>& bytes) = 0;
};
} // namespace camera
