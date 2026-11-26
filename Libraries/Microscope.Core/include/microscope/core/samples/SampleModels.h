#pragma once

#include "microscope/core/configuration/Configuration.h"
#include "microscope/core/equipment/IlluminationSpecifications.h"
#include "microscope/core/navigation/DualSlideNavigation.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace microscope::core {

enum class SampleMaterialClass {
    Biological,
    Botanical,
    Geological,
    MaterialScience,
    CalibrationArtifact,
    Other,
    Unknown
};

enum class MicroscopyTechniqueConstraintKind {
    Required,
    Preferred,
    Supported,
    Prohibited
};

struct MicroscopyTechniqueConstraint final {
    ContrastMethod method = ContrastMethod::Unknown;
    MicroscopyTechniqueConstraintKind kind =
        MicroscopyTechniqueConstraintKind::Supported;
};

enum class ObjectiveRequirementsError {
    NoConstraint,
    NonFiniteValue,
    NonPositiveMagnification,
    InvalidNumericalAperture,
    InvertedMagnificationRange,
    NegativeCoverglassMismatch,
    InvalidImmersionMedium,
    DuplicateImmersionMedium
};

// Portable specimen-side objective requirements. They constrain optical
// performance, not the identity of a particular installed objective.
class ObjectiveMicroscopyRequirements final {
public:
    [[nodiscard]] static Result<
        ObjectiveMicroscopyRequirements,
        ObjectiveRequirementsError> create(
            std::optional<double> minimumMagnification = std::nullopt,
            std::optional<double> maximumMagnification = std::nullopt,
            std::optional<double> minimumNumericalAperture = std::nullopt,
            std::vector<ObjectiveImmersionMedium> allowedImmersionMedia = {},
            std::optional<double> maximumCoverglassDesignMismatchMicrometers =
                std::nullopt);

    [[nodiscard]] const std::optional<double>& minimumMagnification() const noexcept;
    [[nodiscard]] const std::optional<double>& maximumMagnification() const noexcept;
    [[nodiscard]] const std::optional<double>& minimumNumericalAperture() const noexcept;
    [[nodiscard]] const std::vector<ObjectiveImmersionMedium>&
    allowedImmersionMedia() const noexcept;
    [[nodiscard]] const std::optional<double>&
    maximumCoverglassDesignMismatchMicrometers() const noexcept;

private:
    ObjectiveMicroscopyRequirements(
        std::optional<double> minimumMagnification,
        std::optional<double> maximumMagnification,
        std::optional<double> minimumNumericalAperture,
        std::vector<ObjectiveImmersionMedium> allowedImmersionMedia,
        std::optional<double> maximumCoverglassDesignMismatchMicrometers);

    std::optional<double> minimumMagnification_;
    std::optional<double> maximumMagnification_;
    std::optional<double> minimumNumericalAperture_;
    std::vector<ObjectiveImmersionMedium> allowedImmersionMedia_;
    std::optional<double> maximumCoverglassDesignMismatchMicrometers_;
};

enum class CameraPerformanceRequirementsError {
    NoConstraint,
    NonFiniteValue,
    NonPositiveFrameRate,
    NegativeReadNoise,
    NegativeDarkCurrent,
    NonPositiveFullWell
};

// These values are suitability requirements. Failure is reported as a warning
// by SampleMicroscopyRequirementAssessor; it does not silently replace a
// manually selected, physically connected and operable camera.
class CameraPerformanceRequirements final {
public:
    [[nodiscard]] static Result<
        CameraPerformanceRequirements,
        CameraPerformanceRequirementsError> create(
            std::optional<double> minimumFrameRateFramesPerSecond = std::nullopt,
            std::optional<double> maximumReadNoiseElectronsRms = std::nullopt,
            std::optional<double> maximumDarkCurrentElectronsPerPixelPerSecond =
                std::nullopt,
            std::optional<double> minimumFullWellCapacityElectrons =
                std::nullopt);

    [[nodiscard]] const std::optional<double>&
    minimumFrameRateFramesPerSecond() const noexcept;
    [[nodiscard]] const std::optional<double>&
    maximumReadNoiseElectronsRms() const noexcept;
    [[nodiscard]] const std::optional<double>&
    maximumDarkCurrentElectronsPerPixelPerSecond() const noexcept;
    [[nodiscard]] const std::optional<double>&
    minimumFullWellCapacityElectrons() const noexcept;

private:
    CameraPerformanceRequirements(
        std::optional<double> minimumFrameRateFramesPerSecond,
        std::optional<double> maximumReadNoiseElectronsRms,
        std::optional<double> maximumDarkCurrentElectronsPerPixelPerSecond,
        std::optional<double> minimumFullWellCapacityElectrons);

