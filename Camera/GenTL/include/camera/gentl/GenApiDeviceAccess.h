#pragma once

#include "camera/gentl/GenApiCommandAccess.h"
#include "camera/gentl/GenApiFeatureAccess.h"
#include "camera/gentl/GenApiRegisterAccess.h"

#include <memory>

namespace camera::gentl {

// Owns one DeviceSession and one shared GenApi node map for all device-level
// GenApi operations. Feature and command adapters therefore share the same
// remote port, node cache and serialization boundary.
class GenApiDeviceAccess final {
public:
    explicit GenApiDeviceAccess(
        DeviceSession device,
        std::size_t maximumRegisterTransferBytes =
            camera::DefaultCameraRegisterTransferLimitBytes);

    GenApiDeviceAccess(const GenApiDeviceAccess&) = delete;
    GenApiDeviceAccess& operator=(const GenApiDeviceAccess&) = delete;
    GenApiDeviceAccess(GenApiDeviceAccess&&) = delete;
    GenApiDeviceAccess& operator=(GenApiDeviceAccess&&) = delete;

    GenApiFeatureAccess& features() noexcept { return features_; }
    GenApiCommandAccess& commands() noexcept { return commands_; }
    GenApiRegisterAccess& registers() noexcept { return registers_; }

private:
    std::shared_ptr<GenApiNodeMap> nodeMap_;
    GenApiFeatureAccess features_;
    GenApiCommandAccess commands_;
    GenApiRegisterAccess registers_;
};

} // namespace camera::gentl
