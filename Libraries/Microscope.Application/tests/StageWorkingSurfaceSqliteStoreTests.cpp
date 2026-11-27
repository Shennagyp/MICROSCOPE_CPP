#include "microscope/application/persistence/StageWorkingSurfaceSqliteStore.h"

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
using namespace microscope::application::navigation;
using namespace microscope::application::persistence;

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

template <typename T, typename E>
T take(Result<T, E> result) {
    if (!result) throw std::runtime_error("stage working-surface persistence fixture rejected");
    return std::move(result).value();
}

template <typename T>
T id(const char* value) {
    return take(T::create(value));
}

std::string uniqueDatabasePath() {
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    return (std::filesystem::temp_directory_path() /
        ("microscope-stage-working-surface-" + std::to_string(nonce) + ".sqlite")).string();
}

void removeDatabaseFiles(const std::string& path) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path + "-wal", ignored);
    std::filesystem::remove(path + "-shm", ignored);
}

AxisSpecification axis(const char* name, double minimum = -10'000.0,
    double maximum = 250'000.0) {
    return take(AxisSpecification::create(
        id<StageAxisId>(name),
        AxisMotionType::Linear,
        AxisCoordinateUnit::Micrometers,
        take(AxisOperatingRange::create(
            minimum, maximum, AxisCoordinateUnit::Micrometers)),
        take(PositiveAxisResolution::create(0.1)),
        std::nullopt, std::nullopt, std::nullopt,
        AxisPositionFeedbackCapability::DeviceReported));
}

StageSpecification stage(const char* component = "stage-main") {
    return take(StageSpecification::create(
        id<EquipmentComponentId>(component),
        {axis("stage-x"), axis("stage-y", -10'000.0, 150'000.0)}));
}

