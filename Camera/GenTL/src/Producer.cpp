#include "camera/gentl/Producer.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <utility>

namespace camera::gentl {
namespace {

constexpr std::size_t maximumStringBytes = 1024 * 1024;
constexpr std::uint32_t maximumInterfaces = 4096;
constexpr std::uint32_t maximumDevicesPerInterface = 65536;
constexpr std::uint32_t maximumDataStreams = 4096;

void check(GenTL::GC_ERROR status, const char* operation) {
    if (status != GenTL::GC_ERR_SUCCESS) {
        throw std::runtime_error(
            std::string(operation) + " failed with GenTL GC_ERROR " +
            std::to_string(status));
    }
}

template <typename ReadOperation>
std::string readString(ReadOperation&& operation, const char* operationName) {
    std::size_t required = 0;
    auto status = operation(nullptr, &required);
    if (status != GenTL::GC_ERR_BUFFER_TOO_SMALL &&
        status != GenTL::GC_ERR_SUCCESS) {
        check(status, operationName);
    }
    if (required == 0 || required > maximumStringBytes) {
        throw std::runtime_error(
            std::string(operationName) + " returned an invalid string size.");
    }

    std::vector<char> bytes(required, '\0');
    std::size_t capacity = bytes.size();
    status = operation(bytes.data(), &capacity);
    check(status, operationName);
    if (capacity == 0 || capacity > bytes.size()) {
        throw std::runtime_error(
            std::string(operationName) + " returned an invalid string length.");
    }
    bytes.back() = '\0';
    const auto terminator = std::find(bytes.begin(), bytes.end(), '\0');
    if (terminator == bytes.end() || terminator == bytes.begin()) {
        throw std::runtime_error(
            std::string(operationName) + " returned an empty or unterminated string.");
    }
    return std::string(bytes.begin(), terminator);
}

template <typename ReadOperation>
std::optional<std::string> readOptionalInfoString(ReadOperation&& operation) {
    GenTL::INFO_DATATYPE type = GenTL::INFO_DATATYPE_UNKNOWN;
    std::size_t required = 0;
    auto status = operation(&type, nullptr, &required);
    if (status == GenTL::GC_ERR_NOT_IMPLEMENTED ||
        status == GenTL::GC_ERR_NOT_AVAILABLE ||
        status == GenTL::GC_ERR_INVALID_PARAMETER) {
        return std::nullopt;
    }
    if (status != GenTL::GC_ERR_BUFFER_TOO_SMALL &&
        status != GenTL::GC_ERR_SUCCESS) {
        check(status, "GenTL info query");
    }
    if (type != GenTL::INFO_DATATYPE_STRING ||
        required == 0 || required > maximumStringBytes) {
        return std::nullopt;
    }

    std::vector<char> bytes(required, '\0');
    std::size_t capacity = bytes.size();
    status = operation(&type, bytes.data(), &capacity);
    if (status == GenTL::GC_ERR_NOT_IMPLEMENTED ||
        status == GenTL::GC_ERR_NOT_AVAILABLE ||
        status == GenTL::GC_ERR_INVALID_PARAMETER) {
        return std::nullopt;
    }
    check(status, "GenTL info read");
    if (type != GenTL::INFO_DATATYPE_STRING ||
        capacity == 0 || capacity > bytes.size()) {
        return std::nullopt;
    }
    bytes.back() = '\0';
    const auto terminator = std::find(bytes.begin(), bytes.end(), '\0');
    if (terminator == bytes.end()) return std::nullopt;
    return std::string(bytes.begin(), terminator);
}

} // namespace

class ProducerContext final {
public:
    struct InterfaceState {
        GenTL::IF_HANDLE handle = nullptr;
        std::string id;
    };

    ProducerContext(Consumer consumer, std::string path)
        : consumer(std::move(consumer)), path(std::move(path)) {}

    ~ProducerContext() {
        shutdown();
    }

    void shutdown() noexcept {
        std::lock_guard<std::mutex> guard(mutex);
        if (quarantined) return;

        for (auto it = interfaces.rbegin(); it != interfaces.rend(); ++it) {
            const auto status = consumer.invoke(Operation::ifClose, it->second.handle);
            if (status != GenTL::GC_ERR_SUCCESS) {
                quarantined = true;
                return;
            }
        }
        interfaces.clear();

        if (system) {
            const auto status = consumer.invoke(Operation::tlClose, system);
            if (status != GenTL::GC_ERR_SUCCESS) {
                quarantined = true;
                return;
            }
            system = nullptr;
        }

        if (initialized) {
            const auto status = consumer.invoke(Operation::gcCloseLib);
            if (status != GenTL::GC_ERR_SUCCESS) {
                quarantined = true;
                return;
            }
            initialized = false;
        }
    }

