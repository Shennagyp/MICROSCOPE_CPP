#pragma once

#include "camera/CameraModels.h"

#include <optional>
#include <string>
#include <vector>

namespace camera {

// Stable camera-model identity. It excludes the runtime enumeration ID and
// serial number, which identify a connected unit rather than a reusable model.
struct CameraModelKey {
    std::string manufacturer;
    std::string model;
    std::optional<std::string> variant;
};

class CameraModelProfile final {
public:
    [[nodiscard]] static std::optional<CameraModelProfile> create(
        CameraModelKey key,
        CameraCapabilities capabilities,
        CameraSensorCharacteristics sensorCharacteristics);

    [[nodiscard]] const CameraModelKey& key() const noexcept;
    [[nodiscard]] const CameraCapabilities& capabilities() const noexcept;
    [[nodiscard]] const CameraSensorCharacteristics&
    sensorCharacteristics() const noexcept;

private:
    CameraModelProfile(
        CameraModelKey key,
        CameraCapabilities capabilities,
        CameraSensorCharacteristics sensorCharacteristics);

    CameraModelKey key_;
    CameraCapabilities capabilities_;
    CameraSensorCharacteristics sensorCharacteristics_;
};

// In-memory typed representation of the persistable camera model catalog.
// Application persistence owns serialization, revisions and transactions;
// this value type does not access storage.
class CameraModelCatalog final {
public:
    [[nodiscard]] static std::optional<CameraModelCatalog> create(
        std::vector<CameraModelProfile> profiles);

    [[nodiscard]] const std::vector<CameraModelProfile>& profiles() const noexcept;

    // Exact manufacturer/model/variant matching only. No fuzzy match and no
    // inference from serial number or opaque runtime enumeration ID.
    [[nodiscard]] const CameraModelProfile* findExact(
        const CameraIdentity& identity) const noexcept;

private:
    explicit CameraModelCatalog(std::vector<CameraModelProfile> profiles);

    std::vector<CameraModelProfile> profiles_;
};

} // namespace camera
