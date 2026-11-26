#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <type_traits>
#include <utility>

namespace camera::gentl {

// Stable internal operation IDs for the GenTL C function surface. The numeric
// values preserve the ordering used by the existing test fixture.
enum class Operation : std::uint32_t {
    gcGetInfo = 0,
    gcGetLastError = 1,
    gcInitLib = 2,
    gcCloseLib = 3,
    gcReadPort = 4,
    gcWritePort = 5,
    gcGetPortURL = 6,
    gcGetPortInfo = 7,
    gcRegisterEvent = 8,
    gcUnregisterEvent = 9,
    eventGetData = 10,
    eventGetDataInfo = 11,
    eventGetInfo = 12,
    eventFlush = 13,
    eventKill = 14,
    tlOpen = 15,
    tlClose = 16,
    tlGetInfo = 17,
    tlGetNumInterfaces = 18,
    tlGetInterfaceID = 19,
    tlGetInterfaceInfo = 20,
    tlOpenInterface = 21,
    tlUpdateInterfaceList = 22,
    ifClose = 23,
    ifGetInfo = 24,
    ifGetNumDevices = 25,
    ifGetDeviceID = 26,
    ifUpdateDeviceList = 27,
    ifGetDeviceInfo = 28,
    ifOpenDevice = 29,
    devGetPort = 30,
    devGetNumDataStreams = 31,
    devGetDataStreamID = 32,
    devOpenDataStream = 33,
    devGetInfo = 34,
    devClose = 35,
    dsAnnounceBuffer = 36,
    dsAllocAndAnnounceBuffer = 37,
    dsFlushQueue = 38,
    dsStartAcquisition = 39,
    dsStopAcquisition = 40,
    dsGetInfo = 41,
    dsGetBufferID = 42,
    dsClose = 43,
    dsRevokeBuffer = 44,
    dsQueueBuffer = 45,
    dsGetBufferInfo = 46,
    gcGetNumPortURLs = 47,
    gcGetPortURLInfo = 48,
    gcReadPortStacked = 49,
    gcWritePortStacked = 50,
    dsGetBufferChunkData = 51,
    ifGetParentTL = 52,
    devGetParentIF = 53,
    dsGetParentDev = 54,
    dsGetNumBufferParts = 55,
    dsGetBufferPartInfo = 56,
    dsAnnounceCompositeBuffer = 57,
    dsGetBufferInfoStacked = 58,
    dsGetBufferPartInfoStacked = 59,
    dsGetNumFlows = 60,
    dsGetFlowInfo = 61,
    dsGetNumBufferSegments = 62,
    dsGetBufferSegmentInfo = 63
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
