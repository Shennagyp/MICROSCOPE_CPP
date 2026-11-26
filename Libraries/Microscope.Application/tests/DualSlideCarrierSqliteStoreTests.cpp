#include "microscope/application/persistence/DualSlideCarrierSqliteStore.h"
#include "microscope/application/persistence/SampleSlideSqliteStore.h"

#include <sqlite3.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace microscope::core;
using namespace microscope::application::persistence;

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
        throw std::runtime_error("A dual-slide carrier persistence fixture failed.");
    }
    return std::move(result).value();
}

template <typename TId>
TId makeId(const char* value) {
    return take(TId::create(value));
}

std::string uniqueDatabasePath() {
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    return (std::filesystem::temp_directory_path() /
        ("microscope-dual-slide-carrier-" + std::to_string(nonce) + ".sqlite")).string();
}

void removeDatabaseFiles(const std::string& path) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path + "-wal", ignored);
    std::filesystem::remove(path + "-shm", ignored);
}

MicroscopyRequirements makeMicroscopyRequirements() {
    return take(MicroscopyRequirements::create({
        {ContrastMethod::Brightfield, MicroscopyTechniqueConstraintKind::Supported}
    }));
}

SlidePreparation makePreparation() {
    return take(SlidePreparation::create(
        SpecimenPreparationMethod::TissueSection,
        SpecimenFixationState::Fixed));
}

SampleType makeSampleType(const SampleTypeId& id) {
    return take(SampleType::create(
        id,
        1,
        "Histology specimen",
        SampleMaterialClass::Biological,
        makeMicroscopyRequirements()));
}

Slide makeSlide(
    const SlideId& id,
    const SampleType& sampleType,
    double width = 75000.0,
    double height = 25000.0) {
    return take(Slide::create(
        id,
        1,
        sampleType.id(),
        sampleType.revision(),
        "Histology slide",
        take(SlideGeometry::create(width, height)),
        makePreparation()));
}

AxisSpecification makeAxis(const char* id) {
    return take(AxisSpecification::create(
        makeId<StageAxisId>(id),
        AxisMotionType::Linear,
        AxisCoordinateUnit::Micrometers,
        take(AxisOperatingRange::create(
            -100000.0, 200000.0, AxisCoordinateUnit::Micrometers)),
        take(PositiveAxisResolution::create(0.1)),
        std::nullopt,
        std::nullopt,
        std::nullopt,
        AxisPositionFeedbackCapability::DeviceReported));
}

StageSpecification makeStage() {
    return take(StageSpecification::create(
        makeId<EquipmentComponentId>("stage-main"),
        {makeAxis("stage-x"), makeAxis("stage-y")}));
}

DualSlideCarrierSpecification makeCarrierSpecification(
    const StageSpecification& stage,
    CarrierHorizontalDirection direction =
        CarrierHorizontalDirection::PositiveStageXFromLeftToRight) {
    return take(DualSlideCarrierSpecification::create(
        stage,
        makeId<StageAxisId>("stage-x"),
        makeId<StageAxisId>("stage-y"),
        direction));
}

SlideRegistration makeRegistration(
    const SlideId& slideId,
    DualSlideSlot slot,
    double xOffset,
    const char* revision) {
    const auto source = ObservationSource::HardwareReadback;
    const auto geometry = take(SlideGeometry::create(75000.0, 25000.0));
    const auto tl = take(SlideCornerMeasurement::create(
        SlideCorner::TopLeft, {xOffset, 20000.0}, source, 0.5));
    const auto tr = take(SlideCornerMeasurement::create(
        SlideCorner::TopRight, {xOffset + 75000.0, 20000.0}, source, 0.6));
    const auto br = take(SlideCornerMeasurement::create(
        SlideCorner::BottomRight, {xOffset + 75000.0, 45000.0}, source, 0.7));
    const auto bl = take(SlideCornerMeasurement::create(
        SlideCorner::BottomLeft, {xOffset, 45000.0}, source, 0.8));
    return take(SlideRegistration::create(
        slideId,
        makeId<CalibrationRevisionId>(revision),
        slot,
        geometry,
        {tl, tr, br, bl},
        1.25));
}

