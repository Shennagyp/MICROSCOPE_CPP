#include "microscope/core/samples/SampleModels.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace microscope::core {
namespace {

[[nodiscard]] bool hasText(const std::string& value) noexcept {
    return value.find_first_not_of(" \t\n\r\f\v") != std::string::npos;
}

[[nodiscard]] bool isKnownSampleMaterialClass(
    SampleMaterialClass value) noexcept {
    switch (value) {
    case SampleMaterialClass::Biological:
    case SampleMaterialClass::Botanical:
    case SampleMaterialClass::Geological:
    case SampleMaterialClass::MaterialScience:
    case SampleMaterialClass::CalibrationArtifact:
    case SampleMaterialClass::Other:
        return true;
    case SampleMaterialClass::Unknown:
        return false;
    }
    return false;
}

[[nodiscard]] bool isKnownTechniqueConstraintKind(
    MicroscopyTechniqueConstraintKind value) noexcept {
    switch (value) {
    case MicroscopyTechniqueConstraintKind::Required:
    case MicroscopyTechniqueConstraintKind::Preferred:
    case MicroscopyTechniqueConstraintKind::Supported:
    case MicroscopyTechniqueConstraintKind::Prohibited:
        return true;
    }
    return false;
}

[[nodiscard]] bool isKnownContrast(ContrastMethod value) noexcept {
    switch (value) {
    case ContrastMethod::Brightfield:
    case ContrastMethod::PhaseContrast:
    case ContrastMethod::DifferentialInterferenceContrast:
    case ContrastMethod::PolarizedLight:
    case ContrastMethod::Fluorescence:
    case ContrastMethod::Darkfield:
    case ContrastMethod::Other:
        return true;
    case ContrastMethod::Unknown:
        return false;
    }
    return false;
}

[[nodiscard]] bool isKnownImmersionMedium(
    ObjectiveImmersionMedium value) noexcept {
    switch (value) {
    case ObjectiveImmersionMedium::Air:
    case ObjectiveImmersionMedium::Water:
    case ObjectiveImmersionMedium::Oil:
    case ObjectiveImmersionMedium::Glycerol:
    case ObjectiveImmersionMedium::Silicone:
    case ObjectiveImmersionMedium::Other:
        return true;
    case ObjectiveImmersionMedium::Unknown:
        return false;
    }
    return false;
}

[[nodiscard]] bool isKnownPreparationMethod(
    SpecimenPreparationMethod value) noexcept {
    switch (value) {
    case SpecimenPreparationMethod::Smear:
    case SpecimenPreparationMethod::TissueSection:
    case SpecimenPreparationMethod::CellCulture:
    case SpecimenPreparationMethod::WholeMount:
    case SpecimenPreparationMethod::Suspension:
    case SpecimenPreparationMethod::Cytospin:
    case SpecimenPreparationMethod::FreshUnfixed:
    case SpecimenPreparationMethod::Other:
        return true;
    case SpecimenPreparationMethod::Unknown:
        return false;
    }
    return false;
}

[[nodiscard]] bool isKnownFixationState(
    SpecimenFixationState value) noexcept {
    switch (value) {
    case SpecimenFixationState::Live:
    case SpecimenFixationState::Unfixed:
    case SpecimenFixationState::Fixed:
    case SpecimenFixationState::Killed:
    case SpecimenFixationState::Unknown:
        return true;
    }
    return false;
}

[[nodiscard]] bool isKnownLabelKind(SpecimenLabelKind value) noexcept {
    switch (value) {
    case SpecimenLabelKind::Fluorescent:
    case SpecimenLabelKind::Chromogenic:
    case SpecimenLabelKind::Histochemical:
    case SpecimenLabelKind::Other:
        return true;
    }
    return false;
}

} // namespace

ObjectiveMicroscopyRequirements::ObjectiveMicroscopyRequirements(
    std::optional<double> minimumMagnification,
    std::optional<double> maximumMagnification,
    std::optional<double> minimumNumericalAperture,
    std::vector<ObjectiveImmersionMedium> allowedImmersionMedia,
    std::optional<double> maximumCoverglassDesignMismatchMicrometers)
    : minimumMagnification_(minimumMagnification),
      maximumMagnification_(maximumMagnification),
      minimumNumericalAperture_(minimumNumericalAperture),
      allowedImmersionMedia_(std::move(allowedImmersionMedia)),
      maximumCoverglassDesignMismatchMicrometers_(
          maximumCoverglassDesignMismatchMicrometers) {}

