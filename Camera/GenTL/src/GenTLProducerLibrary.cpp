#include "camera/gentl/GenTLProducerLibrary.h"

#include <dlfcn.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace camera::gentl {
namespace {

template <typename Function>
Function resolve(void* library, const char* symbol) {
    dlerror();
    auto* address = dlsym(library, symbol);
    if (const char* error = dlerror()) {
        throw std::runtime_error(
            std::string("GenTL Producer is missing required symbol '") +
            symbol + "': " + error);
    }
    return reinterpret_cast<Function>(address);
}

} // namespace

struct GenTLProducerLibrary::Functions {
    GenTL::PGCGetInfo gcGetInfo = nullptr;
    GenTL::PGCInitLib gcInitLib = nullptr;
    GenTL::PGCCloseLib gcCloseLib = nullptr;
    GenTL::PGCReadPort gcReadPort = nullptr;
    GenTL::PGCWritePort gcWritePort = nullptr;
    GenTL::PGCGetPortURL gcGetPortURL = nullptr;
    GenTL::PGCRegisterEvent gcRegisterEvent = nullptr;
    GenTL::PGCUnregisterEvent gcUnregisterEvent = nullptr;
    GenTL::PEventGetData eventGetData = nullptr;
    GenTL::PEventKill eventKill = nullptr;
    GenTL::PTLOpen tlOpen = nullptr;
    GenTL::PTLClose tlClose = nullptr;
    GenTL::PTLGetNumInterfaces tlGetNumInterfaces = nullptr;
    GenTL::PTLGetInterfaceID tlGetInterfaceID = nullptr;
    GenTL::PTLOpenInterface tlOpenInterface = nullptr;
    GenTL::PTLUpdateInterfaceList tlUpdateInterfaceList = nullptr;
    GenTL::PIFClose ifClose = nullptr;
    GenTL::PIFGetInfo ifGetInfo = nullptr;
    GenTL::PIFGetNumDevices ifGetNumDevices = nullptr;
    GenTL::PIFGetDeviceID ifGetDeviceID = nullptr;
    GenTL::PIFUpdateDeviceList ifUpdateDeviceList = nullptr;
    GenTL::PIFGetDeviceInfo ifGetDeviceInfo = nullptr;
    GenTL::PIFOpenDevice ifOpenDevice = nullptr;
    GenTL::PDevGetPort devGetPort = nullptr;
    GenTL::PDevGetNumDataStreams devGetNumDataStreams = nullptr;
    GenTL::PDevGetDataStreamID devGetDataStreamID = nullptr;
    GenTL::PDevOpenDataStream devOpenDataStream = nullptr;
    GenTL::PDevClose devClose = nullptr;
    GenTL::PDSAllocAndAnnounceBuffer dsAllocAndAnnounceBuffer = nullptr;
    GenTL::PDSFlushQueue dsFlushQueue = nullptr;
    GenTL::PDSStartAcquisition dsStartAcquisition = nullptr;
    GenTL::PDSStopAcquisition dsStopAcquisition = nullptr;
    GenTL::PDSGetInfo dsGetInfo = nullptr;
    GenTL::PDSClose dsClose = nullptr;
    GenTL::PDSRevokeBuffer dsRevokeBuffer = nullptr;
    GenTL::PDSQueueBuffer dsQueueBuffer = nullptr;
    GenTL::PDSGetBufferInfo dsGetBufferInfo = nullptr;
    GenTL::PGCGetNumPortURLs gcGetNumPortURLs = nullptr;
    GenTL::PGCGetPortURLInfo gcGetPortURLInfo = nullptr;
};

GenTLProducerLibrary::GenTLProducerLibrary(
    void* libraryHandle,
    std::filesystem::path path,
    Functions functions) noexcept
    : libraryHandle_(libraryHandle),
      path_(std::move(path)),
      functions_(std::make_unique<Functions>(std::move(functions))) {}

