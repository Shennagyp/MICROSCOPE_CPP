#include "camera/gentl/Producer.h"

#include <algorithm>
#include <cstring>
#include <condition_variable>
#include <fstream>
#include <limits>
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

template <typename Invoke>
std::string readPortUrlString(Invoke&& invoke, GenTL::PORT_HANDLE port,
                              std::uint32_t index, GenTL::URL_INFO_CMD command) {
    GenTL::INFO_DATATYPE type = GenTL::INFO_DATATYPE_UNKNOWN;
    std::size_t required = 0;
    auto status = invoke(Operation::gcGetPortURLInfo,
                         port, index, command, &type, nullptr, &required);
    if (status != GenTL::GC_ERR_BUFFER_TOO_SMALL &&
        status != GenTL::GC_ERR_SUCCESS) check(status, "GCGetPortURLInfo(StringSize)");
    if (type != GenTL::INFO_DATATYPE_STRING ||
        required == 0 || required > maximumStringBytes)
        throw std::runtime_error("GCGetPortURLInfo returned invalid string metadata.");

    std::vector<char> bytes(required, '\0');
    std::size_t capacity = bytes.size();
    status = invoke(Operation::gcGetPortURLInfo,
                    port, index, command, &type, bytes.data(), &capacity);
    check(status, "GCGetPortURLInfo(String)");
    if (type != GenTL::INFO_DATATYPE_STRING ||
        capacity == 0 || capacity > bytes.size())
        throw std::runtime_error("GCGetPortURLInfo returned an invalid string length.");
    bytes.back() = '\0';
    const auto terminator = std::find(bytes.begin(), bytes.end(), '\0');
    if (terminator == bytes.end())
        throw std::runtime_error("GCGetPortURLInfo returned an unterminated string.");
    return std::string(bytes.begin(), terminator);
}

template <typename T, typename Invoke>
T readPortUrlValue(Invoke&& invoke, GenTL::PORT_HANDLE port,
                   std::uint32_t index, GenTL::URL_INFO_CMD command,
                   GenTL::INFO_DATATYPE expectedType) {
    GenTL::INFO_DATATYPE actualType = GenTL::INFO_DATATYPE_UNKNOWN;
    T value{};
    std::size_t size = sizeof(T);
    const auto status = invoke(Operation::gcGetPortURLInfo,
                               port, index, command, &actualType, &value, &size);
    check(status, "GCGetPortURLInfo(Value)");
    if (actualType != expectedType || size != sizeof(T))
        throw std::runtime_error("GCGetPortURLInfo returned a mismatched type or size.");
    return value;
}

constexpr std::uint64_t maximumGenApiXmlBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::size_t genApiXmlReadChunkBytes = 1024 * 1024;


std::uint64_t timeoutMillisecondsToNanoseconds(std::uint64_t milliseconds) {
    if (milliseconds == std::numeric_limits<std::uint64_t>::max()) {
        return GENTL_INFINITE;
    }
    constexpr std::uint64_t nanosecondsPerMillisecond = 1000000ULL;
    if (milliseconds >
        (GENTL_INFINITE - 1) / nanosecondsPerMillisecond) {
        throw std::overflow_error("GenTL timeout exceeds the representable nanosecond range.");
    }
    return milliseconds * nanosecondsPerMillisecond;
}

template <typename T, typename ReadOperation>
std::optional<T> readTypedInfo(
    ReadOperation&& operation,
    GenTL::INFO_DATATYPE expectedType,
    const char* operationName) {
    GenTL::INFO_DATATYPE actualType = GenTL::INFO_DATATYPE_UNKNOWN;
    T value{};
    std::size_t size = sizeof(T);
    const auto status = operation(&actualType, &value, &size);
    if (status == GenTL::GC_ERR_NOT_AVAILABLE ||
        status == GenTL::GC_ERR_NOT_IMPLEMENTED ||
        status == GenTL::GC_ERR_INVALID_PARAMETER) {
        return std::nullopt;
    }
    check(status, operationName);
    if (actualType != expectedType || size != sizeof(T)) {
        throw std::runtime_error(
            std::string(operationName) + " returned a mismatched type or size.");
    }
    return value;
}

} // namespace

class ProducerContext final : public std::enable_shared_from_this<ProducerContext> {
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

    // Called with mutex held. A quarantined context is retained for process
    // lifetime so its Consumer and dynamically loaded C ABI bridge cannot be
    // unloaded while a CTI still owns native handles or callbacks.
    void quarantineLocked() noexcept;

    // Destructor-time cleanup failures cannot retain this object through
    // shared_from_this(); retain the Consumer itself, which owns the C ABI
    // bridge and its GenTL implementation context.
    void retainConsumerOnFailedShutdown() noexcept;

