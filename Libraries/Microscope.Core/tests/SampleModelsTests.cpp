#include "microscope/core/samples/SampleModels.h"

#include <iostream>
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
        throw std::runtime_error("A sample model fixture factory rejected valid input.");
    }
    return std::move(result).value();
}

template <typename TId>
TId makeId(const char* value) {
    return take(TId::create(value));
}

MicroscopyRequirements makeBloodFilmRequirements() {
    const auto objective = take(ObjectiveMicroscopyRequirements::create(
        10.0,
        100.0,
        0.25,
        {ObjectiveImmersionMedium::Air},
        15.0));
    const auto camera = take(CameraPerformanceRequirements::create(
        30.0,
        2.0,
        std::nullopt,
        20000.0));
    return take(MicroscopyRequirements::create(
        {{ContrastMethod::Brightfield,
          MicroscopyTechniqueConstraintKind::Required},
         {ContrastMethod::PhaseContrast,
          MicroscopyTechniqueConstraintKind::Preferred}},
        objective,
        camera));
}

void testSampleTypeAndCatalogRevisionContracts() {
    const auto sampleTypeId = makeId<SampleTypeId>("sample-type-blood-film");
    const auto typeV1 = take(SampleType::create(
        sampleTypeId,
        1,
        "Peripheral blood film",
        SampleMaterialClass::Biological,
        makeBloodFilmRequirements(),
        "Fixed, stained peripheral blood smear for cellular morphology."));
    const auto typeV2 = take(SampleType::create(
        sampleTypeId,
        2,
        "Peripheral blood film",
        SampleMaterialClass::Biological,
        makeBloodFilmRequirements(),
        "Revised specimen handling and imaging requirements."));

    const auto catalog = take(SampleTypeCatalog::create(4, {typeV2}));
    check(catalog.revision() == 4,
          "retains the sample-type catalog revision");
    check(catalog.find(sampleTypeId) != nullptr &&
          catalog.find(sampleTypeId)->revision() == 2,
          "catalog resolves one current definition revision by stable SampleTypeId");
    check(!SampleTypeCatalog::create(5, {typeV1, typeV2}) &&
          SampleTypeCatalog::create(5, {typeV1, typeV2}).error() ==
              SampleTypeCatalogError::DuplicateSampleTypeId,
          "does not load multiple current revisions of one SampleType into a single catalog");
    check(!SampleType::create(
              makeId<SampleTypeId>("invalid-sample-type"),
              0,
              "Invalid",
              SampleMaterialClass::Biological,
              makeBloodFilmRequirements()) &&
          SampleType::create(
              makeId<SampleTypeId>("invalid-sample-type-2"),
              0,
              "Invalid",
              SampleMaterialClass::Biological,
              makeBloodFilmRequirements()).error() == SampleTypeError::ZeroRevision,
          "rejects an unversioned sample type definition");
}

void testSamplePinsSampleTypeRevisionAndSpecimenPinsSampleRevision() {
    const auto sampleTypeId = makeId<SampleTypeId>("sample-type-cell-culture");
    const auto sampleId = makeId<SampleId>("sample-cell-culture-001");
    const auto sample = take(Sample::create(
        sampleId,
        3,
        sampleTypeId,
        2,
        "Cell culture 001",
        "Caco-2 epithelial cell culture",
        "Homo sapiens",
        "LIMS-ACC-00912",
        "Passage 24, treatment group control."));

    check(sample.sampleTypeId() == sampleTypeId &&
          sample.sampleTypeRevision() == 2,
          "pins the exact SampleType revision used when the Sample was defined");
    check(sample.organismScientificName() ==
              std::optional<std::string>{"Homo sapiens"},
          "retains optional organism identity separately from material description");
    check(sample.externalAccession() ==
              std::optional<std::string>{"LIMS-ACC-00912"},
          "retains an external accession without treating it as an internal identity");

    const auto preparation = take(SpecimenPreparation::create(
        SpecimenPreparationMethod::CellCulture,
        SpecimenFixationState::Fixed,
        std::nullopt,
        170.0,
        "Fluoromount-G",
        1.46));
    const auto specimenA = take(Specimen::create(
        makeId<SpecimenId>("specimen-cell-culture-001-a"),
        1,
        sample.id(),
        sample.revision(),
        "Fixed culture coverslip A",
        preparation));
    const auto specimenB = take(Specimen::create(
        makeId<SpecimenId>("specimen-cell-culture-001-b"),
        1,
        sample.id(),
        sample.revision(),
        "Fixed culture coverslip B",
        preparation));

    check(specimenA.sampleId() == sample.id() &&
          specimenA.sampleRevision() == sample.revision(),
          "pins the exact source Sample revision used to prepare a Specimen");
    check(specimenA.id() != specimenB.id() &&
          specimenA.sampleId() == specimenB.sampleId(),
          "allows one source Sample to produce multiple independently tracked Specimens");
}

