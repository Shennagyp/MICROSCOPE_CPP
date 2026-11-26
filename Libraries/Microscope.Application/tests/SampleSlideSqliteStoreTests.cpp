#include "microscope/application/persistence/SampleSlideSqliteStore.h"

#include <sqlite3.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using namespace microscope::application::persistence;
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
        throw std::runtime_error("A SampleType/Slide persistence fixture failed.");
    }
    return std::move(result).value();
}

template <typename TId>
TId makeId(const char* value) {
    return take(TId::create(value));
}

SpectralBand makeBand(double minimum, double maximum) {
    return take(SpectralBand::create(
        take(PositiveNanometers::create(minimum)),
        take(PositiveNanometers::create(maximum))));
}

MicroscopyRequirements makeMicroscopyRequirements() {
    const auto objective = take(ObjectiveMicroscopyRequirements::create(
        10.0,
        100.0,
        0.25,
        {ObjectiveImmersionMedium::Air, ObjectiveImmersionMedium::Oil},
        18.0));
    const auto camera = take(CameraPerformanceRequirements::create(
        30.0,
        2.5,
        1.2,
        24000.0,
        0.65,
        true,
        true));
    const auto condenser = take(CondenserMicroscopyRequirements::create(
        0.25,
        0.90,
        {CondenserDesign::Abbe, CondenserDesign::Achromatic},
        {CondenserImmersionMedium::Air}));
    const auto illumination = take(IlluminationMicroscopyRequirements::create(
        20.0,
        150.0,
        makeBand(430.0, 700.0),
        true));
    return take(MicroscopyRequirements::create(
        {{ContrastMethod::Brightfield,
          MicroscopyTechniqueConstraintKind::Required},
         {ContrastMethod::Fluorescence,
          MicroscopyTechniqueConstraintKind::Supported}},
        objective,
        camera,
        true,
        condenser,
        illumination));
}

SlidePreparationRequirements makePreparationRequirements() {
    return take(SlidePreparationRequirements::create(
        {SpecimenPreparationMethod::TissueSection},
        {SpecimenFixationState::Fixed},
        3.0,
        8.0,
        150.0,
        190.0,
        true,
        {SpecimenLabelKind::Fluorescent}));
}

SampleType makeSampleType(
    const SampleTypeId& id,
    std::uint64_t revision,
    const char* name) {
    return take(SampleType::create(
        id,
        revision,
        name,
        SampleMaterialClass::Biological,
        makeMicroscopyRequirements(),
        "Versioned histology and fluorescence imaging requirements.",
        makePreparationRequirements()));
}

SlidePreparation makeSlidePreparation() {
    const auto label = take(SpecimenLabel::create(
        "Alexa Fluor 488 anti-CD3",
        SpecimenLabelKind::Fluorescent,
        makeBand(470.0, 495.0),
        makeBand(500.0, 550.0),
        take(FluorescenceEmissionSpectrum::create({
            {take(PositiveNanometers::create(500.0)), 0.15},
            {take(PositiveNanometers::create(515.0)), 1.0},
            {take(PositiveNanometers::create(540.0)), 0.12}})),
        makeId<OpticalCompatibilityId>("green-filter-set-r2")));
    return take(SlidePreparation::create(
        SpecimenPreparationMethod::TissueSection,
        SpecimenFixationState::Fixed,
        5.0,
        170.0,
        "Antifade mounting medium",
        1.48,
        {label}));
}

Slide makeSlide(
    const SlideId& id,
    std::uint64_t revision,
    const SampleType& sampleType,
    std::optional<OriginLabId> originLabId = std::nullopt) {
    return take(Slide::create(
        id,
        revision,
        sampleType.id(),
        sampleType.revision(),
        "Kidney cortex section",
        take(SlideGeometry::create(75000.0, 25000.0)),
        makeSlidePreparation(),
        std::move(originLabId)));
}

std::string uniqueDatabasePath(const char* prefix) {
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    return (std::filesystem::temp_directory_path() /
        (std::string(prefix) + std::to_string(nonce) + ".sqlite")).string();
}

void removeDatabaseFiles(const std::string& path) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path + "-wal", ignored);
    std::filesystem::remove(path + "-shm", ignored);
}