    void shutdown() noexcept {
        std::lock_guard<std::mutex> guard(mutex);
        if (quarantined) return;

        for (auto it = interfaces.rbegin(); it != interfaces.rend(); ++it) {
            const auto status = consumer.invoke(Operation::ifClose, it->handle);
            if (status != GenTL::GC_ERR_SUCCESS) {
                quarantined = true;
                retainConsumerOnFailedShutdown();
                return;
            }
        }
        interfaces.clear();

        if (system) {
            const auto status = consumer.invoke(Operation::tlClose, system);
            if (status != GenTL::GC_ERR_SUCCESS) {
                quarantined = true;
                retainConsumerOnFailedShutdown();
                return;
            }
            system = nullptr;
        }

        if (initialized) {
            const auto status = consumer.invoke(Operation::gcCloseLib);
            if (status != GenTL::GC_ERR_SUCCESS) {
                quarantined = true;
                retainConsumerOnFailedShutdown();
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
    std::vector<InterfaceState> interfaces;
};

void ProducerContext::quarantineLocked() noexcept {
    if (quarantined) return;
    quarantined = true;
    try {
        static auto* registryMutex = new std::mutex;
        static auto* registry =
            new std::vector<std::shared_ptr<ProducerContext>>;
        std::lock_guard<std::mutex> guard(*registryMutex);
        registry->push_back(shared_from_this());
    } catch (...) {
        retainConsumerOnFailedShutdown();
    }
}

void ProducerContext::retainConsumerOnFailedShutdown() noexcept {
    try {
        static auto* registryMutex = new std::mutex;
        static auto* registry = new std::vector<Consumer>;
        std::lock_guard<std::mutex> guard(*registryMutex);
        registry->push_back(std::move(consumer));
    } catch (...) {
        // Preserve the object in place if allocation itself is unavailable.
        // Its destructor may still run, but this is the last-resort OOM path.
    }
}


class DeviceSessionState final {
public:
    DeviceSessionState(
        std::shared_ptr<ProducerContext> context,
        GenTL::DEV_HANDLE device,
        GenTL::PORT_HANDLE remotePort,
        GenTL::DEVICE_ACCESS_FLAGS access,
        std::string deviceId) noexcept
        : context(std::move(context)),
          device(device),
          remotePort(remotePort),
          access(access),
          deviceId(std::move(deviceId)) {}

    ~DeviceSessionState() {
        close();
    }

    void close() noexcept {
        if (!device) {
            context.reset();
            remotePort = nullptr;
            return;
        }
        auto owner = context;
        if (owner) {
            std::lock_guard<std::mutex> guard(owner->mutex);
            if (!owner->quarantined) {
                const auto status =
                    owner->consumer.invoke(Operation::devClose, device);
                if (status != GenTL::GC_ERR_SUCCESS) {
                    owner->quarantineLocked();
                }
            }
        }
        device = nullptr;
        remotePort = nullptr;
        context.reset();
    }

    std::shared_ptr<ProducerContext> context;
    GenTL::DEV_HANDLE device = nullptr;
    GenTL::PORT_HANDLE remotePort = nullptr;
    GenTL::DEVICE_ACCESS_FLAGS access = GenTL::DEVICE_ACCESS_UNKNOWN;
    std::string deviceId;
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
        context_->system, &changed,
        timeoutMillisecondsToNanoseconds(timeoutMilliseconds)),
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

        auto interface = std::find_if(
            context_->interfaces.begin(), context_->interfaces.end(),
            [&](const auto& retained) { return retained.id == interfaceId; });
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
            context_->interfaces.push_back(
                ProducerContext::InterfaceState{handle, interfaceId});
            interface = std::prev(context_->interfaces.end());
        }

        const auto interfaceHandle = interface->handle;
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
            interfaceHandle, &changed,
            timeoutMillisecondsToNanoseconds(timeoutMilliseconds)),
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
    const auto interface = std::find_if(
        context_->interfaces.begin(), context_->interfaces.end(),
        [&](const auto& retained) { return retained.id == descriptor.interfaceId; });
    if (interface == context_->interfaces.end()) {
        throw std::invalid_argument(
            "The device descriptor references an interface that is no longer retained.");
    }

    GenTL::DEV_HANDLE device = nullptr;
    check(context_->consumer.invoke(
        Operation::ifOpenDevice,
        interface->handle, descriptor.deviceId.c_str(),
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
            context_->quarantineLocked();
        }
        check(portStatus, "DevGetPort");
        throw std::runtime_error("DevGetPort succeeded without returning a PORT_HANDLE.");
    }

    return DeviceSession(std::make_shared<DeviceSessionState>(
        context_, device, remotePort, access, descriptor.deviceId));
}

const std::string& Producer::ctiPath() const noexcept {
    static const std::string empty;
    return context_ ? context_->path : empty;
}

Producer::operator bool() const noexcept {
    return context_ != nullptr;
}


DeviceSession::DeviceSession(std::shared_ptr<DeviceSessionState> state) noexcept
    : state_(std::move(state)) {}

DeviceSession::~DeviceSession() {
    close();
}

DeviceSession::DeviceSession(DeviceSession&& other) noexcept
    : state_(std::move(other.state_)) {}

DeviceSession& DeviceSession::operator=(DeviceSession&& other) noexcept {
    if (this != &other) {
        close();
        state_ = std::move(other.state_);
    }
    return *this;
}

DeviceSession::operator bool() const noexcept {
    return state_ != nullptr && state_->device != nullptr;
}

const std::string& DeviceSession::deviceId() const noexcept {
    static const std::string empty;
    return state_ ? state_->deviceId : empty;
}

