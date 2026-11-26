#include "camera/gentl/ProducerCatalog.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>
#include <system_error>

namespace camera::gentl {
namespace {

std::string lowerExtension(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
    return extension;
}

} // namespace

std::vector<std::filesystem::path>
ProducerCatalog::discoverProducerPaths() {
    std::vector<std::filesystem::path> result;
    std::set<std::string> seen;

    const char* configuredPaths = std::getenv("GENICAM_GENTL64_PATH");
    if (!configuredPaths || configuredPaths[0] == '\0') return result;

    const std::string searchPath(configuredPaths);
    std::size_t segmentStart = 0;

    while (segmentStart <= searchPath.size()) {
        const auto separator = searchPath.find(':', segmentStart);
        const auto segmentLength = separator == std::string::npos
            ? searchPath.size() - segmentStart
            : separator - segmentStart;

        if (segmentLength != 0) {
            const std::filesystem::path directory(
                searchPath.substr(segmentStart, segmentLength));
            std::error_code error;
            if (std::filesystem::is_directory(directory, error) && !error) {
                std::vector<std::filesystem::path> candidates;
                std::filesystem::directory_iterator iterator(directory, error);
                const std::filesystem::directory_iterator end;
                while (!error && iterator != end) {
                    const auto candidate = iterator->path();
                    std::error_code entryError;
                    if (lowerExtension(candidate) == ".cti" &&
                        std::filesystem::is_regular_file(candidate, entryError) &&
                        !entryError) {
                        candidates.push_back(candidate);
                    }
                    iterator.increment(error);
                }

                std::sort(candidates.begin(), candidates.end(),
                    [](const auto& left, const auto& right) {
                        return left.string() < right.string();
                    });

                for (const auto& candidate : candidates) {
                    std::error_code canonicalError;
                    auto canonical = std::filesystem::canonical(
                        candidate, canonicalError);
                    if (canonicalError) continue;
                    const auto identity = canonical.string();
                    if (seen.insert(identity).second)
                        result.push_back(std::move(canonical));
                }
            }
        }

        if (separator == std::string::npos) break;
        segmentStart = separator + 1;
    }

    return result;
}

ProducerCatalogReport ProducerCatalog::discoverDevices(
    std::uint64_t timeoutMilliseconds) {
    ProducerCatalogReport report;
    report.producerPaths = discoverProducerPaths();

    for (const auto& path : report.producerPaths) {
        try {
            auto producer = Producer::open(path);
            auto devices = producer.discover(timeoutMilliseconds);
            report.devices.insert(
                report.devices.end(),
                std::make_move_iterator(devices.begin()),
                std::make_move_iterator(devices.end()));
        } catch (const std::exception& exception) {
            report.failures.push_back({path, exception.what()});
        } catch (...) {
            report.failures.push_back({
                path, "Unknown error while loading or discovering the CTI Producer."});
        }
    }

    return report;
}

} // namespace camera::gentl
