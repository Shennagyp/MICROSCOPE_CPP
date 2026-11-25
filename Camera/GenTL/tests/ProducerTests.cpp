#include "camera/gentl/Producer.h"

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
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

        auto device = producer.openDevice(descriptor);
        if (!device || device.deviceId() != "device0") return 7;
        producer = camera::gentl::Producer{}; // The session retains producer state.

        const auto streams = device.dataStreamIds();
        if (streams.size() != 1 || streams.front() != "stream0") return 8;

        const auto xmlBytes = device.genApiXml();
        const std::string xml(xmlBytes.begin(), xmlBytes.end());
        if (xml != "<RegisterDescription/>") return 9;

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
            frame->width != 4 || frame->height != 4 ||
            frame->imagePresent != true || frame->incomplete) return 14;

        // The data-stream lease keeps the device handle alive after its
        // DeviceSession owner releases its reference.
        device.close();
        if (device) return 15;
        const auto frameAfterDeviceOwnerClose = stream.nextFrame(100);
        if (!frameAfterDeviceOwnerClose ||
            frameAfterDeviceOwnerClose->frameId != 42) return 16;

        stream.stop();
        if (stream) return 17;

        // A failed DSClose quarantines the native context. Its Consumer and
        // dynamically loaded C ABI bridge must outlive all public wrappers.
        std::weak_ptr<camera::gentl::ConsumerBridge> retainedBridge;
        auto quarantineBridge = camera::gentl::ConsumerBridge::load(argv[1]);
        retainedBridge = quarantineBridge;
        {
            auto quarantineProducer =
                camera::gentl::Producer::open(quarantineBridge, ctiPath);
            const auto quarantineDevices = quarantineProducer.discover();
            if (quarantineDevices.size() != 1) return 18;
            auto quarantineDevice =
                quarantineProducer.openDevice(quarantineDevices.front());
            auto quarantineStream =
                quarantineDevice.openDataStream("stream0", 3);
            quarantineStream.start();
            if (setenv("MOCK_GENTL_FAIL_DS_CLOSE", "1", 1) != 0) return 19;
            quarantineStream.stop();
            unsetenv("MOCK_GENTL_FAIL_DS_CLOSE");
            quarantineDevice.close();
        }
        quarantineBridge.reset();
        if (retainedBridge.expired()) return 20;
    } catch (...) {
        result = 13;
    }

    std::filesystem::remove_all(directory);
    return result;
}