std::vector<std::string> DeviceSession::dataStreamIds() const {
    if (!*this) throw std::logic_error("The GenTL device session is closed.");
    const auto context = state_->context;
    std::lock_guard<std::mutex> guard(context->mutex);
    if (context->quarantined)
        throw std::runtime_error("The GenTL Producer context is quarantined.");

    std::uint32_t count = 0;
    check(context->consumer.invoke(
        Operation::devGetNumDataStreams, state_->device, &count),
        "DevGetNumDataStreams");
    if (count > maximumDataStreams) {
        throw std::runtime_error("The Producer reported an unreasonable data-stream count.");
    }

    std::vector<std::string> result;
    result.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        result.push_back(readString(
            [&](char* buffer, std::size_t* size) {
                return context->consumer.invoke(
                    Operation::devGetDataStreamID,
                    state_->device, index, buffer, size);
            },
            "DevGetDataStreamID"));
    }
    return result;
}

std::vector<std::uint8_t> DeviceSession::readPort(
    std::uint64_t address, std::size_t size) const {
    std::vector<std::uint8_t> bytes(size);
    if (size != 0) readPortInto(address, bytes.data(), size);
    return bytes;
}

void DeviceSession::writePort(
    std::uint64_t address, const std::vector<std::uint8_t>& bytes) const {
    if (!bytes.empty()) writePortFrom(address, bytes.data(), bytes.size());
}

void DeviceSession::readPortInto(
    std::uint64_t address, void* destination, std::size_t size) const {
    if (!*this) throw std::logic_error("The GenTL device session is closed.");
    if (size == 0) return;
    if (!destination)
        throw std::invalid_argument("A non-empty port read requires a destination.");

    const auto context = state_->context;
    std::lock_guard<std::mutex> guard(context->mutex);
    if (context->quarantined)
        throw std::runtime_error("The GenTL Producer context is quarantined.");
    std::size_t actual = size;
    check(context->consumer.invoke(Operation::gcReadPort,
        state_->remotePort, address, destination, &actual), "GCReadPort");
    if (actual != size)
        throw std::runtime_error("GCReadPort returned a partial read.");
}

void DeviceSession::writePortFrom(
    std::uint64_t address, const void* source, std::size_t size) const {
    if (!*this) throw std::logic_error("The GenTL device session is closed.");
    if (size == 0) return;
    if (!source)
        throw std::invalid_argument("A non-empty port write requires a source.");
    if (state_->access == GenTL::DEVICE_ACCESS_READONLY)
        throw std::runtime_error("The GenTL device was opened read-only.");

    const auto context = state_->context;
    std::lock_guard<std::mutex> guard(context->mutex);
    if (context->quarantined)
        throw std::runtime_error("The GenTL Producer context is quarantined.");
    std::size_t actual = size;
    check(context->consumer.invoke(Operation::gcWritePort,
        state_->remotePort, address, source, &actual), "GCWritePort");
    if (actual != size)
        throw std::runtime_error("GCWritePort reported a partial write.");
}

GenTL::DEVICE_ACCESS_FLAGS DeviceSession::accessFlags() const noexcept {
    return state_ ? state_->access : GenTL::DEVICE_ACCESS_UNKNOWN;
}

std::vector<std::uint8_t> DeviceSession::genApiXml() const {
    if (!*this) throw std::logic_error("The GenTL device session is closed.");
    const auto context = state_->context;
    const auto invoke = [context](Operation operation, auto... arguments) {
        std::lock_guard<std::mutex> guard(context->mutex);
        if (context->quarantined)
            throw std::runtime_error("The GenTL Producer context is quarantined.");
        return context->consumer.invoke(operation, arguments...);
    };

    std::uint32_t urlCount = 0;
    check(invoke(Operation::gcGetNumPortURLs, state_->remotePort, &urlCount),
          "GCGetNumPortURLs");
    if (urlCount > 64)
        throw std::runtime_error("The Producer returned an unreasonable GenApi URL count.");

    for (std::uint32_t index = 0; index < urlCount; ++index) {
        const auto scheme = readPortUrlValue<GenTL::URL_SCHEME_ID>(
            invoke, state_->remotePort, index, GenTL::URL_INFO_SCHEME,
            GenTL::INFO_DATATYPE_INT32);

        if (scheme == GenTL::URL_SCHEME_LOCAL) {
            const auto address = readPortUrlValue<std::uint64_t>(
                invoke, state_->remotePort, index, GenTL::URL_INFO_FILE_REGISTER_ADDRESS,
                GenTL::INFO_DATATYPE_UINT64);
            const auto fileSize = readPortUrlValue<std::uint64_t>(
                invoke, state_->remotePort, index, GenTL::URL_INFO_FILE_SIZE,
                GenTL::INFO_DATATYPE_UINT64);
            if (fileSize == 0 || fileSize > maximumGenApiXmlBytes ||
                fileSize > std::numeric_limits<std::size_t>::max())
                throw std::runtime_error("The GenApi XML register file has an invalid size.");

            std::vector<std::uint8_t> xml(static_cast<std::size_t>(fileSize));
            std::uint64_t offset = 0;
            while (offset < fileSize) {
                const auto chunk = static_cast<std::size_t>(
                    std::min<std::uint64_t>(fileSize - offset, genApiXmlReadChunkBytes));
                if (address > std::numeric_limits<std::uint64_t>::max() - offset)
                    throw std::runtime_error("The GenApi XML register address overflows.");
                readPortInto(address + offset,
                    xml.data() + static_cast<std::size_t>(offset), chunk);
                offset += chunk;
            }
            return xml;
        }

        if (scheme == GenTL::URL_SCHEME_FILE) {
            const auto filename = readPortUrlString(
                invoke, state_->remotePort, index, GenTL::URL_INFO_FILENAME);
            const auto filePath = std::filesystem::canonical(filename);
            const auto fileSize = std::filesystem::file_size(filePath);
            if (fileSize == 0 || fileSize > maximumGenApiXmlBytes ||
                fileSize > std::numeric_limits<std::size_t>::max())
                throw std::runtime_error("The GenApi XML file has an invalid size.");
            std::vector<std::uint8_t> xml(static_cast<std::size_t>(fileSize));
            std::ifstream file(filePath, std::ios::binary);
            if (!file || !file.read(reinterpret_cast<char*>(xml.data()),
                                    static_cast<std::streamsize>(xml.size())))
                throw std::runtime_error("The GenApi XML file could not be read.");
            return xml;
        }
    }
    throw std::runtime_error(
        "The Producer exposes no supported GenApi XML URL (LOCAL or FILE scheme).");
}