    std::optional<double> minimumFrameRateFramesPerSecond_;
    std::optional<double> maximumReadNoiseElectronsRms_;
    std::optional<double> maximumDarkCurrentElectronsPerPixelPerSecond_;
    std::optional<double> minimumFullWellCapacityElectrons_;
};

enum class MicroscopyRequirementsError {
    InvalidTechnique,
    DuplicateTechnique,
    MultipleRequiredTechniques,
    FluorescenceMetadataWithoutFluorescenceTechnique
};

// Requirements are independent of a microscope model or installed component
// IDs. They can be evaluated against different microscope constitutions.
class MicroscopyRequirements final {
public:
    [[nodiscard]] static Result<
        MicroscopyRequirements,
        MicroscopyRequirementsError> create(
            std::vector<MicroscopyTechniqueConstraint> techniques = {},
            std::optional<ObjectiveMicroscopyRequirements> objective =
                std::nullopt,
            std::optional<CameraPerformanceRequirements> camera =
                std::nullopt,
            bool requireFluorescentLabelMetadata = false);

    [[nodiscard]] const std::vector<MicroscopyTechniqueConstraint>&
    techniques() const noexcept;
    [[nodiscard]] const std::optional<ObjectiveMicroscopyRequirements>&
    objective() const noexcept;
    [[nodiscard]] const std::optional<CameraPerformanceRequirements>&
    camera() const noexcept;
    [[nodiscard]] bool requireFluorescentLabelMetadata() const noexcept;

private:
    MicroscopyRequirements(
        std::vector<MicroscopyTechniqueConstraint> techniques,
        std::optional<ObjectiveMicroscopyRequirements> objective,
        std::optional<CameraPerformanceRequirements> camera,
        bool requireFluorescentLabelMetadata);

    std::vector<MicroscopyTechniqueConstraint> techniques_;
    std::optional<ObjectiveMicroscopyRequirements> objective_;
    std::optional<CameraPerformanceRequirements> camera_;
    bool requireFluorescentLabelMetadata_ = false;
};

enum class SampleTypeError {
    EmptyName,
    InvalidName,
    EmptyDescription,
    InvalidDescription,
    ZeroRevision,
    InvalidMaterialClass
};

class SampleType final {
public:
    [[nodiscard]] static Result<SampleType, SampleTypeError> create(
        SampleTypeId id,
        std::uint64_t revision,
        std::string name,
        SampleMaterialClass materialClass,
        MicroscopyRequirements microscopyRequirements,
        std::optional<std::string> description = std::nullopt);

    [[nodiscard]] const SampleTypeId& id() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] SampleMaterialClass materialClass() const noexcept;
    [[nodiscard]] const MicroscopyRequirements& microscopyRequirements() const noexcept;
    [[nodiscard]] const std::optional<std::string>& description() const noexcept;

private:
    SampleType(
        SampleTypeId id,
        std::uint64_t revision,
        std::string name,
        SampleMaterialClass materialClass,
        MicroscopyRequirements microscopyRequirements,
        std::optional<std::string> description);

    SampleTypeId id_;
    std::uint64_t revision_;
    std::string name_;
    SampleMaterialClass materialClass_;
    MicroscopyRequirements microscopyRequirements_;
    std::optional<std::string> description_;
};

enum class SampleTypeCatalogError {
    EmptyCatalog,
    ZeroRevision,
    DuplicateSampleTypeId
};

class SampleTypeCatalog final {
public:
    [[nodiscard]] static Result<SampleTypeCatalog, SampleTypeCatalogError> create(
        std::uint64_t revision,
        std::vector<SampleType> sampleTypes);

    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] const std::vector<SampleType>& sampleTypes() const noexcept;
    [[nodiscard]] const SampleType* find(const SampleTypeId& id) const noexcept;

private:
    SampleTypeCatalog(
        std::uint64_t revision,
        std::vector<SampleType> sampleTypes);

    std::uint64_t revision_;
    std::vector<SampleType> sampleTypes_;
};

enum class SampleError {
    EmptyName,
    InvalidName,
    EmptyBiologicalEntity,
    InvalidBiologicalEntity,
    InvalidOrganismScientificName,
    InvalidExternalAccession,
    InvalidDescription,
    ZeroRevision,
    ZeroSampleTypeRevision
};