DualSlideCarrierState makeCarrierState(
    const DualSlideCarrierSpecification& specification,
    const SlideId& leftSlideId,
    const SlideId& rightSlideId,
    double leftOffset,
    double rightOffset,
    const char* leftRegistrationRevision,
    const char* rightRegistrationRevision) {
    auto left = take(SlideSlotState::create(
        DualSlideSlot::Left,
        leftSlideId,
        makeRegistration(
            leftSlideId,
            DualSlideSlot::Left,
            leftOffset,
            leftRegistrationRevision)));
    auto right = take(SlideSlotState::create(
        DualSlideSlot::Right,
        rightSlideId,
        makeRegistration(
            rightSlideId,
            DualSlideSlot::Right,
            rightOffset,
            rightRegistrationRevision)));
    return take(DualSlideCarrierState::create(
        specification, std::move(left), std::move(right)));
}

void testCarrierStoreRequiresSlideWorkspaceSchema() {
    const auto path = uniqueDatabasePath();
    auto opened = DualSlideCarrierSqliteStore::open(path);
    check(!opened &&
          opened.error().code ==
              DualSlideCarrierStoreErrorCode::SampleSlideStoreSchemaRequired,
          "requires SampleSlideSqliteStore schema before carrier registrations can be persisted");
    removeDatabaseFiles(path);
}

void testCarrierStoreRejectsUnsupportedSampleSlideSchemaVersion() {
    const auto path = uniqueDatabasePath();
    sqlite3* database = nullptr;
    const int openResult = sqlite3_open(path.c_str(), &database);
    check(openResult == SQLITE_OK && database != nullptr,
          "opens raw SQLite database for schema-version rejection test");
    if (openResult == SQLITE_OK && database != nullptr) {
        char* errorMessage = nullptr;
        const int created = sqlite3_exec(
            database,
            "CREATE TABLE sample_slide_store_schema ("
            "id INTEGER PRIMARY KEY CHECK (id = 1), "
            "schema_version INTEGER NOT NULL);"
            "INSERT INTO sample_slide_store_schema (id, schema_version) "
            "VALUES (1, 99);",
            nullptr,
            nullptr,
            &errorMessage);
        sqlite3_free(errorMessage);
        check(created == SQLITE_OK,
              "creates a deliberately unsupported SampleSlide schema marker");
        sqlite3_close_v2(database);
    } else if (database != nullptr) {
        sqlite3_close_v2(database);
    }

    auto opened = DualSlideCarrierSqliteStore::open(path);
    check(!opened &&
          opened.error().code ==
              DualSlideCarrierStoreErrorCode::UnsupportedSchemaVersion,
          "refuses to attach carrier tables to an unsupported Slide persistence format");
    removeDatabaseFiles(path);
}