std::string DeviceSession::portUrl() const {
    if (!*this) throw std::logic_error("The GenTL device session is closed.");
    const auto context = state_->context;
    std::lock_guard<std::mutex> guard(context->mutex);
    if (context->quarantined)
        throw std::runtime_error("The GenTL Producer context is quarantined.");
    return readString(
        [&](char* buffer, std::size_t* size) {
            return context->consumer.invoke(
                Operation::gcGetPortURL, state_->remotePort, buffer, size);
        },
        "GCGetPortURL");
}

void DeviceSession::close() {
    state_.reset();
}

struct DataStreamSession::Impl {
    struct Buffer {
        GenTL::BUFFER_HANDLE handle = nullptr;
    };

    explicit Impl(std::shared_ptr<DeviceSessionState> owner)
        : device(std::move(owner)) {}

    ~Impl() {
        close();
    }

    void initialize(const std::string& streamId, std::size_t bufferCount) {
        if (!device || !device->device)
            throw std::logic_error("The GenTL device session is closed.");
        if (streamId.empty())
            throw std::invalid_argument("A GenTL data-stream ID is required.");
        if (bufferCount == 0 || bufferCount > 64)
            throw std::invalid_argument("GenTL buffer count must be between 1 and 64.");

        const auto context = device->context;
        std::lock_guard<std::mutex> guard(context->mutex);
        if (context->quarantined)
            throw std::runtime_error("The GenTL Producer context is quarantined.");

        check(context->consumer.invoke(
            Operation::devOpenDataStream, device->device,
            streamId.c_str(), &stream), "DevOpenDataStream");
        if (!stream)
            throw std::runtime_error(
                "DevOpenDataStream succeeded without returning a DS_HANDLE.");

        const auto payloadSize = readTypedInfo<std::size_t>(
            [&](GenTL::INFO_DATATYPE* type, void* value, std::size_t* size) {
                return context->consumer.invoke(
                    Operation::dsGetInfo, stream, GenTL::STREAM_INFO_PAYLOAD_SIZE,
                    type, value, size);
            },
            GenTL::INFO_DATATYPE_SIZET, "DSGetInfo(PAYLOAD_SIZE)");
        if (!payloadSize || *payloadSize == 0 ||
            *payloadSize > maximumGenApiXmlBytes * 8 ||
            *payloadSize > std::numeric_limits<std::size_t>::max()) {
            throw std::runtime_error("The GenTL stream reported an invalid payload size.");
        }
        payloadBytes = *payloadSize;

        const auto minimumCount = readTypedInfo<std::size_t>(
            [&](GenTL::INFO_DATATYPE* type, void* value, std::size_t* size) {
                return context->consumer.invoke(
                    Operation::dsGetInfo, stream, GenTL::STREAM_INFO_BUF_ANNOUNCE_MIN,
                    type, value, size);
            },
            GenTL::INFO_DATATYPE_SIZET, "DSGetInfo(BUF_ANNOUNCE_MIN)");
        if (minimumCount && bufferCount < *minimumCount) {
            throw std::invalid_argument(
                "The requested GenTL buffer count is below the Producer's announced minimum.");
        }

        check(context->consumer.invoke(
            Operation::gcRegisterEvent, stream, GenTL::EVENT_NEW_BUFFER, &event),
            "GCRegisterEvent(NEW_BUFFER)");
        eventRegistered = true;
        if (!event)
            throw std::runtime_error(
                "GCRegisterEvent succeeded without returning an EVENT_HANDLE.");

        buffers.reserve(bufferCount);
        for (std::size_t index = 0; index < bufferCount; ++index) {
            GenTL::BUFFER_HANDLE handle = nullptr;
            check(context->consumer.invoke(
                Operation::dsAllocAndAnnounceBuffer,
                stream, payloadBytes, nullptr, &handle),
                "DSAllocAndAnnounceBuffer");
            if (!handle) {
                context->quarantineLocked();
                throw std::runtime_error(
                    "DSAllocAndAnnounceBuffer succeeded without returning a BUFFER_HANDLE.");
            }
            buffers.push_back(Buffer{handle});
        }

        for (const auto& buffer : buffers) {
            check(context->consumer.invoke(
                Operation::dsQueueBuffer, stream, buffer.handle),
                "DSQueueBuffer");
        }
    }

