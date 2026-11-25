#pragma once

#include "camera/gentl/ConsumerBridge.h"
#include "camera/gentl/GenTL_v1_6.h"

#include <cstddef>
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

struct DataStreamFrame {
    std::vector<std::uint8_t> payload;
    std::optional<std::uint64_t> frameId;
    std::optional<std::uint64_t> timestampNanoseconds;
    std::optional<std::uint64_t> pixelFormat;
    std::optional<std::uint64_t> pixelFormatNamespace;
    std::optional<std::size_t> width;
    std::optional<std::size_t> height;
    std::optional<std::size_t> imageOffset;
    std::optional<std::size_t> xPadding;
    std::optional<std::size_t> yPadding;
    std::optional<std::uint64_t> payloadType;
    std::optional<bool> imagePresent;
    bool incomplete = false;
};

class ProducerContext;
class DeviceSessionState;
class DataStreamSession;

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
    DataStreamSession openDataStream(
        const std::string& streamId,
        std::size_t bufferCount = 4) const;

    std::vector<std::uint8_t> readPort(
        std::uint64_t address, std::size_t size) const;
    void writePort(
        std::uint64_t address, const std::vector<std::uint8_t>& bytes) const;
    std::string portUrl() const;
    std::vector<std::uint8_t> genApiXml() const;
    void readPortInto(std::uint64_t address, void* destination, std::size_t size) const;
    void writePortFrom(std::uint64_t address, const void* source, std::size_t size) const;
    GenTL::DEVICE_ACCESS_FLAGS accessFlags() const noexcept;

    // Closing the DeviceSession releases this owner's reference. Open streams
    // retain the device handle until their own teardown has completed.
    void close();

private:
    friend class Producer;
    friend class DataStreamSession;
    explicit DeviceSession(std::shared_ptr<DeviceSessionState> state) noexcept;

    std::shared_ptr<DeviceSessionState> state_;
};

class DataStreamSession final {
public:
    DataStreamSession() noexcept = default;
    ~DataStreamSession();

    DataStreamSession(const DataStreamSession&) = delete;
    DataStreamSession& operator=(const DataStreamSession&) = delete;
    DataStreamSession(DataStreamSession&& other) noexcept;
    DataStreamSession& operator=(DataStreamSession&& other) noexcept;

    explicit operator bool() const noexcept;
    void start();

    // Waits up to timeoutMilliseconds for one completed buffer. A timeout
    // returns nullopt. Returned payload bytes are copied before the GenTL
    // buffer is requeued, so the result has independent lifetime.
    std::optional<DataStreamFrame> nextFrame(
        std::uint64_t timeoutMilliseconds = 1000);

    void stop();

private:
    friend class DeviceSession;
    struct Impl;
    DataStreamSession(std::shared_ptr<DeviceSessionState> device,
                      const std::string& streamId,
                      std::size_t bufferCount);
    void reset() noexcept;

    std::unique_ptr<Impl> impl_;
};

class Producer final {
public:
    static Producer open(
        const std::shared_ptr<ConsumerBridge>& bridge,
        const std::filesystem::path& ctiPath);

    Producer() noexcept = default;

    // GenTL list-update timeouts are specified in nanoseconds by the ABI;
    // this public boundary accepts milliseconds and converts them.
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
