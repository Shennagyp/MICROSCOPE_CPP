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

[[nodiscard]] bool validOptionalText(
    const std::optional<std::string>& value) noexcept {
    return !value ||
           (value->find('\0') == std::string::npos && hasText(*value));
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

CameraPerformanceRequirements::CameraPerformanceRequirements(
    std::optional<double> minimumFrameRateFramesPerSecond,
    std::optional<double> maximumReadNoiseElectronsRms,
    std::optional<double> maximumDarkCurrentElectronsPerPixelPerSecond,
    std::optional<double> minimumFullWellCapacityElectrons)
    : minimumFrameRateFramesPerSecond_(minimumFrameRateFramesPerSecond),
      maximumReadNoiseElectronsRms_(maximumReadNoiseElectronsRms),
      maximumDarkCurrentElectronsPerPixelPerSecond_(
          maximumDarkCurrentElectronsPerPixelPerSecond),
      minimumFullWellCapacityElectrons_(minimumFullWellCapacityElectrons) {}

Result<
    CameraPerformanceRequirements,
    CameraPerformanceRequirementsError> CameraPerformanceRequirements::create(
        std::optional<double> minimumFrameRateFramesPerSecond,
        std::optional<double> maximumReadNoiseElectronsRms,
        std::optional<double> maximumDarkCurrentElectronsPerPixelPerSecond,
        std::optional<double> minimumFullWellCapacityElectrons) {
    if (!minimumFrameRateFramesPerSecond &&
        !maximumReadNoiseElectronsRms &&
        !maximumDarkCurrentElectronsPerPixelPerSecond &&
        !minimumFullWellCapacityElectrons) {
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
        !finite(minimumFullWellCapacityElectrons)) {
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

    return Result<
        CameraPerformanceRequirements,
        CameraPerformanceRequirementsError>::success(
            CameraPerformanceRequirements(
                minimumFrameRateFramesPerSecond,
                maximumReadNoiseElectronsRms,
                maximumDarkCurrentElectronsPerPixelPerSecond,
                minimumFullWellCapacityElectrons));
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

MicroscopyRequirements::MicroscopyRequirements(
    std::vector<MicroscopyTechniqueConstraint> techniques,
    std::optional<ObjectiveMicroscopyRequirements> objective,
    std::optional<CameraPerformanceRequirements> camera,
    bool requireFluorescentLabelMetadata)
    : techniques_(std::move(techniques)),
      objective_(std::move(objective)),
      camera_(std::move(camera)),
      requireFluorescentLabelMetadata_(requireFluorescentLabelMetadata) {}

Result<MicroscopyRequirements, MicroscopyRequirementsError>
MicroscopyRequirements::create(
    std::vector<MicroscopyTechniqueConstraint> techniques,
    std::optional<ObjectiveMicroscopyRequirements> objective,
    std::optional<CameraPerformanceRequirements> camera,
    bool requireFluorescentLabelMetadata) {
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
                requireFluorescentLabelMetadata));
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

SampleType::SampleType(
    SampleTypeId id,
    std::uint64_t revision,
    std::string name,
    SampleMaterialClass materialClass,
    MicroscopyRequirements microscopyRequirements,
    std::optional<std::string> description)
    : id_(std::move(id)),
      revision_(revision),
      name_(std::move(name)),
      materialClass_(materialClass),
      microscopyRequirements_(std::move(microscopyRequirements)),
      description_(std::move(description)) {}

Result<SampleType, SampleTypeError> SampleType::create(
    SampleTypeId id,
    std::uint64_t revision,
    std::string name,
    SampleMaterialClass materialClass,
    MicroscopyRequirements microscopyRequirements,
    std::optional<std::string> description) {
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
            std::move(description)));
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

Sample::Sample(
    SampleId id,
    std::uint64_t revision,
    SampleTypeId sampleTypeId,
    std::uint64_t sampleTypeRevision,
    std::string name,
    std::string materialDescription,
    std::optional<std::string> organismScientificName,
    std::optional<std::string> externalAccession,
    std::optional<std::string> description)
    : id_(std::move(id)),
      revision_(revision),
      sampleTypeId_(std::move(sampleTypeId)),
      sampleTypeRevision_(sampleTypeRevision),
      name_(std::move(name)),
      materialDescription_(std::move(materialDescription)),
      organismScientificName_(std::move(organismScientificName)),
      externalAccession_(std::move(externalAccession)),
      description_(std::move(description)) {}

Result<Sample, SampleError> Sample::create(
    SampleId id,
    std::uint64_t revision,
    SampleTypeId sampleTypeId,
    std::uint64_t sampleTypeRevision,
    std::string name,
    std::string materialDescription,
    std::optional<std::string> organismScientificName,
    std::optional<std::string> externalAccession,
    std::optional<std::string> description) {
    if (revision == 0) {
        return Result<Sample, SampleError>::failure(SampleError::ZeroRevision);
    }
    if (sampleTypeRevision == 0) {
        return Result<Sample, SampleError>::failure(
            SampleError::ZeroSampleTypeRevision);
    }
    if (name.find('\0') != std::string::npos) {
        return Result<Sample, SampleError>::failure(SampleError::InvalidName);
    }
    if (!hasText(name)) {
        return Result<Sample, SampleError>::failure(SampleError::EmptyName);
    }
    if (materialDescription.find('\0') != std::string::npos) {
        return Result<Sample, SampleError>::failure(
            SampleError::InvalidMaterialDescription);
    }
    if (!hasText(materialDescription)) {
        return Result<Sample, SampleError>::failure(
            SampleError::EmptyMaterialDescription);
    }
    if (!validOptionalText(organismScientificName)) {
        return Result<Sample, SampleError>::failure(
            SampleError::InvalidOrganismScientificName);
    }
    if (!validOptionalText(externalAccession)) {
        return Result<Sample, SampleError>::failure(
            SampleError::InvalidExternalAccession);
    }
    if (!validOptionalText(description)) {
        return Result<Sample, SampleError>::failure(
            SampleError::InvalidDescription);
    }

    return Result<Sample, SampleError>::success(
        Sample(
            std::move(id),
            revision,
            std::move(sampleTypeId),
            sampleTypeRevision,
            std::move(name),
            std::move(materialDescription),
            std::move(organismScientificName),
            std::move(externalAccession),
            std::move(description)));
}

const SampleId& Sample::id() const noexcept {
    return id_;
}

std::uint64_t Sample::revision() const noexcept {
    return revision_;
}

const SampleTypeId& Sample::sampleTypeId() const noexcept {
    return sampleTypeId_;
}

std::uint64_t Sample::sampleTypeRevision() const noexcept {
    return sampleTypeRevision_;
}

const std::string& Sample::name() const noexcept {
    return name_;
}

const std::string& Sample::materialDescription() const noexcept {
    return materialDescription_;
}

const std::optional<std::string>& Sample::organismScientificName() const noexcept {
    return organismScientificName_;
}

const std::optional<std::string>& Sample::externalAccession() const noexcept {
    return externalAccession_;
}

const std::optional<std::string>& Sample::description() const noexcept {
    return description_;
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

SpecimenPreparation::SpecimenPreparation(
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

Result<SpecimenPreparation, SpecimenPreparationError>
SpecimenPreparation::create(
    SpecimenPreparationMethod method,
    SpecimenFixationState fixationState,
    std::optional<double> sectionThicknessMicrometers,
    std::optional<double> coverglassThicknessMicrometers,
    std::optional<std::string> mountingMediumName,
    std::optional<double> mountingMediumRefractiveIndex,
    std::vector<SpecimenLabel> labels) {
    if (!isKnownPreparationMethod(method)) {
        return Result<SpecimenPreparation, SpecimenPreparationError>::failure(
            SpecimenPreparationError::InvalidMethod);
    }
    if (!isKnownFixationState(fixationState)) {
        return Result<SpecimenPreparation, SpecimenPreparationError>::failure(
            SpecimenPreparationError::InvalidFixationState);
    }
    if (sectionThicknessMicrometers &&
        (!std::isfinite(*sectionThicknessMicrometers) ||
         *sectionThicknessMicrometers <= 0.0)) {
        return Result<SpecimenPreparation, SpecimenPreparationError>::failure(
            SpecimenPreparationError::InvalidSectionThickness);
    }
    if (coverglassThicknessMicrometers &&
        (!std::isfinite(*coverglassThicknessMicrometers) ||
         *coverglassThicknessMicrometers <= 0.0)) {
        return Result<SpecimenPreparation, SpecimenPreparationError>::failure(
            SpecimenPreparationError::InvalidCoverglassThickness);
    }
    if (mountingMediumName &&
        (mountingMediumName->find('\0') != std::string::npos ||
         !hasText(*mountingMediumName))) {
        return Result<SpecimenPreparation, SpecimenPreparationError>::failure(
            SpecimenPreparationError::InvalidMountingMediumName);
    }
    if (mountingMediumRefractiveIndex &&
        (!std::isfinite(*mountingMediumRefractiveIndex) ||
         *mountingMediumRefractiveIndex <= 0.0)) {
        return Result<SpecimenPreparation, SpecimenPreparationError>::failure(
            SpecimenPreparationError::InvalidMountingMediumRefractiveIndex);
    }

    std::set<std::pair<std::string, SpecimenLabelKind>> labelKeys;
    for (const auto& label : labels) {
        if (!labelKeys.insert({label.name(), label.kind()}).second) {
            return Result<SpecimenPreparation, SpecimenPreparationError>::failure(
                SpecimenPreparationError::DuplicateLabel);
        }
    }

    return Result<SpecimenPreparation, SpecimenPreparationError>::success(
        SpecimenPreparation(
            method,
            fixationState,
            sectionThicknessMicrometers,
            coverglassThicknessMicrometers,
            std::move(mountingMediumName),
            mountingMediumRefractiveIndex,
            std::move(labels)));
}

SpecimenPreparationMethod SpecimenPreparation::method() const noexcept {
    return method_;
}

SpecimenFixationState SpecimenPreparation::fixationState() const noexcept {
    return fixationState_;
}

const std::optional<double>&
SpecimenPreparation::sectionThicknessMicrometers() const noexcept {
    return sectionThicknessMicrometers_;
}

const std::optional<double>&
SpecimenPreparation::coverglassThicknessMicrometers() const noexcept {
    return coverglassThicknessMicrometers_;
}

const std::optional<std::string>&
SpecimenPreparation::mountingMediumName() const noexcept {
    return mountingMediumName_;
}

const std::optional<double>&
SpecimenPreparation::mountingMediumRefractiveIndex() const noexcept {
    return mountingMediumRefractiveIndex_;
}

const std::vector<SpecimenLabel>& SpecimenPreparation::labels() const noexcept {
    return labels_;
}

Specimen::Specimen(
    SpecimenId id,
    std::uint64_t revision,
    SampleId sampleId,
    std::uint64_t sampleRevision,
    std::string name,
    SpecimenPreparation preparation)
    : id_(std::move(id)),
      revision_(revision),
      sampleId_(std::move(sampleId)),
      sampleRevision_(sampleRevision),
      name_(std::move(name)),
      preparation_(std::move(preparation)) {}

Result<Specimen, SpecimenError> Specimen::create(
    SpecimenId id,
    std::uint64_t revision,
    SampleId sampleId,
    std::uint64_t sampleRevision,
    std::string name,
    SpecimenPreparation preparation) {
    if (revision == 0) {
        return Result<Specimen, SpecimenError>::failure(
            SpecimenError::ZeroRevision);
    }
    if (sampleRevision == 0) {
        return Result<Specimen, SpecimenError>::failure(
            SpecimenError::ZeroSampleRevision);
    }
    if (name.find('\0') != std::string::npos) {
        return Result<Specimen, SpecimenError>::failure(
            SpecimenError::InvalidName);
    }
    if (!hasText(name)) {
        return Result<Specimen, SpecimenError>::failure(
            SpecimenError::EmptyName);
    }

    return Result<Specimen, SpecimenError>::success(
        Specimen(
            std::move(id),
            revision,
            std::move(sampleId),
            sampleRevision,
            std::move(name),
            std::move(preparation)));
}

const SpecimenId& Specimen::id() const noexcept {
    return id_;
}

std::uint64_t Specimen::revision() const noexcept {
    return revision_;
}

const SampleId& Specimen::sampleId() const noexcept {
    return sampleId_;
}

std::uint64_t Specimen::sampleRevision() const noexcept {
    return sampleRevision_;
}

const std::string& Specimen::name() const noexcept {
    return name_;
}

const SpecimenPreparation& Specimen::preparation() const noexcept {
    return preparation_;
}

SpecimenSlidePlacement::SpecimenSlidePlacement(
    SpecimenPlacementId id,
    SpecimenId specimenId,
    SlideId slideId,
    CalibrationRevisionId slideRegistrationRevision,
    double xMicrometers,
    double yMicrometers,
    double widthMicrometers,
    double heightMicrometers)
    : id_(std::move(id)),
      specimenId_(std::move(specimenId)),
      slideId_(std::move(slideId)),
      slideRegistrationRevision_(std::move(slideRegistrationRevision)),
      xMicrometers_(xMicrometers),
      yMicrometers_(yMicrometers),
      widthMicrometers_(widthMicrometers),
      heightMicrometers_(heightMicrometers) {}

Result<
    SpecimenSlidePlacement,
    SpecimenSlidePlacementError> SpecimenSlidePlacement::create(
        SpecimenPlacementId id,
        SpecimenId specimenId,
        SlideId slideId,
        CalibrationRevisionId slideRegistrationRevision,
        double xMicrometers,
        double yMicrometers,
        double widthMicrometers,
        double heightMicrometers,
        const SlideGeometry& slideGeometry) {
    if (!std::isfinite(xMicrometers) ||
        !std::isfinite(yMicrometers) ||
        !std::isfinite(widthMicrometers) ||
        !std::isfinite(heightMicrometers)) {
        return Result<
            SpecimenSlidePlacement,
            SpecimenSlidePlacementError>::failure(
                SpecimenSlidePlacementError::NonFiniteCoordinate);
    }
    if (xMicrometers < 0.0 || yMicrometers < 0.0 ||
        widthMicrometers <= 0.0 || heightMicrometers <= 0.0) {
        return Result<
            SpecimenSlidePlacement,
            SpecimenSlidePlacementError>::failure(
                SpecimenSlidePlacementError::NonPositiveExtent);
    }

    const double right = xMicrometers + widthMicrometers;
    const double bottom = yMicrometers + heightMicrometers;
    if (!std::isfinite(right) || !std::isfinite(bottom) ||
        right > slideGeometry.widthMicrometers() ||
        bottom > slideGeometry.heightMicrometers()) {
        return Result<
            SpecimenSlidePlacement,
            SpecimenSlidePlacementError>::failure(
                SpecimenSlidePlacementError::OutsideSlide);
    }

    return Result<
        SpecimenSlidePlacement,
        SpecimenSlidePlacementError>::success(
            SpecimenSlidePlacement(
                std::move(id),
                std::move(specimenId),
                std::move(slideId),
                std::move(slideRegistrationRevision),
                xMicrometers,
                yMicrometers,
                widthMicrometers,
                heightMicrometers));
}

const SpecimenPlacementId& SpecimenSlidePlacement::id() const noexcept {
    return id_;
}

const SpecimenId& SpecimenSlidePlacement::specimenId() const noexcept {
    return specimenId_;
}

const SlideId& SpecimenSlidePlacement::slideId() const noexcept {
    return slideId_;
}

const CalibrationRevisionId&
SpecimenSlidePlacement::slideRegistrationRevision() const noexcept {
    return slideRegistrationRevision_;
}

double SpecimenSlidePlacement::xMicrometers() const noexcept {
    return xMicrometers_;
}

double SpecimenSlidePlacement::yMicrometers() const noexcept {
    return yMicrometers_;
}

double SpecimenSlidePlacement::widthMicrometers() const noexcept {
    return widthMicrometers_;
}

double SpecimenSlidePlacement::heightMicrometers() const noexcept {
    return heightMicrometers_;
}

} // namespace microscope::core