    void close() noexcept {
        std::shared_ptr<DeviceSessionState> owner;
        GenTL::DS_HANDLE streamHandle = nullptr;
        GenTL::EVENT_HANDLE eventHandle = nullptr;
        std::size_t activeCallCount = 0;
        bool wasStarted = false;
        bool wasRegistered = false;

        {
            std::unique_lock<std::mutex> guard(mutex);
            if (closed) return;
            if (closing) {
                condition.wait(guard, [&] { return closed; });
                return;
            }
            closing = true;
            stopping = true;
            owner = device;
            streamHandle = stream;
            eventHandle = event;
            activeCallCount = activeCalls;
            wasStarted = started;
            wasRegistered = eventRegistered;
        }

        bool safeToRelease = true;
        auto context = owner ? owner->context : nullptr;
        if (context && streamHandle) {
            {
                std::lock_guard<std::mutex> guard(context->mutex);
                safeToRelease = !context->quarantined;
            }
            if (wasStarted) {
                std::lock_guard<std::mutex> guard(context->mutex);
                const auto status = context->consumer.invoke(
                    Operation::dsStopAcquisition,
                    streamHandle, GenTL::ACQ_STOP_FLAGS_DEFAULT);
                if (status != GenTL::GC_ERR_SUCCESS) {
                    context->quarantineLocked();
                    safeToRelease = false;
                }
            }

            if (wasRegistered) {
                std::lock_guard<std::mutex> guard(context->mutex);
                const auto status = context->consumer.invoke(
                    Operation::gcUnregisterEvent,
                    streamHandle, GenTL::EVENT_NEW_BUFFER);
                if (status != GenTL::GC_ERR_SUCCESS) {
                    context->quarantineLocked();
                    safeToRelease = false;

                    // Unregistration normally aborts all pending EventGetData
                    // calls. If the Producer rejects it, EventKill is the
                    // specified fallback for this stream's single waiter.
                    if (activeCallCount != 0 && eventHandle) {
                        (void)context->consumer.invoke(
                            Operation::eventKill, eventHandle);
                    }
                }
            }

            {
                std::unique_lock<std::mutex> guard(mutex);
                condition.wait(guard, [&] { return activeCalls == 0; });
            }

            if (safeToRelease) {
                std::lock_guard<std::mutex> guard(context->mutex);
                auto status = context->consumer.invoke(
                    Operation::dsFlushQueue,
                    streamHandle, GenTL::ACQ_QUEUE_ALL_DISCARD);
                if (status != GenTL::GC_ERR_SUCCESS) {
                    context->quarantineLocked();
                    safeToRelease = false;
                }

                if (safeToRelease) {
                    for (const auto& buffer : buffers) {
                        void* memory = nullptr;
                        void* privateData = nullptr;
                        status = context->consumer.invoke(
                            Operation::dsRevokeBuffer,
                            streamHandle, buffer.handle, &memory, &privateData);
                        if (status != GenTL::GC_ERR_SUCCESS) {
                            context->quarantineLocked();
                            safeToRelease = false;
                            break;
                        }
                    }
                }

                if (safeToRelease) {
                    status = context->consumer.invoke(
                        Operation::dsClose, streamHandle);
                    if (status != GenTL::GC_ERR_SUCCESS) {
                        context->quarantineLocked();
                        safeToRelease = false;
                    }
                }
            }
        }

        {
            std::lock_guard<std::mutex> guard(mutex);
            stream = nullptr;
            event = nullptr;
            eventRegistered = false;
            started = false;
            buffers.clear();
            closed = true;
            closing = false;
            condition.notify_all();
        }
        device.reset();
    }

    std::shared_ptr<DeviceSessionState> device;
    GenTL::DS_HANDLE stream = nullptr;
    GenTL::EVENT_HANDLE event = nullptr;
    std::size_t payloadBytes = 0;
    std::vector<Buffer> buffers;
    std::mutex mutex;
    std::condition_variable condition;
    bool eventRegistered = false;
    bool started = false;
    bool stopping = false;
    bool waiting = false;
    bool closing = false;
    bool closed = false;
    std::size_t activeCalls = 0;
};

DataStreamSession::DataStreamSession(
    std::shared_ptr<DeviceSessionState> device,
    const std::string& streamId,
    std::size_t bufferCount)
    : impl_(std::make_unique<Impl>(std::move(device))) {
    try {
        impl_->initialize(streamId, bufferCount);
    } catch (...) {
        impl_->close();
        throw;
    }
}

DataStreamSession::~DataStreamSession() {
    reset();
}

DataStreamSession::DataStreamSession(DataStreamSession&& other) noexcept
    : impl_(std::move(other.impl_)) {}

