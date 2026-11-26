#pragma once

#include "camera/CameraModels.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
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
    const auto knownTechnology = [](CameraSensorTechnology value) {
        switch (value) {
        case CameraSensorTechnology::CCD:
        case CameraSensorTechnology::CMOS:
        case CameraSensorTechnology::ScientificCMOS:
        case CameraSensorTechnology::Unknown:
            return true;
        }
        return false;
    };
    const auto knownShutter = [](CameraShutterType value) {
        switch (value) {
        case CameraShutterType::Global:
        case CameraShutterType::Rolling:
        case CameraShutterType::FrameTransfer:
        case CameraShutterType::GlobalReset:
        case CameraShutterType::Unknown:
            return true;
        }
        return false;
    };
    const auto knownColorMode = [](CameraColorMode value) {
        switch (value) {
        case CameraColorMode::Monochrome:
        case CameraColorMode::Color:
        case CameraColorMode::Unknown:
            return true;
        }
        return false;
    };
    switch (sensor.provenance) {
    case CameraSpecificationProvenance::ManufacturerDatasheet:
    case CameraSpecificationProvenance::VendorModelDescriptor:
    case CameraSpecificationProvenance::UserVerified:
    case CameraSpecificationProvenance::Measured:
    case CameraSpecificationProvenance::Unknown:
        break;
    default:
        return false;
    }

    if ((sensor.technology && !knownTechnology(*sensor.technology)) ||
        (sensor.shutterType && !knownShutter(*sensor.shutterType)) ||
        (sensor.colorMode && !knownColorMode(*sensor.colorMode))) {
        return false;
    }

    const auto positiveFinite = [](const std::optional<double>& value) {
        return !value || (std::isfinite(*value) && *value > 0.0);
    };
    const auto nonNegativeFinite = [](const std::optional<double>& value) {
        return !value || (std::isfinite(*value) && *value >= 0.0);
    };

    if (!positiveFinite(sensor.pixelPitchXMicrometers) ||
        !positiveFinite(sensor.pixelPitchYMicrometers) ||
        !nonNegativeFinite(sensor.readNoiseElectronsRms) ||
        !nonNegativeFinite(sensor.darkCurrentElectronsPerPixelPerSecond) ||
        !positiveFinite(sensor.fullWellCapacityElectrons) ||
        !positiveFinite(sensor.maximumFrameRateFramesPerSecond) ||
        (sensor.provenanceReference &&
         sensor.provenanceReference->empty())) {
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
        CameraSensorCharacteristics sensorCharacteristics,
        std::uint64_t profileRevision = 1,
        std::string sourceReference = {});

    [[nodiscard]] const CameraModelKey& key() const noexcept {
        return key_;
    }
    [[nodiscard]] const CameraCapabilities& capabilities() const noexcept {
        return capabilities_;
    }
    [[nodiscard]] const CameraSensorCharacteristics&
    sensorCharacteristics() const noexcept {
        return sensorCharacteristics_;
    }
    [[nodiscard]] std::uint64_t profileRevision() const noexcept {
        return profileRevision_;
    }
    [[nodiscard]] const std::string& sourceReference() const noexcept {
        return sourceReference_;
    }

private:
    CameraModelProfile(
        CameraModelKey key,
        CameraCapabilities capabilities,
        CameraSensorCharacteristics sensorCharacteristics,
        std::uint64_t profileRevision,
        std::string sourceReference)
        : key_(std::move(key)),
          capabilities_(capabilities),
          sensorCharacteristics_(std::move(sensorCharacteristics)),
          profileRevision_(profileRevision),
          sourceReference_(std::move(sourceReference)) {}

    CameraModelKey key_;
    CameraCapabilities capabilities_;
    CameraSensorCharacteristics sensorCharacteristics_;
    std::uint64_t profileRevision_;
    std::string sourceReference_;
};

// In-memory typed representation of the persistable camera model catalog.
// Application persistence owns serialization, revisions and transactions;
// this value type does not access storage.
class CameraModelCatalog final {
public:
    [[nodiscard]] static std::optional<CameraModelCatalog> create(
        std::vector<CameraModelProfile> profiles,
        std::uint64_t catalogRevision = 1);

    [[nodiscard]] const std::vector<CameraModelProfile>& profiles() const noexcept {
        return profiles_;
    }
    [[nodiscard]] std::uint64_t revision() const noexcept {
        return revision_;
    }