    Consumer consumer;
    std::string path;
    GenTL::TL_HANDLE system = nullptr;
    bool initialized = false;
    bool quarantined = false;
    mutable std::mutex mutex;
    std::map<std::string, InterfaceState> interfaces;
};

Producer::Producer(std::shared_ptr<ProducerContext> context) noexcept
    : context_(std::move(context)) {}

Producer Producer::open(
    const std::shared_ptr<ConsumerBridge>& bridge,
    const std::filesystem::path& ctiPath) {
    if (!bridge) {
        throw std::invalid_argument("A loaded GenTL Consumer bridge is required.");
    }
    const auto canonicalPath = std::filesystem::canonical(ctiPath).string();
    auto consumer = bridge->createProducer(canonicalPath);
    auto context = std::make_shared<ProducerContext>(
        std::move(consumer), canonicalPath);

    check(context->consumer.invoke(Operation::gcInitLib), "GCInitLib");
    context->initialized = true;

    check(context->consumer.invoke(Operation::tlOpen, &context->system), "TLOpen");
    if (!context->system) {
        throw std::runtime_error("TLOpen succeeded without returning a TL_HANDLE.");
    }
    return Producer(std::move(context));
}

std::vector<DeviceDescriptor> Producer::discover(
    std::uint64_t timeoutMilliseconds) const {
    if (!context_) {
        throw std::logic_error("The GenTL Producer is not open.");
    }

    std::lock_guard<std::mutex> guard(context_->mutex);
    if (context_->quarantined || !context_->system) {
        throw std::runtime_error("The GenTL Producer context is unavailable.");
    }

    bool8_t changed = false;
    check(context_->consumer.invoke(
        Operation::tlUpdateInterfaceList,
        context_->system, &changed, timeoutMilliseconds),
        "TLUpdateInterfaceList");

    std::uint32_t interfaceCount = 0;
    check(context_->consumer.invoke(
        Operation::tlGetNumInterfaces,
        context_->system, &interfaceCount),
        "TLGetNumInterfaces");
    if (interfaceCount > maximumInterfaces) {
        throw std::runtime_error("The Producer reported an unreasonable interface count.");
    }

    std::vector<DeviceDescriptor> devices;
    std::set<std::string> seenInterfaces;

    for (std::uint32_t index = 0; index < interfaceCount; ++index) {
        const auto interfaceId = readString(
            [&](char* buffer, std::size_t* size) {
                return context_->consumer.invoke(
                    Operation::tlGetInterfaceID,
                    context_->system, index, buffer, size);
            },
            "TLGetInterfaceID");

        if (!seenInterfaces.insert(interfaceId).second) {
            throw std::runtime_error("The Producer returned a duplicate interface ID.");
        }

        auto interface = context_->interfaces.find(interfaceId);
        if (interface == context_->interfaces.end()) {
            GenTL::IF_HANDLE handle = nullptr;
            check(context_->consumer.invoke(
                Operation::tlOpenInterface,
                context_->system, interfaceId.c_str(), &handle),
                "TLOpenInterface");
            if (!handle) {
                throw std::runtime_error(
                    "TLOpenInterface succeeded without returning an IF_HANDLE.");
            }
            interface = context_->interfaces.emplace(
                interfaceId,
                ProducerContext::InterfaceState{handle, interfaceId}).first;
        }

        const auto interfaceHandle = interface->second.handle;
        const auto interfaceDisplayName = readOptionalInfoString(
            [&](GenTL::INFO_DATATYPE* type, void* buffer, std::size_t* size) {
                return context_->consumer.invoke(
                    Operation::ifGetInfo,
                    interfaceHandle,
                    GenTL::INTERFACE_INFO_DISPLAYNAME,
                    type, buffer, size);
            }).value_or(std::string{});

        changed = false;
        check(context_->consumer.invoke(
            Operation::ifUpdateDeviceList,
            interfaceHandle, &changed, timeoutMilliseconds),
            "IFUpdateDeviceList");

        std::uint32_t deviceCount = 0;
        check(context_->consumer.invoke(
            Operation::ifGetNumDevices,
            interfaceHandle, &deviceCount),
            "IFGetNumDevices");
        if (deviceCount > maximumDevicesPerInterface) {
            throw std::runtime_error("The Producer reported an unreasonable device count.");
        }

        std::set<std::string> seenDevices;
        for (std::uint32_t deviceIndex = 0; deviceIndex < deviceCount; ++deviceIndex) {
            const auto deviceId = readString(
                [&](char* buffer, std::size_t* size) {
                    return context_->consumer.invoke(
                        Operation::ifGetDeviceID,
                        interfaceHandle, deviceIndex, buffer, size);
                },
                "IFGetDeviceID");
            if (!seenDevices.insert(deviceId).second) {
                throw std::runtime_error("The Producer returned a duplicate device ID.");
            }

            DeviceDescriptor descriptor;
            descriptor.producerPath = context_->path;
            descriptor.interfaceId = interfaceId;
            descriptor.interfaceDisplayName = interfaceDisplayName;
            descriptor.deviceId = deviceId;
            descriptor.displayName = readOptionalInfoString(
                [&](GenTL::INFO_DATATYPE* type, void* buffer, std::size_t* size) {
                    return context_->consumer.invoke(
                        Operation::ifGetDeviceInfo,
                        interfaceHandle, deviceId.c_str(),
                        GenTL::DEVICE_INFO_DISPLAYNAME,
                        type, buffer, size);
                }).value_or(std::string{});
            descriptor.vendor = readOptionalInfoString(
                [&](GenTL::INFO_DATATYPE* type, void* buffer, std::size_t* size) {
                    return context_->consumer.invoke(
                        Operation::ifGetDeviceInfo,
                        interfaceHandle, deviceId.c_str(),
                        GenTL::DEVICE_INFO_VENDOR,
                        type, buffer, size);
                }).value_or(std::string{});
            descriptor.model = readOptionalInfoString(
                [&](GenTL::INFO_DATATYPE* type, void* buffer, std::size_t* size) {
                    return context_->consumer.invoke(
                        Operation::ifGetDeviceInfo,
                        interfaceHandle, deviceId.c_str(),
                        GenTL::DEVICE_INFO_MODEL,
                        type, buffer, size);
                }).value_or(std::string{});
            descriptor.serialNumber = readOptionalInfoString(
                [&](GenTL::INFO_DATATYPE* type, void* buffer, std::size_t* size) {
                    return context_->consumer.invoke(
                        Operation::ifGetDeviceInfo,
                        interfaceHandle, deviceId.c_str(),
                        GenTL::DEVICE_INFO_SERIAL_NUMBER,
                        type, buffer, size);
                }).value_or(std::string{});
            devices.push_back(std::move(descriptor));
        }
    }
    return devices;
}

DeviceSession Producer::openDevice(
    const DeviceDescriptor& descriptor,
    GenTL::DEVICE_ACCESS_FLAGS access) const {
    if (!context_) {
        throw std::logic_error("The GenTL Producer is not open.");
    }
    if (descriptor.producerPath != context_->path) {
        throw std::invalid_argument(
            "The device descriptor belongs to a different GenTL Producer.");
    }

    std::lock_guard<std::mutex> guard(context_->mutex);
    if (context_->quarantined || !context_->system) {
        throw std::runtime_error("The GenTL Producer context is unavailable.");
    }
    const auto interface = context_->interfaces.find(descriptor.interfaceId);
    if (interface == context_->interfaces.end()) {
        throw std::invalid_argument(
            "The device descriptor references an interface that is no longer retained.");
    }

    GenTL::DEV_HANDLE device = nullptr;
    check(context_->consumer.invoke(
        Operation::ifOpenDevice,
        interface->second.handle, descriptor.deviceId.c_str(),
        access, &device),
        "IFOpenDevice");
    if (!device) {
        throw std::runtime_error(
            "IFOpenDevice succeeded without returning a DEV_HANDLE.");
    }

    GenTL::PORT_HANDLE remotePort = nullptr;
    const auto portStatus = context_->consumer.invoke(
        Operation::devGetPort, device, &remotePort);
    if (portStatus != GenTL::GC_ERR_SUCCESS || !remotePort) {
        const auto closeStatus =
            context_->consumer.invoke(Operation::devClose, device);
        if (closeStatus != GenTL::GC_ERR_SUCCESS) {
            context_->quarantined = true;
        }
        check(portStatus, "DevGetPort");
        throw std::runtime_error("DevGetPort succeeded without returning a PORT_HANDLE.");
    }

    return DeviceSession(
        context_, device, remotePort, descriptor.deviceId);
}

const std::string& Producer::ctiPath() const noexcept {
    static const std::string empty;
    return context_ ? context_->path : empty;
}

Producer::operator bool() const noexcept {
    return context_ != nullptr;
}

DeviceSession::DeviceSession(
    std::shared_ptr<ProducerContext> context,
    GenTL::DEV_HANDLE device,
    GenTL::PORT_HANDLE remotePort,
    std::string deviceId) noexcept
    : context_(std::move(context)),
      device_(device),
      remotePort_(remotePort),
      deviceId_(std::move(deviceId)) {}

DeviceSession::~DeviceSession() {
    reset();
}

DeviceSession::DeviceSession(DeviceSession&& other) noexcept
    : context_(std::move(other.context_)),
      device_(std::exchange(other.device_, nullptr)),
      remotePort_(std::exchange(other.remotePort_, nullptr)),
      deviceId_(std::move(other.deviceId_)) {}

DeviceSession& DeviceSession::operator=(DeviceSession&& other) noexcept {
    if (this != &other) {
        reset();
        context_ = std::move(other.context_);
        device_ = std::exchange(other.device_, nullptr);
        remotePort_ = std::exchange(other.remotePort_, nullptr);
        deviceId_ = std::move(other.deviceId_);
    }
    return *this;
}

DeviceSession::operator bool() const noexcept {
    return context_ != nullptr && device_ != nullptr;
}

const std::string& DeviceSession::deviceId() const noexcept {
    return deviceId_;
}

std::vector<std::string> DeviceSession::dataStreamIds() const {
    if (!*this) throw std::logic_error("The GenTL device session is closed.");
    std::lock_guard<std::mutex> guard(context_->mutex);
    if (context_->quarantined) throw std::runtime_error("The GenTL Producer context is quarantined.");
    std::uint32_t count = 0;
    check(context_->consumer.invoke(
        Operation::devGetNumDataStreams, device_, &count),
        "DevGetNumDataStreams");
    if (count > maximumDataStreams) {
        throw std::runtime_error("The Producer reported an unreasonable data-stream count.");
    }

    std::vector<std::string> result;
    result.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        result.push_back(readString(
            [&](char* buffer, std::size_t* size) {
                return context_->consumer.invoke(
                    Operation::devGetDataStreamID,
                    device_, index, buffer, size);
            },
            "DevGetDataStreamID"));
    }
    return result;
}

