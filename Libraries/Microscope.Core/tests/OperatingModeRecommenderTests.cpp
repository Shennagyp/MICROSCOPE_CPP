#include "microscope/core/configuration/OperatingModeRecommender.h"

#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
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

template <typename T, typename E>
T take(Result<T, E> result) {
    if (!result) {
        throw std::runtime_error("OperatingMode recommender fixture rejected valid input");
    }
    return std::move(result).value();
}

template <typename TId>
TId id(const char* value) { return take(TId::create(value)); }

OperationMode makeMode(const char* modeId, ContrastMethod method) {
    const auto microscopeId = id<MicroscopeId>("recommendation-microscope");
    const auto typedModeId = id<OperationModeId>(modeId);
    return take(OperationMode::create(
        microscopeId, typedModeId, 1, modeId, method));
}

OperationMode makeModeForMicroscope(
    const char* microscopeValue,
    const char* modeValue,
    ContrastMethod method) {
    return take(OperationMode::create(
        id<MicroscopeId>(microscopeValue),
        id<OperationModeId>(modeValue),
        1,
        modeValue,
        method));
}

SampleType makeSampleType(
    const char* sampleTypeId,
    std::vector<MicroscopyTechniqueConstraint> techniques) {
    auto requirements = take(MicroscopyRequirements::create(std::move(techniques)));
    return take(SampleType::create(
        id<SampleTypeId>(sampleTypeId),
        1,
        "Kidney histology",
        SampleMaterialClass::Biological,
        std::move(requirements)));
}

Slide makeSlide(
    const SampleType& sampleType,
    const char* slideId,
    std::vector<Stain> stains = {}) {
    return take(Slide::create(
        id<SlideId>(slideId),
        1,
        sampleType.id(),
        sampleType.revision(),
        "HIST-001",
        "Kidney cortex",
        take(SlideGeometry::create(75000.0, 25000.0)),
        take(SlidePreparation::create(
            SpecimenPreparationMethod::TissueSection,
            SpecimenFixationState::Fixed)),
        std::move(stains)));
}

Stain makeStain(
    const char* stainId,
    const char* name,
    std::vector<ContrastMethod> recommendedMethods = {}) {
    std::optional<StainMicroscopySuitability> suitability;
    if (!recommendedMethods.empty()) {
        suitability = take(StainMicroscopySuitability::create(
            std::move(recommendedMethods)));
    }
    return take(Stain::create(
        id<StainId>(stainId), name, std::nullopt, std::move(suitability)));
}

void testSampleTypeHardConstraintsPrecedeStainRecommendations() {
    const auto sampleType = makeSampleType(
        "sample-type-hard-brightfield",
        {{ContrastMethod::Brightfield, MicroscopyTechniqueConstraintKind::Required},
         {ContrastMethod::PhaseContrast, MicroscopyTechniqueConstraintKind::Supported},
         {ContrastMethod::Darkfield, MicroscopyTechniqueConstraintKind::Prohibited}});
    const auto slide = makeSlide(sampleType, "slide-hard-brightfield", {
        makeStain("stain-he", "Hematoxylin and eosin",
                  {ContrastMethod::Darkfield, ContrastMethod::PhaseContrast})});

    const auto recommended = take(OperatingModeRecommender::recommend(
        id<MicroscopeId>("recommendation-microscope"),
        sampleType,
        slide,
        {makeMode("mode-darkfield", ContrastMethod::Darkfield),
         makeMode("mode-phase", ContrastMethod::PhaseContrast),
         makeMode("mode-brightfield", ContrastMethod::Brightfield)}));

    check(recommended.candidates().size() == 1,
          "only the exact required technique remains eligible");
    check(recommended.unambiguousRecommendation() &&
          recommended.unambiguousRecommendation()->operationModeId ==
              id<OperationModeId>("mode-brightfield"),
          "recommends the required SampleType mode even when stain advice prefers another method");
    check(recommended.unambiguousRecommendation() &&
          !recommended.unambiguousRecommendation()->recommendedByStain(),
          "does not report stain recommendation evidence that conflicts with SampleType hard constraints");
}

