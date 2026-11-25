#pragma once

#include "camera/CameraFeatureAccess.h"
#include "camera/gentl/GenApiNodeMap.h"

#include <memory>
#include <string_view>

namespace camera::gentl {

// Adapts the GenApi node map to the backend-neutral feature contract.
// The DeviceSession is moved into this object so callers cannot close its
// remote port while GenApi nodes are still connected to it.
class GenApiFeatureAccess final : public camera::ICameraFeatureAccess {
public:
    explicit GenApiFeatureAccess(DeviceSession device);
    explicit GenApiFeatureAccess(std::shared_ptr<GenApiNodeMap> nodeMap);

    GenApiFeatureAccess(const GenApiFeatureAccess&) = delete;
    GenApiFeatureAccess& operator=(const GenApiFeatureAccess&) = delete;
    GenApiFeatureAccess(GenApiFeatureAccess&&) = delete;
    GenApiFeatureAccess& operator=(GenApiFeatureAccess&&) = delete;

    camera::CameraFeatureResult<std::vector<camera::CameraFeatureDescriptor>>
    featureDescriptors() override;

    camera::CameraFeatureResult<camera::CameraFeatureValue>
    readFeature(std::string_view name) override;

    camera::CameraFeatureResult<std::monostate>
    writeFeature(std::string_view name,
                 const camera::CameraFeatureValue& value) override;

private:
    std::shared_ptr<GenApiNodeMap> nodeMap_;
};

} // namespace camera::gentl