// A Sample is the source material / biological entity (REMBI Biosample
// concept), not a glass slide and not a prepared specimen.
class Sample final {
public:
    [[nodiscard]] static Result<Sample, SampleError> create(
        SampleId id,
        std::uint64_t revision,
        SampleTypeId sampleTypeId,
        std::uint64_t sampleTypeRevision,
        std::string name,
        std::string materialDescription,
        std::optional<std::string> organismScientificName = std::nullopt,
        std::optional<std::string> externalAccession = std::nullopt,
        std::optional<std::string> description = std::nullopt);

    [[nodiscard]] const SampleId& id() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] const SampleTypeId& sampleTypeId() const noexcept;
    [[nodiscard]] std::uint64_t sampleTypeRevision() const noexcept;
    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] const std::string& materialDescription() const noexcept;
    [[nodiscard]] const std::optional<std::string>& organismScientificName() const noexcept;
    [[nodiscard]] const std::optional<std::string>& externalAccession() const noexcept;
    [[nodiscard]] const std::optional<std::string>& description() const noexcept;

private:
    Sample(
        SampleId id,
        std::uint64_t revision,
        SampleTypeId sampleTypeId,
        std::uint64_t sampleTypeRevision,
        std::string name,
        std::string materialDescription,
        std::optional<std::string> organismScientificName,
        std::optional<std::string> externalAccession,
        std::optional<std::string> description);

    SampleId id_;
    std::uint64_t revision_;
    SampleTypeId sampleTypeId_;
    std::uint64_t sampleTypeRevision_;
    std::string name_;
    std::string materialDescription_;
    std::optional<std::string> organismScientificName_;
    std::optional<std::string> externalAccession_;
    std::optional<std::string> description_;
};

enum class SpecimenPreparationMethod {
    Smear,
    TissueSection,
    CellCulture,
    WholeMount,
    Suspension,
    Cytospin,
    FreshUnfixed,
    Other,
    Unknown
};

enum class SpecimenFixationState {
    Live,
    Unfixed,
    Fixed,
    Killed,
    Unknown
};

enum class SpecimenLabelKind {
    Fluorescent,
    Chromogenic,
    Histochemical,
    Other
};

enum class SpecimenLabelError {
    EmptyName,
    InvalidName,
    InvalidKind,
    SpectralMetadataOnNonFluorescentLabel
};

class SpecimenLabel final {
public:
    [[nodiscard]] static Result<SpecimenLabel, SpecimenLabelError> create(
        std::string name,
        SpecimenLabelKind kind,
        std::optional<SpectralBand> excitationBand = std::nullopt,
        std::optional<SpectralBand> emissionBand = std::nullopt,
        std::optional<FluorescenceEmissionSpectrum> emissionSpectrum =
            std::nullopt,
        std::optional<OpticalCompatibilityId> filterSetCompatibilityId =
            std::nullopt);

    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] SpecimenLabelKind kind() const noexcept;
    [[nodiscard]] const std::optional<SpectralBand>& excitationBand() const noexcept;
    [[nodiscard]] const std::optional<SpectralBand>& emissionBand() const noexcept;
    [[nodiscard]] const std::optional<FluorescenceEmissionSpectrum>&
    emissionSpectrum() const noexcept;
    [[nodiscard]] const std::optional<OpticalCompatibilityId>&
    filterSetCompatibilityId() const noexcept;

private:
    SpecimenLabel(
        std::string name,
        SpecimenLabelKind kind,
        std::optional<SpectralBand> excitationBand,
        std::optional<SpectralBand> emissionBand,
        std::optional<FluorescenceEmissionSpectrum> emissionSpectrum,
        std::optional<OpticalCompatibilityId> filterSetCompatibilityId);

    std::string name_;
    SpecimenLabelKind kind_;
    std::optional<SpectralBand> excitationBand_;
    std::optional<SpectralBand> emissionBand_;
    std::optional<FluorescenceEmissionSpectrum> emissionSpectrum_;
    std::optional<OpticalCompatibilityId> filterSetCompatibilityId_;
};

enum class SpecimenPreparationError {
    InvalidMethod,
    InvalidFixationState,
    InvalidSectionThickness,
    InvalidCoverglassThickness,
    InvalidMountingMediumName,
    InvalidMountingMediumRefractiveIndex,
    DuplicateLabel
};

// Actual preparation facts for one specimen instance. Unknown physical facts
// remain absent; they are not filled from the SampleType template.
class SpecimenPreparation final {
public:
    [[nodiscard]] static Result<SpecimenPreparation, SpecimenPreparationError> create(
        SpecimenPreparationMethod method,
        SpecimenFixationState fixationState,
        std::optional<double> sectionThicknessMicrometers = std::nullopt,
        std::optional<double> coverglassThicknessMicrometers = std::nullopt,
        std::optional<std::string> mountingMediumName = std::nullopt,
        std::optional<double> mountingMediumRefractiveIndex = std::nullopt,
        std::vector<SpecimenLabel> labels = {});

