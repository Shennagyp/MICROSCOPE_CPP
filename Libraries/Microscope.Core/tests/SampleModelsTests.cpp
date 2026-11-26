#include "microscope/core/samples/SampleModels.h"

#include <iostream>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using namespace microscope::core;
int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

template <typename TValue, typename TError>
TValue take(Result<TValue, TError> result) {
    if (!result) {
        throw std::runtime_error("A sample/slide model fixture rejected valid input.");
    }
    return std::move(result).value();
}

template <typename TId>
TId makeId(const char* value) { return take(TId::create(value)); }

MicroscopyRequirements makeMicroscopyRequirements() {
    const auto objective = take(ObjectiveMicroscopyRequirements::create(
        10.0, 100.0, 0.25, {ObjectiveImmersionMedium::Air}, 15.0));
    const auto camera = take(CameraPerformanceRequirements::create(
        30.0, 2.0, std::nullopt, 20000.0));
    return take(MicroscopyRequirements::create(
        {{ContrastMethod::Brightfield, MicroscopyTechniqueConstraintKind::Required},
         {ContrastMethod::PhaseContrast, MicroscopyTechniqueConstraintKind::Preferred}},
        objective, camera));
}

SlidePreparationRequirements makePreparationRequirements() {
    return take(SlidePreparationRequirements::create(
        {SpecimenPreparationMethod::TissueSection},
        {SpecimenFixationState::Fixed},
        3.0, 8.0, 150.0, 190.0, true,
        {SpecimenLabelKind::Fluorescent}));
}

SampleType makeSampleType(
    const SampleTypeId& id,
    std::uint64_t revision,
    std::optional<SlidePreparationRequirements> preparation = std::nullopt) {
    return take(SampleType::create(
        id, revision, "Peripheral blood film",
        SampleMaterialClass::Biological,
        makeMicroscopyRequirements(),
        "Fixed, stained material for cellular morphology.",
        std::move(preparation)));
}

SlidePreparation makePreparation(std::vector<SpecimenLabel> labels = {}) {
    return take(SlidePreparation::create(
        SpecimenPreparationMethod::TissueSection,
        SpecimenFixationState::Fixed,
        5.0, 170.0, "Antifade mounting medium", 1.48,
        std::move(labels)));
}

void testSampleTypeAndCatalogRevisionContracts() {
    const auto typeId = makeId<SampleTypeId>("sample-type-blood-film");
    const auto v1 = makeSampleType(typeId, 1);
    const auto v2 = makeSampleType(typeId, 2);
    const auto catalog = take(SampleTypeCatalog::create(4, {v2}));
    check(catalog.revision() == 4, "retains the SampleType catalog revision");
    check(catalog.find(typeId) && catalog.find(typeId)->revision() == 2,
          "resolves the current SampleType by stable identity");
    const auto duplicate = SampleTypeCatalog::create(5, {v1, v2});
    check(!duplicate && duplicate.error() == SampleTypeCatalogError::DuplicateSampleTypeId,
          "does not load multiple revisions as simultaneous current catalog entries");
    const auto zero = SampleType::create(
        makeId<SampleTypeId>("sample-type-zero"), 0, "Invalid",
        SampleMaterialClass::Biological, makeMicroscopyRequirements());
    check(!zero && zero.error() == SampleTypeError::ZeroRevision,
          "rejects an unversioned SampleType");
}

