#pragma once

#include "microscope/core/equipment/MicroscopyTechniques.h"
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

enum class CondenserRequirementsError {
    NoConstraint,
    NonFiniteValue,
    InvalidAperture,
    InvertedApertureRange,
    InvalidDesign,
    DuplicateDesign,
    InvalidImmersionMedium,
    DuplicateImmersionMedium
};

// SampleType-level condenser requirements use optical performance and capability,
// not a microscope-specific condenser component identity.
class CondenserMicroscopyRequirements final {
public:
    [[nodiscard]] static Result<
        CondenserMicroscopyRequirements,
        CondenserRequirementsError> create(
            std::optional<double> minimumApertureNA = std::nullopt,
            std::optional<double> maximumApertureNA = std::nullopt,
            std::vector<CondenserDesign> allowedDesigns = {},
            std::vector<CondenserImmersionMedium> allowedImmersionMedia = {});

    [[nodiscard]] const std::optional<double>& minimumApertureNA() const noexcept;
    [[nodiscard]] const std::optional<double>& maximumApertureNA() const noexcept;
    [[nodiscard]] const std::vector<CondenserDesign>& allowedDesigns() const noexcept;
    [[nodiscard]] const std::vector<CondenserImmersionMedium>&
    allowedImmersionMedia() const noexcept;

private:
    CondenserMicroscopyRequirements(
        std::optional<double> minimumApertureNA,
        std::optional<double> maximumApertureNA,
        std::vector<CondenserDesign> allowedDesigns,
        std::vector<CondenserImmersionMedium> allowedImmersionMedia);

    std::optional<double> minimumApertureNA_;
    std::optional<double> maximumApertureNA_;
    std::vector<CondenserDesign> allowedDesigns_;
    std::vector<CondenserImmersionMedium> allowedImmersionMedia_;
};

enum class IlluminationRequirementsError {
    NoConstraint,
    NonFiniteValue,
    NegativeMinimumIrradiance,
    NonPositiveMaximumIrradiance,
    InvertedIrradianceRange
};

// Absolute specimen-plane irradiance constraints use the same physical unit as
// LightPathCalibration. They are never inferred from a lamp drive percentage.
class IlluminationMicroscopyRequirements final {
public:
    [[nodiscard]] static Result<
        IlluminationMicroscopyRequirements,
        IlluminationRequirementsError> create(
            std::optional<double> minimumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter =
                std::nullopt,
            std::optional<double> maximumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter =
                std::nullopt,
            std::optional<SpectralBand> requiredSpectralBand = std::nullopt,
            bool requireContinuouslyAdjustableSource = false);

    [[nodiscard]] const std::optional<double>&
    minimumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter() const noexcept;
    [[nodiscard]] const std::optional<double>&
    maximumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter() const noexcept;
    [[nodiscard]] const std::optional<SpectralBand>& requiredSpectralBand() const noexcept;
    [[nodiscard]] bool requireContinuouslyAdjustableSource() const noexcept;

private:
    IlluminationMicroscopyRequirements(
        std::optional<double> minimumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter,
        std::optional<double> maximumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter,
        std::optional<SpectralBand> requiredSpectralBand,
        bool requireContinuouslyAdjustableSource);

    std::optional<double> minimumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter_;
    std::optional<double> maximumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter_;
    std::optional<SpectralBand> requiredSpectralBand_;
    bool requireContinuouslyAdjustableSource_ = false;
};

enum class CameraPerformanceRequirementsError {
    NoConstraint,
    NonFiniteValue,
    NonPositiveFrameRate,
    NegativeReadNoise,
    NegativeDarkCurrent,
    NonPositiveFullWell,
    InvalidQuantumEfficiency
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
                std::nullopt,
            std::optional<double> minimumMeanQuantumEfficiency =
                std::nullopt,
            bool preferMonochrome = false,
            bool preferGlobalShutter = false);

    [[nodiscard]] const std::optional<double>&
    minimumFrameRateFramesPerSecond() const noexcept;
    [[nodiscard]] const std::optional<double>&
    maximumReadNoiseElectronsRms() const noexcept;
    [[nodiscard]] const std::optional<double>&
    maximumDarkCurrentElectronsPerPixelPerSecond() const noexcept;
    [[nodiscard]] const std::optional<double>&
    minimumFullWellCapacityElectrons() const noexcept;
    [[nodiscard]] const std::optional<double>&
    minimumMeanQuantumEfficiency() const noexcept;
    [[nodiscard]] bool preferMonochrome() const noexcept;
    [[nodiscard]] bool preferGlobalShutter() const noexcept;

