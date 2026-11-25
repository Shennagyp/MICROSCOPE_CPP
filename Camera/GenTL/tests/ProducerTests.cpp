#include "camera/gentl/Producer.h"

#include <filesystem>
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

        auto device = producer.openDevice(descriptor);
        if (!device || device.deviceId() != "device0") return 6;

        const auto streams = device.dataStreamIds();
        if (streams.size() != 1 || streams.front() != "stream0") return 7;

        const auto xmlBytes = device.genApiXml();
        const std::string xml(xmlBytes.begin(), xmlBytes.end());
        if (xml != "<RegisterDescription/>") return 8;

        const auto bytes = device.readPort(0x10203040, 4);
        if (bytes != std::vector<std::uint8_t>{0xA0, 0xA1, 0xA2, 0xA3}) return 9;

        device.writePort(0x10203040, {0x10, 0x20, 0x30, 0x40});
        if (device.portUrl() != "Mock://DevicePort") return 10;

        auto foreignDescriptor = descriptor;
        foreignDescriptor.producerPath += ".foreign";
        bool rejectedForeign = false;
        try {
            auto invalid = producer.openDevice(foreignDescriptor);
            (void)invalid;
        } catch (const std::invalid_argument&) {
            rejectedForeign = true;
        }
        if (!rejectedForeign) return 11;

        device.close();
        if (device) return 12;
    } catch (...) {
        result = 13;
    }

    std::filesystem::remove_all(directory);
    return result;
}
