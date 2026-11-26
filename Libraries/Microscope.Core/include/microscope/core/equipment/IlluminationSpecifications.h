#pragma once

#include "microscope/core/equipment/OpticalSpecifications.h"
#include "microscope/core/ids/Ids.h"
#include "microscope/core/result/Result.h"

#include <optional>
#include <string>
#include <vector>

namespace microscope::core {

enum class IlluminationSourceTechnology {
    Led,
    Halogen,
    XenonArc,
    MercuryArc,
    MetalHalideArc,
    Laser,
    Other,
    Unknown
};

enum class IlluminationIntensityUnit {
    Percent,
    Milliamperes,
    Milliwatts,
    Relative,
    DeviceDefined
};

enum class IlluminationControlKind {
    Fixed,
    Continuous,
    DeviceDefined,
    Unknown
};

enum class SpectralBandError {
    InvertedRange
};

class SpectralBand final {
public:
    [[nodiscard]] static Result<SpectralBand, SpectralBandError> create(
        PositiveNanometers minimumWavelength,
        PositiveNanometers maximumWavelength);

    [[nodiscard]] double minimumWavelengthNanometers() const noexcept;
    [[nodiscard]] double maximumWavelengthNanometers() const noexcept;

private:
    SpectralBand(
        PositiveNanometers minimumWavelength,
        PositiveNanometers maximumWavelength);

    PositiveNanometers minimumWavelength_;
    PositiveNanometers maximumWavelength_;
};

enum class SpectralPowerDistributionError {
    TooFewSamples,
    InvalidWavelength,
    InvalidPower,
    WavelengthsNotIncreasing,
    NoPositivePower
};

struct SpectralPowerSample {
    double wavelengthNanometers = 0.0;
    // Relative spectral radiant output. Absolute irradiance is established by
    // measured light-path calibration, not inferred from this normalized curve.
    double relativePower = 0.0;
};

class SpectralPowerDistribution final {
public:
    [[nodiscard]] static Result<
        SpectralPowerDistribution,
        SpectralPowerDistributionError> create(
            std::vector<SpectralPowerSample> samples);

    [[nodiscard]] const std::vector<SpectralPowerSample>&
    samples() const noexcept;

private:
    explicit SpectralPowerDistribution(
        std::vector<SpectralPowerSample> samples);

    std::vector<SpectralPowerSample> samples_;
};

enum class IlluminationIntensityRangeError {
    InvalidUnit,
    InvalidValue,
    NonIncreasingRange
};

// Values use the declared unit and are not compared across unlike units.
class IlluminationIntensityRange final {
public:
    [[nodiscard]] static Result<
        IlluminationIntensityRange,
        IlluminationIntensityRangeError> create(
            double minimum,
            double maximum,
            IlluminationIntensityUnit unit);

    [[nodiscard]] double minimum() const noexcept;
    [[nodiscard]] double maximum() const noexcept;
    [[nodiscard]] IlluminationIntensityUnit unit() const noexcept;

private:
    IlluminationIntensityRange(
        double minimum,
        double maximum,
        IlluminationIntensityUnit unit);

    double minimum_;
    double maximum_;
    IlluminationIntensityUnit unit_;
};

enum class IlluminationChannelError {
    InvalidControlKind,
    MissingContinuousRange,
    UnexpectedContinuousRange,
    InvalidSpectralPowerDistribution
};

// A physical source may expose multiple independently identified spectral
// channels, each with its own optional spectrum and intensity capability.
class IlluminationChannel final {
public:
    [[nodiscard]] static Result<
        IlluminationChannel,
        IlluminationChannelError> create(
            IlluminationChannelId id,
            std::optional<SpectralBand> spectralBand,
            IlluminationControlKind controlKind,
            std::optional<IlluminationIntensityRange> intensityRange,
            std::optional<SpectralPowerDistribution> spectralPowerDistribution =
                std::nullopt);

    [[nodiscard]] const IlluminationChannelId& id() const noexcept;
    [[nodiscard]] const std::optional<SpectralBand>& spectralBand() const noexcept;
    [[nodiscard]] IlluminationControlKind controlKind() const noexcept;
    [[nodiscard]] const std::optional<IlluminationIntensityRange>&
    intensityRange() const noexcept;
    [[nodiscard]] const std::optional<SpectralPowerDistribution>&
    spectralPowerDistribution() const noexcept;

private:
    IlluminationChannel(
        IlluminationChannelId id,
        std::optional<SpectralBand> spectralBand,
        IlluminationControlKind controlKind,
        std::optional<IlluminationIntensityRange> intensityRange,
        std::optional<SpectralPowerDistribution> spectralPowerDistribution);

    IlluminationChannelId id_;
    std::optional<SpectralBand> spectralBand_;
    IlluminationControlKind controlKind_;
    std::optional<IlluminationIntensityRange> intensityRange_;
    std::optional<SpectralPowerDistribution> spectralPowerDistribution_;
};

enum class IlluminationSpecificationError {
    InvalidTechnology,
    NoChannels,
    DuplicateChannelId
};

// Static equipment specification only. Actual availability, current power,
// control readback and runtime capabilities belong to the adapter/runtime.
class IlluminationSpecification final {
public:
    [[nodiscard]] static Result<
        IlluminationSpecification,
        IlluminationSpecificationError> create(
            EquipmentComponentId componentId,
            IlluminationSourceTechnology technology,
            std::vector<IlluminationChannel> channels);

    [[nodiscard]] const EquipmentComponentId& componentId() const noexcept;
    [[nodiscard]] IlluminationSourceTechnology technology() const noexcept;
    [[nodiscard]] const std::vector<IlluminationChannel>& channels() const noexcept;

private:
    IlluminationSpecification(
        EquipmentComponentId componentId,
        IlluminationSourceTechnology technology,
        std::vector<IlluminationChannel> channels);

    EquipmentComponentId componentId_;
    IlluminationSourceTechnology technology_;
    std::vector<IlluminationChannel> channels_;
};

} // namespace microscope::core
