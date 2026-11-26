#include "camera/gentl/GenTLFrameAdapter.h"
#include "camera/gentl/ProducerCatalog.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace {

class EnvironmentRestore final {
public:
    explicit EnvironmentRestore(const char* name)
        : name_(name) {
        if (const char* value = std::getenv(name_.c_str()))
            previous_ = value;
    }

    ~EnvironmentRestore() {
        if (previous_) setenv(name_.c_str(), previous_->c_str(), 1);
        else unsetenv(name_.c_str());
    }

private:
    std::string name_;
    std::optional<std::string> previous_;
};

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 1;

    const auto root =
        std::filesystem::temp_directory_path() / "camera-gentl-catalog-tests";
    std::filesystem::remove_all(root);

    try {
        const auto firstDirectory = root / "first";
        const auto secondDirectory = root / "second";
        const auto nestedDirectory = firstDirectory / "nested";
        std::filesystem::create_directories(nestedDirectory);
        std::filesystem::create_directories(secondDirectory);

        const auto firstCti = firstDirectory / "first.cti";
        const auto secondCti = secondDirectory / "second.CTI";
        const auto brokenCti = firstDirectory / "broken.cti";

        std::filesystem::copy_file(argv[1], firstCti);
        std::filesystem::copy_file(argv[1], secondCti);
        {
            std::ofstream invalid(brokenCti);
            invalid << "not a loadable CTI library";
        }
        {
            std::ofstream ignored(firstDirectory / "not-a-producer.txt");
            ignored << "ignored";
        }
        std::filesystem::copy_file(
            argv[1], nestedDirectory / "nested.cti");

        EnvironmentRestore restore("GENICAM_GENTL64_PATH");
        if (setenv("GENICAM_GENTL64_PATH", "", 1) != 0) return 2;
        if (!camera::gentl::ProducerCatalog::discoverProducerPaths().empty())
            return 3;

        const auto searchPath =
            (root / "missing").string() + ":" + firstDirectory.string() + ":" +
            secondDirectory.string() + ":" + firstDirectory.string();
        if (setenv("GENICAM_GENTL64_PATH", searchPath.c_str(), 1) != 0)
            return 4;

        const auto paths =
            camera::gentl::ProducerCatalog::discoverProducerPaths();
        if (paths.size() != 3 ||
            std::filesystem::canonical(firstCti) != paths[1] ||
            std::filesystem::canonical(secondCti) != paths[2]) {
            // The first directory is lexically sorted: broken.cti precedes
            // first.cti. The second directory contributes second.CTI.
            return 5;
        }

        auto report =
            camera::gentl::ProducerCatalog::discoverDevices(5000);
        if (report.producerPaths != paths) return 6;
        if (report.failures.size() != 1 ||
            report.failures.front().producerPath !=
                std::filesystem::canonical(brokenCti)) return 7;
        if (report.producers.size() != 2) return 8;

        std::set<std::string> discoveredProducers;
        std::size_t discoveredDeviceCount = 0;
        for (const auto& entry : report.producers) {
            if (!entry.producer || entry.devices.size() != 2) return 9;
            for (const auto& device : entry.devices) {
                if (device.producerPath != entry.producer.ctiPath() ||
                    device.interfaceId.empty() || device.deviceId.empty()) return 10;
                discoveredProducers.insert(device.producerPath);
                ++discoveredDeviceCount;
            }
        }
        if (discoveredDeviceCount != 4 || discoveredProducers.size() != 2 ||
            discoveredProducers.count(std::filesystem::canonical(firstCti).string()) == 0 ||
            discoveredProducers.count(std::filesystem::canonical(secondCti).string()) == 0) {
            return 11;
        }

        // Catalog descriptors remain usable because their owning Producer
        // retains the exact interface handles used during discovery.
        auto& selectedProducer = report.producers.front().producer;
        auto selectedDevice = selectedProducer.openDevice(
            report.producers.front().devices.front());
        const auto streamIds = selectedDevice.dataStreamIds();
        if (!selectedDevice || streamIds.empty()) return 12;

        auto stream = selectedDevice.openDataStream(streamIds.front(), 3);
        stream.start();
        auto frame = stream.nextFrame(5000);
        if (!frame || frame->incomplete || frame->payload.empty()) return 13;
        const auto common = camera::gentl::makeCommonFrame(std::move(*frame));
        if (!common || !*common || (*common)->pixels.empty()) return 14;
        stream.stop();
        selectedDevice.close();
    } catch (...) {
        std::filesystem::remove_all(root);
        return 15;
    }

    std::filesystem::remove_all(root);
    return 0;
}