void testSampleTypeRevisionHistoryAndFullRequirementRoundTrip() {
    const auto path = uniqueDatabasePath("microscope-sample-type-store-");
    const auto typeId = makeId<SampleTypeId>("sample-type-kidney-histology");
    {
        auto opened = SampleSlideSqliteStore::open(path);
        check(static_cast<bool>(opened), "opens the SampleType/Slide SQLite store");
        if (!opened) {
            removeDatabaseFiles(path);
            return;
        }
        auto store = std::move(opened).value();

        const auto absent = store->loadSampleTypeRevision(typeId, 1);
        check(absent && !absent.value(),
              "returns an empty result for an unknown SampleType revision");

        const auto revision1 = makeSampleType(typeId, 1, "Kidney histology");
        const auto saved1 = store->saveSampleTypeRevision(revision1, 0);
        check(saved1.isSuccess(),
              "creates the first immutable SampleType revision with expected revision zero");

        const auto revision2 = makeSampleType(
            typeId, 2, "Kidney histology with updated imaging requirements");
        const auto saved2 = store->saveSampleTypeRevision(revision2, 1);
        check(saved2.isSuccess(),
              "appends a new SampleType revision using compare-and-swap semantics");

        const auto historical = store->loadSampleTypeRevision(typeId, 1);
        check(historical && historical.value().has_value(),
              "loads the exact historical SampleType revision after a newer revision exists");
        if (historical && historical.value()) {
            const auto& loaded = *historical.value();
            check(loaded.revision() == 1 &&
                  loaded.name() == "Kidney histology" &&
                  loaded.materialClass() == SampleMaterialClass::Biological,
                  "round-trips SampleType identity, revision, name and material class");
            const auto& requirements = loaded.microscopyRequirements();
            check(requirements.techniques().size() == 2 &&
                  requirements.techniques()[0].method == ContrastMethod::Brightfield &&
                  requirements.techniques()[1].kind ==
                      MicroscopyTechniqueConstraintKind::Supported,
                  "round-trips technique requirements and their constraint semantics");
            check(requirements.objective() &&
                  requirements.objective()->minimumMagnification() ==
                      std::optional<double>{10.0} &&
                  requirements.objective()->allowedImmersionMedia().size() == 2 &&
                  requirements.objective()->maximumCoverglassDesignMismatchMicrometers() ==
                      std::optional<double>{18.0},
                  "round-trips the complete objective requirement envelope");
            check(requirements.camera() &&
                  requirements.camera()->minimumFrameRateFramesPerSecond() ==
                      std::optional<double>{30.0} &&
                  requirements.camera()->preferGlobalShutter() &&
                  requirements.camera()->preferMonochrome() &&
                  requirements.camera()->minimumMeanQuantumEfficiency() ==
                      std::optional<double>{0.65},
                  "round-trips camera performance requirements and preferences");
            check(requirements.condenser() &&
                  requirements.condenser()->allowedDesigns().size() == 2 &&
                  requirements.condenser()->maximumApertureNA() ==
                      std::optional<double>{0.90},
                  "round-trips condenser performance and design constraints");
            check(requirements.illumination() &&
                  requirements.illumination()->requiredSpectralBand() &&
                  requirements.illumination()->requiredSpectralBand()->minimumWavelengthNanometers() ==
                      430.0 &&
                  requirements.illumination()->requireContinuouslyAdjustableSource(),
                  "round-trips spectral and specimen-plane illumination requirements");
            check(requirements.requireFluorescentLabelMetadata(),
                  "round-trips the fluorescent-label metadata requirement");
            check(loaded.preparationRequirements() &&
                  loaded.preparationRequirements()->allowedMethods().size() == 1 &&
                  loaded.preparationRequirements()->minimumSectionThicknessMicrometers() ==
                      std::optional<double>{3.0} &&
                  loaded.preparationRequirements()->requiredLabelKinds().front() ==
                      SpecimenLabelKind::Fluorescent,
                  "round-trips reusable Slide preparation requirements");
            check(loaded.description() &&
                  *loaded.description() ==
                      "Versioned histology and fluorescence imaging requirements.",
                  "round-trips the optional SampleType description");
        }

        const auto current = store->loadCurrentSampleType(typeId);
        check(current && current.value() && current.value()->revision() == 2,
              "resolves the current SampleType revision independently of history");
        const auto staleWrite = store->saveSampleTypeRevision(revision2, 1);
        check(!staleWrite.isSuccess() &&
              staleWrite.error().code == SampleSlideStoreErrorCode::RevisionConflict,
              "rejects a stale writer after the current SampleType revision advances");
        const auto skippedRevision = SampleType::create(
            typeId,
            4,
            "Skipped revision",
            SampleMaterialClass::Biological,
            makeMicroscopyRequirements());
        check(static_cast<bool>(skippedRevision),
              "constructs a valid domain revision for the sequence-gap rejection case");
        if (skippedRevision) {
            const auto skipped = store->saveSampleTypeRevision(
                skippedRevision.value(), 2);
            check(!skipped.isSuccess() &&
                  skipped.error().code == SampleSlideStoreErrorCode::InvalidRevision,
                  "rejects skipped revision numbers instead of creating a broken history");
        }
    }
    removeDatabaseFiles(path);
}