Result<
    ObjectiveMicroscopyRequirements,
    ObjectiveRequirementsError> ObjectiveMicroscopyRequirements::create(
        std::optional<double> minimumMagnification,
        std::optional<double> maximumMagnification,
        std::optional<double> minimumNumericalAperture,
        std::vector<ObjectiveImmersionMedium> allowedImmersionMedia,
        std::optional<double> maximumCoverglassDesignMismatchMicrometers) {
    if (!minimumMagnification && !maximumMagnification &&
        !minimumNumericalAperture && allowedImmersionMedia.empty() &&
        !maximumCoverglassDesignMismatchMicrometers) {
        return Result<
            ObjectiveMicroscopyRequirements,
            ObjectiveRequirementsError>::failure(
                ObjectiveRequirementsError::NoConstraint);
    }

    const auto finite = [](const std::optional<double>& value) {
        return !value || std::isfinite(*value);
    };
    if (!finite(minimumMagnification) ||
        !finite(maximumMagnification) ||
        !finite(minimumNumericalAperture) ||
        !finite(maximumCoverglassDesignMismatchMicrometers)) {
        return Result<
            ObjectiveMicroscopyRequirements,
            ObjectiveRequirementsError>::failure(
                ObjectiveRequirementsError::NonFiniteValue);
    }

    if ((minimumMagnification && *minimumMagnification <= 0.0) ||
        (maximumMagnification && *maximumMagnification <= 0.0)) {
        return Result<
            ObjectiveMicroscopyRequirements,
            ObjectiveRequirementsError>::failure(
                ObjectiveRequirementsError::NonPositiveMagnification);
    }
    if (minimumMagnification && maximumMagnification &&
        *minimumMagnification > *maximumMagnification) {
        return Result<
            ObjectiveMicroscopyRequirements,
            ObjectiveRequirementsError>::failure(
                ObjectiveRequirementsError::InvertedMagnificationRange);
    }
    if (minimumNumericalAperture &&
        *minimumNumericalAperture <= 0.0) {
        return Result<
            ObjectiveMicroscopyRequirements,
            ObjectiveRequirementsError>::failure(
                ObjectiveRequirementsError::InvalidNumericalAperture);
    }
    if (maximumCoverglassDesignMismatchMicrometers &&
        *maximumCoverglassDesignMismatchMicrometers < 0.0) {
        return Result<
            ObjectiveMicroscopyRequirements,
            ObjectiveRequirementsError>::failure(
                ObjectiveRequirementsError::NegativeCoverglassMismatch);
    }

    std::set<ObjectiveImmersionMedium> media;
    for (const auto medium : allowedImmersionMedia) {
        if (!isKnownImmersionMedium(medium)) {
            return Result<
                ObjectiveMicroscopyRequirements,
                ObjectiveRequirementsError>::failure(
                    ObjectiveRequirementsError::InvalidImmersionMedium);
        }
        if (!media.insert(medium).second) {
            return Result<
                ObjectiveMicroscopyRequirements,
                ObjectiveRequirementsError>::failure(
                    ObjectiveRequirementsError::DuplicateImmersionMedium);
        }
    }

    return Result<
        ObjectiveMicroscopyRequirements,
        ObjectiveRequirementsError>::success(
            ObjectiveMicroscopyRequirements(
                minimumMagnification,
                maximumMagnification,
                minimumNumericalAperture,
                std::move(allowedImmersionMedia),
                maximumCoverglassDesignMismatchMicrometers));
}

const std::optional<double>&
ObjectiveMicroscopyRequirements::minimumMagnification() const noexcept {
    return minimumMagnification_;
}

const std::optional<double>&
ObjectiveMicroscopyRequirements::maximumMagnification() const noexcept {
    return maximumMagnification_;
}

const std::optional<double>&
ObjectiveMicroscopyRequirements::minimumNumericalAperture() const noexcept {
    return minimumNumericalAperture_;
}

const std::vector<ObjectiveImmersionMedium>&
ObjectiveMicroscopyRequirements::allowedImmersionMedia() const noexcept {
    return allowedImmersionMedia_;
}

const std::optional<double>&
ObjectiveMicroscopyRequirements::maximumCoverglassDesignMismatchMicrometers()
    const noexcept {
    return maximumCoverglassDesignMismatchMicrometers_;
}

CondenserMicroscopyRequirements::CondenserMicroscopyRequirements(
    std::optional<double> minimumApertureNA,
    std::optional<double> maximumApertureNA,
    std::vector<CondenserDesign> allowedDesigns,
    std::vector<CondenserImmersionMedium> allowedImmersionMedia)
    : minimumApertureNA_(minimumApertureNA),
      maximumApertureNA_(maximumApertureNA),
      allowedDesigns_(std::move(allowedDesigns)),
      allowedImmersionMedia_(std::move(allowedImmersionMedia)) {}

Result<CondenserMicroscopyRequirements, CondenserRequirementsError>
CondenserMicroscopyRequirements::create(
    std::optional<double> minimumApertureNA,
    std::optional<double> maximumApertureNA,
    std::vector<CondenserDesign> allowedDesigns,
    std::vector<CondenserImmersionMedium> allowedImmersionMedia) {
    if (!minimumApertureNA && !maximumApertureNA &&
        allowedDesigns.empty() && allowedImmersionMedia.empty()) {
        return Result<
            CondenserMicroscopyRequirements,
            CondenserRequirementsError>::failure(
                CondenserRequirementsError::NoConstraint);
    }

    const auto finite = [](const std::optional<double>& value) {
        return !value || std::isfinite(*value);
    };
    if (!finite(minimumApertureNA) || !finite(maximumApertureNA)) {
        return Result<
            CondenserMicroscopyRequirements,
            CondenserRequirementsError>::failure(
                CondenserRequirementsError::NonFiniteValue);
    }
    if ((minimumApertureNA && *minimumApertureNA < 0.0) ||
        (maximumApertureNA && *maximumApertureNA <= 0.0)) {
        return Result<
            CondenserMicroscopyRequirements,
            CondenserRequirementsError>::failure(
                CondenserRequirementsError::InvalidAperture);
    }
    if (minimumApertureNA && maximumApertureNA &&
        *minimumApertureNA > *maximumApertureNA) {
        return Result<
            CondenserMicroscopyRequirements,
            CondenserRequirementsError>::failure(
                CondenserRequirementsError::InvertedApertureRange);
    }

    std::set<CondenserDesign> designs;
    for (const auto design : allowedDesigns) {
        if (design == CondenserDesign::Unknown) {
            return Result<
                CondenserMicroscopyRequirements,
                CondenserRequirementsError>::failure(
                    CondenserRequirementsError::InvalidDesign);
        }
        if (!designs.insert(design).second) {
            return Result<
                CondenserMicroscopyRequirements,
                CondenserRequirementsError>::failure(
                    CondenserRequirementsError::DuplicateDesign);
        }
    }

    std::set<CondenserImmersionMedium> immersionMedia;
    for (const auto medium : allowedImmersionMedia) {
        if (medium == CondenserImmersionMedium::Unknown) {
            return Result<
                CondenserMicroscopyRequirements,
                CondenserRequirementsError>::failure(
                    CondenserRequirementsError::InvalidImmersionMedium);
        }
        if (!immersionMedia.insert(medium).second) {
            return Result<
                CondenserMicroscopyRequirements,
                CondenserRequirementsError>::failure(
                    CondenserRequirementsError::DuplicateImmersionMedium);
        }
    }

    return Result<
        CondenserMicroscopyRequirements,
        CondenserRequirementsError>::success(
            CondenserMicroscopyRequirements(
                minimumApertureNA,
                maximumApertureNA,
                std::move(allowedDesigns),
                std::move(allowedImmersionMedia)));
}