std::vector<std::uint8_t> DeviceSession::readPort(
    std::uint64_t address, std::size_t size) const {
    if (!*this) throw std::logic_error("The GenTL device session is closed.");
    std::lock_guard<std::mutex> guard(context_->mutex);
    if (context_->quarantined) throw std::runtime_error("The GenTL Producer context is quarantined.");
    if (size == 0) return {};
    std::vector<std::uint8_t> bytes(size);
    std::size_t actual = bytes.size();
    check(context_->consumer.invoke(
        Operation::gcReadPort,
        remotePort_, address, bytes.data(), &actual),
        "GCReadPort");
    if (actual > bytes.size()) {
        throw std::runtime_error("GCReadPort returned a size larger than the destination.");
    }
    bytes.resize(actual);
    return bytes;
}

void DeviceSession::writePort(
    std::uint64_t address, const std::vector<std::uint8_t>& bytes) const {
    if (!*this) throw std::logic_error("The GenTL device session is closed.");
    std::lock_guard<std::mutex> guard(context_->mutex);
    if (context_->quarantined) throw std::runtime_error("The GenTL Producer context is quarantined.");
    if (bytes.empty()) return;
    std::size_t actual = bytes.size();
    const void* data = bytes.empty() ? nullptr : bytes.data();
    check(context_->consumer.invoke(
        Operation::gcWritePort,
        remotePort_, address, data, &actual),
        "GCWritePort");
    if (actual != bytes.size()) {
        throw std::runtime_error("GCWritePort reported an unexpected byte count.");
    }
}

