#include <camera/gentl/GenTLFrameAdapter.h>
#include <camera/gentl/Producer.h>

#include <exception>
#include <iostream>
#include <string>
#include <utility>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: camera_module_gentl_consumer <producer.cti>\n";
        return 64;
    }

    try {
        // This executable is built against the installed and relocated
        // CameraModule package, not the source-tree target.
        auto producer = camera::gentl::Producer::open(argv[1]);
        const auto devices = producer.discover(5000);
        if (devices.size() < 2) {
            std::cerr << "Expected both simulated GenTL devices to be discovered.\n";
            return 2;
        }

        auto device = producer.openDevice(devices.front());
        const auto streamIds = device.dataStreamIds();
        if (streamIds.empty()) {
            std::cerr << "The installed consumer found no data stream.\n";
            return 3;
        }

        auto stream = device.openDataStream(streamIds.front(), 3);
        stream.start();
        const auto frame = stream.nextFrame(5000);
        if (!frame || frame->incomplete || frame->payload.empty()) {
            std::cerr << "The installed consumer did not receive a complete frame.\n";
            return 4;
        }

        auto commonFrame =
            camera::gentl::makeCommonFrame(std::move(*frame));
        if (!commonFrame || !*commonFrame ||
            (*commonFrame)->width == 0 || (*commonFrame)->height == 0 ||
            (*commonFrame)->pixels.empty()) {
            std::cerr << "The installed consumer could not project the frame into Camera.Common.\n";
            return 5;
        }

        stream.stop();
        device.close();
        std::cout << "Relocated CameraModule GenTL consumer passed: "
                  << devices.size() << " devices discovered; native CTI opened; "
                  << (*commonFrame)->width << 'x' << (*commonFrame)->height
                  << " frame delivered and projected.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "CameraModule GenTL consumer failed: " << error.what() << '\n';
        return 1;
    }
}