const std::optional<double>&
CondenserMicroscopyRequirements::minimumApertureNA() const noexcept {
    return minimumApertureNA_;
}

const std::optional<double>&
CondenserMicroscopyRequirements::maximumApertureNA() const noexcept {
    return maximumApertureNA_;
}

const std::vector<CondenserDesign>&
CondenserMicroscopyRequirements::allowedDesigns() const noexcept {
    return allowedDesigns_;
}

const std::vector<CondenserImmersionMedium>&
CondenserMicroscopyRequirements::allowedImmersionMedia() const noexcept {
    return allowedImmersionMedia_;
}

IlluminationMicroscopyRequirements::IlluminationMicroscopyRequirements(
    std::optional<double> minimumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter,
    std::optional<double> maximumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter,
    std::optional<SpectralBand> requiredSpectralBand,
    bool requireContinuouslyAdjustableSource)
    : minimumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter_(
          minimumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter),
      maximumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter_(
          maximumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter),
      requiredSpectralBand_(std::move(requiredSpectralBand)),
      requireContinuouslyAdjustableSource_(requireContinuouslyAdjustableSource) {}

Result<
    IlluminationMicroscopyRequirements,
    IlluminationRequirementsError> IlluminationMicroscopyRequirements::create(
        std::optional<double> minimumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter,
        std::optional<double> maximumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter,
        std::optional<SpectralBand> requiredSpectralBand,
        bool requireContinuouslyAdjustableSource) {
    if (!minimumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter &&
        !maximumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter &&
        !requiredSpectralBand &&
        !requireContinuouslyAdjustableSource) {
        return Result<
            IlluminationMicroscopyRequirements,
            IlluminationRequirementsError>::failure(
                IlluminationRequirementsError::NoConstraint);
    }

    const auto finite = [](const std::optional<double>& value) {
        return !value || std::isfinite(*value);
    };
    if (!finite(minimumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter) ||
        !finite(maximumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter)) {
        return Result<
            IlluminationMicroscopyRequirements,
            IlluminationRequirementsError>::failure(
                IlluminationRequirementsError::NonFiniteValue);
    }
    if (minimumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter &&
        *minimumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter < 0.0) {
        return Result<
            IlluminationMicroscopyRequirements,
            IlluminationRequirementsError>::failure(
                IlluminationRequirementsError::NegativeMinimumIrradiance);
    }
    if (maximumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter &&
        *maximumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter <= 0.0) {
        return Result<
            IlluminationMicroscopyRequirements,
            IlluminationRequirementsError>::failure(
                IlluminationRequirementsError::NonPositiveMaximumIrradiance);
    }
    if (minimumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter &&
        maximumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter &&
        *minimumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter >
            *maximumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter) {
        return Result<
            IlluminationMicroscopyRequirements,
            IlluminationRequirementsError>::failure(
                IlluminationRequirementsError::InvertedIrradianceRange);
    }

    return Result<
        IlluminationMicroscopyRequirements,
        IlluminationRequirementsError>::success(
            IlluminationMicroscopyRequirements(
                minimumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter,
                maximumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter,
                std::move(requiredSpectralBand),
                requireContinuouslyAdjustableSource));
}

const std::optional<double>&
IlluminationMicroscopyRequirements::
minimumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter() const noexcept {
    return minimumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter_;
}

const std::optional<double>&
IlluminationMicroscopyRequirements::
maximumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter() const noexcept {
    return maximumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter_;
}

const std::optional<SpectralBand>&
IlluminationMicroscopyRequirements::requiredSpectralBand() const noexcept {
    return requiredSpectralBand_;
}

bool IlluminationMicroscopyRequirements::
requireContinuouslyAdjustableSource() const noexcept {
    return requireContinuouslyAdjustableSource_;
}

CameraPerformanceRequirements::CameraPerformanceRequirements(
    std::optional<double> minimumFrameRateFramesPerSecond,
    std::optional<double> maximumReadNoiseElectronsRms,
    std::optional<double> maximumDarkCurrentElectronsPerPixelPerSecond,
    std::optional<double> minimumFullWellCapacityElectrons,
    std::optional<double> minimumMeanQuantumEfficiency,
    bool preferMonochrome,
    bool preferGlobalShutter)
    : minimumFrameRateFramesPerSecond_(minimumFrameRateFramesPerSecond),
      maximumReadNoiseElectronsRms_(maximumReadNoiseElectronsRms),
      maximumDarkCurrentElectronsPerPixelPerSecond_(
          maximumDarkCurrentElectronsPerPixelPerSecond),
      minimumFullWellCapacityElectrons_(minimumFullWellCapacityElectrons),
      minimumMeanQuantumEfficiency_(minimumMeanQuantumEfficiency),
      preferMonochrome_(preferMonochrome),
      preferGlobalShutter_(preferGlobalShutter) {}