void testSlidesPinSampleTypeAndOwnIndependentDetails() {
    const auto type = makeSampleType(
        makeId<SampleTypeId>("sample-type-fixed-section"),
        7, makePreparationRequirements());
    const auto geometry = take(SlideGeometry::create(75000.0, 25000.0));
    const auto originLab = makeId<OriginLabId>("origin-lab-cairo");
    const auto slideA = take(Slide::create(
        makeId<SlideId>("slide-a"), 1, type.id(), type.revision(),
        "BLOOD-001", "Slide A", geometry, makePreparation()));
    const auto slideB = take(Slide::create(
        makeId<SlideId>("slide-b"), 1, type.id(), type.revision(),
        "BLOOD-002", "Slide B", geometry, makePreparation(), {}, originLab));

    check(slideA.id() != slideB.id(), "each physical slide has a distinct identity");
    check(slideA.sampleTypeId() == type.id() &&
          slideA.sampleTypeRevision() == type.revision() &&
          slideB.sampleTypeId() == type.id() &&
          slideB.sampleTypeRevision() == type.revision(),
          "independent slides directly pin the same exact SampleType revision");
    check(!slideA.originLabId() && slideB.originLabId() == originLab,
          "OriginLabId is optional and retained only when supplied");
    check(slideA.geometry().widthMicrometers() == 75000.0 &&
          slideA.preparation().sectionThicknessMicrometers() == std::optional<double>{5.0},
          "geometry and observed preparation facts are owned by the Slide");
    check(type.preparationRequirements().has_value(),
          "reusable preparation requirements are owned by SampleType");
}

void testSlidePreservesSampleTypeRevisionAndRejectsInvalidRevisions() {
    const auto typeId = makeId<SampleTypeId>("sample-type-versioned");
    const auto v1 = makeSampleType(typeId, 1);
    const auto v2 = makeSampleType(typeId, 2);
    const auto geometry = take(SlideGeometry::create(75000.0, 25000.0));
    const auto historicalSlide = take(Slide::create(
        makeId<SlideId>("slide-pinned-to-v1"), 3, v1.id(), v1.revision(),
        "HIST-003", "Slide prepared under SampleType v1",
        geometry, makePreparation()));
    check(historicalSlide.sampleTypeId() == v2.id() &&
          historicalSlide.sampleTypeRevision() == 1 && v2.revision() == 2,
          "does not silently rebind an existing Slide when SampleType advances");

    const auto zeroSlideRevision = Slide::create(
        makeId<SlideId>("slide-zero-revision"), 0, typeId, 1,
        "INVALID-001", "Invalid", geometry, makePreparation());
    check(!zeroSlideRevision && zeroSlideRevision.error() == SlideError::ZeroRevision,
          "rejects an unversioned Slide record");

    const auto zeroTypeRevision = Slide::create(
        makeId<SlideId>("slide-zero-type-revision"), 1, typeId, 0,
        "INVALID-002", "Invalid", geometry, makePreparation());
    check(!zeroTypeRevision &&
          zeroTypeRevision.error() == SlideError::ZeroSampleTypeRevision,
          "rejects a Slide without a pinned SampleType revision");
}

void testStainIdentityAndOptionalSuitabilityAreAdvisory() {
    const auto stainId = makeId<StainId>("stain-he");
    const auto identityOnly = take(Stain::create(stainId, "Hematoxylin and eosin"));
    check(!identityOnly.microscopySuitability(),
          "Stain is valid with identity only and no suitability metadata");

    const auto suitability = take(StainMicroscopySuitability::create({
        ContrastMethod::Brightfield, ContrastMethod::PolarizedLight}));
    const auto stain = take(Stain::create(
        stainId, "Hematoxylin and eosin", "Routine histological stain",
        suitability));
    check(stain.microscopySuitability() &&
          stain.microscopySuitability()->recommendedContrastMethods().size() == 2,
          "retains optional recommended contrast methods");

    const auto duplicateMethods = StainMicroscopySuitability::create({
        ContrastMethod::Brightfield, ContrastMethod::Brightfield});
    check(!duplicateMethods &&
          duplicateMethods.error() ==
              StainMicroscopySuitabilityError::DuplicateRecommendedContrastMethod,
          "rejects duplicate stain recommendations");
    const auto unknownMethod = StainMicroscopySuitability::create({
        ContrastMethod::Unknown});
    check(!unknownMethod &&
          unknownMethod.error() ==
              StainMicroscopySuitabilityError::InvalidRecommendedContrastMethod,
          "rejects unknown methods in explicit recommendations");

    const auto typeId = makeId<SampleTypeId>("sample-type-stained-slide");
    const auto geometry = take(SlideGeometry::create(75000.0, 25000.0));
    const auto slide = take(Slide::create(
        makeId<SlideId>("slide-stained-he"), 1, typeId, 1,
        "HE-001", "Kidney cortex", geometry, makePreparation(), {stain}));
    check(slide.code() == std::optional<std::string>{"HE-001"} &&
          slide.stains().size() == 1 &&
          slide.stains().front().id() == stainId,
          "Slide owns its code and stain identity");

    const auto duplicateStains = Slide::create(
        makeId<SlideId>("slide-duplicate-stains"), 1, typeId, 1,
        "HE-002", "Duplicate stains", geometry, makePreparation(),
        {stain, stain});
    check(!duplicateStains &&
          duplicateStains.error() == SlideError::DuplicateStain,
          "rejects duplicate stain identities on one Slide");

    const auto legacy = take(Slide::restoreLegacy(
        makeId<SlideId>("slide-legacy-no-code"), 1, typeId, 1,
        "Legacy slide", geometry, makePreparation()));
    check(!legacy.code() && legacy.stains().empty(),
          "legacy Slide restoration preserves missing code and stain facts");
}