    [[nodiscard]] SpecimenPreparationMethod method() const noexcept;
    [[nodiscard]] SpecimenFixationState fixationState() const noexcept;
    [[nodiscard]] const std::optional<double>& sectionThicknessMicrometers() const noexcept;
    [[nodiscard]] const std::optional<double>& coverglassThicknessMicrometers() const noexcept;
    [[nodiscard]] const std::optional<std::string>& mountingMediumName() const noexcept;
    [[nodiscard]] const std::optional<double>& mountingMediumRefractiveIndex() const noexcept;
    [[nodiscard]] const std::vector<SpecimenLabel>& labels() const noexcept;

private:
    SpecimenPreparation(
        SpecimenPreparationMethod method,
        SpecimenFixationState fixationState,
        std::optional<double> sectionThicknessMicrometers,
        std::optional<double> coverglassThicknessMicrometers,
        std::optional<std::string> mountingMediumName,
        std::optional<double> mountingMediumRefractiveIndex,
        std::vector<SpecimenLabel> labels);

    SpecimenPreparationMethod method_;
    SpecimenFixationState fixationState_;
    std::optional<double> sectionThicknessMicrometers_;
    std::optional<double> coverglassThicknessMicrometers_;
    std::optional<std::string> mountingMediumName_;
    std::optional<double> mountingMediumRefractiveIndex_;
    std::vector<SpecimenLabel> labels_;
};

enum class SpecimenError {
    EmptyName,
    InvalidName,
    ZeroRevision,
    ZeroSampleRevision
};

// A Specimen is one prepared physical imaging instance derived from an exact
// Sample revision. A sample may produce many independently prepared specimens.
class Specimen final {
public:
    [[nodiscard]] static Result<Specimen, SpecimenError> create(
        SpecimenId id,
        std::uint64_t revision,
        SampleId sampleId,
        std::uint64_t sampleRevision,
        std::string name,
        SpecimenPreparation preparation);

    [[nodiscard]] const SpecimenId& id() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] const SampleId& sampleId() const noexcept;
    [[nodiscard]] std::uint64_t sampleRevision() const noexcept;
    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] const SpecimenPreparation& preparation() const noexcept;

private:
    Specimen(
        SpecimenId id,
        std::uint64_t revision,
        SampleId sampleId,
        std::uint64_t sampleRevision,
        std::string name,
        SpecimenPreparation preparation);

    SpecimenId id_;
    std::uint64_t revision_;
    SampleId sampleId_;
    std::uint64_t sampleRevision_;
    std::string name_;
    SpecimenPreparation preparation_;
};

enum class SpecimenSlidePlacementError {
    NonFiniteCoordinate,
    NonPositiveExtent,
    OutsideSlide
};

// A specimen can occupy a bounded region of a physical slide. The placement is
// bound to a slide-registration revision, so navigation can reject stale maps.
class SpecimenSlidePlacement final {
public:
    [[nodiscard]] static Result<
        SpecimenSlidePlacement,
        SpecimenSlidePlacementError> create(
            SpecimenPlacementId id,
            SpecimenId specimenId,
            SlideId slideId,
            CalibrationRevisionId slideRegistrationRevision,
            double xMicrometers,
            double yMicrometers,
            double widthMicrometers,
            double heightMicrometers,
            const SlideGeometry& slideGeometry);

    [[nodiscard]] const SpecimenPlacementId& id() const noexcept;
    [[nodiscard]] const SpecimenId& specimenId() const noexcept;
    [[nodiscard]] const SlideId& slideId() const noexcept;
    [[nodiscard]] const CalibrationRevisionId& slideRegistrationRevision() const noexcept;
    [[nodiscard]] double xMicrometers() const noexcept;
    [[nodiscard]] double yMicrometers() const noexcept;
    [[nodiscard]] double widthMicrometers() const noexcept;
    [[nodiscard]] double heightMicrometers() const noexcept;

private:
    SpecimenSlidePlacement(
        SpecimenPlacementId id,
        SpecimenId specimenId,
        SlideId slideId,
        CalibrationRevisionId slideRegistrationRevision,
        double xMicrometers,
        double yMicrometers,
        double widthMicrometers,
        double heightMicrometers);

    SpecimenPlacementId id_;
    SpecimenId specimenId_;
    SlideId slideId_;
    CalibrationRevisionId slideRegistrationRevision_;
    double xMicrometers_;
    double yMicrometers_;
    double widthMicrometers_;
    double heightMicrometers_;
};

} // namespace microscope::core
