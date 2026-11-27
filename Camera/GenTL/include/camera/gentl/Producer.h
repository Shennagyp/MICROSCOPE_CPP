#pragma once

#include "camera/AcquisitionIntegrityTracker.h"
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

struct DataStreamPart {
    std::size_t offsetBytes = 0;
    std::size_t byteSize = 0;
    std::optional<std::uint64_t> dataType;
    std::optional<std::uint64_t> dataFormat;
    std::optional<std::uint64_t> dataFormatNamespace;
    std::optional<std::size_t> width;
    std::optional<std::size_t> height;
    std::optional<std::size_t> xOffset;
    std::optional<std::size_t> yOffset;
    std::optional<std::size_t> xPadding;
    std::optional<std::size_t> deliveredImageHeight;
    std::optional<std::uint64_t> sourceId;
    std::optional<std::uint64_t> regionId;
    std::optional<std::uint64_t> dataPurposeId;
};

struct DataStreamFrame {
    std::vector<std::uint8_t> payload;
    std::vector<DataStreamPart> parts;
    std::optional<std::uint64_t> frameId;
    std::optional<std::uint64_t> timestampNanoseconds;
    std::optional<std::uint64_t> pixelFormat;
    std::optional<std::uint64_t> pixelFormatNamespace;
    std::optional<GenTL::PIXELENDIANNESS_ID> pixelEndianness;
    std::optional<std::size_t> width;
    std::optional<std::size_t> height;
    std::optional<std::size_t> deliveredImageHeight;
    std::optional<std::size_t> imageOffset;
    std::optional<std::size_t> xPadding;
    std::optional<std::size_t> yPadding;
    std::optional<std::size_t> payloadType;
    std::optional<bool> imagePresent;
    std::optional<bool> containsChunkData;
    std::optional<bool> dataLargerThanBuffer;
    std::optional<bool> isComposite;
    bool incomplete = false;
};

struct DataStreamStatistics {
    camera::AcquisitionIntegritySnapshot integrity;
    std::optional<std::string> transportType;
    std::optional<std::uint64_t> producerDeliveredBuffers;
    std::optional<std::uint64_t> producerBufferUnderruns;
    std::optional<std::uint64_t> producerFramesStarted;
    std::optional<std::size_t> announcedBuffers;
    std::optional<std::size_t> queuedBuffers;
    std::optional<std::size_t> buffersAwaitingDelivery;
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

    // Combines protocol-neutral continuity accounting with optional native
    // GenTL stream counters. Missing optional counters remain absent.
    DataStreamStatistics statistics();

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
    // Opens a CTI through the in-process C++ GenTL Consumer. No separately
    // deployed Consumer bridge or Swift runtime is required.
    static Producer open(const std::filesystem::path& ctiPath);

    // Explicit bridge injection remains available for alternative consumers
    // and boundary tests.
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
    static Producer openWithConsumer(
        Consumer consumer, std::string canonicalPath);
    std::shared_ptr<ProducerContext> context_;
};

} // namespace camera::gentl
