#include "camera/gentl/Producer.h"
#include "camera/gentl/GenTLFrameAdapter.h"

#include <cstdlib>
#include <cstdio>
#include <chrono>
#include <dlfcn.h>
#include <filesystem>
#include <memory>
#include <optional>
#include <fstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

int runProducerTests(int argc, char** argv) {
    if (argc != 2) return 1;

    const auto directory =
        std::filesystem::temp_directory_path() / "camera-gentl-producer-tests";
    std::filesystem::create_directories(directory);
    const auto ctiPath = directory / "mock.cti";
    {
        std::ofstream file(ctiPath);
        file << "test fixture";
    }

    int result = 0;
    try {
        auto bridge = camera::gentl::ConsumerBridge::load(argv[1]);
        auto producer = camera::gentl::Producer::open(bridge, ctiPath);
        if (!producer) return 2;

        const auto devices = producer.discover();
        if (devices.size() != 1) return 3;
        const auto& descriptor = devices.front();
        if (descriptor.interfaceId != "iface0" ||
            descriptor.interfaceDisplayName != "Mock Interface" ||
            descriptor.deviceId != "device0" ||
            descriptor.displayName != "Mock Camera" ||
            descriptor.vendor != "Mock Vendor" ||
            descriptor.model != "Mock Model" ||
            descriptor.serialNumber != "MOCK-0001") return 4;

        // A second discovery reuses the retained Interface handle.
        if (producer.discover().size() != 1) return 5;

        auto foreignDescriptor = descriptor;
        foreignDescriptor.producerPath += ".foreign";
        bool rejectedForeign = false;
        try {
            auto invalid = producer.openDevice(foreignDescriptor);
            (void)invalid;
        } catch (const std::invalid_argument&) {
            rejectedForeign = true;
        }
        if (!rejectedForeign) return 6;

        auto staleInterfaceDescriptor = descriptor;
        staleInterfaceDescriptor.interfaceId = "missing-interface";
        bool rejectedStaleInterface = false;
        try {
            auto invalid = producer.openDevice(staleInterfaceDescriptor);
            (void)invalid;
        } catch (const std::invalid_argument&) {
            rejectedStaleInterface = true;
        }
        if (!rejectedStaleInterface) return 34;

        auto staleDeviceDescriptor = descriptor;
        staleDeviceDescriptor.deviceId = "missing-device";
        bool rejectedStaleDevice = false;
        try {
            auto invalid = producer.openDevice(staleDeviceDescriptor);
            (void)invalid;
        } catch (const std::runtime_error&) {
            rejectedStaleDevice = true;
        }
        if (!rejectedStaleDevice) return 35;

        auto device = producer.openDevice(descriptor);
        if (!device || device.deviceId() != "device0") return 7;
        producer = camera::gentl::Producer{}; // The session retains producer state.

        const auto streams = device.dataStreamIds();
        if (streams.size() != 1 || streams.front() != "stream0") return 8;

        const auto xmlBytes = device.genApiXml();
        const std::string xml(xmlBytes.begin(), xmlBytes.end());
        if (xml.find("<RegisterDescription") == std::string::npos ||
            xml.find("MockGenApiCamera") == std::string::npos) return 9;

        const auto bytes = device.readPort(0x10203040, 4);
        if (bytes != std::vector<std::uint8_t>{0xA0, 0xA1, 0xA2, 0xA3}) return 10;

        device.writePort(0x10203040, {0x10, 0x20, 0x30, 0x40});
        if (device.portUrl() != "Mock://DevicePort") return 11;

        bool rejectedInsufficientBuffers = false;
        try {
            auto invalid = device.openDataStream("stream0", 1);
            (void)invalid;
        } catch (const std::invalid_argument&) {
            rejectedInsufficientBuffers = true;
        }
        if (!rejectedInsufficientBuffers) return 12;

        auto stream = device.openDataStream("stream0", 3);
        if (!stream) return 13;
        stream.start();
        const auto frame = stream.nextFrame(100);
        if (!frame || frame->payload.size() != 16 ||
            frame->payload.front() != 0x30 ||
            frame->payload.back() != 0x3F ||
            frame->frameId != 42 ||
            frame->timestampNanoseconds != 123456789 ||
            frame->pixelFormat != UINT64_C(0x01080001) ||
            frame->pixelFormatNamespace != GenTL::PIXELFORMAT_NAMESPACE_PFNC_32BIT ||
            frame->pixelEndianness != GenTL::PIXELENDIANNESS_LITTLE ||
            frame->width != 4 || frame->height != 4 ||
            frame->deliveredImageHeight != 4 ||
            frame->imageOffset != 0 || frame->xPadding != 0 ||
            frame->yPadding != 0 || frame->payloadType != GenTL::PAYLOAD_TYPE_IMAGE ||
            frame->imagePresent != true || frame->containsChunkData != false ||
            frame->dataLargerThanBuffer != false || frame->isComposite != false ||
            frame->incomplete) return 14;

        const auto expectedCommonPixels = frame->payload;
        auto commonInput = *frame;
        const auto commonFrame =
            camera::gentl::makeCommonFrame(std::move(commonInput));
        if (!commonFrame ||
            (*commonFrame)->pixelFormat != camera::PixelFormat::Raw8 ||
            (*commonFrame)->pixelByteOrder != camera::PixelByteOrder::NotApplicable ||
            (*commonFrame)->width != 4 || (*commonFrame)->height != 4 ||
            (*commonFrame)->strideBytes != 4 ||
            (*commonFrame)->significantBitsPerPixel != 8 ||
            (*commonFrame)->storageBitsPerPixel != 8 ||
            (*commonFrame)->pixels != expectedCommonPixels ||
            !(*commonFrame)->pixelFormatIdentifier ||
            (*commonFrame)->pixelFormatIdentifier->scheme != "pfnc" ||
            (*commonFrame)->pixelFormatIdentifier->value != UINT64_C(0x01080001) ||
            !(*commonFrame)->timestamp ||
            (*commonFrame)->timestamp->ticks != 123456789 ||
            (*commonFrame)->timestamp->ticksPerSecond != UINT64_C(1000000000) ||
            (*commonFrame)->sequenceNumber != 42) return 15;

        auto incompleteFrame = *frame;
        incompleteFrame.incomplete = true;
        if (camera::gentl::makeCommonFrame(incompleteFrame)) return 16;
        auto paddedFrame = *frame;
        paddedFrame.xPadding = 1;
        const auto opaquePaddedFrame =
            camera::gentl::makeCommonFrame(paddedFrame);
        if (!opaquePaddedFrame ||
            (*opaquePaddedFrame)->pixelFormat != camera::PixelFormat::Unknown ||
            (*opaquePaddedFrame)->strideBytes != 0 ||
            (*opaquePaddedFrame)->rowPaddingBytes != 1) return 17;
        auto chunkedFrame = *frame;
        chunkedFrame.containsChunkData = true;
        if (camera::gentl::makeCommonFrame(chunkedFrame)) return 18;

        // The data-stream lease keeps the device handle alive after its
        // DeviceSession owner releases its reference.
        device.close();
        if (device) return 19;
        const auto frameAfterDeviceOwnerClose = stream.nextFrame(100);
        if (!frameAfterDeviceOwnerClose ||
            frameAfterDeviceOwnerClose->frameId != 42) return 20;

        stream.stop();
        if (stream) return 21;


        // GCUnregisterEvent must abort a pending EventGetData operation.
        // The mock blocks deterministically until the event is unregistered.
        void* mockLibrary = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        if (!mockLibrary) { std::fprintf(stderr, "wait cancellation: dlopen failed: %s\\n", dlerror()); return 25; }
        using WaitEnteredFunction = int (*)();
        const auto waitEntered = reinterpret_cast<WaitEnteredFunction>(
            dlsym(mockLibrary, "gentl_mock_event_wait_entered"));
        if (!waitEntered) {
            std::fprintf(stderr, "wait cancellation: control symbol missing\\n");
            dlclose(mockLibrary);
            return 26;
        }

        auto waitProducer = camera::gentl::Producer::open(bridge, ctiPath);
        const auto waitDevices = waitProducer.discover();
        if (waitDevices.size() != 1) {
            std::fprintf(stderr, "wait cancellation: device discovery failed\\n");
            dlclose(mockLibrary);
            return 27;
        }
        auto waitDevice = waitProducer.openDevice(waitDevices.front());
        auto waitStream = waitDevice.openDataStream("stream0", 3);
        waitStream.start();

        std::optional<camera::gentl::DataStreamFrame> waitedFrame;
        bool waitFailed = false;
        std::thread waiter([&] {
            try {
                waitedFrame = waitStream.nextFrame(5000);
            } catch (...) {
                waitFailed = true;
            }
        });

        const auto waitDeadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!waitEntered() && std::chrono::steady_clock::now() < waitDeadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (!waitEntered()) {
            waitStream.stop();
            waiter.join();
            std::fprintf(stderr, "wait cancellation: mock waiter was not entered\\n");
            dlclose(mockLibrary);
            return 28;
        }

        waitStream.stop();
        waiter.join();
        if (waitFailed || waitedFrame.has_value() || waitStream) {
            std::fprintf(stderr, "wait cancellation: failed=%d frame=%d stream=%d\\n",
                waitFailed ? 1 : 0, waitedFrame.has_value() ? 1 : 0,
                static_cast<bool>(waitStream) ? 1 : 0);
            return 29;
        }
        waitDevice.close();

        // If a Producer rejects GCUnregisterEvent, EventKill must still
        // release the one permitted waiter. The native context is quarantined
        // because its normal event/stream teardown did not complete.
        auto fallbackProducer = camera::gentl::Producer::open(bridge, ctiPath);
        const auto fallbackDevices = fallbackProducer.discover();
        if (fallbackDevices.size() != 1) {
            dlclose(mockLibrary);
            return 30;
        }
        auto fallbackDevice = fallbackProducer.openDevice(fallbackDevices.front());
        auto fallbackStream = fallbackDevice.openDataStream("stream0", 3);
        fallbackStream.start();

        std::optional<camera::gentl::DataStreamFrame> fallbackFrame;
        bool fallbackWaitFailed = false;
        std::thread fallbackWaiter([&] {
            try {
                fallbackFrame = fallbackStream.nextFrame(5000);
            } catch (...) {
                fallbackWaitFailed = true;
            }
        });

        const auto fallbackDeadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!waitEntered() &&
               std::chrono::steady_clock::now() < fallbackDeadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (!waitEntered()) {
            fallbackStream.stop();
            fallbackWaiter.join();
            dlclose(mockLibrary);
            return 31;
        }

        if (setenv("MOCK_GENTL_FAIL_EVENT_UNREGISTER", "1", 1) != 0) {
            fallbackStream.stop();
            fallbackWaiter.join();
            dlclose(mockLibrary);
            return 32;
        }
        fallbackStream.stop();
        unsetenv("MOCK_GENTL_FAIL_EVENT_UNREGISTER");
        fallbackWaiter.join();
        dlclose(mockLibrary);
        if (fallbackWaitFailed || fallbackFrame.has_value() || fallbackStream)
            return 33;
        fallbackDevice.close();

        // A failed DSClose quarantines the native context. Its Consumer and
        // dynamically loaded C ABI bridge must outlive all public wrappers.
        std::weak_ptr<camera::gentl::ConsumerBridge> retainedBridge;
        auto quarantineBridge = camera::gentl::ConsumerBridge::load(argv[1]);
        retainedBridge = quarantineBridge;
        {
            auto quarantineProducer =
                camera::gentl::Producer::open(quarantineBridge, ctiPath);
            const auto quarantineDevices = quarantineProducer.discover();
            if (quarantineDevices.size() != 1) return 22;
            auto quarantineDevice =
                quarantineProducer.openDevice(quarantineDevices.front());
            auto quarantineStream =
                quarantineDevice.openDataStream("stream0", 3);
            quarantineStream.start();
            if (setenv("MOCK_GENTL_FAIL_DS_CLOSE", "1", 1) != 0) return 23;
            quarantineStream.stop();
            unsetenv("MOCK_GENTL_FAIL_DS_CLOSE");
            quarantineDevice.close();
        }
        quarantineBridge.reset();
        if (retainedBridge.expired()) return 24;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "ProducerTests exception: %s\\n", error.what());
        result = 99;
    } catch (...) {
        std::fprintf(stderr, "ProducerTests unknown exception\\n");
        result = 99;
    }

    std::filesystem::remove_all(directory);
    return result;
}


int main(int argc, char** argv) {
    const int result = runProducerTests(argc, argv);
    if (result != 0)
        std::fprintf(stderr, "ProducerTests failed with code %d\\n", result);
    return result;
}
