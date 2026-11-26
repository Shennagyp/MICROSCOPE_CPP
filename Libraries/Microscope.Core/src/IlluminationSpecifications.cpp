#include "microscope/core/equipment/IlluminationSpecifications.h"

#include <cmath>
#include <set>
#include <utility>

namespace microscope::core {
namespace {

[[nodiscard]] bool isKnownIntensityUnit(
    IlluminationIntensityUnit unit) noexcept {
    switch (unit) {
    case IlluminationIntensityUnit::Percent:
    case IlluminationIntensityUnit::Milliamperes:
    case IlluminationIntensityUnit::Milliwatts:
    case IlluminationIntensityUnit::Relative:
    case IlluminationIntensityUnit::DeviceDefined:
        return true;
    }
    return false;
}

[[nodiscard]] bool isKnownControlKind(
    IlluminationControlKind kind) noexcept {
    switch (kind) {
    case IlluminationControlKind::Fixed:
    case IlluminationControlKind::Continuous:
    case IlluminationControlKind::DeviceDefined:
    case IlluminationControlKind::Unknown:
        return true;
    }
    return false;
}

[[nodiscard]] bool isKnownSourceTechnology(
    IlluminationSourceTechnology technology) noexcept {
    switch (technology) {
    case IlluminationSourceTechnology::Led:
    case IlluminationSourceTechnology::Halogen:
    case IlluminationSourceTechnology::XenonArc:
    case IlluminationSourceTechnology::MercuryArc:
    case IlluminationSourceTechnology::MetalHalideArc:
    case IlluminationSourceTechnology::Laser:
    case IlluminationSourceTechnology::Other:
    case IlluminationSourceTechnology::Unknown:
        return true;
    }
    return false;
}

} // namespace

SpectralBand::SpectralBand(
    PositiveNanometers minimumWavelength,
    PositiveNanometers maximumWavelength)
    : minimumWavelength_(minimumWavelength),
      maximumWavelength_(maximumWavelength) {}

Result<SpectralBand, SpectralBandError> SpectralBand::create(
    PositiveNanometers minimumWavelength,
    PositiveNanometers maximumWavelength) {
    if (minimumWavelength.value() > maximumWavelength.value()) {
        return Result<SpectralBand, SpectralBandError>::failure(
            SpectralBandError::InvertedRange);
    }

    return Result<SpectralBand, SpectralBandError>::success(
        SpectralBand(minimumWavelength, maximumWavelength));
}

double SpectralBand::minimumWavelengthNanometers() const noexcept {
    return minimumWavelength_.value();
}

double SpectralBand::maximumWavelengthNanometers() const noexcept {
    return maximumWavelength_.value();
}

SpectralPowerDistribution::SpectralPowerDistribution(
    std::vector<SpectralPowerSample> samples)
    : samples_(std::move(samples)) {}

Result<
    SpectralPowerDistribution,
    SpectralPowerDistributionError> SpectralPowerDistribution::create(
        std::vector<SpectralPowerSample> samples) {
    if (samples.size() < 2) {
        return Result<
            SpectralPowerDistribution,
            SpectralPowerDistributionError>::failure(
                SpectralPowerDistributionError::TooFewSamples);
    }

    double previousWavelength = 0.0;
    bool hasPositivePower = false;
    for (const auto& sample : samples) {
        if (!std::isfinite(sample.wavelengthNanometers) ||
            sample.wavelengthNanometers <= 0.0) {
            return Result<
                SpectralPowerDistribution,
                SpectralPowerDistributionError>::failure(
                    SpectralPowerDistributionError::InvalidWavelength);
        }
        if (sample.wavelengthNanometers <= previousWavelength) {
            return Result<
                SpectralPowerDistribution,
                SpectralPowerDistributionError>::failure(
                    SpectralPowerDistributionError::WavelengthsNotIncreasing);
        }
        if (!std::isfinite(sample.relativePower) || sample.relativePower < 0.0) {
            return Result<
                SpectralPowerDistribution,
                SpectralPowerDistributionError>::failure(
                    SpectralPowerDistributionError::InvalidPower);
        }
        hasPositivePower = hasPositivePower || sample.relativePower > 0.0;
        previousWavelength = sample.wavelengthNanometers;
    }

    if (!hasPositivePower) {
        return Result<
            SpectralPowerDistribution,
            SpectralPowerDistributionError>::failure(
                SpectralPowerDistributionError::NoPositivePower);
    }

    return Result<
        SpectralPowerDistribution,
        SpectralPowerDistributionError>::success(
            SpectralPowerDistribution(std::move(samples)));
}

const std::vector<SpectralPowerSample>&
SpectralPowerDistribution::samples() const noexcept {
    return samples_;
}

IlluminationIntensityRange::IlluminationIntensityRange(
    double minimum,
    double maximum,
    IlluminationIntensityUnit unit)
    : minimum_(minimum),
      maximum_(maximum),
      unit_(unit) {}

Result<
    IlluminationIntensityRange,
    IlluminationIntensityRangeError> IlluminationIntensityRange::create(
        double minimum,
        double maximum,
        IlluminationIntensityUnit unit) {
    if (!isKnownIntensityUnit(unit)) {
        return Result<
            IlluminationIntensityRange,
            IlluminationIntensityRangeError>::failure(
                IlluminationIntensityRangeError::InvalidUnit);
    }

    if (!std::isfinite(minimum) ||
        !std::isfinite(maximum) ||
        minimum < 0.0 ||
        maximum < 0.0) {
        return Result<
            IlluminationIntensityRange,
            IlluminationIntensityRangeError>::failure(
                IlluminationIntensityRangeError::InvalidValue);
    }

    if (maximum <= minimum) {
        return Result<
            IlluminationIntensityRange,
            IlluminationIntensityRangeError>::failure(
                IlluminationIntensityRangeError::NonIncreasingRange);
    }

    return Result<
        IlluminationIntensityRange,
        IlluminationIntensityRangeError>::success(
            IlluminationIntensityRange(minimum, maximum, unit));
}

double IlluminationIntensityRange::minimum() const noexcept {
    return minimum_;
}

double IlluminationIntensityRange::maximum() const noexcept {
    return maximum_;
}

IlluminationIntensityUnit IlluminationIntensityRange::unit() const noexcept {
    return unit_;
}

IlluminationChannel::IlluminationChannel(
    IlluminationChannelId id,
    std::optional<SpectralBand> spectralBand,
    IlluminationControlKind controlKind,
    std::optional<IlluminationIntensityRange> intensityRange,
    std::optional<SpectralPowerDistribution> spectralPowerDistribution)
    : id_(std::move(id)),
      spectralBand_(std::move(spectralBand)),
      controlKind_(controlKind),
      intensityRange_(std::move(intensityRange)),
      spectralPowerDistribution_(std::move(spectralPowerDistribution)) {}

Result<
    IlluminationChannel,
    IlluminationChannelError> IlluminationChannel::create(
        IlluminationChannelId id,
        std::optional<SpectralBand> spectralBand,
        IlluminationControlKind controlKind,
        std::optional<IlluminationIntensityRange> intensityRange,
        std::optional<SpectralPowerDistribution> spectralPowerDistribution) {
    if (!isKnownControlKind(controlKind)) {
        return Result<
            IlluminationChannel,
            IlluminationChannelError>::failure(
                IlluminationChannelError::InvalidControlKind);
    }

    if (controlKind == IlluminationControlKind::Continuous &&
        !intensityRange) {
        return Result<
            IlluminationChannel,
            IlluminationChannelError>::failure(
                IlluminationChannelError::MissingContinuousRange);
    }

    if (controlKind != IlluminationControlKind::Continuous &&
        intensityRange) {
        return Result<
            IlluminationChannel,
            IlluminationChannelError>::failure(
                IlluminationChannelError::UnexpectedContinuousRange);
    }

    if (spectralBand && spectralPowerDistribution) {
        for (const auto& sample : spectralPowerDistribution->samples()) {
            if (sample.wavelengthNanometers <
                    spectralBand->minimumWavelengthNanometers() ||
                sample.wavelengthNanometers >
                    spectralBand->maximumWavelengthNanometers()) {
                return Result<
                    IlluminationChannel,
                    IlluminationChannelError>::failure(
                        IlluminationChannelError::InvalidSpectralPowerDistribution);
            }
        }
    }

    return Result<
        IlluminationChannel,
        IlluminationChannelError>::success(
            IlluminationChannel(
                std::move(id),
                std::move(spectralBand),
                controlKind,
                std::move(intensityRange),
                std::move(spectralPowerDistribution)));
}

const IlluminationChannelId& IlluminationChannel::id() const noexcept {
    return id_;
}

const std::optional<SpectralBand>&
IlluminationChannel::spectralBand() const noexcept {
    return spectralBand_;
}

IlluminationControlKind IlluminationChannel::controlKind() const noexcept {
    return controlKind_;
}

const std::optional<IlluminationIntensityRange>&
IlluminationChannel::intensityRange() const noexcept {
    return intensityRange_;
}

const std::optional<SpectralPowerDistribution>&
IlluminationChannel::spectralPowerDistribution() const noexcept {
    return spectralPowerDistribution_;
}

IlluminationSpecification::IlluminationSpecification(
    EquipmentComponentId componentId,
    IlluminationSourceTechnology technology,
    std::vector<IlluminationChannel> channels)
    : componentId_(std::move(componentId)),
      technology_(technology),
      channels_(std::move(channels)) {}

Result<
    IlluminationSpecification,
    IlluminationSpecificationError> IlluminationSpecification::create(
        EquipmentComponentId componentId,
        IlluminationSourceTechnology technology,
        std::vector<IlluminationChannel> channels) {
    if (!isKnownSourceTechnology(technology)) {
        return Result<
            IlluminationSpecification,
            IlluminationSpecificationError>::failure(
                IlluminationSpecificationError::InvalidTechnology);
    }

    if (channels.empty()) {
        return Result<
            IlluminationSpecification,
            IlluminationSpecificationError>::failure(
                IlluminationSpecificationError::NoChannels);
    }

    std::set<IlluminationChannelId> channelIds;
    for (const auto& channel : channels) {
        if (!channelIds.insert(channel.id()).second) {
            return Result<
                IlluminationSpecification,
                IlluminationSpecificationError>::failure(
                    IlluminationSpecificationError::DuplicateChannelId);
        }
    }

    return Result<
        IlluminationSpecification,
        IlluminationSpecificationError>::success(
            IlluminationSpecification(
                std::move(componentId),
                technology,
                std::move(channels)));
}

const EquipmentComponentId& IlluminationSpecification::componentId() const noexcept {
    return componentId_;
}

IlluminationSourceTechnology
IlluminationSpecification::technology() const noexcept {
    return technology_;
}

const std::vector<IlluminationChannel>&
IlluminationSpecification::channels() const noexcept {
    return channels_;
}

} // namespace microscope::core