void testEmptyPhysicalCarrierStateRoundTripsWithoutInventingSlides() {
    const auto path = uniqueDatabasePath();
    const auto microscopeId = makeId<MicroscopeId>("microscope-empty-carrier");
    {
        auto slideStoreResult = SampleSlideSqliteStore::open(path);
        check(static_cast<bool>(slideStoreResult),
              "opens SampleSlideSqliteStore for an empty carrier workspace");
        if (!slideStoreResult) {
            removeDatabaseFiles(path);
            return;
        }
        auto slideStore = std::move(slideStoreResult).value();
        auto carrierStoreResult = DualSlideCarrierSqliteStore::open(path);
        check(static_cast<bool>(carrierStoreResult),
              "opens carrier store with initialized but empty Slide tables");
        if (!carrierStoreResult) {
            removeDatabaseFiles(path);
            return;
        }
        auto carrierStore = std::move(carrierStoreResult).value();
        const auto specification = makeCarrierSpecification(makeStage());
        const auto left = take(SlideSlotState::create(
            DualSlideSlot::Left, std::nullopt));
        const auto right = take(SlideSlotState::create(
            DualSlideSlot::Right, std::nullopt));
        const auto emptyState = take(DualSlideCarrierState::create(
            specification, left, right));

        check(carrierStore->saveCurrentState(
                  microscopeId, 1, emptyState, {}, 0).isSuccess(),
              "persists an installed empty two-slot carrier without fabricated Slide records");
        const auto loaded = carrierStore->loadCurrentState(
            microscopeId, 1, specification);
        check(loaded && loaded.value() &&
              loaded.value()->state.left().slot() == DualSlideSlot::Left &&
              !loaded.value()->state.left().slideId() &&
              !loaded.value()->state.left().registration() &&
              loaded.value()->state.right().slot() == DualSlideSlot::Right &&
              !loaded.value()->state.right().slideId() &&
              !loaded.value()->state.right().registration(),
              "restores both empty physical slots as empty, not as placeholder Slides or registrations");
    }
    removeDatabaseFiles(path);
}