DataStreamSession& DataStreamSession::operator=(DataStreamSession&& other) noexcept {
    if (this != &other) {
        reset();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

DataStreamSession::operator bool() const noexcept {
    if (!impl_) return false;
    std::lock_guard<std::mutex> guard(impl_->mutex);
    return !impl_->closed && impl_->stream != nullptr;
}

void DataStreamSession::start() {
    if (!impl_) throw std::logic_error("The GenTL data stream is closed.");
    std::lock_guard<std::mutex> stateGuard(impl_->mutex);
    if (impl_->closed || impl_->stopping || !impl_->stream)
        throw std::logic_error("The GenTL data stream is closed.");
    if (impl_->started) return;

    const auto context = impl_->device->context;
    std::lock_guard<std::mutex> contextGuard(context->mutex);
    if (context->quarantined)
        throw std::runtime_error("The GenTL Producer context is quarantined.");
    check(context->consumer.invoke(
        Operation::dsStartAcquisition,
        impl_->stream,
        GenTL::ACQ_START_FLAGS_DEFAULT,
        GENTL_INFINITE),
        "DSStartAcquisition");
    impl_->started = true;
}

std::optional<DataStreamFrame> DataStreamSession::nextFrame(
    std::uint64_t timeoutMilliseconds) {
    if (!impl_) throw std::logic_error("The GenTL data stream is closed.");

    auto* state = impl_.get();
    GenTL::EVENT_HANDLE eventHandle = nullptr;
    GenTL::DS_HANDLE streamHandle = nullptr;
    std::shared_ptr<ProducerContext> context;
    {
        std::lock_guard<std::mutex> guard(state->mutex);
        if (state->closed || state->stopping) return std::nullopt;
        if (!state->started)
            throw std::logic_error("Start the GenTL data stream before waiting for frames.");
        if (state->waiting)
            throw std::logic_error("Only one GenTL frame waiter is permitted per data stream.");
        state->waiting = true;
        ++state->activeCalls;
        eventHandle = state->event;
        streamHandle = state->stream;
        context = state->device->context;
    }

    struct ActiveCallGuard {
        Impl* state;
        ~ActiveCallGuard() {
            std::lock_guard<std::mutex> guard(state->mutex);
            state->waiting = false;
            if (state->activeCalls > 0) --state->activeCalls;
            state->condition.notify_all();
        }
    } activeGuard{state};

    GenTL::EVENT_NEW_BUFFER_DATA eventData{};
    std::size_t eventSize = sizeof(eventData);
    const auto status = context->consumer.invoke(
        Operation::eventGetData,
        eventHandle,
        &eventData,
        &eventSize,
        timeoutMillisecondsToNanoseconds(timeoutMilliseconds));

    if (status == GenTL::GC_ERR_TIMEOUT) return std::nullopt;
    if (status == GenTL::GC_ERR_ABORT ||
        status == GenTL::GC_ERR_INVALID_HANDLE) {
        // Close may unregister the event after this call has acquired its
        // active-call lease but just before EventGetData enters the Consumer.
        // In that race the Producer can report INVALID_HANDLE rather than
        // ABORT; both are normal cancellation results once stopping begins.
        std::lock_guard<std::mutex> guard(state->mutex);
        if (state->stopping) return std::nullopt;
    }
    check(status, "EventGetData(NEW_BUFFER)");
    if (eventSize != sizeof(eventData) || !eventData.BufferHandle)
        throw std::runtime_error("EventGetData returned an invalid NEW_BUFFER payload.");

    {
        std::lock_guard<std::mutex> guard(state->mutex);
        if (state->stopping || state->closed) return std::nullopt;
    }

    const auto knownBuffer = std::find_if(
        state->buffers.begin(), state->buffers.end(),
        [&](const auto& item) { return item.handle == eventData.BufferHandle; });
    if (knownBuffer == state->buffers.end()) {
        std::lock_guard<std::mutex> guard(context->mutex);
        context->quarantineLocked();
        throw std::runtime_error(
            "The Producer signaled a buffer handle that this stream did not announce.");
    }

    DataStreamFrame frame;
    try {
        std::lock_guard<std::mutex> guard(context->mutex);
        if (context->quarantined)
            throw std::runtime_error("The GenTL Producer context is quarantined.");

        const auto readBufferInfo = [&](GenTL::BUFFER_INFO_CMD command,
                                        GenTL::INFO_DATATYPE expected,
                                        const char* name) {
            return readTypedInfo<void*>(
                [&](GenTL::INFO_DATATYPE* type, void* value, std::size_t* size) {
                    return context->consumer.invoke(
                        Operation::dsGetBufferInfo,
                        streamHandle, eventData.BufferHandle,
                        command, type, value, size);
                },
                expected, name);
        };

        const auto base = readBufferInfo(
            GenTL::BUFFER_INFO_BASE, GenTL::INFO_DATATYPE_PTR,
            "DSGetBufferInfo(BASE)");
        const auto filled = readTypedInfo<std::size_t>(
            [&](GenTL::INFO_DATATYPE* type, void* value, std::size_t* size) {
                return context->consumer.invoke(
                    Operation::dsGetBufferInfo,
                    streamHandle, eventData.BufferHandle,
                    GenTL::BUFFER_INFO_SIZE_FILLED, type, value, size);
            },
            GenTL::INFO_DATATYPE_SIZET, "DSGetBufferInfo(SIZE_FILLED)");
        if (!base || !*base || !filled || *filled > state->payloadBytes)
            throw std::runtime_error("The Producer returned invalid buffer memory metadata.");

        const auto incomplete = readTypedInfo<bool8_t>(
            [&](GenTL::INFO_DATATYPE* type, void* value, std::size_t* size) {
                return context->consumer.invoke(
                    Operation::dsGetBufferInfo,
                    streamHandle, eventData.BufferHandle,
                    GenTL::BUFFER_INFO_IS_INCOMPLETE, type, value, size);
            },
            GenTL::INFO_DATATYPE_BOOL8, "DSGetBufferInfo(IS_INCOMPLETE)");
        const auto imagePresent = readTypedInfo<bool8_t>(
            [&](GenTL::INFO_DATATYPE* type, void* value, std::size_t* size) {
                return context->consumer.invoke(
                    Operation::dsGetBufferInfo,
                    streamHandle, eventData.BufferHandle,
                    GenTL::BUFFER_INFO_IMAGEPRESENT, type, value, size);
            },
            GenTL::INFO_DATATYPE_BOOL8, "DSGetBufferInfo(IMAGEPRESENT)");

        frame.frameId = readTypedInfo<std::uint64_t>(
            [&](GenTL::INFO_DATATYPE* type, void* value, std::size_t* size) {
                return context->consumer.invoke(
                    Operation::dsGetBufferInfo,
                    streamHandle, eventData.BufferHandle,
                    GenTL::BUFFER_INFO_FRAMEID, type, value, size);
            },
            GenTL::INFO_DATATYPE_UINT64, "DSGetBufferInfo(FRAMEID)");
        frame.timestampNanoseconds = readTypedInfo<std::uint64_t>(
            [&](GenTL::INFO_DATATYPE* type, void* value, std::size_t* size) {
                return context->consumer.invoke(
                    Operation::dsGetBufferInfo,
                    streamHandle, eventData.BufferHandle,
                    GenTL::BUFFER_INFO_TIMESTAMP_NS, type, value, size);
            },
            GenTL::INFO_DATATYPE_UINT64, "DSGetBufferInfo(TIMESTAMP_NS)");
        frame.pixelFormat = readTypedInfo<std::uint64_t>(
            [&](GenTL::INFO_DATATYPE* type, void* value, std::size_t* size) {
                return context->consumer.invoke(
                    Operation::dsGetBufferInfo,
                    streamHandle, eventData.BufferHandle,
                    GenTL::BUFFER_INFO_PIXELFORMAT, type, value, size);
            },
            GenTL::INFO_DATATYPE_UINT64, "DSGetBufferInfo(PIXELFORMAT)");
        frame.pixelFormatNamespace = readTypedInfo<std::uint64_t>(
            [&](GenTL::INFO_DATATYPE* type, void* value, std::size_t* size) {
                return context->consumer.invoke(
                    Operation::dsGetBufferInfo,
                    streamHandle, eventData.BufferHandle,
                    GenTL::BUFFER_INFO_PIXELFORMAT_NAMESPACE, type, value, size);
            },
            GenTL::INFO_DATATYPE_UINT64,
            "DSGetBufferInfo(PIXELFORMAT_NAMESPACE)");
        frame.pixelEndianness =
            readTypedInfo<GenTL::PIXELENDIANNESS_ID>(
                [&](GenTL::INFO_DATATYPE* type, void* value, std::size_t* size) {
                    return context->consumer.invoke(
                        Operation::dsGetBufferInfo,
                        streamHandle, eventData.BufferHandle,
                        GenTL::BUFFER_INFO_PIXEL_ENDIANNESS, type, value, size);
                },
                GenTL::INFO_DATATYPE_INT32,
                "DSGetBufferInfo(PIXEL_ENDIANNESS)");
        frame.width = readTypedInfo<std::size_t>(
            [&](GenTL::INFO_DATATYPE* type, void* value, std::size_t* size) {
                return context->consumer.invoke(
                    Operation::dsGetBufferInfo,
                    streamHandle, eventData.BufferHandle,
                    GenTL::BUFFER_INFO_WIDTH, type, value, size);
            },
            GenTL::INFO_DATATYPE_SIZET, "DSGetBufferInfo(WIDTH)");
        frame.height = readTypedInfo<std::size_t>(
            [&](GenTL::INFO_DATATYPE* type, void* value, std::size_t* size) {
                return context->consumer.invoke(
                    Operation::dsGetBufferInfo,
                    streamHandle, eventData.BufferHandle,
                    GenTL::BUFFER_INFO_HEIGHT, type, value, size);
            },
            GenTL::INFO_DATATYPE_SIZET, "DSGetBufferInfo(HEIGHT)");
        frame.deliveredImageHeight = readTypedInfo<std::size_t>(
            [&](GenTL::INFO_DATATYPE* type, void* value, std::size_t* size) {
                return context->consumer.invoke(
                    Operation::dsGetBufferInfo,
                    streamHandle, eventData.BufferHandle,
                    GenTL::BUFFER_INFO_DELIVERED_IMAGEHEIGHT, type, value, size);
            },
            GenTL::INFO_DATATYPE_SIZET,
            "DSGetBufferInfo(DELIVERED_IMAGEHEIGHT)");
        frame.imageOffset = readTypedInfo<std::size_t>(
            [&](GenTL::INFO_DATATYPE* type, void* value, std::size_t* size) {
                return context->consumer.invoke(
                    Operation::dsGetBufferInfo,
                    streamHandle, eventData.BufferHandle,
                    GenTL::BUFFER_INFO_IMAGEOFFSET, type, value, size);
            },
            GenTL::INFO_DATATYPE_SIZET, "DSGetBufferInfo(IMAGEOFFSET)");
        frame.xPadding = readTypedInfo<std::size_t>(
            [&](GenTL::INFO_DATATYPE* type, void* value, std::size_t* size) {
                return context->consumer.invoke(
                    Operation::dsGetBufferInfo,
                    streamHandle, eventData.BufferHandle,
                    GenTL::BUFFER_INFO_XPADDING, type, value, size);
            },
            GenTL::INFO_DATATYPE_SIZET, "DSGetBufferInfo(XPADDING)");
        frame.yPadding = readTypedInfo<std::size_t>(
            [&](GenTL::INFO_DATATYPE* type, void* value, std::size_t* size) {
                return context->consumer.invoke(
                    Operation::dsGetBufferInfo,
                    streamHandle, eventData.BufferHandle,
                    GenTL::BUFFER_INFO_YPADDING, type, value, size);
            },
            GenTL::INFO_DATATYPE_SIZET, "DSGetBufferInfo(YPADDING)");
        frame.payloadType = readTypedInfo<std::size_t>(
            [&](GenTL::INFO_DATATYPE* type, void* value, std::size_t* size) {
                return context->consumer.invoke(
                    Operation::dsGetBufferInfo,
                    streamHandle, eventData.BufferHandle,
                    GenTL::BUFFER_INFO_PAYLOADTYPE, type, value, size);
            },
            GenTL::INFO_DATATYPE_SIZET, "DSGetBufferInfo(PAYLOADTYPE)");

        if (incomplete) frame.incomplete = *incomplete != 0;
        if (imagePresent) frame.imagePresent = *imagePresent != 0;
        const auto containsChunkData = readTypedInfo<bool8_t>(
            [&](GenTL::INFO_DATATYPE* type, void* value, std::size_t* size) {
                return context->consumer.invoke(
                    Operation::dsGetBufferInfo,
                    streamHandle, eventData.BufferHandle,
                    GenTL::BUFFER_INFO_CONTAINS_CHUNKDATA, type, value, size);
            },
            GenTL::INFO_DATATYPE_BOOL8, "DSGetBufferInfo(CONTAINS_CHUNKDATA)");
        if (containsChunkData)
            frame.containsChunkData = *containsChunkData != 0;
        const auto dataLargerThanBuffer = readTypedInfo<bool8_t>(
            [&](GenTL::INFO_DATATYPE* type, void* value, std::size_t* size) {
                return context->consumer.invoke(
                    Operation::dsGetBufferInfo,
                    streamHandle, eventData.BufferHandle,
                    GenTL::BUFFER_INFO_DATA_LARGER_THAN_BUFFER, type, value, size);
            },
            GenTL::INFO_DATATYPE_BOOL8,
            "DSGetBufferInfo(DATA_LARGER_THAN_BUFFER)");
        if (dataLargerThanBuffer)
            frame.dataLargerThanBuffer = *dataLargerThanBuffer != 0;
        const auto isComposite = readTypedInfo<bool8_t>(
            [&](GenTL::INFO_DATATYPE* type, void* value, std::size_t* size) {
                return context->consumer.invoke(
                    Operation::dsGetBufferInfo,
                    streamHandle, eventData.BufferHandle,
                    GenTL::BUFFER_INFO_IS_COMPOSITE, type, value, size);
            },
            GenTL::INFO_DATATYPE_BOOL8, "DSGetBufferInfo(IS_COMPOSITE)");
        if (isComposite)
            frame.isComposite = *isComposite != 0;

        frame.payload.resize(*filled);
        if (*filled != 0)
            std::memcpy(frame.payload.data(), *base, *filled);

        check(context->consumer.invoke(
            Operation::dsQueueBuffer, streamHandle, eventData.BufferHandle),
            "DSQueueBuffer(requeue)");
    } catch (...) {
        std::lock_guard<std::mutex> guard(context->mutex);
        if (!context->quarantined) {
            const auto requeueStatus = context->consumer.invoke(
                Operation::dsQueueBuffer, streamHandle, eventData.BufferHandle);
            if (requeueStatus != GenTL::GC_ERR_SUCCESS)
                context->quarantineLocked();
        }
        throw;
    }

    return frame;
}

void DataStreamSession::stop() {
    if (impl_) impl_->close();
}

void DataStreamSession::reset() noexcept {
    if (impl_) {
        impl_->close();
        impl_.reset();
    }
}

DataStreamSession DeviceSession::openDataStream(
    const std::string& streamId, std::size_t bufferCount) const {
    if (!*this) throw std::logic_error("The GenTL device session is closed.");
    return DataStreamSession(state_, streamId, bufferCount);
}

} // namespace camera::gentl