void testPreparationRequirementsDoNotPopulateObservedSlideFacts() {
    const auto requirements = makePreparationRequirements();
    check(requirements.allowedMethods().front() == SpecimenPreparationMethod::TissueSection,
          "constrains preparation method independently of observed Slide values");
    check(requirements.requireMountingMedium() &&
          requirements.requiredLabelKinds().front() == SpecimenLabelKind::Fluorescent,
          "can require a mounting medium and label kinds");
    const auto type = makeSampleType(
        makeId<SampleTypeId>("sample-type-preparation"), 1, requirements);
    check(type.preparationRequirements() &&
          type.preparationRequirements()->maximumSectionThicknessMicrometers() ==
              std::optional<double>{8.0},
          "pins a reusable preparation envelope to the SampleType revision");

    const auto inverted = SlidePreparationRequirements::create(
        {SpecimenPreparationMethod::TissueSection}, {}, 8.0, 3.0);
    check(!inverted &&
          inverted.error() == SlidePreparationRequirementsError::InvalidSectionThicknessRange,
          "rejects an inverted preparation thickness interval");
    const auto duplicate = SlidePreparationRequirements::create(
        {SpecimenPreparationMethod::TissueSection, SpecimenPreparationMethod::TissueSection});
    check(!duplicate &&
          duplicate.error() == SlidePreparationRequirementsError::DuplicateMethod,
          "rejects duplicate allowed preparation methods");

    const auto unknownFacts = take(SlidePreparation::create(
        SpecimenPreparationMethod::TissueSection, SpecimenFixationState::Fixed));
    check(!unknownFacts.sectionThicknessMicrometers() &&
          !unknownFacts.coverglassThicknessMicrometers() &&
          !unknownFacts.mountingMediumName(),
          "leaves unknown actual preparation facts absent rather than copying requirements");
}

void testCondenserAndIlluminationRequirements() {
    const auto condenser = take(CondenserMicroscopyRequirements::create(
        0.25, 0.90, {CondenserDesign::Abbe, CondenserDesign::Achromatic},
        {CondenserImmersionMedium::Air}));
    check(condenser.minimumApertureNA() == std::optional<double>{0.25} &&
          condenser.maximumApertureNA() == std::optional<double>{0.90},
          "retains condenser aperture limits in the SampleType contract");

    const auto band = take(SpectralBand::create(
        take(PositiveNanometers::create(500.0)),
        take(PositiveNanometers::create(560.0))));
    const auto illumination = take(IlluminationMicroscopyRequirements::create(
        25.0, 125.0, band, true));
    check(illumination.minimumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter() ==
              std::optional<double>{25.0} &&
          illumination.maximumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter() ==
              std::optional<double>{125.0} &&
          illumination.requiredSpectralBand().has_value() &&
          illumination.requireContinuouslyAdjustableSource(),
          "stores calibrated specimen-plane irradiance, spectrum and control requirements");

    const auto inverted = IlluminationMicroscopyRequirements::create(150.0, 100.0);
    check(!inverted &&
          inverted.error() == IlluminationRequirementsError::InvertedIrradianceRange,
          "rejects inverted illumination bounds");
    const auto requirements = take(MicroscopyRequirements::create(
        {{ContrastMethod::Brightfield, MicroscopyTechniqueConstraintKind::Required}},
        std::nullopt, std::nullopt, false, condenser, illumination));
    check(requirements.condenser() && requirements.illumination(),
          "composes condenser and illumination constraints into one microscopy contract");
}

