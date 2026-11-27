#pragma once
#include "camera/CameraRegisterAccess.h"
#include "camera/gentl/GenApiNodeMap.h"
#include <memory>

namespace camera::gentl {
class GenApiRegisterAccess final : public camera::ICameraRegisterAccess {
public:
    explicit GenApiRegisterAccess(
        std::shared_ptr<GenApiNodeMap> nodeMap,
        std::size_t maximumTransferBytes =
            camera::DefaultCameraRegisterTransferLimitBytes);
    camera::CameraRegisterResult<std::vector<camera::CameraRegisterDescriptor>>
    registerDescriptors() override;
    camera::CameraRegisterResult<std::vector<std::uint8_t>>
    readRegister(std::string_view name) override;
    camera::CameraRegisterResult<std::monostate>
    writeRegister(std::string_view name,
                  const std::vector<std::uint8_t>& bytes) override;
private:
    std::shared_ptr<GenApiNodeMap> nodeMap_;
    std::size_t maximumTransferBytes_;
};
} // namespace camera::gentl