void testSampleTypePreferenceOutranksStainAdvisoryPreference() {
    const auto sampleType = makeSampleType(
        "sample-type-preferred-phase",
        {{ContrastMethod::Brightfield, MicroscopyTechniqueConstraintKind::Supported},
         {ContrastMethod::PhaseContrast, MicroscopyTechniqueConstraintKind::Preferred}});
    const auto slide = makeSlide(sampleType, "slide-preferred-phase", {
        makeStain("stain-he", "Hematoxylin and eosin",
                  {ContrastMethod::Brightfield})});

    const auto recommendations = take(OperatingModeRecommender::recommend(
        id<MicroscopeId>("recommendation-microscope"),
        sampleType,
        slide,
        {makeMode("mode-brightfield", ContrastMethod::Brightfield),
         makeMode("mode-phase", ContrastMethod::PhaseContrast)}));

    check(recommendations.candidates().size() == 2,
          "both explicitly supported and preferred contrast modes remain eligible");
    check(recommendations.unambiguousRecommendation() &&
          recommendations.unambiguousRecommendation()->operationModeId ==
              id<OperationModeId>("mode-phase"),
          "SampleType Preferred mode outranks conflicting Stain advice");
    check(recommendations.candidates().front().preferredBySampleType &&
          !recommendations.candidates().front().recommendedByStain(),
          "top ranking records SampleType evidence without fabricating stain support");
}

void testMultipleStainsContributeAdvisoryEvidenceAndTiesStayAmbiguous() {
    const auto sampleType = makeSampleType(
        "sample-type-stain-tie",
        {{ContrastMethod::Brightfield, MicroscopyTechniqueConstraintKind::Supported},
         {ContrastMethod::PhaseContrast, MicroscopyTechniqueConstraintKind::Supported}});
    const auto slide = makeSlide(sampleType, "slide-stain-tie", {
        makeStain("stain-he", "Hematoxylin and eosin",
                  {ContrastMethod::Brightfield}),
        makeStain("stain-reticulin", "Reticulin",
                  {ContrastMethod::PhaseContrast})});

    const auto recommendations = take(OperatingModeRecommender::recommend(
        id<MicroscopeId>("recommendation-microscope"),
        sampleType,
        slide,
        {makeMode("mode-phase", ContrastMethod::PhaseContrast),
         makeMode("mode-brightfield", ContrastMethod::Brightfield)}));

    check(recommendations.candidates().size() == 2 &&
          recommendations.candidates()[0].recommendedByStain() &&
          recommendations.candidates()[1].recommendedByStain(),
          "each stain contributes advisory evidence to its matching compatible mode");
    check(recommendations.unambiguousRecommendation() == nullptr,
          "does not choose arbitrarily when independent Stain recommendations tie");
}

void testIdentityOnlyStainAddsNoRecommendationEvidence() {
    const auto sampleType = makeSampleType("sample-type-no-stain-advice", {});
    const auto slide = makeSlide(sampleType, "slide-no-stain-advice", {
        makeStain("stain-no-advice", "Uncharacterized stain")});

    const auto recommendations = take(OperatingModeRecommender::recommend(
        id<MicroscopeId>("recommendation-microscope"),
        sampleType,
        slide,
        {makeMode("mode-brightfield", ContrastMethod::Brightfield),
         makeMode("mode-phase", ContrastMethod::PhaseContrast)}));

    check(recommendations.candidates().size() == 2,
          "identity-only stains do not restrict otherwise valid modes");
    check(!recommendations.candidates()[0].recommendedByStain() &&
          !recommendations.candidates()[1].recommendedByStain(),
          "missing optional suitability metadata contributes no fabricated advice");
    check(recommendations.unambiguousRecommendation() == nullptr,
          "multiple candidates without recommendation evidence remain ambiguous");
}

void testStainCannotSelectModeWithoutSampleTypeTechniqueContext() {
    const auto sampleType = makeSampleType("sample-type-no-technique-context", {});
    const auto slide = makeSlide(sampleType, "slide-stain-without-context", {
        makeStain("stain-he-contextless", "Hematoxylin and eosin",
                  {ContrastMethod::Brightfield})});

    const auto recommendations = take(OperatingModeRecommender::recommend(
        id<MicroscopeId>("recommendation-microscope"),
        sampleType,
        slide,
        {makeMode("mode-brightfield", ContrastMethod::Brightfield),
         makeMode("mode-phase", ContrastMethod::PhaseContrast)}));

    check(recommendations.candidates().size() == 2,
          "both intent candidates remain available without SampleType technique context");
    check(!recommendations.candidates()[0].recommendedByStain() &&
          !recommendations.candidates()[1].recommendedByStain(),
          "does not use Stain suitability as standalone mode-selection evidence");
    check(recommendations.unambiguousRecommendation() == nullptr,
          "keeps selection ambiguous when only the Stain suggests a contrast method");
}

