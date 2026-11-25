#include "camera/gentl/GenApiDeviceAccess.h"

#include <utility>

namespace camera::gentl {

GenApiDeviceAccess::GenApiDeviceAccess(DeviceSession device)
    : nodeMap_(std::make_shared<GenApiNodeMap>(
          std::make_shared<DeviceSession>(std::move(device)))),
      features_(nodeMap_),
      commands_(nodeMap_) {}

} // namespace camera::gentl
