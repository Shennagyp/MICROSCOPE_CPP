#include "camera/gentl/ProducerCatalog.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <string>

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
        const auto searchPath =
            firstDirectory.string() + ":" + secondDirectory.string() + ":" +
            firstDirectory.string() + ":" + (root / "missing").string();
        if (setenv("GENICAM_GENTL64_PATH", searchPath.c_str(), 1) != 0)
            return 2;

        const auto paths =
            camera::gentl::ProducerCatalog::discoverProducerPaths();
        if (paths.size() != 3 ||
            std::filesystem::canonical(firstCti) != paths[1] ||
            std::filesystem::canonical(secondCti) != paths[2]) {
            // The first directory is lexically sorted: broken.cti precedes
            // first.cti. The second directory contributes second.CTI.
            return 3;
        }

        const auto report =
            camera::gentl::ProducerCatalog::discoverDevices(5000);
        if (report.producerPaths != paths) return 4;
        if (report.failures.size() != 1 ||
            report.failures.front().producerPath !=
                std::filesystem::canonical(brokenCti)) return 5;
        if (report.devices.size() != 4) return 6;

        std::set<std::string> discoveredProducers;
        for (const auto& device : report.devices) {
            if (device.producerPath.empty() ||
                device.interfaceId.empty() ||
                device.deviceId.empty()) return 7;
            discoveredProducers.insert(device.producerPath);
        }
        if (discoveredProducers.size() != 2 ||
            !discoveredProducers.contains(std::filesystem::canonical(firstCti).string()) ||
            !discoveredProducers.contains(std::filesystem::canonical(secondCti).string())) {
            return 8;
        }
    } catch (...) {
        std::filesystem::remove_all(root);
        return 9;
    }

    std::filesystem::remove_all(root);
    return 0;
}