void testSlidePinsExactSampleTypeRevisionAndRoundTripsCompletePreparation() {
    const auto path = uniqueDatabasePath("microscope-slide-store-");
    const auto typeId = makeId<SampleTypeId>("sample-type-slide-history");
    const auto slideId = makeId<SlideId>("slide-kidney-cortex-001");
    const auto originLabId = makeId<OriginLabId>("origin-lab-pathology-01");
    {
        auto opened = SampleSlideSqliteStore::open(path);
        check(static_cast<bool>(opened), "opens the Slide persistence store");
        if (!opened) {
            removeDatabaseFiles(path);
            return;
        }
        auto store = std::move(opened).value();

        const auto sampleTypeV1 = makeSampleType(typeId, 1, "Kidney histology v1");
        const auto sampleTypeV2 = makeSampleType(typeId, 2, "Kidney histology v2");
        check(store->saveSampleTypeRevision(sampleTypeV1, 0).isSuccess(),
              "persists the SampleType revision required by the Slide");
        const auto slideV1 = makeSlide(slideId, 1, sampleTypeV1, originLabId);
        const auto savedSlide = store->saveSlideRevision(slideV1, 0);
        check(savedSlide.isSuccess(),
              "persists a Slide only after its exact SampleType revision exists");

        check(store->saveSampleTypeRevision(sampleTypeV2, 1).isSuccess(),
              "advances the current SampleType without rewriting the existing Slide");

        const auto historicalSlide = store->loadSlideRevision(slideId, 1);
        check(historicalSlide && historicalSlide.value().has_value(),
              "loads the immutable Slide revision by identity and revision");
        if (historicalSlide && historicalSlide.value()) {
            const auto& loaded = *historicalSlide.value();
            check(loaded.sampleTypeId() == typeId &&
                  loaded.sampleTypeRevision() == 1,
                  "preserves the Slide's exact historical SampleType foreign-key revision");
            check(loaded.originLabId() == originLabId,
                  "round-trips optional OriginLabId without requiring a Lab entity");
            check(loaded.geometry().widthMicrometers() == 75000.0 &&
                  loaded.geometry().heightMicrometers() == 25000.0,
                  "round-trips physical slide geometry");
            const auto& preparation = loaded.preparation();
            check(preparation.method() == SpecimenPreparationMethod::TissueSection &&
                  preparation.fixationState() == SpecimenFixationState::Fixed &&
                  preparation.sectionThicknessMicrometers() ==
                      std::optional<double>{5.0} &&
                  preparation.coverglassThicknessMicrometers() ==
                      std::optional<double>{170.0} &&
                  preparation.mountingMediumRefractiveIndex() ==
                      std::optional<double>{1.48},
                  "round-trips all measured preparation fields");
            check(preparation.labels().size() == 1,
                  "round-trips Slide label collection cardinality");
            if (!preparation.labels().empty()) {
                const auto& label = preparation.labels().front();
                check(label.name() == "Alexa Fluor 488 anti-CD3" &&
                      label.kind() == SpecimenLabelKind::Fluorescent,
                      "round-trips label identity and class");
                check(label.excitationBand() &&
                      label.excitationBand()->minimumWavelengthNanometers() == 470.0 &&
                      label.emissionBand() &&
                      label.emissionBand()->maximumWavelengthNanometers() == 550.0,
                      "round-trips excitation and emission spectral bands");
                check(label.emissionSpectrum() &&
                      label.emissionSpectrum()->samples().size() == 3 &&
                      label.emissionSpectrum()->samples()[1].relativeIntensity == 1.0,
                      "round-trips the full relative emission-power distribution");
                check(label.filterSetCompatibilityId() &&
                      label.filterSetCompatibilityId()->value() == "green-filter-set-r2",
                      "round-trips fluorescent filter-set compatibility identity");
            }
        }

        const auto currentSlide = store->loadCurrentSlide(slideId);
        check(currentSlide && currentSlide.value() &&
              currentSlide.value()->revision() == 1,
              "loads the current Slide pointer");
        const auto noSuchRevision = store->loadSlideRevision(slideId, 2);
        check(noSuchRevision && !noSuchRevision.value(),
              "returns no value for an absent Slide revision");
    }
    removeDatabaseFiles(path);
}

