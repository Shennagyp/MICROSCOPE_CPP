#pragma once

#include "camera/gentl/ConsumerBridge.h"
#include "camera/gentl/GenTL_v1_6.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace camera::gentl {

struct DeviceDescriptor {
    std::string producerPath;
    std::string interfaceId;
    std::string interfaceDisplayName;
    std::string deviceId;
    std::string displayName;
    std::string vendor;
    std::string model;
    std::string serialNumber;
};

class ProducerContext;

class DeviceSession final {
public:
    DeviceSession() noexcept = default;
    ~DeviceSession();

    DeviceSession(const DeviceSession&) = delete;
    DeviceSession& operator=(const DeviceSession&) = delete;
    DeviceSession(DeviceSession&& other) noexcept;
    DeviceSession& operator=(DeviceSession&& other) noexcept;

    explicit operator bool() const noexcept;
    const std::string& deviceId() const noexcept;

    std::vector<std::string> dataStreamIds() const;
    std::vector<std::uint8_t> readPort(
        std::uint64_t address, std::size_t size) const;
    void writePort(
        std::uint64_t address, const std::vector<std::uint8_t>& bytes) const;
    std::string portUrl() const;

    void close();

private:
    friend class Producer;
    DeviceSession(std::shared_ptr<ProducerContext> context,
                  GenTL::DEV_HANDLE device,
                  GenTL::PORT_HANDLE remotePort,
                  std::string deviceId) noexcept;
    void reset() noexcept;

    std::shared_ptr<ProducerContext> context_;
    GenTL::DEV_HANDLE device_ = nullptr;
    GenTL::PORT_HANDLE remotePort_ = nullptr;
    std::string deviceId_;
};

class Producer final {
public:
    static Producer open(
        const std::shared_ptr<ConsumerBridge>& bridge,
        const std::filesystem::path& ctiPath);

    Producer() noexcept = default;

    std::vector<DeviceDescriptor> discover(
        std::uint64_t timeoutMilliseconds = 2000) const;

    DeviceSession openDevice(
        const DeviceDescriptor& device,
        GenTL::DEVICE_ACCESS_FLAGS access =
            GenTL::DEVICE_ACCESS_CONTROL) const;

    const std::string& ctiPath() const noexcept;

    explicit operator bool() const noexcept;

private:
    explicit Producer(std::shared_ptr<ProducerContext> context) noexcept;
    std::shared_ptr<ProducerContext> context_;
};

} // namespace camera::gentl
