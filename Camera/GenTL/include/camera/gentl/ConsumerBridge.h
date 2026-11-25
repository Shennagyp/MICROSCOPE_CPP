#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <type_traits>
#include <utility>

namespace camera::gentl {

// Operation IDs are the stable C ABI bridge contract. Their order mirrors the
// 64 raw GenTLAPI operations in AMSCOPE_DRIVER's GenTLConsumerCAPI package.
enum class Operation : std::uint32_t {
    gCGetInfo = 0,
    gCGetLastError = 1,
    gCInitLib = 2,
    gCCloseLib = 3,
    gCReadPort = 4,
    gCWritePort = 5,
    gCGetPortURL = 6,
    gCGetPortInfo = 7,
    gCRegisterEvent = 8,
    gCUnregisterEvent = 9,
    eventGetData = 10,
    eventGetDataInfo = 11,
    eventGetInfo = 12,
    eventFlush = 13,
    eventKill = 14,
    tLOpen = 15,
    tLClose = 16,
    tLGetInfo = 17,
    tLGetNumInterfaces = 18,
    tLGetInterfaceID = 19,
    tLGetInterfaceInfo = 20,
    tLOpenInterface = 21,
    tLUpdateInterfaceList = 22,
    iFClose = 23,
    iFGetInfo = 24,
    iFGetNumDevices = 25,
    iFGetDeviceID = 26,
    iFUpdateDeviceList = 27,
    iFGetDeviceInfo = 28,
    iFOpenDevice = 29,
    devGetPort = 30,
    devGetNumDataStreams = 31,
    devGetDataStreamID = 32,
    devOpenDataStream = 33,
    devGetInfo = 34,
    devClose = 35,
    dSAnnounceBuffer = 36,
    dSAllocAndAnnounceBuffer = 37,
    dSFlushQueue = 38,
    dSStartAcquisition = 39,
    dSStopAcquisition = 40,
    dSGetInfo = 41,
    dSGetBufferID = 42,
    dSClose = 43,
    dSRevokeBuffer = 44,
    dSQueueBuffer = 45,
    dSGetBufferInfo = 46,
    gCGetNumPortURLs = 47,
    gCGetPortURLInfo = 48,
    gCReadPortStacked = 49,
    gCWritePortStacked = 50,
    dSGetBufferChunkData = 51,
    iFGetParentTL = 52,
    devGetParentIF = 53,
    dSGetParentDev = 54,
    dSGetNumBufferParts = 55,
    dSGetBufferPartInfo = 56,
    dSAnnounceCompositeBuffer = 57,
    dSGetBufferInfoStacked = 58,
    dSGetBufferPartInfoStacked = 59,
    dSGetNumFlows = 60,
    dSGetFlowInfo = 61,
    dSGetNumBufferSegments = 62,
    dSGetBufferSegmentInfo = 63
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
        const std::array<std::uint64_t, sizeof...(Args)> encoded{{encode(arguments)...}};
        return invokeRaw(operation, encoded.data(),
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