void testSlideCannotReferenceAnUnpersistedSampleTypeRevision() {
    const auto path = uniqueDatabasePath("microscope-slide-fk-");
    {
        auto opened = SampleSlideSqliteStore::open(path);
        check(static_cast<bool>(opened), "opens the foreign-key test store");
        if (!opened) {
            removeDatabaseFiles(path);
            return;
        }
        auto store = std::move(opened).value();
        const auto sampleTypeId =
            makeId<SampleTypeId>("sample-type-exact-revision");
        const auto persistedV1 = makeSampleType(
            sampleTypeId, 1, "Persisted SampleType v1");
        const auto unpersistedV2 = makeSampleType(
            sampleTypeId, 2, "Unpersisted SampleType v2");
        check(store->saveSampleTypeRevision(persistedV1, 0).isSuccess(),
              "persists an earlier SampleType revision for the exact-reference test");
        const auto slide = makeSlide(
            makeId<SlideId>("slide-with-dangling-type-revision"),
            1,
            unpersistedV2);
        const auto saved = store->saveSlideRevision(slide, 0);
        check(!saved.isSuccess() &&
              saved.error().code ==
                  SampleSlideStoreErrorCode::SlideSampleTypeRevisionNotFound,
              "rejects a Slide pinned to an unpersisted revision even when an older revision of that SampleType exists");
        const auto current = store->loadCurrentSlide(slide.id());
        check(current && !current.value(),
              "failed Slide writes leave no current pointer or partial record");
    }
    removeDatabaseFiles(path);
}

void testConcurrentStoreHandlesRejectStaleRevisionWriters() {
    const auto path = uniqueDatabasePath("microscope-slide-cas-");
    {
        auto firstOpen = SampleSlideSqliteStore::open(path);
        auto secondOpen = SampleSlideSqliteStore::open(path);
        check(firstOpen && secondOpen,
              "opens two independent store connections to one SQLite database");
        if (!firstOpen || !secondOpen) {
            removeDatabaseFiles(path);
            return;
        }
        auto first = std::move(firstOpen).value();
        auto second = std::move(secondOpen).value();
        const auto typeId = makeId<SampleTypeId>("sample-type-concurrent");
        const auto revision1 = makeSampleType(typeId, 1, "Revision one");
        const auto revision2 = makeSampleType(typeId, 2, "Revision two");
        check(first->saveSampleTypeRevision(revision1, 0).isSuccess(),
              "first connection commits the initial revision");
        check(second->saveSampleTypeRevision(revision2, 1).isSuccess(),
              "second connection advances from its expected current revision");
        const auto stale = first->saveSampleTypeRevision(revision2, 1);
        check(!stale.isSuccess() &&
              stale.error().code == SampleSlideStoreErrorCode::RevisionConflict,
              "first connection cannot overwrite the current pointer using stale expected revision");
        const auto current = first->loadCurrentSampleType(typeId);
        check(current && current.value() && current.value()->revision() == 2,
              "stale writer leaves the committed current revision unchanged");

        const auto slideId = makeId<SlideId>("slide-concurrent");
        const auto slideRevision1 = makeSlide(slideId, 1, revision1);
        const auto slideRevision2 = makeSlide(slideId, 2, revision1);
        check(first->saveSlideRevision(slideRevision1, 0).isSuccess(),
              "first connection commits the initial Slide revision");
        check(second->saveSlideRevision(slideRevision2, 1).isSuccess(),
              "second connection appends the next Slide revision");
        const auto staleSlide = first->saveSlideRevision(slideRevision2, 1);
        check(!staleSlide.isSuccess() &&
              staleSlide.error().code == SampleSlideStoreErrorCode::RevisionConflict,
              "stale Slide writer cannot move the current pointer backward or overwrite a revision");
        const auto historicalSlide = first->loadSlideRevision(slideId, 1);
        const auto currentSlide = first->loadCurrentSlide(slideId);
        check(historicalSlide && historicalSlide.value() &&
              currentSlide && currentSlide.value() &&
              historicalSlide.value()->revision() == 1 &&
              currentSlide.value()->revision() == 2,
              "preserves immutable Slide history and the latest Slide pointer after a stale writer");
    }
    removeDatabaseFiles(path);
}