std::string DeviceSession::portUrl() const {
    if (!*this) throw std::logic_error("The GenTL device session is closed.");
    std::lock_guard<std::mutex> guard(context_->mutex);
    if (context_->quarantined) throw std::runtime_error("The GenTL Producer context is quarantined.");
    return readString(
        [&](char* buffer, std::size_t* size) {
            return context_->consumer.invoke(
                Operation::gcGetPortURL, remotePort_, buffer, size);
        },
        "GCGetPortURL");
}

void DeviceSession::close() {
    if (!device_) {
        context_.reset();
        remotePort_ = nullptr;
        return;
    }
    auto context = context_;
    if (!context) {
        device_ = nullptr;
        remotePort_ = nullptr;
        return;
    }

    GenTL::GC_ERROR status = GenTL::GC_ERR_SUCCESS;
    {
        std::lock_guard<std::mutex> guard(context->mutex);
        if (context->quarantined) {
            device_ = nullptr;
            remotePort_ = nullptr;
            context_.reset();
            return;
        }
        status = context->consumer.invoke(Operation::devClose, device_);
        if (status != GenTL::GC_ERR_SUCCESS) {
            context->quarantined = true;
        }
        device_ = nullptr;
        remotePort_ = nullptr;
    }
    if (status != GenTL::GC_ERR_SUCCESS) {
        check(status, "DevClose");
    }
    context_.reset();
}

void DeviceSession::reset() noexcept {
    try {
        close();
    } catch (...) {
        if (context_) {
            std::lock_guard<std::mutex> guard(context_->mutex);
            context_->quarantined = true;
        }
        device_ = nullptr;
        remotePort_ = nullptr;
        context_.reset();
    }
}

} // namespace camera::gentl