Result<
    CameraPerformanceRequirements,
    CameraPerformanceRequirementsError> CameraPerformanceRequirements::create(
        std::optional<double> minimumFrameRateFramesPerSecond,
        std::optional<double> maximumReadNoiseElectronsRms,
        std::optional<double> maximumDarkCurrentElectronsPerPixelPerSecond,
        std::optional<double> minimumFullWellCapacityElectrons,
        std::optional<double> minimumMeanQuantumEfficiency,
        bool preferMonochrome,
        bool preferGlobalShutter) {
    if (!minimumFrameRateFramesPerSecond &&
        !maximumReadNoiseElectronsRms &&
        !maximumDarkCurrentElectronsPerPixelPerSecond &&
        !minimumFullWellCapacityElectrons &&
        !minimumMeanQuantumEfficiency &&
        !preferMonochrome &&
        !preferGlobalShutter) {
        return Result<
            CameraPerformanceRequirements,
            CameraPerformanceRequirementsError>::failure(
                CameraPerformanceRequirementsError::NoConstraint);
    }

    const auto finite = [](const std::optional<double>& value) {
        return !value || std::isfinite(*value);
    };
    if (!finite(minimumFrameRateFramesPerSecond) ||
        !finite(maximumReadNoiseElectronsRms) ||
        !finite(maximumDarkCurrentElectronsPerPixelPerSecond) ||
        !finite(minimumFullWellCapacityElectrons) ||
        !finite(minimumMeanQuantumEfficiency)) {
        return Result<
            CameraPerformanceRequirements,
            CameraPerformanceRequirementsError>::failure(
                CameraPerformanceRequirementsError::NonFiniteValue);
    }
    if (minimumFrameRateFramesPerSecond &&
        *minimumFrameRateFramesPerSecond <= 0.0) {
        return Result<
            CameraPerformanceRequirements,
            CameraPerformanceRequirementsError>::failure(
                CameraPerformanceRequirementsError::NonPositiveFrameRate);
    }
    if (maximumReadNoiseElectronsRms &&
        *maximumReadNoiseElectronsRms < 0.0) {
        return Result<
            CameraPerformanceRequirements,
            CameraPerformanceRequirementsError>::failure(
                CameraPerformanceRequirementsError::NegativeReadNoise);
    }
    if (maximumDarkCurrentElectronsPerPixelPerSecond &&
        *maximumDarkCurrentElectronsPerPixelPerSecond < 0.0) {
        return Result<
            CameraPerformanceRequirements,
            CameraPerformanceRequirementsError>::failure(
                CameraPerformanceRequirementsError::NegativeDarkCurrent);
    }
    if (minimumFullWellCapacityElectrons &&
        *minimumFullWellCapacityElectrons <= 0.0) {
        return Result<
            CameraPerformanceRequirements,
            CameraPerformanceRequirementsError>::failure(
                CameraPerformanceRequirementsError::NonPositiveFullWell);
    }
    if (minimumMeanQuantumEfficiency &&
        (*minimumMeanQuantumEfficiency < 0.0 ||
         *minimumMeanQuantumEfficiency > 1.0)) {
        return Result<
            CameraPerformanceRequirements,
            CameraPerformanceRequirementsError>::failure(
                CameraPerformanceRequirementsError::InvalidQuantumEfficiency);
    }

    return Result<
        CameraPerformanceRequirements,
        CameraPerformanceRequirementsError>::success(
            CameraPerformanceRequirements(
                minimumFrameRateFramesPerSecond,
                maximumReadNoiseElectronsRms,
                maximumDarkCurrentElectronsPerPixelPerSecond,
                minimumFullWellCapacityElectrons,
                minimumMeanQuantumEfficiency,
                preferMonochrome,
                preferGlobalShutter));
}

const std::optional<double>&
CameraPerformanceRequirements::minimumFrameRateFramesPerSecond() const noexcept {
    return minimumFrameRateFramesPerSecond_;
}

const std::optional<double>&
CameraPerformanceRequirements::maximumReadNoiseElectronsRms() const noexcept {
    return maximumReadNoiseElectronsRms_;
}

const std::optional<double>&
CameraPerformanceRequirements::maximumDarkCurrentElectronsPerPixelPerSecond()
    const noexcept {
    return maximumDarkCurrentElectronsPerPixelPerSecond_;
}

const std::optional<double>&
CameraPerformanceRequirements::minimumFullWellCapacityElectrons() const noexcept {
    return minimumFullWellCapacityElectrons_;
}

const std::optional<double>&
CameraPerformanceRequirements::minimumMeanQuantumEfficiency() const noexcept {
    return minimumMeanQuantumEfficiency_;
}

bool CameraPerformanceRequirements::preferMonochrome() const noexcept {
    return preferMonochrome_;
}

bool CameraPerformanceRequirements::preferGlobalShutter() const noexcept {
    return preferGlobalShutter_;
}

MicroscopyRequirements::MicroscopyRequirements(
    std::vector<MicroscopyTechniqueConstraint> techniques,
    std::optional<ObjectiveMicroscopyRequirements> objective,
    std::optional<CameraPerformanceRequirements> camera,
    bool requireFluorescentLabelMetadata,
    std::optional<CondenserMicroscopyRequirements> condenser,
    std::optional<IlluminationMicroscopyRequirements> illumination)
    : techniques_(std::move(techniques)),
      objective_(std::move(objective)),
      camera_(std::move(camera)),
      requireFluorescentLabelMetadata_(requireFluorescentLabelMetadata),
      condenser_(std::move(condenser)),
      illumination_(std::move(illumination)) {}