std::shared_ptr<GenTLProducerLibrary> GenTLProducerLibrary::load(
    const std::filesystem::path& ctiPath) {
    const auto canonicalPath = std::filesystem::canonical(ctiPath);
    const auto nativePath = canonicalPath.string();
    void* library = dlopen(nativePath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        const char* error = dlerror();
        throw std::runtime_error(
            "Could not load GenTL Producer '" + nativePath + "': " +
            (error ? error : "unknown dynamic-loader error"));
    }

    try {
        Functions functions;
        functions.gcGetInfo = resolve<GenTL::PGCGetInfo>(library, "GCGetInfo");
        functions.gcInitLib = resolve<GenTL::PGCInitLib>(library, "GCInitLib");
        functions.gcCloseLib = resolve<GenTL::PGCCloseLib>(library, "GCCloseLib");
        functions.gcReadPort = resolve<GenTL::PGCReadPort>(library, "GCReadPort");
        functions.gcWritePort = resolve<GenTL::PGCWritePort>(library, "GCWritePort");
        functions.gcGetPortURL = resolve<GenTL::PGCGetPortURL>(library, "GCGetPortURL");
        functions.gcRegisterEvent =
            resolve<GenTL::PGCRegisterEvent>(library, "GCRegisterEvent");
        functions.gcUnregisterEvent =
            resolve<GenTL::PGCUnregisterEvent>(library, "GCUnregisterEvent");
        functions.eventGetData =
            resolve<GenTL::PEventGetData>(library, "EventGetData");
        functions.eventKill = resolve<GenTL::PEventKill>(library, "EventKill");
        functions.tlOpen = resolve<GenTL::PTLOpen>(library, "TLOpen");
        functions.tlClose = resolve<GenTL::PTLClose>(library, "TLClose");
        functions.tlGetNumInterfaces =
            resolve<GenTL::PTLGetNumInterfaces>(library, "TLGetNumInterfaces");
        functions.tlGetInterfaceID =
            resolve<GenTL::PTLGetInterfaceID>(library, "TLGetInterfaceID");
        functions.tlOpenInterface =
            resolve<GenTL::PTLOpenInterface>(library, "TLOpenInterface");
        functions.tlUpdateInterfaceList =
            resolve<GenTL::PTLUpdateInterfaceList>(library, "TLUpdateInterfaceList");
        functions.ifClose = resolve<GenTL::PIFClose>(library, "IFClose");
        functions.ifGetInfo = resolve<GenTL::PIFGetInfo>(library, "IFGetInfo");
        functions.ifGetNumDevices =
            resolve<GenTL::PIFGetNumDevices>(library, "IFGetNumDevices");
        functions.ifGetDeviceID =
            resolve<GenTL::PIFGetDeviceID>(library, "IFGetDeviceID");
        functions.ifUpdateDeviceList =
            resolve<GenTL::PIFUpdateDeviceList>(library, "IFUpdateDeviceList");
        functions.ifGetDeviceInfo =
            resolve<GenTL::PIFGetDeviceInfo>(library, "IFGetDeviceInfo");
        functions.ifOpenDevice =
            resolve<GenTL::PIFOpenDevice>(library, "IFOpenDevice");
        functions.devGetPort = resolve<GenTL::PDevGetPort>(library, "DevGetPort");
        functions.devGetNumDataStreams =
            resolve<GenTL::PDevGetNumDataStreams>(library, "DevGetNumDataStreams");
        functions.devGetDataStreamID =
            resolve<GenTL::PDevGetDataStreamID>(library, "DevGetDataStreamID");
        functions.devOpenDataStream =
            resolve<GenTL::PDevOpenDataStream>(library, "DevOpenDataStream");
        functions.devClose = resolve<GenTL::PDevClose>(library, "DevClose");
        functions.dsAllocAndAnnounceBuffer =
            resolve<GenTL::PDSAllocAndAnnounceBuffer>(
                library, "DSAllocAndAnnounceBuffer");
        functions.dsFlushQueue =
            resolve<GenTL::PDSFlushQueue>(library, "DSFlushQueue");
        functions.dsStartAcquisition =
            resolve<GenTL::PDSStartAcquisition>(library, "DSStartAcquisition");
        functions.dsStopAcquisition =
            resolve<GenTL::PDSStopAcquisition>(library, "DSStopAcquisition");
        functions.dsGetInfo = resolve<GenTL::PDSGetInfo>(library, "DSGetInfo");
        functions.dsClose = resolve<GenTL::PDSClose>(library, "DSClose");
        functions.dsRevokeBuffer =
            resolve<GenTL::PDSRevokeBuffer>(library, "DSRevokeBuffer");
        functions.dsQueueBuffer =
            resolve<GenTL::PDSQueueBuffer>(library, "DSQueueBuffer");
        functions.dsGetBufferInfo =
            resolve<GenTL::PDSGetBufferInfo>(library, "DSGetBufferInfo");
        functions.gcGetNumPortURLs =
            resolve<GenTL::PGCGetNumPortURLs>(library, "GCGetNumPortURLs");
        functions.gcGetPortURLInfo =
            resolve<GenTL::PGCGetPortURLInfo>(library, "GCGetPortURLInfo");

        auto result = std::shared_ptr<GenTLProducerLibrary>(
            new GenTLProducerLibrary(
                library, canonicalPath, std::move(functions)));
        library = nullptr;
        return result;
    } catch (...) {
        if (library) dlclose(library);
        throw;
    }
}

