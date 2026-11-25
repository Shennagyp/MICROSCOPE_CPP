#pragma once

#include "camera/CameraCommandAccess.h"
#include "camera/gentl/GenApiNodeMap.h"

#include <memory>
#include <string_view>

namespace camera::gentl {

// GenApi command nodes are deliberately separate from feature values.
// Execute issues the command; isCommandDone observes device-side completion.
class GenApiCommandAccess final : public camera::ICameraCommandAccess {
public:
    explicit GenApiCommandAccess(DeviceSession device);
    explicit GenApiCommandAccess(std::shared_ptr<GenApiNodeMap> nodeMap);

    GenApiCommandAccess(const GenApiCommandAccess&) = delete;
    GenApiCommandAccess& operator=(const GenApiCommandAccess&) = delete;
    GenApiCommandAccess(GenApiCommandAccess&&) = delete;
    GenApiCommandAccess& operator=(GenApiCommandAccess&&) = delete;

    camera::CameraCommandResult<std::vector<camera::CameraCommandDescriptor>>
    commandDescriptors() override;

    camera::CameraCommandResult<std::monostate>
    executeCommand(std::string_view name) override;

    camera::CameraCommandResult<bool>
    isCommandDone(std::string_view name) override;

private:
    std::shared_ptr<GenApiNodeMap> nodeMap_;
};

} // namespace camera::gentl