void testImmutableRevisionRowsRejectDirectSqlMutation() {
    const auto path = uniqueDatabasePath("microscope-immutable-rows-");
    const auto typeId = makeId<SampleTypeId>("sample-type-immutable");
    const auto slideId = makeId<SlideId>("slide-immutable");
    {
        auto opened = SampleSlideSqliteStore::open(path);
        check(static_cast<bool>(opened), "opens the immutable-row persistence store");
        if (!opened) {
            removeDatabaseFiles(path);
            return;
        }
        auto store = std::move(opened).value();
        const auto sampleType = makeSampleType(typeId, 1, "Immutable SampleType");
        const auto slide = makeSlide(slideId, 1, sampleType);
        check(store->saveSampleTypeRevision(sampleType, 0).isSuccess(),
              "writes SampleType revision before immutability checks");
        check(store->saveSlideRevision(slide, 0).isSuccess(),
              "writes Slide revision before immutability checks");

        sqlite3* direct = nullptr;
        const int openedDirect = sqlite3_open(path.c_str(), &direct);
        check(openedDirect == SQLITE_OK && direct != nullptr,
              "opens an independent SQLite connection for adversarial mutation");
        if (openedDirect == SQLITE_OK && direct != nullptr) {
            const auto execute = [direct](const char* sql) {
                char* error = nullptr;
                const int result = sqlite3_exec(direct, sql, nullptr, nullptr, &error);
                sqlite3_free(error);
                return result;
            };
            const int updateType = execute(
                "UPDATE sample_type_revision SET payload = zeroblob(1) "
                "WHERE sample_type_id = 'sample-type-immutable' AND revision = 1");
            const int deleteType = execute(
                "DELETE FROM sample_type_revision "
                "WHERE sample_type_id = 'sample-type-immutable' AND revision = 1");
            const int updateSlide = execute(
                "UPDATE slide_revision SET payload = zeroblob(1) "
                "WHERE slide_id = 'slide-immutable' AND revision = 1");
            const int deleteSlide = execute(
                "DELETE FROM slide_revision "
                "WHERE slide_id = 'slide-immutable' AND revision = 1");
            const int insertTypeGap = execute(
                "INSERT INTO sample_type_revision "
                "(sample_type_id, revision, payload_format_version, payload) "
                "VALUES ('sample-type-immutable', 9, 1, x'01')");
            const int insertSlideGap = execute(
                "INSERT INTO slide_revision "
                "(slide_id, revision, sample_type_id, sample_type_revision, "
                "payload_format_version, payload) "
                "VALUES ('slide-immutable', 9, 'sample-type-immutable', 1, 1, x'01')");
            const int rewindTypeCurrent = execute(
                "UPDATE sample_type_current SET revision = 1 "
                "WHERE sample_type_id = 'sample-type-immutable'");
            const int rewindSlideCurrent = execute(
                "UPDATE slide_current SET revision = 1 "
                "WHERE slide_id = 'slide-immutable'");
            const int deleteTypeCurrent = execute(
                "DELETE FROM sample_type_current "
                "WHERE sample_type_id = 'sample-type-immutable'");
            const int deleteSlideCurrent = execute(
                "DELETE FROM slide_current WHERE slide_id = 'slide-immutable'");
            check((updateType & 0xff) == SQLITE_CONSTRAINT,
                  "database trigger rejects direct mutation of immutable SampleType revisions");
            check((deleteType & 0xff) == SQLITE_CONSTRAINT,
                  "database trigger rejects direct deletion of immutable SampleType revisions");
            check((updateSlide & 0xff) == SQLITE_CONSTRAINT,
                  "database trigger rejects direct mutation of immutable Slide revisions");
            check((deleteSlide & 0xff) == SQLITE_CONSTRAINT,
                  "database trigger rejects direct deletion of immutable Slide revisions");
            check((insertTypeGap & 0xff) == SQLITE_CONSTRAINT,
                  "database trigger rejects skipped SampleType revision insertion");
            check((insertSlideGap & 0xff) == SQLITE_CONSTRAINT,
                  "database trigger rejects skipped Slide revision insertion");
            check((rewindTypeCurrent & 0xff) == SQLITE_CONSTRAINT,
                  "database trigger rejects rewinding the current SampleType revision");
            check((rewindSlideCurrent & 0xff) == SQLITE_CONSTRAINT,
                  "database trigger rejects rewinding the current Slide revision");
            check((deleteTypeCurrent & 0xff) == SQLITE_CONSTRAINT,
                  "database trigger protects the SampleType current pointer from deletion");
            check((deleteSlideCurrent & 0xff) == SQLITE_CONSTRAINT,
                  "database trigger protects the Slide current pointer from deletion");
            sqlite3_close_v2(direct);
        } else if (direct != nullptr) {
            sqlite3_close_v2(direct);
        }

        const auto loadedType = store->loadSampleTypeRevision(typeId, 1);
        const auto loadedSlide = store->loadSlideRevision(slideId, 1);
        check(loadedType && loadedType.value() &&
              loadedType.value()->name() == "Immutable SampleType",
              "rejected direct writes leave the SampleType payload unchanged");
        check(loadedSlide && loadedSlide.value() &&
              loadedSlide.value()->sampleTypeRevision() == 1,
              "rejected direct writes leave the Slide payload and exact revision binding unchanged");
    }
    removeDatabaseFiles(path);
}