void testFluorescentLabelStoresSpectralProfileAndFilterCompatibility() {
    const auto excitation = take(SpectralBand::create(
        take<PositiveNanometers>(PositiveNanometers::create(470.0)),
        take<PositiveNanometers>(PositiveNanometers::create(495.0))));
    const auto emission = take(SpectralBand::create(
        take<PositiveNanometers>(PositiveNanometers::create(500.0)),
        take<PositiveNanometers>(PositiveNanometers::create(550.0))));
    const auto spectrum = take(FluorescenceEmissionSpectrum::create({
        FluorescenceEmissionSample{
            take<PositiveNanometers>(PositiveNanometers::create(500.0)), 0.2},
        FluorescenceEmissionSample{
            take<PositiveNanometers>(PositiveNanometers::create(515.0)), 1.0},
        FluorescenceEmissionSample{
            take<PositiveNanometers>(PositiveNanometers::create(540.0)), 0.1}}));
    const auto filterSetId =
        makeId<OpticalCompatibilityId>("fluorescence-filter-set-green-1");

    const auto label = take(SpecimenLabel::create(
        "Alexa Fluor 488 anti-CD3",
        SpecimenLabelKind::Fluorescent,
        excitation,
        emission,
        spectrum,
        filterSetId));

    const auto preparation = take(SpecimenPreparation::create(
        SpecimenPreparationMethod::TissueSection,
        SpecimenFixationState::Fixed,
        5.0,
        170.0,
        "Antifade mounting medium",
        1.48,
        {label}));

    check(preparation.labels().size() == 1,
          "stores fluorescence labels as specimen preparation facts");
    check(preparation.labels().front().emissionSpectrum().has_value() &&
          preparation.labels().front().emissionSpectrum()->samples().size() == 3,
          "retains the fluorophore emission spectrum rather than a color name alone");
    check(preparation.labels().front().filterSetCompatibilityId() == filterSetId,
          "retains the matched filter-set compatibility identity on the label");

    const auto invalidNonFluorescentLabel = SpecimenLabel::create(
        "H&E",
        SpecimenLabelKind::Histochemical,
        std::nullopt,
        emission);
    check(!invalidNonFluorescentLabel &&
          invalidNonFluorescentLabel.error() ==
              SpecimenLabelError::SpectralMetadataOnNonFluorescentLabel,
          "does not attach fluorescence spectra to a non-fluorescent label");
}

void testFluorescenceMetadataRequirementNeedsFluorescenceTechnique() {
    const auto invalid = MicroscopyRequirements::create(
        {{ContrastMethod::Brightfield,
          MicroscopyTechniqueConstraintKind::Required}},
        std::nullopt,
        std::nullopt,
        true);
    check(!invalid &&
          invalid.error() ==
              MicroscopyRequirementsError::FluorescenceMetadataWithoutFluorescenceTechnique,
          "requires a fluorescence technique declaration before requiring fluorescent label metadata");

    const auto valid = MicroscopyRequirements::create(
        {{ContrastMethod::Fluorescence,
          MicroscopyTechniqueConstraintKind::Required}},
        std::nullopt,
        std::nullopt,
        true);
    check(static_cast<bool>(valid),
          "allows a SampleType to require fluorescence imaging with label metadata");
}

void testSpecimenSlidePlacementIsBoundedAndRegistrationRevisioned() {
    const auto geometry = take(SlideGeometry::create(75000.0, 25000.0));
    const auto placement = take(SpecimenSlidePlacement::create(
        makeId<SpecimenPlacementId>("placement-smear-region-1"),
        makeId<SpecimenId>("specimen-smear-1"),
        makeId<SlideId>("slide-left-001"),
        makeId<CalibrationRevisionId>("slide-left-registration-r7"),
        12000.0,
        4000.0,
        35000.0,
        15000.0,
        geometry));

    check(placement.xMicrometers() == 12000.0 &&
          placement.yMicrometers() == 4000.0 &&
          placement.widthMicrometers() == 35000.0 &&
          placement.heightMicrometers() == 15000.0,
          "stores the specimen footprint in slide-local micrometers");
    check(placement.slideRegistrationRevision() ==
              makeId<CalibrationRevisionId>("slide-left-registration-r7"),
          "binds the placement to the slide registration revision");

    const auto outside = SpecimenSlidePlacement::create(
        makeId<SpecimenPlacementId>("placement-outside"),
        makeId<SpecimenId>("specimen-smear-1"),
        makeId<SlideId>("slide-left-001"),
        makeId<CalibrationRevisionId>("slide-left-registration-r7"),
        70000.0,
        20000.0,
        10000.0,
        6000.0,
        geometry);
    check(!outside &&
          outside.error() == SpecimenSlidePlacementError::OutsideSlide,
          "rejects a specimen region that extends beyond the physical slide geometry");

    const auto invalidExtent = SpecimenSlidePlacement::create(
        makeId<SpecimenPlacementId>("placement-zero-width"),
        makeId<SpecimenId>("specimen-smear-1"),
        makeId<SlideId>("slide-left-001"),
        makeId<CalibrationRevisionId>("slide-left-registration-r7"),
        1.0,
        1.0,
        0.0,
        100.0,
        geometry);
    check(!invalidExtent &&
          invalidExtent.error() ==
              SpecimenSlidePlacementError::NonPositiveExtent,
          "rejects a zero-width specimen placement");
}

} // namespace

int main() {
    testSampleTypeAndCatalogRevisionContracts();
    testSamplePinsSampleTypeRevisionAndSpecimenPinsSampleRevision();
    testFluorescentLabelStoresSpectralProfileAndFilterCompatibility();
    testFluorescenceMetadataRequirementNeedsFluorescenceTechnique();
    testSpecimenSlidePlacementIsBoundedAndRegistrationRevisioned();

    if (failures != 0) {
        std::cerr << failures << " sample model contract test(s) failed.\n";
        return 1;
    }

    std::cout << "All sample model contract tests passed.\n";
    return 0;
}
