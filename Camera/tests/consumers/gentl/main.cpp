#include <camera/gentl/GenTLFrameAdapter.h>
#include <camera/gentl/ProducerCatalog.h>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: camera_module_gentl_consumer <producer.cti>\n";
        return 64;
    }

    const auto temporaryDirectory =
        std::filesystem::temp_directory_path() /
        "camera-module-relocated-gentl-consumer";
    std::filesystem::remove_all(temporaryDirectory);

    const char* previousSearchPath = std::getenv("GENICAM_GENTL64_PATH");
    const std::optional<std::string> savedSearchPath = previousSearchPath
        ? std::optional<std::string>{previousSearchPath}
        : std::nullopt;

    try {
        std::filesystem::create_directories(temporaryDirectory);
        const auto simulatorPath = temporaryDirectory / "external-simulator.cti";
        std::filesystem::copy_file(
            std::filesystem::canonical(argv[1]), simulatorPath);

        if (setenv("GENICAM_GENTL64_PATH",
                   temporaryDirectory.c_str(), 1) != 0) {
            throw std::runtime_error(
                "Could not configure the GenTL Producer search path.");
        }

        {
        // This executable is built against the installed and relocated
        // CameraModule package, not the source-tree target.
        auto catalog = camera::gentl::ProducerCatalog::discoverDevices(5000);
        if (catalog.producerPaths.size() != 1 ||
            catalog.producers.size() != 1 ||
            !catalog.failures.empty()) {
            throw std::runtime_error(
                "The relocated ProducerCatalog did not load exactly one CTI.");
        }

        auto& entry = catalog.producers.front();
        if (entry.producer.ctiPath() !=
                std::filesystem::canonical(simulatorPath).string() ||
            entry.devices.size() < 2) {
            throw std::runtime_error(
                "The relocated catalog did not discover both simulated cameras.");
        }

        auto device = entry.producer.openDevice(entry.devices.front());
        const auto streamIds = device.dataStreamIds();
        if (streamIds.empty()) {
            throw std::runtime_error(
                "The installed consumer found no data stream.");
        }

        auto stream = device.openDataStream(streamIds.front(), 3);
        stream.start();
        auto frame = stream.nextFrame(5000);
        if (!frame || frame->incomplete || frame->payload.empty()) {
            throw std::runtime_error(
                "The installed consumer did not receive a complete frame.");
        }

        auto commonFrame =
            camera::gentl::makeCommonFrame(std::move(*frame));
        if (!commonFrame || !*commonFrame ||
            (*commonFrame)->width == 0 || (*commonFrame)->height == 0 ||
            (*commonFrame)->pixels.empty()) {
            throw std::runtime_error(
                "The installed consumer could not project the frame into Camera.Common.");
        }

        stream.stop();
        device.close();
        std::cout << "Relocated CameraModule GenTL consumer passed: "
                  << catalog.producerPaths.size() << " CTI discovered; "
                  << entry.devices.size() << " devices enumerated; "
                  << (*commonFrame)->width << 'x' << (*commonFrame)->height
                  << " frame delivered and projected.\n";

        }
        if (savedSearchPath)
            setenv("GENICAM_GENTL64_PATH", savedSearchPath->c_str(), 1);
        else
            unsetenv("GENICAM_GENTL64_PATH");
        std::filesystem::remove_all(temporaryDirectory);
        return 0;
    } catch (const std::exception& error) {
        if (savedSearchPath)
            setenv("GENICAM_GENTL64_PATH", savedSearchPath->c_str(), 1);
        else
            unsetenv("GENICAM_GENTL64_PATH");
        std::filesystem::remove_all(temporaryDirectory);
        std::cerr << "CameraModule GenTL consumer failed: "
                  << error.what() << '\n';
        return 1;
    }
}
