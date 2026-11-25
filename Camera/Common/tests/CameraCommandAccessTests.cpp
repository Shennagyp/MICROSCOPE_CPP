#include "camera/CameraCommandAccess.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace {

class FakeCommandAccess final : public camera::ICameraCommandAccess {
public:
    camera::CameraCommandResult<std::vector<camera::CameraCommandDescriptor>>
    commandDescriptors() override {
        return std::vector<camera::CameraCommandDescriptor>{
            {"AcquisitionStart", "Acquisition Start", true, true, std::nullopt},
            {"DeviceReset", "Device Reset", false, false,
             std::string("Command unavailable.")}};
    }

    camera::CameraCommandResult<std::monostate>
    executeCommand(std::string_view name) override {
        if (name != "AcquisitionStart") {
            return camera::CameraCommandAccessError{
                camera::CameraCommandAccessError::Code::CommandNotFound,
                std::string(name), "Command not found.", 0};
        }
        issued = true;
        complete = false;
        return std::monostate{};
    }

    camera::CameraCommandResult<bool>
    isCommandDone(std::string_view name) override {
        if (name != "AcquisitionStart") {
            return camera::CameraCommandAccessError{
                camera::CameraCommandAccessError::Code::CommandNotFound,
                std::string(name), "Command not found.", 0};
        }
        return complete;
    }

    bool issued = false;
    bool complete = false;
};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

} // namespace

int main() {
    FakeCommandAccess access;
    const auto descriptors = access.commandDescriptors();
    require(std::holds_alternative<
                std::vector<camera::CameraCommandDescriptor>>(descriptors),
            "Command discovery did not return descriptors.");
    const auto& values =
        std::get<std::vector<camera::CameraCommandDescriptor>>(descriptors);
    require(values.size() == 2, "Unexpected command descriptor count.");
    require(values[0].isExecutable, "Executable command was marked unavailable.");
    require(!values[1].isExecutable && values[1].disabledReason,
            "Unavailable command state was not represented.");

    const auto executed = access.executeCommand("AcquisitionStart");
    require(std::holds_alternative<std::monostate>(executed),
            "Command execution request failed.");
    require(access.issued, "Command was not issued.");

    const auto done = access.isCommandDone("AcquisitionStart");
    require(std::holds_alternative<bool>(done) &&
            !std::get<bool>(done),
            "Command execution was incorrectly treated as completed.");

    access.complete = true;
    const auto completed = access.isCommandDone("AcquisitionStart");
    require(std::holds_alternative<bool>(completed) &&
            std::get<bool>(completed),
            "Command completion state was not observable.");

    const auto missing = access.executeCommand("Missing");
    require(std::holds_alternative<camera::CameraCommandAccessError>(missing),
            "Missing command did not produce a typed error.");
    return 0;
}
