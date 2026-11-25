#pragma once

#include "camera/gentl/Producer.h"

#include <GenApi/GenApi.h>

#include <memory>
#include <mutex>
#include <utility>

namespace camera::gentl {

class GenApiPort final : public GenApi::IPort {
public:
    explicit GenApiPort(std::shared_ptr<DeviceSession> device) noexcept;

    GenApi::EAccessMode GetAccessMode() const override;
    void Read(void* buffer, std::int64_t address, std::int64_t length) override;
    void Write(const void* buffer, std::int64_t address, std::int64_t length) override;

private:
    std::shared_ptr<DeviceSession> device_;
};

class GenApiNodeMap final {
public:
    explicit GenApiNodeMap(std::shared_ptr<DeviceSession> device);

    GenApiNodeMap(const GenApiNodeMap&) = delete;
    GenApiNodeMap& operator=(const GenApiNodeMap&) = delete;
    GenApiNodeMap(GenApiNodeMap&&) = delete;
    GenApiNodeMap& operator=(GenApiNodeMap&&) = delete;

    template <typename Function>
    decltype(auto) withNodeMap(Function&& function) {
        std::lock_guard<std::recursive_mutex> guard(mutex_);
        return std::forward<Function>(function)(nodeMap_);
    }

private:
    // Destruction order is significant: nodeMap_ disconnects before port_
    // and the retained DeviceSession are released.
    std::shared_ptr<DeviceSession> device_;
    GenApiPort port_;
    GenApi::CNodeMapRef nodeMap_;
    std::recursive_mutex mutex_;
};

} // namespace camera::gentl