private:
    CameraPerformanceRequirements(
        std::optional<double> minimumFrameRateFramesPerSecond,
        std::optional<double> maximumReadNoiseElectronsRms,
        std::optional<double> maximumDarkCurrentElectronsPerPixelPerSecond,
        std::optional<double> minimumFullWellCapacityElectrons,
        std::optional<double> minimumMeanQuantumEfficiency,
        bool preferMonochrome,
        bool preferGlobalShutter);

    std::optional<double> minimumFrameRateFramesPerSecond_;
    std::optional<double> maximumReadNoiseElectronsRms_;
    std::optional<double> maximumDarkCurrentElectronsPerPixelPerSecond_;
    std::optional<double> minimumFullWellCapacityElectrons_;
    std::optional<double> minimumMeanQuantumEfficiency_;
    bool preferMonochrome_ = false;
    bool preferGlobalShutter_ = false;
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
            bool requireFluorescentLabelMetadata = false,
            std::optional<CondenserMicroscopyRequirements> condenser =
                std::nullopt,
            std::optional<IlluminationMicroscopyRequirements> illumination =
                std::nullopt);

    [[nodiscard]] const std::vector<MicroscopyTechniqueConstraint>&
    techniques() const noexcept;
    [[nodiscard]] const std::optional<ObjectiveMicroscopyRequirements>&
    objective() const noexcept;
    [[nodiscard]] const std::optional<CameraPerformanceRequirements>&
    camera() const noexcept;
    [[nodiscard]] bool requireFluorescentLabelMetadata() const noexcept;
    [[nodiscard]] const std::optional<CondenserMicroscopyRequirements>&
    condenser() const noexcept;
    [[nodiscard]] const std::optional<IlluminationMicroscopyRequirements>&
    illumination() const noexcept;

private:
    MicroscopyRequirements(
        std::vector<MicroscopyTechniqueConstraint> techniques,
        std::optional<ObjectiveMicroscopyRequirements> objective,
        std::optional<CameraPerformanceRequirements> camera,
        bool requireFluorescentLabelMetadata,
        std::optional<CondenserMicroscopyRequirements> condenser,
        std::optional<IlluminationMicroscopyRequirements> illumination);

    std::vector<MicroscopyTechniqueConstraint> techniques_;
    std::optional<ObjectiveMicroscopyRequirements> objective_;
    std::optional<CameraPerformanceRequirements> camera_;
    bool requireFluorescentLabelMetadata_ = false;
    std::optional<CondenserMicroscopyRequirements> condenser_;
    std::optional<IlluminationMicroscopyRequirements> illumination_;
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

enum class SlidePreparationRequirementsError {
    NoConstraint,
    InvalidMethod,
    DuplicateMethod,
    InvalidFixationState,
    DuplicateFixationState,
    NonFiniteThickness,
    InvalidSectionThicknessRange,
    InvalidCoverglassThicknessRange,
    InvalidLabelKind,
    DuplicateLabelKind
};

// Constraints on actual slide preparation. SampleType stores the reusable preparation envelope; Slide stores the observed
// preparation snapshot.
class SlidePreparationRequirements final {
public:
    [[nodiscard]] static Result<
        SlidePreparationRequirements,
        SlidePreparationRequirementsError> create(
            std::vector<SpecimenPreparationMethod> allowedMethods = {},
            std::vector<SpecimenFixationState> allowedFixationStates = {},
            std::optional<double> minimumSectionThicknessMicrometers = std::nullopt,
            std::optional<double> maximumSectionThicknessMicrometers = std::nullopt,
            std::optional<double> minimumCoverglassThicknessMicrometers = std::nullopt,
            std::optional<double> maximumCoverglassThicknessMicrometers = std::nullopt,
            bool requireMountingMedium = false,
            std::vector<SpecimenLabelKind> requiredLabelKinds = {});