    // Candidate matching supports backends that disclose only a model
    // string. Missing manufacturer/variant broadens the result set; it never
    // silently selects one of several profiles.
    [[nodiscard]] std::vector<const CameraModelProfile*> findCandidates(
        const CameraIdentity& identity) const {
        std::vector<const CameraModelProfile*> candidates;
        if (!identity.model) {
            return candidates;
        }

        const auto model = detail::normalizeCameraModelText(*identity.model);
        const auto manufacturer = identity.manufacturer
            ? detail::normalizeCameraModelText(*identity.manufacturer)
            : std::string{};
        const auto variant = identity.modelVariant
            ? detail::normalizeCameraModelText(*identity.modelVariant)
            : std::string{};

        for (const auto& profile : profiles_) {
            const auto& key = profile.key();
            if (detail::normalizeCameraModelText(key.model) != model) {
                continue;
            }
            if (!manufacturer.empty() &&
                detail::normalizeCameraModelText(key.manufacturer) != manufacturer) {
                continue;
            }
            if (!variant.empty() &&
                detail::normalizeCameraModelText(key.variant.value_or("")) != variant) {
                continue;
            }
            candidates.push_back(&profile);
        }
        return candidates;
    }

    // Exact manufacturer/model/variant matching only.
    [[nodiscard]] const CameraModelProfile* findExact(
        const CameraIdentity& identity) const {
        if (!identity.manufacturer || !identity.model ||
            detail::normalizeCameraModelText(*identity.manufacturer).empty() ||
            detail::normalizeCameraModelText(*identity.model).empty()) {
            return nullptr;
        }

        const CameraModelKey query{
            *identity.manufacturer,
            *identity.model,
            identity.modelVariant};
        const bool hasVariantIdentity =
            identity.modelVariant &&
            !detail::normalizeCameraModelText(*identity.modelVariant).empty();
        if (hasVariantIdentity) {
            for (const auto& profile : profiles_) {
                if (detail::sameCameraModelKey(profile.key(), query)) {
                    return &profile;
                }
            }
            return nullptr;
        }

        // A generic (no-variant) profile is exact only when no more specific
        // variants exist for the same manufacturer/model. Otherwise the
        // catalog must leave the selection ambiguous.
        const auto candidates = findCandidates(identity);
        if (candidates.size() != 1 || candidates.front()->key().variant) {
            return nullptr;
        }
        return candidates.front();
    }

private:
    CameraModelCatalog(
        std::vector<CameraModelProfile> profiles,
        std::uint64_t revision)
        : profiles_(std::move(profiles)),
          revision_(revision) {}

    std::vector<CameraModelProfile> profiles_;
    std::uint64_t revision_;
};

inline std::optional<CameraModelProfile> CameraModelProfile::create(
    CameraModelKey key,
    CameraCapabilities capabilities,
    CameraSensorCharacteristics sensorCharacteristics,
    std::uint64_t profileRevision,
    std::string sourceReference) {
    if (!sourceReference.empty() &&
        sensorCharacteristics.provenanceReference &&
        *sensorCharacteristics.provenanceReference != sourceReference) {
        return std::nullopt;
    }
    if (!sourceReference.empty() &&
        !sensorCharacteristics.provenanceReference) {
        sensorCharacteristics.provenanceReference = sourceReference;
    }

    if (profileRevision == 0 ||
        key.manufacturer.find('\0') != std::string::npos ||
        key.model.find('\0') != std::string::npos ||
        (key.variant && key.variant->find('\0') != std::string::npos) ||
        sourceReference.find('\0') != std::string::npos ||
        (sensorCharacteristics.provenanceReference &&
         sensorCharacteristics.provenanceReference->find('\0') != std::string::npos) ||
        detail::normalizeCameraModelText(key.manufacturer).empty() ||
        detail::normalizeCameraModelText(key.model).empty() ||
        (key.variant && detail::normalizeCameraModelText(*key.variant).empty()) ||
        !detail::validSensorCharacteristics(sensorCharacteristics)) {
        return std::nullopt;
    }
    return CameraModelProfile(
        std::move(key), capabilities, std::move(sensorCharacteristics),
        profileRevision, std::move(sourceReference));
}

inline std::optional<CameraModelCatalog> CameraModelCatalog::create(
    std::vector<CameraModelProfile> profiles,
    std::uint64_t catalogRevision) {
    if (catalogRevision == 0) {
        return std::nullopt;
    }
    for (std::size_t left = 0; left < profiles.size(); ++left) {
        for (std::size_t right = left + 1; right < profiles.size(); ++right) {
            if (detail::sameCameraModelKey(
                    profiles[left].key(), profiles[right].key())) {
                return std::nullopt;
            }
        }
    }
    return CameraModelCatalog(std::move(profiles), catalogRevision);
}

} // namespace camera
