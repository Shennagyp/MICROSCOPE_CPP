#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace camera {

struct CameraCommandDescriptor {
    std::string name;
    std::string displayName;
    bool isAvailable = false;
    bool isExecutable = false;
    std::optional<std::string> disabledReason;
};

struct CameraCommandAccessError {
    enum class Code {
        Unsupported,
        CommandNotFound,
        CommandUnavailable,
        CommandNotExecutable,
        BackendFailure
    };

    Code code = Code::BackendFailure;
    std::string command;
    std::string message;
    std::int32_t nativeCode = 0;
};

template <typename T>
using CameraCommandResult = std::variant<T, CameraCommandAccessError>;

// Commands are actions, not feature values. Execution and completion are
// separate because GenApi commands may run asynchronously on the device.
class ICameraCommandAccess {
public:
    virtual ~ICameraCommandAccess() = default;

    virtual CameraCommandResult<std::vector<CameraCommandDescriptor>>
    commandDescriptors() = 0;

    // Returns when the backend has issued the command. It does not imply that
    // an asynchronous device operation has completed.
    virtual CameraCommandResult<std::monostate>
    executeCommand(std::string_view name) = 0;

    // Polls the command's device-side completion state.
    virtual CameraCommandResult<bool>
    isCommandDone(std::string_view name) = 0;
};

} // namespace camera