Result<MicroscopyRequirements, MicroscopyRequirementsError>
MicroscopyRequirements::create(
    std::vector<MicroscopyTechniqueConstraint> techniques,
    std::optional<ObjectiveMicroscopyRequirements> objective,
    std::optional<CameraPerformanceRequirements> camera,
    bool requireFluorescentLabelMetadata,
    std::optional<CondenserMicroscopyRequirements> condenser,
    std::optional<IlluminationMicroscopyRequirements> illumination) {
    std::set<ContrastMethod> methods;
    std::size_t requiredCount = 0;
    bool fluorescenceTechniqueDeclared = false;

    for (const auto& technique : techniques) {
        if (!isKnownContrast(technique.method) ||
            !isKnownTechniqueConstraintKind(technique.kind)) {
            return Result<
                MicroscopyRequirements,
                MicroscopyRequirementsError>::failure(
                    MicroscopyRequirementsError::InvalidTechnique);
        }
        if (!methods.insert(technique.method).second) {
            return Result<
                MicroscopyRequirements,
                MicroscopyRequirementsError>::failure(
                    MicroscopyRequirementsError::DuplicateTechnique);
        }
        requiredCount += technique.kind ==
            MicroscopyTechniqueConstraintKind::Required ? 1U : 0U;
        if (technique.method == ContrastMethod::Fluorescence &&
            technique.kind != MicroscopyTechniqueConstraintKind::Prohibited) {
            fluorescenceTechniqueDeclared = true;
        }
    }

    if (requiredCount > 1) {
        return Result<
            MicroscopyRequirements,
            MicroscopyRequirementsError>::failure(
                MicroscopyRequirementsError::MultipleRequiredTechniques);
    }
    if (requireFluorescentLabelMetadata && !fluorescenceTechniqueDeclared) {
        return Result<
            MicroscopyRequirements,
            MicroscopyRequirementsError>::failure(
                MicroscopyRequirementsError::FluorescenceMetadataWithoutFluorescenceTechnique);
    }

    return Result<
        MicroscopyRequirements,
        MicroscopyRequirementsError>::success(
            MicroscopyRequirements(
                std::move(techniques),
                std::move(objective),
                std::move(camera),
                requireFluorescentLabelMetadata,
                std::move(condenser),
                std::move(illumination)));
}

const std::vector<MicroscopyTechniqueConstraint>&
MicroscopyRequirements::techniques() const noexcept {
    return techniques_;
}

const std::optional<ObjectiveMicroscopyRequirements>&
MicroscopyRequirements::objective() const noexcept {
    return objective_;
}

const std::optional<CameraPerformanceRequirements>&
MicroscopyRequirements::camera() const noexcept {
    return camera_;
}

bool MicroscopyRequirements::requireFluorescentLabelMetadata() const noexcept {
    return requireFluorescentLabelMetadata_;
}

const std::optional<CondenserMicroscopyRequirements>&
MicroscopyRequirements::condenser() const noexcept {
    return condenser_;
}

const std::optional<IlluminationMicroscopyRequirements>&
MicroscopyRequirements::illumination() const noexcept {
    return illumination_;
}

SlidePreparationRequirements::SlidePreparationRequirements(
    std::vector<SpecimenPreparationMethod> allowedMethods,
    std::vector<SpecimenFixationState> allowedFixationStates,
    std::optional<double> minimumSectionThicknessMicrometers,
    std::optional<double> maximumSectionThicknessMicrometers,
    std::optional<double> minimumCoverglassThicknessMicrometers,
    std::optional<double> maximumCoverglassThicknessMicrometers,
    bool requireMountingMedium,
    std::vector<SpecimenLabelKind> requiredLabelKinds)
    : allowedMethods_(std::move(allowedMethods)),
      allowedFixationStates_(std::move(allowedFixationStates)),
      minimumSectionThicknessMicrometers_(minimumSectionThicknessMicrometers),
      maximumSectionThicknessMicrometers_(maximumSectionThicknessMicrometers),
      minimumCoverglassThicknessMicrometers_(minimumCoverglassThicknessMicrometers),
      maximumCoverglassThicknessMicrometers_(maximumCoverglassThicknessMicrometers),
      requireMountingMedium_(requireMountingMedium),
      requiredLabelKinds_(std::move(requiredLabelKinds)) {}

Result<
    SlidePreparationRequirements,
    SlidePreparationRequirementsError>
SlidePreparationRequirements::create(
    std::vector<SpecimenPreparationMethod> allowedMethods,
    std::vector<SpecimenFixationState> allowedFixationStates,
    std::optional<double> minimumSectionThicknessMicrometers,
    std::optional<double> maximumSectionThicknessMicrometers,
    std::optional<double> minimumCoverglassThicknessMicrometers,
    std::optional<double> maximumCoverglassThicknessMicrometers,
    bool requireMountingMedium,
    std::vector<SpecimenLabelKind> requiredLabelKinds) {
    if (allowedMethods.empty() &&
        allowedFixationStates.empty() &&
        !minimumSectionThicknessMicrometers &&
        !maximumSectionThicknessMicrometers &&
        !minimumCoverglassThicknessMicrometers &&
        !maximumCoverglassThicknessMicrometers &&
        !requireMountingMedium &&
        requiredLabelKinds.empty()) {
        return Result<
            SlidePreparationRequirements,
            SlidePreparationRequirementsError>::failure(
                SlidePreparationRequirementsError::NoConstraint);
    }

    const auto finite = [](const std::optional<double>& value) {
        return !value || std::isfinite(*value);
    };
    if (!finite(minimumSectionThicknessMicrometers) ||
        !finite(maximumSectionThicknessMicrometers) ||
        !finite(minimumCoverglassThicknessMicrometers) ||
        !finite(maximumCoverglassThicknessMicrometers)) {
        return Result<
            SlidePreparationRequirements,
            SlidePreparationRequirementsError>::failure(
                SlidePreparationRequirementsError::NonFiniteThickness);
    }

    if ((minimumSectionThicknessMicrometers &&
         *minimumSectionThicknessMicrometers <= 0.0) ||
        (maximumSectionThicknessMicrometers &&
         *maximumSectionThicknessMicrometers <= 0.0) ||
        (minimumSectionThicknessMicrometers &&
         maximumSectionThicknessMicrometers &&
         *minimumSectionThicknessMicrometers >
             *maximumSectionThicknessMicrometers)) {
        return Result<
            SlidePreparationRequirements,
            SlidePreparationRequirementsError>::failure(
                SlidePreparationRequirementsError::InvalidSectionThicknessRange);
    }

    if ((minimumCoverglassThicknessMicrometers &&
         *minimumCoverglassThicknessMicrometers <= 0.0) ||
        (maximumCoverglassThicknessMicrometers &&
         *maximumCoverglassThicknessMicrometers <= 0.0) ||
        (minimumCoverglassThicknessMicrometers &&
         maximumCoverglassThicknessMicrometers &&
         *minimumCoverglassThicknessMicrometers >
             *maximumCoverglassThicknessMicrometers)) {
        return Result<
            SlidePreparationRequirements,
            SlidePreparationRequirementsError>::failure(
                SlidePreparationRequirementsError::InvalidCoverglassThicknessRange);
    }

    std::set<SpecimenPreparationMethod> methods;
    for (const auto method : allowedMethods) {
        if (!isKnownPreparationMethod(method)) {
            return Result<
                SlidePreparationRequirements,
                SlidePreparationRequirementsError>::failure(
                    SlidePreparationRequirementsError::InvalidMethod);
        }
        if (!methods.insert(method).second) {
            return Result<
                SlidePreparationRequirements,
                SlidePreparationRequirementsError>::failure(
                    SlidePreparationRequirementsError::DuplicateMethod);
        }
    }

    std::set<SpecimenFixationState> fixationStates;
    for (const auto state : allowedFixationStates) {
        if (state == SpecimenFixationState::Unknown) {
            return Result<
                SlidePreparationRequirements,
                SlidePreparationRequirementsError>::failure(
                    SlidePreparationRequirementsError::InvalidFixationState);
        }
        if (!fixationStates.insert(state).second) {
            return Result<
                SlidePreparationRequirements,
                SlidePreparationRequirementsError>::failure(
                    SlidePreparationRequirementsError::DuplicateFixationState);
        }
    }

    std::set<SpecimenLabelKind> labelKinds;
    for (const auto kind : requiredLabelKinds) {
        if (!isKnownLabelKind(kind)) {
            return Result<
                SlidePreparationRequirements,
                SlidePreparationRequirementsError>::failure(
                    SlidePreparationRequirementsError::InvalidLabelKind);
        }
        if (!labelKinds.insert(kind).second) {
            return Result<
                SlidePreparationRequirements,
                SlidePreparationRequirementsError>::failure(
                    SlidePreparationRequirementsError::DuplicateLabelKind);
        }
    }

    return Result<
        SlidePreparationRequirements,
        SlidePreparationRequirementsError>::success(
            SlidePreparationRequirements(
                std::move(allowedMethods),
                std::move(allowedFixationStates),
                minimumSectionThicknessMicrometers,
                maximumSectionThicknessMicrometers,
                minimumCoverglassThicknessMicrometers,
                maximumCoverglassThicknessMicrometers,
                requireMountingMedium,
                std::move(requiredLabelKinds)));
}