void testStoreReopensAndResolvesHistoricalSlideAndSampleType() {
    const auto path = uniqueDatabasePath("microscope-slide-reopen-");
    const auto typeId = makeId<SampleTypeId>("sample-type-reopen");
    const auto slideId = makeId<SlideId>("slide-reopen");
    {
        auto opened = SampleSlideSqliteStore::open(path);
        check(static_cast<bool>(opened), "opens the first persistence session");
        if (!opened) {
            removeDatabaseFiles(path);
            return;
        }
        auto store = std::move(opened).value();
        const auto type = makeSampleType(typeId, 1, "Reopen type");
        check(store->saveSampleTypeRevision(type, 0).isSuccess(),
              "commits SampleType before shutdown");
        const auto slide = makeSlide(slideId, 1, type);
        check(store->saveSlideRevision(slide, 0).isSuccess(),
              "commits Slide before shutdown");
    }
    {
        auto reopened = SampleSlideSqliteStore::open(path);
        check(static_cast<bool>(reopened), "reopens the existing database");
        if (reopened) {
            auto store = std::move(reopened).value();
            const auto type = store->loadSampleTypeRevision(typeId, 1);
            const auto slide = store->loadSlideRevision(slideId, 1);
            check(type && type.value() && type.value()->revision() == 1,
                  "restores the exact SampleType revision after process/store restart");
            check(slide && slide.value() &&
                  slide.value()->sampleTypeId() == typeId &&
                  slide.value()->sampleTypeRevision() == 1,
                  "restores the Slide-to-SampleType revision relationship after restart");
        }
    }
    removeDatabaseFiles(path);
}

} // namespace

int main() {
    testSampleTypeRevisionHistoryAndFullRequirementRoundTrip();
    testSlidePinsExactSampleTypeRevisionAndRoundTripsCompletePreparation();
    testSlideCannotReferenceAnUnpersistedSampleTypeRevision();
    testConcurrentStoreHandlesRejectStaleRevisionWriters();
    testImmutableRevisionRowsRejectDirectSqlMutation();
    testStoreReopensAndResolvesHistoricalSlideAndSampleType();

    if (failures != 0) {
        std::cerr << failures << " SampleType/Slide SQLite persistence test(s) failed.\n";
        return 1;
    }
    std::cout << "All SampleType/Slide SQLite persistence tests passed.\n";
    return 0;
}
