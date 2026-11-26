#include "camera/gentl/Producer.h"
#include "camera/gentl/GenTLFrameAdapter.h"

#include <charconv>
#include <exception>
#include <iostream>
#include <string>
#include <stdexcept>

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) {
        std::cerr
            << "Usage: camera_gentl_simulator_smoke "
            << "<native-test-bridge-library> <vendor-producer.cti> "
            << "[device-index]\n";
        return 64;
    }

    std::size_t selectedDeviceIndex = 0;
    if (argc == 4) {
        const std::string indexText = argv[3];
        const auto result = std::from_chars(
            indexText.data(), indexText.data() + indexText.size(),
            selectedDeviceIndex);
        if (result.ec != std::errc{} ||
            result.ptr != indexText.data() + indexText.size()) {
            std::cerr << "The device index must be a non-negative integer.\n";
            return 65;
        }
    }

    try {
        const auto bridge =
            camera::gentl::ConsumerBridge::load(argv[1]);
        auto producer = camera::gentl::Producer::open(bridge, argv[2]);

        std::cout << "CTI: " << producer.ctiPath() << '\n';
        const auto devices = producer.discover(5000);
        if (devices.empty()) {
            std::cerr << "The CTI Producer discovered no devices.\n";
            return 2;
        }
        if (selectedDeviceIndex >= devices.size()) {
            std::cerr << "Requested device index " << selectedDeviceIndex
                      << " is outside the discovered device range [0, "
                      << (devices.size() - 1) << "].\n";
            return 66;
        }

        const auto& descriptor = devices[selectedDeviceIndex];
        std::cout << "Discovered " << devices.size()
                  << " device(s); testing device index "
                  << selectedDeviceIndex << ":\n"
                  << "  interface: " << descriptor.interfaceId << '\n'
                  << "  device ID: " << descriptor.deviceId << '\n'
                  << "  display:   " << descriptor.displayName << '\n'
                  << "  vendor:    " << descriptor.vendor << '\n'
                  << "  model:     " << descriptor.model << '\n'
                  << "  serial:    " << descriptor.serialNumber << '\n';

        auto device = producer.openDevice(descriptor);
        const auto xml = device.genApiXml();
        if (xml.empty()) {
            std::cerr << "The device returned an empty GenApi XML document.\n";
            return 3;
        }
        std::cout << "GenApi XML bytes: " << xml.size() << '\n';

        const auto streamIds = device.dataStreamIds();
        if (streamIds.empty()) {
            std::cerr << "The device exposes no data streams.\n";
            return 4;
        }

        {
            auto stream = device.openDataStream(streamIds.front(), 4);
            stream.start();

            constexpr std::size_t requiredFrames = 5;
            std::uint64_t previousFrameId = 0;
            for (std::size_t index = 0; index < requiredFrames; ++index) {
                const auto frame = stream.nextFrame(5000);
                if (!frame) {
                    throw std::runtime_error(
                        "Timed out waiting for simulated frame " +
                        std::to_string(index + 1) + ".");
                }
                if (frame->incomplete) {
                    throw std::runtime_error(
                        "The Producer returned an incomplete frame.");
                }
                if (frame->payload.size() != 16) {
                    throw std::runtime_error(
                        "The deterministic simulator returned an unexpected payload size.");
                }
                if (!frame->frameId || !frame->timestampNanoseconds ||
                    *frame->frameId != index + 1 ||
                    (index > 0 && *frame->frameId != previousFrameId + 1)) {
                    throw std::runtime_error(
                        "Frame IDs or timestamps were not reported consistently.");
                }
                if (frame->payload.front() != static_cast<std::uint8_t>(0x30 + index)) {
                    throw std::runtime_error(
                        "Frame payload did not advance with the simulated frame sequence.");
                }
                previousFrameId = *frame->frameId;
                if (frame->imagePresent && !*frame->imagePresent) {
                    throw std::runtime_error(
                        "The completed buffer does not contain an image.");
                }

                const auto commonFrame =
                    camera::gentl::makeCommonFrame(*frame);

                std::cout << "Frame " << (index + 1)
                          << ": bytes=" << frame->payload.size()
                          << " common-projection="
                          << (commonFrame ? "mapped" : "opaque-or-unsupported");
                if (frame->width) std::cout << " width=" << *frame->width;
                if (frame->height) std::cout << " height=" << *frame->height;
                if (frame->pixelFormat) {
                    std::cout << " pixel-format=0x" << std::hex
                              << *frame->pixelFormat << std::dec;
                }
                if (frame->frameId) {
                    std::cout << " frame-id=" << *frame->frameId;
                }
                std::cout << '\n';
            }

            stream.stop();
        }

        device.close();
        std::cout << "Simulator GenTL smoke passed: discovery, device open, "
                     "GenApi XML, five complete image buffers, stream stop "
                     "and device close.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Simulator GenTL smoke failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