const std::vector<SpecimenPreparationMethod>&
SlidePreparationRequirements::allowedMethods() const noexcept {
    return allowedMethods_;
}

const std::vector<SpecimenFixationState>&
SlidePreparationRequirements::allowedFixationStates() const noexcept {
    return allowedFixationStates_;
}

const std::optional<double>&
SlidePreparationRequirements::minimumSectionThicknessMicrometers() const noexcept {
    return minimumSectionThicknessMicrometers_;
}

const std::optional<double>&
SlidePreparationRequirements::maximumSectionThicknessMicrometers() const noexcept {
    return maximumSectionThicknessMicrometers_;
}

const std::optional<double>&
SlidePreparationRequirements::minimumCoverglassThicknessMicrometers() const noexcept {
    return minimumCoverglassThicknessMicrometers_;
}

const std::optional<double>&
SlidePreparationRequirements::maximumCoverglassThicknessMicrometers() const noexcept {
    return maximumCoverglassThicknessMicrometers_;
}

bool SlidePreparationRequirements::requireMountingMedium() const noexcept {
    return requireMountingMedium_;
}

const std::vector<SpecimenLabelKind>&
SlidePreparationRequirements::requiredLabelKinds() const noexcept {
    return requiredLabelKinds_;
}

SampleType::SampleType(
    SampleTypeId id,
    std::uint64_t revision,
    std::string name,
    SampleMaterialClass materialClass,
    MicroscopyRequirements microscopyRequirements,
    std::optional<std::string> description,
    std::optional<SlidePreparationRequirements> preparationRequirements)
    : id_(std::move(id)),
      revision_(revision),
      name_(std::move(name)),
      materialClass_(materialClass),
      microscopyRequirements_(std::move(microscopyRequirements)),
      description_(std::move(description)),
      preparationRequirements_(std::move(preparationRequirements)) {}

Result<SampleType, SampleTypeError> SampleType::create(
    SampleTypeId id,
    std::uint64_t revision,
    std::string name,
    SampleMaterialClass materialClass,
    MicroscopyRequirements microscopyRequirements,
    std::optional<std::string> description,
    std::optional<SlidePreparationRequirements> preparationRequirements) {
    if (revision == 0) {
        return Result<SampleType, SampleTypeError>::failure(
            SampleTypeError::ZeroRevision);
    }
    if (name.find('\0') != std::string::npos) {
        return Result<SampleType, SampleTypeError>::failure(
            SampleTypeError::InvalidName);
    }
    if (!hasText(name)) {
        return Result<SampleType, SampleTypeError>::failure(
            SampleTypeError::EmptyName);
    }
    if (!isKnownSampleMaterialClass(materialClass)) {
        return Result<SampleType, SampleTypeError>::failure(
            SampleTypeError::InvalidMaterialClass);
    }
    if (description && description->find('\0') != std::string::npos) {
        return Result<SampleType, SampleTypeError>::failure(
            SampleTypeError::InvalidDescription);
    }
    if (description && !hasText(*description)) {
        return Result<SampleType, SampleTypeError>::failure(
            SampleTypeError::EmptyDescription);
    }

    return Result<SampleType, SampleTypeError>::success(
        SampleType(
            std::move(id),
            revision,
            std::move(name),
            materialClass,
            std::move(microscopyRequirements),
            std::move(description),
            std::move(preparationRequirements)));
}

const SampleTypeId& SampleType::id() const noexcept {
    return id_;
}

std::uint64_t SampleType::revision() const noexcept {
    return revision_;
}