void testFourCornerCarrierRegistrationRoundTripsWithExactSlideRevisions() {
    const auto path = uniqueDatabasePath();
    const auto microscopeId = makeId<MicroscopeId>("microscope-carrier-test");
    const auto leftSlideId = makeId<SlideId>("slide-carrier-left");
    const auto rightSlideId = makeId<SlideId>("slide-carrier-right");
    const auto sampleTypeId = makeId<SampleTypeId>("sample-type-carrier-test");
    const auto sampleType = makeSampleType(sampleTypeId);
    const auto leftSlide = makeSlide(leftSlideId, sampleType);
    const auto rightSlide = makeSlide(rightSlideId, sampleType);

    {
        auto slideStoreResult = SampleSlideSqliteStore::open(path);
        check(static_cast<bool>(slideStoreResult),
              "opens SampleSlideSqliteStore before the carrier store");
        if (!slideStoreResult) {
            removeDatabaseFiles(path);
            return;
        }
        auto slideStore = std::move(slideStoreResult).value();
        check(slideStore->saveSampleTypeRevision(sampleType, 0).isSuccess(),
              "persists the SampleType used by both mounted slides");
        check(slideStore->saveSlideRevision(leftSlide, 0).isSuccess(),
              "persists the left physical Slide before carrier assignment");
        check(slideStore->saveSlideRevision(rightSlide, 0).isSuccess(),
              "persists the right physical Slide before carrier assignment");

        auto carrierStoreResult = DualSlideCarrierSqliteStore::open(path);
        check(static_cast<bool>(carrierStoreResult),
              "opens the carrier registration store on the same SQLite database");
        if (!carrierStoreResult) {
            removeDatabaseFiles(path);
            return;
        }
        auto carrierStore = std::move(carrierStoreResult).value();

        const auto stage = makeStage();
        const auto specification = makeCarrierSpecification(stage);
        const auto firstState = makeCarrierState(
            specification,
            leftSlideId,
            rightSlideId,
            5000.0,
            95000.0,
            "left-registration-v1",
            "right-registration-v1");
        const std::vector<Slide> currentSlides{leftSlide, rightSlide};

        const auto saved = carrierStore->saveCurrentState(
            microscopeId, 7, firstState, currentSlides, 0);
        check(saved.isSuccess(),
              "persists one immutable two-slot carrier revision against equipment revision seven");
        if (!saved.isSuccess()) {
            std::cerr << "  carrier save error code: "
                      << static_cast<int>(saved.error().code)
                      << " detail: " << saved.error().detail << '\n';
            removeDatabaseFiles(path);
            return;
        }

        const auto loaded = carrierStore->loadCurrentState(
            microscopeId, 7, specification);
        check(loaded && loaded.value() && loaded.value()->revision == 1,
              "loads the first current carrier revision");
        if (loaded && loaded.value()) {
            const auto& state = loaded.value()->state;
            check(state.left().slot() == DualSlideSlot::Left &&
                  state.left().slideId() == leftSlideId &&
                  state.right().slot() == DualSlideSlot::Right &&
                  state.right().slideId() == rightSlideId,
                  "round-trips the two horizontal physical slot assignments");
            check(loaded.value()->leftSlideRevision == 1 &&
                  loaded.value()->rightSlideRevision == 1,
                  "pins each physical slot to the exact persisted Slide revision");

            const auto* leftRegistration = state.registrationFor(leftSlideId);
            const auto* rightRegistration = state.registrationFor(rightSlideId);
            check(leftRegistration != nullptr && rightRegistration != nullptr,
                  "restores both independent four-corner registrations");
            if (leftRegistration && rightRegistration) {
                check(leftRegistration->cornerMeasurements().size() == 4 &&
                      leftRegistration->cornerMeasurements()[0].corner() ==
                          SlideCorner::TopLeft &&
                      leftRegistration->cornerMeasurements()[1].corner() ==
                          SlideCorner::TopRight &&
                      leftRegistration->cornerMeasurements()[2].corner() ==
                          SlideCorner::BottomRight &&
                      leftRegistration->cornerMeasurements()[3].corner() ==
                          SlideCorner::BottomLeft,
                      "restores all four named corner measurement records");
                check(leftRegistration->cornerMeasurements()[1].uncertaintyMicrometers() ==
                          0.6 &&
                      leftRegistration->cornerMeasurements()[3].source() ==
                          ObservationSource::HardwareReadback,
                      "preserves per-corner uncertainty and measurement provenance");
                check(leftRegistration->acceptedMaximumFitResidualMicrometers() ==
                          1.25,
                      "preserves the accepted affine-fit residual threshold");

                const auto mapped = leftRegistration->toStage({37500.0, 12500.0});
                check(mapped &&
                      std::abs(mapped.value().horizontalMicrometers - 42500.0) < 1e-6 &&
                      std::abs(mapped.value().transverseMicrometers - 32500.0) < 1e-6,
                      "reconstructed affine transform maps the left Slide center correctly");
            }
        }

        const auto emptyOtherEquipmentRevision =
            carrierStore->loadCurrentState(microscopeId, 8, specification);
        check(emptyOtherEquipmentRevision &&
              !emptyOtherEquipmentRevision.value(),
              "does not reuse physical corner calibration under a different equipment revision");

        const auto reversedSpecification = makeCarrierSpecification(
            stage,
            CarrierHorizontalDirection::NegativeStageXFromLeftToRight);
        const auto wrongBinding = carrierStore->loadCurrentState(
            microscopeId, 7, reversedSpecification);
        check(!wrongBinding &&
              wrongBinding.error().code ==
                  DualSlideCarrierStoreErrorCode::EquipmentBindingMismatch,
              "rejects loading calibrated positions against a different physical left-to-right axis direction");

        const auto updatedState = makeCarrierState(
            specification,
            leftSlideId,
            rightSlideId,
            5100.0,
            95100.0,
            "left-registration-v2",
            "right-registration-v2");
        check(carrierStore->saveCurrentState(
                  microscopeId, 7, updatedState, currentSlides, 1).isSuccess(),
              "appends a new carrier calibration revision without rewriting the old measurements");

        const auto historical = carrierStore->loadRevision(
            microscopeId, 7, specification, 1);
        const auto current = carrierStore->loadCurrentState(
            microscopeId, 7, specification);
        check(historical && historical.value() &&
              historical.value()->revision == 1 &&
              current && current.value() &&
              current.value()->revision == 2,
              "retains the prior carrier registration while advancing the current pointer");
        if (historical && historical.value()) {
            const auto* registration =
                historical.value()->state.registrationFor(leftSlideId);
            check(registration &&
                  registration->revision().value() == "left-registration-v1",
                  "historical carrier revision retains the exact earlier corner-registration identity");
        }

        sqlite3* direct = nullptr;
        const int directOpen = sqlite3_open(path.c_str(), &direct);
        check(directOpen == SQLITE_OK && direct != nullptr,
              "opens an independent raw SQLite connection to challenge carrier immutability");
        if (directOpen == SQLITE_OK && direct != nullptr) {
            const auto executeDirect = [direct](const char* sql) {
                char* error = nullptr;
                const int result = sqlite3_exec(direct, sql, nullptr, nullptr, &error);
                sqlite3_free(error);
                return result;
            };
            const int updateRevision = executeDirect(
                "UPDATE dual_slide_carrier_revision SET payload = x'00' "
                "WHERE microscope_id = 'microscope-carrier-test' "
                "AND equipment_revision = 7 AND stage_component_id = 'stage-main' "
                "AND revision = 1");
            const int deleteRevision = executeDirect(
                "DELETE FROM dual_slide_carrier_revision "
                "WHERE microscope_id = 'microscope-carrier-test' "
                "AND equipment_revision = 7 AND stage_component_id = 'stage-main' "
                "AND revision = 1");
            const int insertGap = executeDirect(
                "INSERT INTO dual_slide_carrier_revision ("
                "microscope_id, equipment_revision, stage_component_id, revision, "
                "payload_format_version, payload) "
                "VALUES ('microscope-carrier-test', 7, 'stage-main', 9, 1, x'01')");
            const int rewindCurrent = executeDirect(
                "UPDATE dual_slide_carrier_current SET revision = 1 "
                "WHERE microscope_id = 'microscope-carrier-test' "
                "AND equipment_revision = 7 AND stage_component_id = 'stage-main'");
            const int deleteCurrent = executeDirect(
                "DELETE FROM dual_slide_carrier_current "
                "WHERE microscope_id = 'microscope-carrier-test' "
                "AND equipment_revision = 7 AND stage_component_id = 'stage-main'");
            check((updateRevision & 0xff) == SQLITE_CONSTRAINT,
                  "database trigger rejects direct carrier history UPDATE");
            check((deleteRevision & 0xff) == SQLITE_CONSTRAINT,
                  "database trigger rejects direct carrier history DELETE");
            check((insertGap & 0xff) == SQLITE_CONSTRAINT,
                  "database trigger rejects direct skipped carrier revision insertion");
            check((rewindCurrent & 0xff) == SQLITE_CONSTRAINT,
                  "database trigger rejects rewinding the current carrier pointer");
            check((deleteCurrent & 0xff) == SQLITE_CONSTRAINT,
                  "database trigger protects the current carrier pointer from deletion");
            sqlite3_close_v2(direct);
        } else if (direct != nullptr) {
            sqlite3_close_v2(direct);
        }

        const auto protectedHistory = carrierStore->loadRevision(
            microscopeId, 7, specification, 1);
        const auto protectedCurrent = carrierStore->loadCurrentState(
            microscopeId, 7, specification);
        check(protectedHistory && protectedHistory.value() &&
              protectedCurrent && protectedCurrent.value() &&
              protectedCurrent.value()->revision == 2,
              "rejected raw SQL mutations leave historical calibration and current pointer intact");
    }

    removeDatabaseFiles(path);
}

