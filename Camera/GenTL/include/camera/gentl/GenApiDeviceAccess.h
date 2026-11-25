#pragma once

#include "camera/gentl/GenApiCommandAccess.h"
#include "camera/gentl/GenApiFeatureAccess.h"

#include <memory>

namespace camera::gentl {

// Owns one DeviceSession and one shared GenApi node map for all device-level
// GenApi operations. Feature and command adapters therefore share the same
// remote port, node cache and serialization boundary.
class GenApiDeviceAccess final {
public:
    explicit GenApiDeviceAccess(DeviceSession device);

    GenApiDeviceAccess(const GenApiDeviceAccess&) = delete;
    GenApiDeviceAccess& operator=(const GenApiDeviceAccess&) = delete;
    GenApiDeviceAccess(GenApiDeviceAccess&&) = delete;
    GenApiDeviceAccess& operator=(GenApiDeviceAccess&&) = delete;

    GenApiFeatureAccess& features() noexcept { return features_; }
    GenApiCommandAccess& commands() noexcept { return commands_; }

private:
    std::shared_ptr<GenApiNodeMap> nodeMap_;
    GenApiFeatureAccess features_;
    GenApiCommandAccess commands_;
};

} // namespace camera::gentl