const std::string& SampleType::name() const noexcept {
    return name_;
}

SampleMaterialClass SampleType::materialClass() const noexcept {
    return materialClass_;
}

const MicroscopyRequirements& SampleType::microscopyRequirements() const noexcept {
    return microscopyRequirements_;
}

const std::optional<std::string>& SampleType::description() const noexcept {
    return description_;
}

const std::optional<SlidePreparationRequirements>&
SampleType::preparationRequirements() const noexcept {
    return preparationRequirements_;
}

SampleTypeCatalog::SampleTypeCatalog(
    std::uint64_t revision,
    std::vector<SampleType> sampleTypes)
    : revision_(revision),
      sampleTypes_(std::move(sampleTypes)) {}

Result<SampleTypeCatalog, SampleTypeCatalogError> SampleTypeCatalog::create(
    std::uint64_t revision,
    std::vector<SampleType> sampleTypes) {
    if (revision == 0) {
        return Result<SampleTypeCatalog, SampleTypeCatalogError>::failure(
            SampleTypeCatalogError::ZeroRevision);
    }
    if (sampleTypes.empty()) {
        return Result<SampleTypeCatalog, SampleTypeCatalogError>::failure(
            SampleTypeCatalogError::EmptyCatalog);
    }

    std::set<SampleTypeId> ids;
    for (const auto& sampleType : sampleTypes) {
        if (!ids.insert(sampleType.id()).second) {
            return Result<SampleTypeCatalog, SampleTypeCatalogError>::failure(
                SampleTypeCatalogError::DuplicateSampleTypeId);
        }
    }

    return Result<SampleTypeCatalog, SampleTypeCatalogError>::success(
        SampleTypeCatalog(revision, std::move(sampleTypes)));
}

std::uint64_t SampleTypeCatalog::revision() const noexcept {
    return revision_;
}

const std::vector<SampleType>& SampleTypeCatalog::sampleTypes() const noexcept {
    return sampleTypes_;
}

const SampleType* SampleTypeCatalog::find(const SampleTypeId& id) const noexcept {
    const auto found = std::find_if(
        sampleTypes_.begin(), sampleTypes_.end(),
        [&id](const SampleType& sampleType) {
            return sampleType.id() == id;
        });
    return found == sampleTypes_.end() ? nullptr : &*found;
}

SpecimenLabel::SpecimenLabel(
    std::string name,
    SpecimenLabelKind kind,
    std::optional<SpectralBand> excitationBand,
    std::optional<SpectralBand> emissionBand,
    std::optional<FluorescenceEmissionSpectrum> emissionSpectrum,
    std::optional<OpticalCompatibilityId> filterSetCompatibilityId)
    : name_(std::move(name)),
      kind_(kind),
      excitationBand_(std::move(excitationBand)),
      emissionBand_(std::move(emissionBand)),
      emissionSpectrum_(std::move(emissionSpectrum)),
      filterSetCompatibilityId_(std::move(filterSetCompatibilityId)) {}

Result<SpecimenLabel, SpecimenLabelError> SpecimenLabel::create(
    std::string name,
    SpecimenLabelKind kind,
    std::optional<SpectralBand> excitationBand,
    std::optional<SpectralBand> emissionBand,
    std::optional<FluorescenceEmissionSpectrum> emissionSpectrum,
    std::optional<OpticalCompatibilityId> filterSetCompatibilityId) {
    if (name.find('\0') != std::string::npos) {
        return Result<SpecimenLabel, SpecimenLabelError>::failure(
            SpecimenLabelError::InvalidName);
    }
    if (!hasText(name)) {
        return Result<SpecimenLabel, SpecimenLabelError>::failure(
            SpecimenLabelError::EmptyName);
    }
    if (!isKnownLabelKind(kind)) {
        return Result<SpecimenLabel, SpecimenLabelError>::failure(
            SpecimenLabelError::InvalidKind);
    }
    if (kind != SpecimenLabelKind::Fluorescent &&
        (excitationBand || emissionBand || emissionSpectrum ||
         filterSetCompatibilityId)) {
        return Result<SpecimenLabel, SpecimenLabelError>::failure(
            SpecimenLabelError::SpectralMetadataOnNonFluorescentLabel);
    }

    return Result<SpecimenLabel, SpecimenLabelError>::success(
        SpecimenLabel(
            std::move(name),
            kind,
            std::move(excitationBand),
            std::move(emissionBand),
            std::move(emissionSpectrum),
            std::move(filterSetCompatibilityId)));
}

const std::string& SpecimenLabel::name() const noexcept {
    return name_;
}

SpecimenLabelKind SpecimenLabel::kind() const noexcept {
    return kind_;
}

const std::optional<SpectralBand>& SpecimenLabel::excitationBand() const noexcept {
    return excitationBand_;
}

const std::optional<SpectralBand>& SpecimenLabel::emissionBand() const noexcept {
    return emissionBand_;
}

const std::optional<FluorescenceEmissionSpectrum>&
SpecimenLabel::emissionSpectrum() const noexcept {
    return emissionSpectrum_;
}

const std::optional<OpticalCompatibilityId>&
SpecimenLabel::filterSetCompatibilityId() const noexcept {
    return filterSetCompatibilityId_;
}

SlidePreparation::SlidePreparation(
    SpecimenPreparationMethod method,
    SpecimenFixationState fixationState,
    std::optional<double> sectionThicknessMicrometers,
    std::optional<double> coverglassThicknessMicrometers,
    std::optional<std::string> mountingMediumName,
    std::optional<double> mountingMediumRefractiveIndex,
    std::vector<SpecimenLabel> labels)
    : method_(method),
      fixationState_(fixationState),
      sectionThicknessMicrometers_(sectionThicknessMicrometers),
      coverglassThicknessMicrometers_(coverglassThicknessMicrometers),
      mountingMediumName_(std::move(mountingMediumName)),
      mountingMediumRefractiveIndex_(mountingMediumRefractiveIndex),
      labels_(std::move(labels)) {}