    [[nodiscard]] const std::vector<SpecimenPreparationMethod>& allowedMethods() const noexcept;
    [[nodiscard]] const std::vector<SpecimenFixationState>& allowedFixationStates() const noexcept;
    [[nodiscard]] const std::optional<double>& minimumSectionThicknessMicrometers() const noexcept;
    [[nodiscard]] const std::optional<double>& maximumSectionThicknessMicrometers() const noexcept;
    [[nodiscard]] const std::optional<double>& minimumCoverglassThicknessMicrometers() const noexcept;
    [[nodiscard]] const std::optional<double>& maximumCoverglassThicknessMicrometers() const noexcept;
    [[nodiscard]] bool requireMountingMedium() const noexcept;
    [[nodiscard]] const std::vector<SpecimenLabelKind>& requiredLabelKinds() const noexcept;

private:
    SlidePreparationRequirements(
        std::vector<SpecimenPreparationMethod> allowedMethods,
        std::vector<SpecimenFixationState> allowedFixationStates,
        std::optional<double> minimumSectionThicknessMicrometers,
        std::optional<double> maximumSectionThicknessMicrometers,
        std::optional<double> minimumCoverglassThicknessMicrometers,
        std::optional<double> maximumCoverglassThicknessMicrometers,
        bool requireMountingMedium,
        std::vector<SpecimenLabelKind> requiredLabelKinds);

    std::vector<SpecimenPreparationMethod> allowedMethods_;
    std::vector<SpecimenFixationState> allowedFixationStates_;
    std::optional<double> minimumSectionThicknessMicrometers_;
    std::optional<double> maximumSectionThicknessMicrometers_;
    std::optional<double> minimumCoverglassThicknessMicrometers_;
    std::optional<double> maximumCoverglassThicknessMicrometers_;
    bool requireMountingMedium_ = false;
    std::vector<SpecimenLabelKind> requiredLabelKinds_;
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
        std::optional<std::string> description = std::nullopt,
        std::optional<SlidePreparationRequirements> preparationRequirements =
            std::nullopt);

    [[nodiscard]] const SampleTypeId& id() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] SampleMaterialClass materialClass() const noexcept;
    [[nodiscard]] const MicroscopyRequirements& microscopyRequirements() const noexcept;
    [[nodiscard]] const std::optional<std::string>& description() const noexcept;
    [[nodiscard]] const std::optional<SlidePreparationRequirements>&
    preparationRequirements() const noexcept;
private:
    SampleType(
        SampleTypeId id,
        std::uint64_t revision,
        std::string name,
        SampleMaterialClass materialClass,
        MicroscopyRequirements microscopyRequirements,
        std::optional<std::string> description,
        std::optional<SlidePreparationRequirements> preparationRequirements);

    SampleTypeId id_;
    std::uint64_t revision_;
    std::string name_;
    SampleMaterialClass materialClass_;
    MicroscopyRequirements microscopyRequirements_;
    std::optional<std::string> description_;
    std::optional<SlidePreparationRequirements> preparationRequirements_;
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

enum class SlideError {
    EmptyName,
    InvalidName,
    ZeroRevision,
    ZeroSampleTypeRevision
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

enum class SlidePreparationError {
    InvalidMethod,
    InvalidFixationState,
    InvalidSectionThickness,
    InvalidCoverglassThickness,
    InvalidMountingMediumName,
    InvalidMountingMediumRefractiveIndex,
    DuplicateLabel
};

// Actual preparation facts recorded for one physical slide. Unknown physical
// facts remain absent; they are not filled from the SampleType requirements.
class SlidePreparation final {
public:
    [[nodiscard]] static Result<SlidePreparation, SlidePreparationError> create(
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
    SlidePreparation(
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


class Slide final {
public:
    [[nodiscard]] static Result<Slide, SlideError> create(
        SlideId id,
        std::uint64_t revision,
        SampleTypeId sampleTypeId,
        std::uint64_t sampleTypeRevision,
        std::string name,
        SlideGeometry geometry,
        SlidePreparation preparation,
        std::optional<OriginLabId> originLabId = std::nullopt);

    [[nodiscard]] const SlideId& id() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] const SampleTypeId& sampleTypeId() const noexcept;
    [[nodiscard]] std::uint64_t sampleTypeRevision() const noexcept;
    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] const SlideGeometry& geometry() const noexcept;
    [[nodiscard]] const SlidePreparation& preparation() const noexcept;
    [[nodiscard]] const std::optional<OriginLabId>& originLabId() const noexcept;

private:
    Slide(
        SlideId id,
        std::uint64_t revision,
        SampleTypeId sampleTypeId,
        std::uint64_t sampleTypeRevision,
        std::string name,
        SlideGeometry geometry,
        SlidePreparation preparation,
        std::optional<OriginLabId> originLabId);

    SlideId id_;
    std::uint64_t revision_;
    SampleTypeId sampleTypeId_;
    std::uint64_t sampleTypeRevision_;
    std::string name_;
    SlideGeometry geometry_;
    SlidePreparation preparation_;
    std::optional<OriginLabId> originLabId_;
};

} // namespace microscope::core
