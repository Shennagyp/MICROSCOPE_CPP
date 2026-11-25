#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <type_traits>
#include <utility>

namespace camera::gentl {

// Operation IDs are the stable C ABI bridge contract. Their order mirrors the
// 64 raw GenTLAPI operations in AMSCOPE_DRIVER's GenTLConsumerCAPI package.
enum class Operation : std::uint32_t {
    GCGetInfo = 0,
    GCGetLastError = 1,
    GCInitLib = 2,
    GCCloseLib = 3,
    GCReadPort = 4,
    GCWritePort = 5,
    GCGetPortURL = 6,
    GCGetPortInfo = 7,
    GCRegisterEvent = 8,
    GCUnregisterEvent = 9,
    EventGetData = 10,
    EventGetDataInfo = 11,
    EventGetInfo = 12,
    EventFlush = 13,
    EventKill = 14,
    TLOpen = 15,
    TLClose = 16,
    TLGetInfo = 17,
    TLGetNumInterfaces = 18,
    TLGetInterfaceID = 19,
    TLGetInterfaceInfo = 20,
    TLOpenInterface = 21,
    TLUpdateInterfaceList = 22,
    IFClose = 23,
    IFGetInfo = 24,
    IFGetNumDevices = 25,
    IFGetDeviceID = 26,
    IFUpdateDeviceList = 27,
    IFGetDeviceInfo = 28,
    IFOpenDevice = 29,
    DevGetPort = 30,
    DevGetNumDataStreams = 31,
    DevGetDataStreamID = 32,
    DevOpenDataStream = 33,
    DevGetInfo = 34,
    DevClose = 35,
    DSAnnounceBuffer = 36,
    DSAllocAndAnnounceBuffer = 37,
    DSFlushQueue = 38,
    DSStartAcquisition = 39,
    DSStopAcquisition = 40,
    DSGetInfo = 41,
    DSGetBufferID = 42,
    DSClose = 43,
    DSRevokeBuffer = 44,
    DSQueueBuffer = 45,
    DSGetBufferInfo = 46,
    GCGetNumPortURLs = 47,
    GCGetPortURLInfo = 48,
    GCReadPortStacked = 49,
    GCWritePortStacked = 50,
    DSGetBufferChunkData = 51,
    IFGetParentTL = 52,
    DevGetParentIF = 53,
    DSGetParentDev = 54,
    DSGetNumBufferParts = 55,
    DSGetBufferPartInfo = 56,
    DSAnnounceCompositeBuffer = 57,
    DSGetBufferInfoStacked = 58,
    DSGetBufferPartInfoStacked = 59,
    DSGetNumFlows = 60,
    DSGetFlowInfo = 61,
    DSGetNumBufferSegments = 62,
    DSGetBufferSegmentInfo = 63
};

class ConsumerBridge;

class Consumer final {
public:
    Consumer() noexcept = default;
    ~Consumer();

    Consumer(const Consumer&) = delete;
    Consumer& operator=(const Consumer&) = delete;
    Consumer(Consumer&& other) noexcept;
    Consumer& operator=(Consumer&& other) noexcept;

    explicit operator bool() const noexcept;

    std::int32_t invokeRaw(
        Operation operation,
        const std::uint64_t* arguments,
        std::uint32_t argumentCount) const noexcept;

    template <typename... Args>
    std::int32_t invoke(Operation operation, Args... arguments) const noexcept {
        const std::uint64_t encoded[] = {encode(arguments)...};
        return invokeRaw(operation, encoded,
                         static_cast<std::uint32_t>(sizeof...(Args)));
    }

private:
    friend class ConsumerBridge;
    Consumer(std::shared_ptr<const ConsumerBridge> owner, void* context) noexcept;
    void reset() noexcept;

    template <typename T>
    static std::uint64_t encode(T value) noexcept {
        if constexpr (std::is_same_v<T, std::nullptr_t>) {
            return 0;
        } else if constexpr (std::is_pointer_v<T>) {
            return static_cast<std::uint64_t>(
                reinterpret_cast<std::uintptr_t>(value));
        } else if constexpr (std::is_enum_v<T>) {
            return static_cast<std::uint64_t>(
                static_cast<std::underlying_type_t<T>>(value));
        } else {
            return static_cast<std::uint64_t>(value);
        }
    }

    std::shared_ptr<const ConsumerBridge> owner_;
    void* context_ = nullptr;
};

class ConsumerBridge final
    : public std::enable_shared_from_this<ConsumerBridge> {
public:
    static std::shared_ptr<ConsumerBridge> load(
        const std::filesystem::path& libraryPath);

    ~ConsumerBridge();

    ConsumerBridge(const ConsumerBridge&) = delete;
    ConsumerBridge& operator=(const ConsumerBridge&) = delete;

    Consumer createProducer(const std::filesystem::path& ctiPath) const;

    std::uint32_t bridgeAbiVersion() const noexcept;
    std::uint32_t genTLHeaderVersion() const noexcept;

private:
    friend class Consumer;

    using CreateFunction = void* (*)(const char*, char*, std::uint32_t);
    using DestroyFunction = void (*)(void*);
    using VersionFunction = std::uint32_t (*)();
    using InvokeFunction = std::int32_t (*)(
        void*, std::uint32_t, const std::uint64_t*, std::uint32_t);

    ConsumerBridge(void* libraryHandle,
                   CreateFunction create,
                   DestroyFunction destroy,
                   VersionFunction bridgeVersion,
                   VersionFunction headerVersion,
                   InvokeFunction invoke) noexcept;

    void* libraryHandle_ = nullptr;
    CreateFunction create_ = nullptr;
    DestroyFunction destroy_ = nullptr;
    VersionFunction bridgeVersion_ = nullptr;
    VersionFunction headerVersion_ = nullptr;
    InvokeFunction invoke_ = nullptr;
};

} // namespace camera::gentl