void testUnsupportedAndProhibitedModesAreFilteredWithoutStainOverride() {
    const auto sampleType = makeSampleType(
        "sample-type-supported-only",
        {{ContrastMethod::Brightfield, MicroscopyTechniqueConstraintKind::Supported},
         {ContrastMethod::PhaseContrast, MicroscopyTechniqueConstraintKind::Prohibited}});
    const auto slide = makeSlide(sampleType, "slide-supported-only", {
        makeStain("stain-phase", "Phase stain", {ContrastMethod::PhaseContrast})});

    const auto recommendations = take(OperatingModeRecommender::recommend(
        id<MicroscopeId>("recommendation-microscope"),
        sampleType,
        slide,
        {makeMode("mode-phase", ContrastMethod::PhaseContrast),
         makeMode("mode-darkfield", ContrastMethod::Darkfield),
         makeMode("mode-brightfield", ContrastMethod::Brightfield)}));

    check(recommendations.candidates().size() == 1 &&
          recommendations.candidates().front().operationModeId ==
              id<OperationModeId>("mode-brightfield"),
          "keeps the SampleType supported set and prohibition authoritative over Stain advice");
}

void testSlideAndModeInputBoundariesAreValidated() {
    const auto sampleType = makeSampleType("sample-type-boundaries", {});
    const auto mismatchedSlide = take(Slide::create(
        id<SlideId>("slide-other-sample-type"),
        1,
        id<SampleTypeId>("another-sample-type"),
        1,
        "HIST-002",
        "Other sample type",
        take(SlideGeometry::create(75000.0, 25000.0)),
        take(SlidePreparation::create(
            SpecimenPreparationMethod::TissueSection,
            SpecimenFixationState::Fixed))));
    const auto sampleTypeMismatch = OperatingModeRecommender::recommend(
        id<MicroscopeId>("recommendation-microscope"),
        sampleType,
        mismatchedSlide,
        {makeMode("mode-brightfield", ContrastMethod::Brightfield)});
    check(!sampleTypeMismatch &&
          sampleTypeMismatch.error() ==
              OperatingModeRecommendationError::SlideSampleTypeMismatch,
          "rejects recommendation input with a Slide pinned to a different SampleType revision");

    const auto empty = OperatingModeRecommender::recommend(
        id<MicroscopeId>("recommendation-microscope"),
        sampleType,
        makeSlide(sampleType, "slide-empty-candidates"),
        {});
    check(!empty && empty.error() ==
              OperatingModeRecommendationError::EmptyCandidateModes,
          "reports an absent candidate-mode input as a caller error");

    const auto otherMode = makeModeForMicroscope(
        "another-microscope", "mode-other-microscope", ContrastMethod::Brightfield);
    const auto wrongMicroscope = OperatingModeRecommender::recommend(
        id<MicroscopeId>("recommendation-microscope"),
        sampleType,
        makeSlide(sampleType, "slide-mixed-microscope-candidates"),
        {makeMode("mode-brightfield", ContrastMethod::Brightfield), otherMode});
    check(!wrongMicroscope &&
          wrongMicroscope.error() ==
              OperatingModeRecommendationError::CandidateMicroscopeMismatch,
          "rejects candidate sets mixing physical microscopes");

    const auto duplicate = OperatingModeRecommender::recommend(
        id<MicroscopeId>("recommendation-microscope"),
        sampleType,
        makeSlide(sampleType, "slide-duplicate-modes"),
        {makeMode("mode-brightfield", ContrastMethod::Brightfield),
         makeMode("mode-brightfield", ContrastMethod::Brightfield)});
    check(!duplicate &&
          duplicate.error() ==
              OperatingModeRecommendationError::DuplicateOperationModeId,
          "rejects duplicate candidate revisions for one mode identity");
}

} // namespace

int main() {
    testSampleTypeHardConstraintsPrecedeStainRecommendations();
    testSampleTypePreferenceOutranksStainAdvisoryPreference();
    testMultipleStainsContributeAdvisoryEvidenceAndTiesStayAmbiguous();
    testIdentityOnlyStainAddsNoRecommendationEvidence();
    testStainCannotSelectModeWithoutSampleTypeTechniqueContext();
    testUnsupportedAndProhibitedModesAreFilteredWithoutStainOverride();
    testSlideAndModeInputBoundariesAreValidated();

    if (failures != 0) {
        std::cerr << failures << " OperatingMode recommender test(s) failed.\n";
        return 1;
    }
    std::cout << "All OperatingMode recommender tests passed.\n";
}