void testCarrierStoreRejectsStaleWritersAndMismatchedSlideGeometry() {
    const auto path = uniqueDatabasePath();
    const auto microscopeId = makeId<MicroscopeId>("microscope-carrier-conflict");
    const auto sampleType = makeSampleType(
        makeId<SampleTypeId>("sample-type-carrier-conflict"));
    const auto leftSlideId = makeId<SlideId>("slide-carrier-conflict-left");
    const auto rightSlideId = makeId<SlideId>("slide-carrier-conflict-right");
    const auto leftSlide = makeSlide(leftSlideId, sampleType);
    const auto rightSlide = makeSlide(rightSlideId, sampleType);

    {
        auto slideStoreResult = SampleSlideSqliteStore::open(path);
        check(static_cast<bool>(slideStoreResult),
              "opens the source Slide store for stale-writer testing");
        if (!slideStoreResult) {
            removeDatabaseFiles(path);
            return;
        }
        auto slideStore = std::move(slideStoreResult).value();
        check(slideStore->saveSampleTypeRevision(sampleType, 0).isSuccess(),
              "persists the carrier test SampleType");
        check(slideStore->saveSlideRevision(leftSlide, 0).isSuccess() &&
              slideStore->saveSlideRevision(rightSlide, 0).isSuccess(),
              "persists both Slides for the carrier stale-writer test");

        auto firstResult = DualSlideCarrierSqliteStore::open(path);
        auto secondResult = DualSlideCarrierSqliteStore::open(path);
        check(firstResult && secondResult,
              "opens two independent carrier store connections");
        if (!firstResult || !secondResult) {
            removeDatabaseFiles(path);
            return;
        }
        auto first = std::move(firstResult).value();
        auto second = std::move(secondResult).value();

        const auto stage = makeStage();
        const auto specification = makeCarrierSpecification(stage);
        const auto initialState = makeCarrierState(
            specification, leftSlideId, rightSlideId,
            5000.0, 95000.0, "left-conflict-v1", "right-conflict-v1");
        const std::vector<Slide> slides{leftSlide, rightSlide};
        check(first->saveCurrentState(microscopeId, 3, initialState, slides, 0).isSuccess(),
              "first connection creates the carrier state");

        const auto nextState = makeCarrierState(
            specification, leftSlideId, rightSlideId,
            5200.0, 95200.0, "left-conflict-v2", "right-conflict-v2");
        check(second->saveCurrentState(microscopeId, 3, nextState, slides, 1).isSuccess(),
              "second connection appends carrier revision two");

        const auto stale = first->saveCurrentState(
            microscopeId, 3, initialState, slides, 1);
        check(!stale.isSuccess() &&
              stale.error().code == DualSlideCarrierStoreErrorCode::RevisionConflict,
              "stale carrier writer cannot overwrite the latest registration pointer");

        const auto wrongGeometry = makeSlide(leftSlideId, sampleType, 70000.0, 25000.0);
        const std::vector<Slide> mismatchedSlides{wrongGeometry, rightSlide};
        const auto geometryMismatch = first->saveCurrentState(
            microscopeId, 3, nextState, mismatchedSlides, 2);
        check(!geometryMismatch.isSuccess() &&
              geometryMismatch.error().code ==
                  DualSlideCarrierStoreErrorCode::SlideGeometryMismatch,
              "rejects four-corner mapping dimensions that differ from the physical Slide geometry");

        const auto current = first->loadCurrentState(
            microscopeId, 3, specification);
        check(current && current.value() && current.value()->revision == 2,
              "failed geometry validation leaves the committed carrier state unchanged");
    }

    removeDatabaseFiles(path);
}

} // namespace

int main() {
    testCarrierStoreRequiresSlideWorkspaceSchema();
    testCarrierStoreRejectsUnsupportedSampleSlideSchemaVersion();
    testEmptyPhysicalCarrierStateRoundTripsWithoutInventingSlides();
    testFourCornerCarrierRegistrationRoundTripsWithExactSlideRevisions();
    testCarrierStoreRejectsStaleWritersAndMismatchedSlideGeometry();

    if (failures != 0) {
        std::cerr << failures << " dual-slide carrier persistence test(s) failed\n";
        return 1;
    }
    std::cout << "Dual-slide carrier SQLite persistence tests passed\n";
    return 0;
}
