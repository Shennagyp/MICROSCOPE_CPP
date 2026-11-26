#pragma once

#include "camera/CameraModels.h"

#include <algorithm>
#include <cmath>
#include <cctype>
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


namespace detail {

inline std::string normalizeCameraModelText(std::string value) {
    const auto first = std::find_if_not(
        value.begin(), value.end(),
        [](unsigned char ch) { return std::isspace(ch) != 0; });
    const auto last = std::find_if_not(
        value.rbegin(), value.rend(),
        [](unsigned char ch) { return std::isspace(ch) != 0; }).base();
    if (first >= last) {
        return {};
    }

    std::string normalized(first, last);
    std::transform(
        normalized.begin(), normalized.end(), normalized.begin(),
        [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
    return normalized;
}

inline bool validSensorCharacteristics(
    const CameraSensorCharacteristics& sensor) noexcept {
    const auto positiveFinite = [](const std::optional<double>& value) {
        return !value || (std::isfinite(*value) && *value > 0.0);
    };

    if (!positiveFinite(sensor.pixelPitchXMicrometers) ||
        !positiveFinite(sensor.pixelPitchYMicrometers) ||
        !positiveFinite(sensor.readNoiseElectronsRms) ||
        !positiveFinite(sensor.darkCurrentElectronsPerPixelPerSecond) ||
        !positiveFinite(sensor.fullWellCapacityElectrons) ||
        !positiveFinite(sensor.maximumFrameRateFramesPerSecond)) {
        return false;
    }

    double previousWavelength = 0.0;
    for (const auto& sample : sensor.quantumEfficiency) {
        if (!std::isfinite(sample.wavelengthNanometers) ||
            sample.wavelengthNanometers <= previousWavelength ||
            !std::isfinite(sample.quantumEfficiency) ||
            sample.quantumEfficiency < 0.0 ||
            sample.quantumEfficiency > 1.0) {
            return false;
        }
        previousWavelength = sample.wavelengthNanometers;
    }

    if (sensor.activeSensorDimensions &&
        (sensor.activeSensorDimensions->width == 0 ||
         sensor.activeSensorDimensions->height == 0)) {
        return false;
    }
    return true;
}

inline bool sameCameraModelKey(
    const CameraModelKey& left,
    const CameraModelKey& right) {
    return normalizeCameraModelText(left.manufacturer) ==
               normalizeCameraModelText(right.manufacturer) &&
           normalizeCameraModelText(left.model) ==
               normalizeCameraModelText(right.model) &&
           normalizeCameraModelText(left.variant.value_or("")) ==
               normalizeCameraModelText(right.variant.value_or(""));
}

} // namespace detail

class CameraModelProfile final {
public:
    [[nodiscard]] static std::optional<CameraModelProfile> create(
        CameraModelKey key,
        CameraCapabilities capabilities,
        CameraSensorCharacteristics sensorCharacteristics) {
        if (detail::normalizeCameraModelText(key.manufacturer).empty() ||
            detail::normalizeCameraModelText(key.model).empty() ||
            (key.variant && detail::normalizeCameraModelText(*key.variant).empty()) ||
            !detail::validSensorCharacteristics(sensorCharacteristics)) {
            return std::nullopt;
        }
        return CameraModelProfile(
            std::move(key), capabilities, std::move(sensorCharacteristics));
    }

    [[nodiscard]] const CameraModelKey& key() const noexcept;
    [[nodiscard]] const CameraCapabilities& capabilities() const noexcept;
    [[nodiscard]] const CameraSensorCharacteristics&
    sensorCharacteristics() const noexcept;

private:
    CameraModelProfile(
        CameraModelKey key,
        CameraCapabilities capabilities,
        CameraSensorCharacteristics sensorCharacteristics)
        : key_(std::move(key)),
          capabilities_(capabilities),
          sensorCharacteristics_(std::move(sensorCharacteristics)) {}

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
        std::vector<CameraModelProfile> profiles) {
        for (std::size_t left = 0; left < profiles.size(); ++left) {
            for (std::size_t right = left + 1; right < profiles.size(); ++right) {
                if (detail::sameCameraModelKey(
                        profiles[left].key(), profiles[right].key())) {
                    return std::nullopt;
                }
            }
        }
        return CameraModelCatalog(std::move(profiles));
    }

    [[nodiscard]] const std::vector<CameraModelProfile>& profiles() const noexcept {
        return profiles_;
    }

    // Exact manufacturer/model/variant matching only. No fuzzy match and no
    // inference from serial number or opaque runtime enumeration ID.
    [[nodiscard]] const CameraModelProfile* findExact(
        const CameraIdentity& identity) const noexcept {
        if (!identity.manufacturer || !identity.model) {
            return nullptr;
        }

        const CameraModelKey query{
            *identity.manufacturer,
            *identity.model,
            identity.modelVariant};
        for (const auto& profile : profiles_) {
            if (detail::sameCameraModelKey(profile.key(), query)) {
                return &profile;
            }
        }
        return nullptr;
    }

private:
    explicit CameraModelCatalog(std::vector<CameraModelProfile> profiles)
        : profiles_(std::move(profiles)) {}

    std::vector<CameraModelProfile> profiles_;
};

} // namespace camera
