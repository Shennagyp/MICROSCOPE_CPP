#pragma once

#include "camera/gentl/Producer.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace camera::gentl {

struct ProducerCatalogFailure {
    std::filesystem::path producerPath;
    std::string message;
};

// A catalog entry retains the Producer instance that performed discovery.
// Its DeviceDescriptor values can therefore be passed directly to openDevice;
// the retained interface handles are not detached from their owner.
struct ProducerCatalogEntry {
    Producer producer;
    std::vector<DeviceDescriptor> devices;
};

// A scan is partial-success by design: one incompatible or damaged CTI must
// not hide devices exposed by other installed Producers.
struct ProducerCatalogReport {
    std::vector<std::filesystem::path> producerPaths;
    std::vector<ProducerCatalogEntry> producers;
    std::vector<ProducerCatalogFailure> failures;
};

class ProducerCatalog final {
public:
    // Reads GENICAM_GENTL64_PATH, whose UNIX entries are colon-separated
    // directories containing 64-bit .cti Producer libraries. Paths are
    // canonicalized, deduplicated, and returned in deterministic search order.
    static std::vector<std::filesystem::path> discoverProducerPaths();

    // Opens each discovered CTI independently, enumerates its devices, then
    // closes the Producer. DeviceDescriptor retains producerPath so callers
    // can reopen the correct Producer before opening a device.
    static ProducerCatalogReport discoverDevices(
        std::uint64_t timeoutMilliseconds = 2000);
};

} // namespace camera::gentl