void testFluorescentSlideLabelPreservesSpectralProfile() {
    const auto excitation = take(SpectralBand::create(
        take(PositiveNanometers::create(470.0)),
        take(PositiveNanometers::create(495.0))));
    const auto emission = take(SpectralBand::create(
        take(PositiveNanometers::create(500.0)),
        take(PositiveNanometers::create(550.0))));
    const auto spectrum = take(FluorescenceEmissionSpectrum::create({
        {take(PositiveNanometers::create(500.0)), 0.2},
        {take(PositiveNanometers::create(515.0)), 1.0},
        {take(PositiveNanometers::create(540.0)), 0.1}}));
    const auto filterId = makeId<OpticalCompatibilityId>("fluorescence-filter-set-green-1");
    const auto label = take(SpecimenLabel::create(
        "Alexa Fluor 488 anti-CD3", SpecimenLabelKind::Fluorescent,
        excitation, emission, spectrum, filterId));
    const auto preparation = makePreparation({label});
    check(preparation.labels().size() == 1 &&
          preparation.labels().front().emissionSpectrum() &&
          preparation.labels().front().emissionSpectrum()->samples().size() == 3,
          "retains the emission profile on the Slide preparation record");
    check(preparation.labels().front().filterSetCompatibilityId() == filterId,
          "retains filter-set compatibility identity on the fluorescent label");
    const auto invalidLabel = SpecimenLabel::create(
        "H&E", SpecimenLabelKind::Histochemical, std::nullopt, emission);
    check(!invalidLabel &&
          invalidLabel.error() == SpecimenLabelError::SpectralMetadataOnNonFluorescentLabel,
          "rejects fluorescence metadata on a non-fluorescent label");
}

void testFluorescenceMetadataRequiresFluorescenceTechnique() {
    const auto invalid = MicroscopyRequirements::create(
        {{ContrastMethod::Brightfield, MicroscopyTechniqueConstraintKind::Required}},
        std::nullopt, std::nullopt, true);
    check(!invalid &&
          invalid.error() ==
              MicroscopyRequirementsError::FluorescenceMetadataWithoutFluorescenceTechnique,
          "does not allow fluorescence-label metadata requirements without a fluorescence method");
    const auto valid = MicroscopyRequirements::create(
        {{ContrastMethod::Fluorescence, MicroscopyTechniqueConstraintKind::Required}},
        std::nullopt, std::nullopt, true);
    check(static_cast<bool>(valid),
          "allows fluorescence imaging requirements with explicit label metadata");
}

} // namespace

int main() {
    testSampleTypeAndCatalogRevisionContracts();
    testSlidesPinSampleTypeAndOwnIndependentDetails();
    testSlidePreservesSampleTypeRevisionAndRejectsInvalidRevisions();
    testStainIdentityAndOptionalSuitabilityAreAdvisory();
    testPreparationRequirementsDoNotPopulateObservedSlideFacts();
    testCondenserAndIlluminationRequirements();
    testFluorescentSlideLabelPreservesSpectralProfile();
    testFluorescenceMetadataRequiresFluorescenceTechnique();
    if (failures != 0) {
        std::cerr << failures << " sample/slide model contract test(s) failed.\n";
        return 1;
    }
    std::cout << "All sample/slide model contract tests passed.\n";
    return 0;
}