GenTLProducerLibrary::~GenTLProducerLibrary() {
    if (libraryHandle_) dlclose(libraryHandle_);
}

const std::filesystem::path& GenTLProducerLibrary::path() const noexcept {
    return path_;
}

Consumer GenTLProducerLibrary::createConsumer() {
    auto owner = shared_from_this();
    return Consumer(
        std::move(owner), this,
        [](const void*, void* producerContext, Operation operation,
           const std::uint64_t* arguments,
           std::uint32_t argumentCount) noexcept -> std::int32_t {
            if (!producerContext) return GenTL::GC_ERR_INVALID_HANDLE;
            const auto* producer =
                static_cast<const GenTLProducerLibrary*>(producerContext);
            return producer->invokeRaw(operation, arguments, argumentCount);
        },
        [](const void*, void*) noexcept {
            // The shared owner retains the CTI library. There is no per-consumer
            // native context to destroy.
        });
}

std::int32_t GenTLProducerLibrary::invokeRaw(
    Operation operation,
    const std::uint64_t* arguments,
    std::uint32_t argumentCount) const noexcept {
    if (!functions_ || (argumentCount != 0 && !arguments))
        return GenTL::GC_ERR_INVALID_PARAMETER;

    const auto pointer = [&](std::uint32_t index) {
        return reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(arguments[index]));
    };
    const auto countIs = [&](std::uint32_t expected) {
        return argumentCount == expected;
    };
    const auto invalid = [] { return GenTL::GC_ERR_INVALID_PARAMETER; };
    const auto& f = *functions_;

    switch (operation) {
    case Operation::gcGetInfo:
        return countIs(4) ? f.gcGetInfo(
            static_cast<GenTL::TL_INFO_CMD>(arguments[0]),
            static_cast<GenTL::INFO_DATATYPE*>(pointer(1)),
            pointer(2), static_cast<std::size_t*>(pointer(3))) : invalid();
    case Operation::gcInitLib:
        return countIs(0) ? f.gcInitLib() : invalid();
    case Operation::gcCloseLib:
        return countIs(0) ? f.gcCloseLib() : invalid();
    case Operation::gcReadPort:
        return countIs(4) ? f.gcReadPort(
            static_cast<GenTL::PORT_HANDLE>(pointer(0)), arguments[1],
            pointer(2), static_cast<std::size_t*>(pointer(3))) : invalid();
    case Operation::gcWritePort:
        return countIs(4) ? f.gcWritePort(
            static_cast<GenTL::PORT_HANDLE>(pointer(0)), arguments[1],
            pointer(2), static_cast<std::size_t*>(pointer(3))) : invalid();
    case Operation::gcGetPortURL:
        return countIs(3) ? f.gcGetPortURL(
            static_cast<GenTL::PORT_HANDLE>(pointer(0)),
            static_cast<char*>(pointer(1)),
            static_cast<std::size_t*>(pointer(2))) : invalid();
    case Operation::gcRegisterEvent:
        return countIs(3) ? f.gcRegisterEvent(
            static_cast<GenTL::EVENTSRC_HANDLE>(pointer(0)),
            static_cast<GenTL::EVENT_TYPE>(arguments[1]),
            static_cast<GenTL::EVENT_HANDLE*>(pointer(2))) : invalid();
    case Operation::gcUnregisterEvent:
        return countIs(2) ? f.gcUnregisterEvent(
            static_cast<GenTL::EVENTSRC_HANDLE>(pointer(0)),
            static_cast<GenTL::EVENT_TYPE>(arguments[1])) : invalid();
    case Operation::eventGetData:
        return countIs(4) ? f.eventGetData(
            static_cast<GenTL::EVENT_HANDLE>(pointer(0)), pointer(1),
            static_cast<std::size_t*>(pointer(2)), arguments[3]) : invalid();
    case Operation::eventKill:
        return countIs(1) ? f.eventKill(
            static_cast<GenTL::EVENT_HANDLE>(pointer(0))) : invalid();
    case Operation::tlOpen:
        return countIs(1) ? f.tlOpen(
            static_cast<GenTL::TL_HANDLE*>(pointer(0))) : invalid();
    case Operation::tlClose:
        return countIs(1) ? f.tlClose(
            static_cast<GenTL::TL_HANDLE>(pointer(0))) : invalid();
    case Operation::tlGetNumInterfaces:
        return countIs(2) ? f.tlGetNumInterfaces(
            static_cast<GenTL::TL_HANDLE>(pointer(0)),
            static_cast<std::uint32_t*>(pointer(1))) : invalid();
    case Operation::tlGetInterfaceID:
        return countIs(4) ? f.tlGetInterfaceID(
            static_cast<GenTL::TL_HANDLE>(pointer(0)),
            static_cast<std::uint32_t>(arguments[1]),
            static_cast<char*>(pointer(2)),
            static_cast<std::size_t*>(pointer(3))) : invalid();
    case Operation::tlOpenInterface:
        return countIs(3) ? f.tlOpenInterface(
            static_cast<GenTL::TL_HANDLE>(pointer(0)),
            static_cast<const char*>(pointer(1)),
            static_cast<GenTL::IF_HANDLE*>(pointer(2))) : invalid();
    case Operation::tlUpdateInterfaceList:
        return countIs(3) ? f.tlUpdateInterfaceList(
            static_cast<GenTL::TL_HANDLE>(pointer(0)),
            static_cast<bool8_t*>(pointer(1)), arguments[2]) : invalid();
    case Operation::ifClose:
        return countIs(1) ? f.ifClose(
            static_cast<GenTL::IF_HANDLE>(pointer(0))) : invalid();
    case Operation::ifGetInfo:
        return countIs(5) ? f.ifGetInfo(
            static_cast<GenTL::IF_HANDLE>(pointer(0)),
            static_cast<GenTL::INTERFACE_INFO_CMD>(arguments[1]),
            static_cast<GenTL::INFO_DATATYPE*>(pointer(2)),
            pointer(3), static_cast<std::size_t*>(pointer(4))) : invalid();
    case Operation::ifGetNumDevices:
        return countIs(2) ? f.ifGetNumDevices(
            static_cast<GenTL::IF_HANDLE>(pointer(0)),
            static_cast<std::uint32_t*>(pointer(1))) : invalid();
    case Operation::ifGetDeviceID:
        return countIs(4) ? f.ifGetDeviceID(
            static_cast<GenTL::IF_HANDLE>(pointer(0)),
            static_cast<std::uint32_t>(arguments[1]),
            static_cast<char*>(pointer(2)),
            static_cast<std::size_t*>(pointer(3))) : invalid();
    case Operation::ifUpdateDeviceList:
        return countIs(3) ? f.ifUpdateDeviceList(
            static_cast<GenTL::IF_HANDLE>(pointer(0)),
            static_cast<bool8_t*>(pointer(1)), arguments[2]) : invalid();
    case Operation::ifGetDeviceInfo:
        return countIs(6) ? f.ifGetDeviceInfo(
            static_cast<GenTL::IF_HANDLE>(pointer(0)),
            static_cast<const char*>(pointer(1)),
            static_cast<GenTL::DEVICE_INFO_CMD>(arguments[2]),
            static_cast<GenTL::INFO_DATATYPE*>(pointer(3)),
            pointer(4), static_cast<std::size_t*>(pointer(5))) : invalid();
    case Operation::ifOpenDevice:
        return countIs(4) ? f.ifOpenDevice(
            static_cast<GenTL::IF_HANDLE>(pointer(0)),
            static_cast<const char*>(pointer(1)),
            static_cast<GenTL::DEVICE_ACCESS_FLAGS>(arguments[2]),
            static_cast<GenTL::DEV_HANDLE*>(pointer(3))) : invalid();
    case Operation::devGetPort:
        return countIs(2) ? f.devGetPort(
            static_cast<GenTL::DEV_HANDLE>(pointer(0)),
            static_cast<GenTL::PORT_HANDLE*>(pointer(1))) : invalid();
    case Operation::devGetNumDataStreams:
        return countIs(2) ? f.devGetNumDataStreams(
            static_cast<GenTL::DEV_HANDLE>(pointer(0)),
            static_cast<std::uint32_t*>(pointer(1))) : invalid();
    case Operation::devGetDataStreamID:
        return countIs(4) ? f.devGetDataStreamID(
            static_cast<GenTL::DEV_HANDLE>(pointer(0)),
            static_cast<std::uint32_t>(arguments[1]),
            static_cast<char*>(pointer(2)),
            static_cast<std::size_t*>(pointer(3))) : invalid();
    case Operation::devOpenDataStream:
        return countIs(3) ? f.devOpenDataStream(
            static_cast<GenTL::DEV_HANDLE>(pointer(0)),
            static_cast<const char*>(pointer(1)),
            static_cast<GenTL::DS_HANDLE*>(pointer(2))) : invalid();
    case Operation::devClose:
        return countIs(1) ? f.devClose(
            static_cast<GenTL::DEV_HANDLE>(pointer(0))) : invalid();
    case Operation::dsAllocAndAnnounceBuffer:
        return countIs(4) ? f.dsAllocAndAnnounceBuffer(
            static_cast<GenTL::DS_HANDLE>(pointer(0)),
            static_cast<std::size_t>(arguments[1]), pointer(2),
            static_cast<GenTL::BUFFER_HANDLE*>(pointer(3))) : invalid();
    case Operation::dsFlushQueue:
        return countIs(2) ? f.dsFlushQueue(
            static_cast<GenTL::DS_HANDLE>(pointer(0)),
            static_cast<GenTL::ACQ_QUEUE_TYPE>(arguments[1])) : invalid();
    case Operation::dsStartAcquisition:
        return countIs(3) ? f.dsStartAcquisition(
            static_cast<GenTL::DS_HANDLE>(pointer(0)),
            static_cast<GenTL::ACQ_START_FLAGS>(arguments[1]),
            arguments[2]) : invalid();
    case Operation::dsStopAcquisition:
        return countIs(2) ? f.dsStopAcquisition(
            static_cast<GenTL::DS_HANDLE>(pointer(0)),
            static_cast<GenTL::ACQ_STOP_FLAGS>(arguments[1])) : invalid();
    case Operation::dsGetInfo:
        return countIs(5) ? f.dsGetInfo(
            static_cast<GenTL::DS_HANDLE>(pointer(0)),
            static_cast<GenTL::STREAM_INFO_CMD>(arguments[1]),
            static_cast<GenTL::INFO_DATATYPE*>(pointer(2)),
            pointer(3), static_cast<std::size_t*>(pointer(4))) : invalid();
    case Operation::dsClose:
        return countIs(1) ? f.dsClose(
            static_cast<GenTL::DS_HANDLE>(pointer(0))) : invalid();
    case Operation::dsRevokeBuffer:
        return countIs(4) ? f.dsRevokeBuffer(
            static_cast<GenTL::DS_HANDLE>(pointer(0)),
            static_cast<GenTL::BUFFER_HANDLE>(pointer(1)),
            static_cast<void**>(pointer(2)),
            static_cast<void**>(pointer(3))) : invalid();
    case Operation::dsQueueBuffer:
        return countIs(2) ? f.dsQueueBuffer(
            static_cast<GenTL::DS_HANDLE>(pointer(0)),
            static_cast<GenTL::BUFFER_HANDLE>(pointer(1))) : invalid();
    case Operation::dsGetBufferInfo:
        return countIs(6) ? f.dsGetBufferInfo(
            static_cast<GenTL::DS_HANDLE>(pointer(0)),
            static_cast<GenTL::BUFFER_HANDLE>(pointer(1)),
            static_cast<GenTL::BUFFER_INFO_CMD>(arguments[2]),
            static_cast<GenTL::INFO_DATATYPE*>(pointer(3)),
            pointer(4), static_cast<std::size_t*>(pointer(5))) : invalid();
    case Operation::gcGetNumPortURLs:
        return countIs(2) ? f.gcGetNumPortURLs(
            static_cast<GenTL::PORT_HANDLE>(pointer(0)),
            static_cast<std::uint32_t*>(pointer(1))) : invalid();
    case Operation::gcGetPortURLInfo:
        return countIs(6) ? f.gcGetPortURLInfo(
            static_cast<GenTL::PORT_HANDLE>(pointer(0)),
            static_cast<std::uint32_t>(arguments[1]),
            static_cast<GenTL::URL_INFO_CMD>(arguments[2]),
            static_cast<GenTL::INFO_DATATYPE*>(pointer(3)),
            pointer(4), static_cast<std::size_t*>(pointer(5))) : invalid();
    default:
        return GenTL::GC_ERR_NOT_IMPLEMENTED;
    }
}

} // namespace camera::gentl