SlideRectangleDefinition2D rectangle(
    const char* slide,
    const char* rectangleRevision,
    double x,
    double y,
    SlideRectangleLengthAxis axisAssignment = SlideRectangleLengthAxis::StageX) {
    return take(SlideRectangleDefinition2D::create(
        id<SlideId>(slide),
        id<CalibrationRevisionId>(rectangleRevision),
        {x, y},
        {x + 1.0, y + 1.0},
        axisAssignment,
        75'000.0,
        25'000.0));
}

SlideCornerCorrespondence directCorrespondence() {
    return take(SlideCornerCorrespondence::create({
        SlideRectangleCorner2D::StopPoint,
        SlideRectangleCorner2D::LengthEdgeEnd,
        SlideRectangleCorner2D::OppositeCorner,
        SlideRectangleCorner2D::WidthEdgeEnd}));
}

SlideCornerCorrespondence rotatedCorrespondence() {
    return take(SlideCornerCorrespondence::create({
        SlideRectangleCorner2D::LengthEdgeEnd,
        SlideRectangleCorner2D::OppositeCorner,
        SlideRectangleCorner2D::WidthEdgeEnd,
        SlideRectangleCorner2D::StopPoint}));
}

StageWorkingSurface2D surface(const StageSpecification& installedStage,
    double firstSlideX = 5'000.0,
    const char* firstRectangleRevision = "cad-slide-a-v1") {
    auto first = rectangle("slide-a", firstRectangleRevision,
        firstSlideX, 10'000.0);
    auto second = rectangle("slide-b", "cad-slide-b-v1",
        85'000.0, 10'000.0, SlideRectangleLengthAxis::StageY);
    return take(StageWorkingSurface2D::create(
        installedStage,
        id<StageAxisId>("stage-x"),
        id<StageAxisId>("stage-y"),
        {0.0, 0.0},
        {200'000.0, 100'000.0},
        {std::move(first), std::move(second)}));
}

std::vector<StageWorkingSurfaceSlideBinding> bindings() {
    return {
        {id<SlideId>("slide-a"), directCorrespondence()},
        {id<SlideId>("slide-b"), rotatedCorrespondence()}
    };
}

bool sqlExec(const std::string& path, const char* sql) {
    sqlite3* database = nullptr;
    if (sqlite3_open(path.c_str(), &database) != SQLITE_OK) {
        if (database) sqlite3_close_v2(database);
        return false;
    }
    char* message = nullptr;
    const int result = sqlite3_exec(database, sql, nullptr, nullptr, &message);
    sqlite3_free(message);
    sqlite3_close_v2(database);
    return result == SQLITE_OK;
}

void opensIndependentDatabaseAndRejectsEmptyPath() {
    auto emptyPath = StageWorkingSurfaceSqliteStore::open("");
    check(!emptyPath &&
        emptyPath.error().code == StageWorkingSurfaceStoreErrorCode::EmptyDatabasePath,
        "rejects an empty database path");

    const auto path = uniqueDatabasePath();
    auto opened = StageWorkingSurfaceSqliteStore::open(path);
    check(static_cast<bool>(opened),
        "creates its own schema without requiring SampleSlide or carrier stores first");
    if (opened) opened.value().reset();
    removeDatabaseFiles(path);
}

void roundTripsLayoutsAndPreservesHistoricalRevisions() {
    const auto path = uniqueDatabasePath();
    const auto installedStage = stage();
    auto opened = StageWorkingSurfaceSqliteStore::open(path);
    if (!opened) throw std::runtime_error("could not open stage working-surface store");
    auto& store = *opened.value();

    const auto firstSurface = surface(installedStage);
    const auto firstBindings = bindings();
    const auto firstRevision = store.saveCurrentState(
        id<MicroscopeId>("scope-a"), 7, installedStage,
        firstSurface, firstBindings, 0);
    check(firstRevision && firstRevision.value() == 1,
        "creates the first immutable CAD layout revision");
    if (!firstRevision) {
        removeDatabaseFiles(path);
        return;
    }

    auto loadedFirst = store.loadCurrentState(
        id<MicroscopeId>("scope-a"), 7, installedStage);
    check(loadedFirst && loadedFirst.value().has_value(),
        "loads the current CAD layout revision");
    if (loadedFirst && loadedFirst.value()) {
        const auto& value = *loadedFirst.value();
        check(value.revision == 1 && value.workingSurface.slideRectangles().size() == 2,
            "round-trips two physical slide placements in one stage-local CAD surface");
        check(value.workingSurface.xAxisId().value() == "stage-x" &&
            value.workingSurface.yAxisId().value() == "stage-y" &&
            value.workingSurface.minimumBoundary().xMicrometers == 0.0 &&
            value.workingSurface.maximumBoundary().yMicrometers == 100'000.0,
            "round-trips stage axis identity and the bounded working surface");
        const auto* loadedA = value.workingSurface.rectangleFor(id<SlideId>("slide-a"));
        const auto* loadedB = value.workingSurface.rectangleFor(id<SlideId>("slide-b"));
        check(loadedA && loadedB &&
            loadedA->drawnOppositeEndpoint().xMicrometers == 5'001.0 &&
            loadedA->lengthAxis() == SlideRectangleLengthAxis::StageX &&
            loadedB->lengthAxis() == SlideRectangleLengthAxis::StageY,
            "round-trips CAD stop point, initial handle and axis assignment");

        check(value.slideBindings.size() == 2,
            "persists one explicit corner correspondence for every CAD slide rectangle");
        bool direct = false;
        bool rotated = false;
        for (const auto& binding : value.slideBindings) {
            if (binding.slideId.value() == "slide-a") {
                direct = binding.correspondence.mappings()[0] ==
                    SlideRectangleCorner2D::StopPoint &&
                    binding.correspondence.measuredXMapsToWorkingSurfaceX();
            }
            if (binding.slideId.value() == "slide-b") {
                rotated = binding.correspondence.mappings()[0] ==
                    SlideRectangleCorner2D::LengthEdgeEnd &&
                    !binding.correspondence.measuredXMapsToWorkingSurfaceX();
            }
        }
        check(direct && rotated,
            "round-trips direct and rotated corner correspondences without losing orientation");
    }

    auto movedSurface = surface(installedStage, 6'000.0, "cad-slide-a-v2");
    const auto secondRevision = store.saveCurrentState(
        id<MicroscopeId>("scope-a"), 7, installedStage,
        movedSurface, firstBindings, 1);
    check(secondRevision && secondRevision.value() == 2,
        "appends a second layout revision with optimistic concurrency");
    auto loadedHistorical = store.loadRevision(
        id<MicroscopeId>("scope-a"), 7, installedStage, 1);
    auto loadedCurrent = store.loadCurrentState(
        id<MicroscopeId>("scope-a"), 7, installedStage);
    check(loadedHistorical && loadedHistorical.value() &&
        loadedHistorical.value()->workingSurface.rectangleFor(id<SlideId>("slide-a"))
            ->stopPointEndpoint().xMicrometers == 5'000.0,
        "historical CAD revisions remain immutable after later layout edits");
    check(loadedCurrent && loadedCurrent.value() &&
        loadedCurrent.value()->revision == 2 &&
        loadedCurrent.value()->workingSurface.rectangleFor(id<SlideId>("slide-a"))
            ->stopPointEndpoint().xMicrometers == 6'000.0,
        "current CAD layout advances independently from its historical revision");

    const auto conflict = store.saveCurrentState(
        id<MicroscopeId>("scope-a"), 7, installedStage,
        movedSurface, firstBindings, 1);
    check(!conflict &&
        conflict.error().code == StageWorkingSurfaceStoreErrorCode::RevisionConflict,
        "rejects a stale expected revision instead of overwriting a concurrent CAD edit");

    auto otherMicroscope = store.loadCurrentState(
        id<MicroscopeId>("scope-b"), 7, installedStage);
    auto otherEquipment = store.loadCurrentState(
        id<MicroscopeId>("scope-a"), 8, installedStage);
    check(otherMicroscope && !otherMicroscope.value() &&
        otherEquipment && !otherEquipment.value(),
        "isolates layout revision streams by microscope and equipment-definition revision");

    opened.value().reset();
    removeDatabaseFiles(path);
}

void rejectsIncompleteBindingsAndForeignStageOwnership() {
    const auto path = uniqueDatabasePath();
    const auto installedStage = stage();
    auto opened = StageWorkingSurfaceSqliteStore::open(path);
    if (!opened) throw std::runtime_error("could not open stage working-surface store");
    auto& store = *opened.value();
    const auto workingSurface = surface(installedStage);

    auto incomplete = bindings();
    incomplete.pop_back();
    auto missing = store.saveCurrentState(
        id<MicroscopeId>("scope-validation"), 1, installedStage,
        workingSurface, incomplete, 0);
    check(!missing &&
        missing.error().code == StageWorkingSurfaceStoreErrorCode::InvalidSlideCornerBindings,
        "rejects a CAD slide rectangle that has no explicit corner correspondence");

    auto duplicate = bindings();
    duplicate[1].slideId = id<SlideId>("slide-a");
    auto duplicated = store.saveCurrentState(
        id<MicroscopeId>("scope-validation"), 1, installedStage,
        workingSurface, duplicate, 0);
    check(!duplicated &&
        duplicated.error().code == StageWorkingSurfaceStoreErrorCode::InvalidSlideCornerBindings,
        "rejects duplicate bindings rather than associating one correspondence with multiple slide rectangles");

    const auto foreignStage = stage("other-stage");
    auto mismatch = store.saveCurrentState(
        id<MicroscopeId>("scope-validation"), 1, foreignStage,
        workingSurface, bindings(), 0);
    check(!mismatch &&
        mismatch.error().code == StageWorkingSurfaceStoreErrorCode::EquipmentBindingMismatch,
        "rejects saving coordinates against a different installed stage component");

    auto valid = store.saveCurrentState(
        id<MicroscopeId>("scope-validation"), 1, installedStage,
        workingSurface, bindings(), 0);
    check(valid && valid.value() == 1,
        "validation failures do not consume a layout revision or block a subsequent valid save");

    auto invalidRevision = store.loadRevision(
        id<MicroscopeId>("scope-validation"), 0, installedStage, 1);
    check(!invalidRevision &&
        invalidRevision.error().code == StageWorkingSurfaceStoreErrorCode::InvalidRevision,
        "rejects zero equipment-definition revision during load");

    opened.value().reset();
    removeDatabaseFiles(path);
}

void persistsAnEmptyWorkingSurfaceWithoutInventingSlideBindings() {
    const auto path = uniqueDatabasePath();
    const auto installedStage = stage();
    auto opened = StageWorkingSurfaceSqliteStore::open(path);
    if (!opened) throw std::runtime_error("could not open stage working-surface store");

    const auto emptySurface = take(StageWorkingSurface2D::createEmpty(
        installedStage,
        id<StageAxisId>("stage-x"),
        id<StageAxisId>("stage-y"),
        {0.0, 0.0},
        take(StageTravelRange2D::create(200'000.0, 100'000.0))));
    const auto saved = opened.value()->saveCurrentState(
        id<MicroscopeId>("scope-empty"), 1, installedStage,
        emptySurface, {}, 0);
    check(saved && saved.value() == 1,
        "persists a blank CAD canvas with an empty correspondence collection");

    auto loaded = opened.value()->loadCurrentState(
        id<MicroscopeId>("scope-empty"), 1, installedStage);
    check(loaded && loaded.value() &&
        loaded.value()->workingSurface.slideRectangles().empty() &&
        loaded.value()->slideBindings.empty(),
        "reloads an empty canvas without synthesizing slide identities or bindings");

    opened.value().reset();
    removeDatabaseFiles(path);
}

void revisionRowsAreImmutable() {
    const auto path = uniqueDatabasePath();
    const auto installedStage = stage();
    auto opened = StageWorkingSurfaceSqliteStore::open(path);
    if (!opened) throw std::runtime_error("could not open stage working-surface store");
    const auto saved = opened.value()->saveCurrentState(
        id<MicroscopeId>("scope-immutable"), 1, installedStage,
        surface(installedStage), bindings(), 0);
    check(static_cast<bool>(saved), "creates a layout row before checking immutability");

    const bool updated = sqlExec(path,
        "UPDATE stage_working_surface_revision SET minimum_x = 10 "
        "WHERE microscope_id = 'scope-immutable' AND revision = 1");
    const bool childUpdated = sqlExec(path,
        "UPDATE stage_working_surface_slide SET length_micrometers = 1 "
        "WHERE microscope_id = 'scope-immutable' AND slide_id = 'slide-a'");
    const bool lateChildInserted = sqlExec(path,
        "INSERT INTO stage_working_surface_slide ("
        "microscope_id, equipment_revision, stage_component_id, revision, slide_id, "
        "rectangle_revision_id, stop_x, stop_y, drawn_x, drawn_y, length_axis, "
        "length_micrometers, width_micrometers, top_left_corner, top_right_corner, "
        "bottom_right_corner, bottom_left_corner) VALUES ("
        "'scope-immutable', 1, 'stage-main', 1, 'late-slide', 'cad-late-v1', "
        "1, 1, 2, 2, 0, 10, 5, 0, 1, 2, 3)");
    check(!updated && !childUpdated && !lateChildInserted,
        "SQLite triggers prevent mutation or late insertion into a published historical layout revision");

    opened.value().reset();
    removeDatabaseFiles(path);
}

} // namespace

int main() {
    try {
        opensIndependentDatabaseAndRejectsEmptyPath();
        roundTripsLayoutsAndPreservesHistoricalRevisions();
        rejectsIncompleteBindingsAndForeignStageOwnership();
        persistsAnEmptyWorkingSurfaceWithoutInventingSlideBindings();
        revisionRowsAreImmutable();
    } catch (const std::exception& error) {
        std::cerr << "UNCAUGHT: " << error.what() << '\n';
        return 1;
    }

    if (failures != 0) return 1;
    std::cout << "Stage working-surface SQLite persistence tests passed\n";
    return 0;
}