Result<SlidePreparation, SlidePreparationError>
SlidePreparation::create(
    SpecimenPreparationMethod method,
    SpecimenFixationState fixationState,
    std::optional<double> sectionThicknessMicrometers,
    std::optional<double> coverglassThicknessMicrometers,
    std::optional<std::string> mountingMediumName,
    std::optional<double> mountingMediumRefractiveIndex,
    std::vector<SpecimenLabel> labels) {
    if (!isKnownPreparationMethod(method)) {
        return Result<SlidePreparation, SlidePreparationError>::failure(
            SlidePreparationError::InvalidMethod);
    }
    if (!isKnownFixationState(fixationState)) {
        return Result<SlidePreparation, SlidePreparationError>::failure(
            SlidePreparationError::InvalidFixationState);
    }
    if (sectionThicknessMicrometers &&
        (!std::isfinite(*sectionThicknessMicrometers) ||
         *sectionThicknessMicrometers <= 0.0)) {
        return Result<SlidePreparation, SlidePreparationError>::failure(
            SlidePreparationError::InvalidSectionThickness);
    }
    if (coverglassThicknessMicrometers &&
        (!std::isfinite(*coverglassThicknessMicrometers) ||
         *coverglassThicknessMicrometers <= 0.0)) {
        return Result<SlidePreparation, SlidePreparationError>::failure(
            SlidePreparationError::InvalidCoverglassThickness);
    }
    if (mountingMediumName &&
        (mountingMediumName->find('\0') != std::string::npos ||
         !hasText(*mountingMediumName))) {
        return Result<SlidePreparation, SlidePreparationError>::failure(
            SlidePreparationError::InvalidMountingMediumName);
    }
    if (mountingMediumRefractiveIndex &&
        (!std::isfinite(*mountingMediumRefractiveIndex) ||
         *mountingMediumRefractiveIndex <= 0.0)) {
        return Result<SlidePreparation, SlidePreparationError>::failure(
            SlidePreparationError::InvalidMountingMediumRefractiveIndex);
    }

    std::set<std::pair<std::string, SpecimenLabelKind>> labelKeys;
    for (const auto& label : labels) {
        if (!labelKeys.insert({label.name(), label.kind()}).second) {
            return Result<SlidePreparation, SlidePreparationError>::failure(
                SlidePreparationError::DuplicateLabel);
        }
    }

    return Result<SlidePreparation, SlidePreparationError>::success(
        SlidePreparation(
            method,
            fixationState,
            sectionThicknessMicrometers,
            coverglassThicknessMicrometers,
            std::move(mountingMediumName),
            mountingMediumRefractiveIndex,
            std::move(labels)));
}

SpecimenPreparationMethod SlidePreparation::method() const noexcept {
    return method_;
}

SpecimenFixationState SlidePreparation::fixationState() const noexcept {
    return fixationState_;
}

const std::optional<double>&
SlidePreparation::sectionThicknessMicrometers() const noexcept {
    return sectionThicknessMicrometers_;
}

const std::optional<double>&
SlidePreparation::coverglassThicknessMicrometers() const noexcept {
    return coverglassThicknessMicrometers_;
}

const std::optional<std::string>&
SlidePreparation::mountingMediumName() const noexcept {
    return mountingMediumName_;
}

const std::optional<double>&
SlidePreparation::mountingMediumRefractiveIndex() const noexcept {
    return mountingMediumRefractiveIndex_;
}

const std::vector<SpecimenLabel>& SlidePreparation::labels() const noexcept {
    return labels_;
}


Slide::Slide(
    SlideId id,
    std::uint64_t revision,
    SampleTypeId sampleTypeId,
    std::uint64_t sampleTypeRevision,
    std::string name,
    SlideGeometry geometry,
    SlidePreparation preparation,
    std::optional<OriginLabId> originLabId)
    : id_(std::move(id)),
      revision_(revision),
      sampleTypeId_(std::move(sampleTypeId)),
      sampleTypeRevision_(sampleTypeRevision),
      name_(std::move(name)),
      geometry_(std::move(geometry)),
      preparation_(std::move(preparation)),
      originLabId_(std::move(originLabId)) {}

Result<Slide, SlideError> Slide::create(
    SlideId id,
    std::uint64_t revision,
    SampleTypeId sampleTypeId,
    std::uint64_t sampleTypeRevision,
    std::string name,
    SlideGeometry geometry,
    SlidePreparation preparation,
    std::optional<OriginLabId> originLabId) {
    if (revision == 0) {
        return Result<Slide, SlideError>::failure(SlideError::ZeroRevision);
    }
    if (sampleTypeRevision == 0) {
        return Result<Slide, SlideError>::failure(
            SlideError::ZeroSampleTypeRevision);
    }
    if (name.find(char{}) != std::string::npos) {
        return Result<Slide, SlideError>::failure(SlideError::InvalidName);
    }
    if (!hasText(name)) {
        return Result<Slide, SlideError>::failure(SlideError::EmptyName);
    }

    return Result<Slide, SlideError>::success(
        Slide(
            std::move(id),
            revision,
            std::move(sampleTypeId),
            sampleTypeRevision,
            std::move(name),
            std::move(geometry),
            std::move(preparation),
            std::move(originLabId)));
}

const SlideId& Slide::id() const noexcept { return id_; }
std::uint64_t Slide::revision() const noexcept { return revision_; }
const SampleTypeId& Slide::sampleTypeId() const noexcept { return sampleTypeId_; }
std::uint64_t Slide::sampleTypeRevision() const noexcept { return sampleTypeRevision_; }
const std::string& Slide::name() const noexcept { return name_; }
const SlideGeometry& Slide::geometry() const noexcept { return geometry_; }
const SlidePreparation& Slide::preparation() const noexcept { return preparation_; }
const std::optional<OriginLabId>& Slide::originLabId() const noexcept { return originLabId_; }

} // namespace microscope::core
