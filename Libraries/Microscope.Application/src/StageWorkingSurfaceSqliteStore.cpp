#include "microscope/application/persistence/StageWorkingSurfaceSqliteStore.h"

#include <sqlite3.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace microscope::application::persistence {
namespace {

using Error = StageWorkingSurfaceStoreError;
using ErrorCode = StageWorkingSurfaceStoreErrorCode;

constexpr sqlite3_int64 kSchemaVersion = 1;
constexpr std::size_t kMaximumSlideCount = 4096;
constexpr std::size_t kMaximumIdentityBytes = 4096;
constexpr std::uint64_t kMaximumSqliteInteger =
    static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max());

Error makeError(
    ErrorCode code,
    sqlite3* database,
    std::string detail = {}) {
    if (detail.empty() && database != nullptr) detail = sqlite3_errmsg(database);
    return Error{code, database == nullptr ? 0 : sqlite3_errcode(database),
        std::move(detail)};
}

bool execute(sqlite3* database, const char* sql) {
    char* message = nullptr;
    const int result = sqlite3_exec(database, sql, nullptr, nullptr, &message);
    sqlite3_free(message);
    return result == SQLITE_OK;
}

class Statement final {
public:
    Statement(sqlite3* database, const char* sql) : database_(database) {
        prepared_ = sqlite3_prepare_v2(database_, sql, -1, &statement_, nullptr) == SQLITE_OK;
    }

    ~Statement() {
        if (statement_ != nullptr) sqlite3_finalize(statement_);
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    bool prepared() const noexcept { return prepared_; }
    sqlite3_stmt* get() const noexcept { return statement_; }
    int step() { return sqlite3_step(statement_); }

    bool bindText(int index, const std::string& value) {
        if (value.empty() || value.find('\0') != std::string::npos ||
            value.size() > kMaximumIdentityBytes ||
            value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            return false;
        }
        return sqlite3_bind_text(statement_, index, value.data(),
            static_cast<int>(value.size()), SQLITE_TRANSIENT) == SQLITE_OK;
    }

    bool bindInt64(int index, std::uint64_t value) {
        if (value > kMaximumSqliteInteger) return false;
        return sqlite3_bind_int64(statement_, index,
            static_cast<sqlite3_int64>(value)) == SQLITE_OK;
    }

    bool bindInt(int index, int value) {
        return sqlite3_bind_int(statement_, index, value) == SQLITE_OK;
    }

    bool bindDouble(int index, double value) {
        return std::isfinite(value) &&
            sqlite3_bind_double(statement_, index, value) == SQLITE_OK;
    }

private:
    sqlite3* database_ = nullptr;
    sqlite3_stmt* statement_ = nullptr;
    bool prepared_ = false;
};

class Transaction final {
public:
    explicit Transaction(sqlite3* database) : database_(database) {
        active_ = execute(database_, "BEGIN IMMEDIATE");
    }

    ~Transaction() {
        if (active_) {
            const bool rolledBack = execute(database_, "ROLLBACK");
            (void)rolledBack;
        }
    }

    bool active() const noexcept { return active_; }

    bool commit() {
        if (!active_ || !execute(database_, "COMMIT")) return false;
        active_ = false;
        return true;
    }

private:
    sqlite3* database_ = nullptr;
    bool active_ = false;
};

bool readText(sqlite3_stmt* statement, int column, std::string& value) {
    if (sqlite3_column_type(statement, column) != SQLITE_TEXT) return false;
    const auto* data = sqlite3_column_text(statement, column);
    const int count = sqlite3_column_bytes(statement, column);
    if (data == nullptr || count <= 0 ||
        static_cast<std::size_t>(count) > kMaximumIdentityBytes) return false;
    value.assign(reinterpret_cast<const char*>(data), static_cast<std::size_t>(count));
    return value.find('\0') == std::string::npos;
}

bool readDouble(sqlite3_stmt* statement, int column, double& value) {
    const int type = sqlite3_column_type(statement, column);
    if (type != SQLITE_FLOAT && type != SQLITE_INTEGER) return false;
    value = sqlite3_column_double(statement, column);
    return std::isfinite(value);
}

bool readInt64(sqlite3_stmt* statement, int column, sqlite3_int64& value) {
    if (sqlite3_column_type(statement, column) != SQLITE_INTEGER) return false;
    value = sqlite3_column_int64(statement, column);
    return true;
}

bool validPositiveRevision(std::uint64_t revision) noexcept {
    return revision > 0 && revision <= kMaximumSqliteInteger;
}

bool readCurrentRevision(
    sqlite3* database,
    const core::MicroscopeId& microscopeId,
    std::uint64_t equipmentRevision,
    const core::EquipmentComponentId& stageComponentId,
    std::optional<std::uint64_t>& revision) {
    Statement statement(database,
        "SELECT revision FROM stage_working_surface_current "
        "WHERE microscope_id = ?1 AND equipment_revision = ?2 "
        "AND stage_component_id = ?3");
    if (!statement.prepared() ||
        !statement.bindText(1, microscopeId.value()) ||
        !statement.bindInt64(2, equipmentRevision) ||
        !statement.bindText(3, stageComponentId.value())) return false;

    const int step = statement.step();
    if (step == SQLITE_DONE) {
        revision.reset();
        return true;
    }
    if (step != SQLITE_ROW) return false;
    sqlite3_int64 value = 0;
    if (!readInt64(statement.get(), 0, value) || value <= 0 ||
        statement.step() != SQLITE_DONE) return false;
    revision = static_cast<std::uint64_t>(value);
    return true;
}

int lengthAxisTag(core::SlideRectangleLengthAxis axis) {
    switch (axis) {
    case core::SlideRectangleLengthAxis::StageX: return 0;
    case core::SlideRectangleLengthAxis::StageY: return 1;
    }
    return -1;
}

std::optional<core::SlideRectangleLengthAxis> parseLengthAxis(sqlite3_int64 tag) {
    switch (tag) {
    case 0: return core::SlideRectangleLengthAxis::StageX;
    case 1: return core::SlideRectangleLengthAxis::StageY;
    default: return std::nullopt;
    }
}

int cornerTag(navigation::SlideRectangleCorner2D corner) {
    switch (corner) {
    case navigation::SlideRectangleCorner2D::StopPoint: return 0;
    case navigation::SlideRectangleCorner2D::LengthEdgeEnd: return 1;
    case navigation::SlideRectangleCorner2D::OppositeCorner: return 2;
    case navigation::SlideRectangleCorner2D::WidthEdgeEnd: return 3;
    }
    return -1;
}

std::optional<navigation::SlideRectangleCorner2D> parseCorner(sqlite3_int64 tag) {
    switch (tag) {
    case 0: return navigation::SlideRectangleCorner2D::StopPoint;
    case 1: return navigation::SlideRectangleCorner2D::LengthEdgeEnd;
    case 2: return navigation::SlideRectangleCorner2D::OppositeCorner;
    case 3: return navigation::SlideRectangleCorner2D::WidthEdgeEnd;
    default: return std::nullopt;
    }
}

bool validateWorkingSurface(
    const core::StageSpecification& installedStage,
    const core::StageWorkingSurface2D& workingSurface) {
    if (workingSurface.stageComponentId() != installedStage.componentId()) return false;
    const auto validated = core::StageWorkingSurface2D::create(
        installedStage,
        workingSurface.xAxisId(),
        workingSurface.yAxisId(),
        workingSurface.minimumBoundary(),
        workingSurface.maximumBoundary(),
        workingSurface.slideRectangles());
    return validated &&
        validated.value().xAxisUnit() == workingSurface.xAxisUnit() &&
        validated.value().yAxisUnit() == workingSurface.yAxisUnit();
}

bool validateBindings(
    const core::StageWorkingSurface2D& workingSurface,
    const std::vector<StageWorkingSurfaceSlideBinding>& bindings) {
    if (workingSurface.slideRectangles().size() > kMaximumSlideCount ||
        bindings.size() != workingSurface.slideRectangles().size()) return false;

    std::unordered_map<core::SlideId,
        const StageWorkingSurfaceSlideBinding*> bySlide;
    bySlide.reserve(bindings.size());
    for (const auto& binding : bindings) {
        if (!bySlide.emplace(binding.slideId, &binding).second) return false;
    }
    for (const auto& rectangle : workingSurface.slideRectangles()) {
        if (bySlide.find(rectangle.slideId()) == bySlide.end()) return false;
    }
    return true;
}

Error invalidRecord(sqlite3* database, const char* detail) {
    return makeError(ErrorCode::InvalidPersistedRecord, database, detail);
}

} // namespace

StageWorkingSurfaceSqliteStore::StageWorkingSurfaceSqliteStore(
    sqlite3* database) noexcept : database_(database) {}

StageWorkingSurfaceSqliteStore::~StageWorkingSurfaceSqliteStore() {
    if (database_ != nullptr) sqlite3_close_v2(database_);
}

core::Result<std::unique_ptr<StageWorkingSurfaceSqliteStore>,
    StageWorkingSurfaceStoreError>
StageWorkingSurfaceSqliteStore::open(const std::string& databasePath) {
    if (databasePath.empty()) {
        return core::Result<std::unique_ptr<StageWorkingSurfaceSqliteStore>,
            StageWorkingSurfaceStoreError>::failure(
                StageWorkingSurfaceStoreError{
                    ErrorCode::EmptyDatabasePath, SQLITE_MISUSE,
                    "The stage working-surface database path is empty."});
    }

    sqlite3* database = nullptr;
    const int opened = sqlite3_open_v2(
        databasePath.c_str(), &database,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr);
    if (opened != SQLITE_OK) {
        auto error = makeError(ErrorCode::DatabaseOpenFailed, database,
            "Could not open the stage working-surface SQLite database.");
        if (database != nullptr) sqlite3_close_v2(database);
        return core::Result<std::unique_ptr<StageWorkingSurfaceSqliteStore>,
            StageWorkingSurfaceStoreError>::failure(std::move(error));
    }

    sqlite3_busy_timeout(database, 5000);
    if (!execute(database, "PRAGMA foreign_keys = ON")) {
        auto error = makeError(ErrorCode::DatabaseOpenFailed, database,
            "Could not enable SQLite foreign-key enforcement.");
        sqlite3_close_v2(database);
        return core::Result<std::unique_ptr<StageWorkingSurfaceSqliteStore>,
            StageWorkingSurfaceStoreError>::failure(std::move(error));
    }

    auto store = std::unique_ptr<StageWorkingSurfaceSqliteStore>(
        new StageWorkingSurfaceSqliteStore(database));
    const auto initialized = store->initializeSchema();
    if (!initialized) {
        return core::Result<std::unique_ptr<StageWorkingSurfaceSqliteStore>,
            StageWorkingSurfaceStoreError>::failure(initialized.error());
    }
    return core::Result<std::unique_ptr<StageWorkingSurfaceSqliteStore>,
        StageWorkingSurfaceStoreError>::success(std::move(store));
}

core::Status<StageWorkingSurfaceStoreError>
StageWorkingSurfaceSqliteStore::initializeSchema() {
    const char* schema = R"SQL(
        CREATE TABLE IF NOT EXISTS stage_working_surface_store_metadata (
            singleton INTEGER PRIMARY KEY CHECK (singleton = 1),
            schema_version INTEGER NOT NULL
        );

        CREATE TABLE IF NOT EXISTS stage_working_surface_revision (
            microscope_id TEXT NOT NULL,
            equipment_revision INTEGER NOT NULL CHECK (equipment_revision > 0),
            stage_component_id TEXT NOT NULL,
            revision INTEGER NOT NULL CHECK (revision > 0),
            x_axis_id TEXT NOT NULL,
            y_axis_id TEXT NOT NULL,
            minimum_x REAL NOT NULL,
            minimum_y REAL NOT NULL,
            maximum_x REAL NOT NULL,
            maximum_y REAL NOT NULL,
            PRIMARY KEY (
                microscope_id, equipment_revision, stage_component_id, revision
            ),
            CHECK (minimum_x < maximum_x),
            CHECK (minimum_y < maximum_y)
        );

        CREATE TABLE IF NOT EXISTS stage_working_surface_slide (
            microscope_id TEXT NOT NULL,
            equipment_revision INTEGER NOT NULL,
            stage_component_id TEXT NOT NULL,
            revision INTEGER NOT NULL,
            slide_id TEXT NOT NULL,
            rectangle_revision_id TEXT NOT NULL,
            stop_x REAL NOT NULL,
            stop_y REAL NOT NULL,
            drawn_x REAL NOT NULL,
            drawn_y REAL NOT NULL,
            length_axis INTEGER NOT NULL CHECK (length_axis IN (0, 1)),
            length_micrometers REAL NOT NULL CHECK (length_micrometers > 0),
            width_micrometers REAL NOT NULL CHECK (width_micrometers > 0),
            top_left_corner INTEGER NOT NULL CHECK (top_left_corner BETWEEN 0 AND 3),
            top_right_corner INTEGER NOT NULL CHECK (top_right_corner BETWEEN 0 AND 3),
            bottom_right_corner INTEGER NOT NULL CHECK (bottom_right_corner BETWEEN 0 AND 3),
            bottom_left_corner INTEGER NOT NULL CHECK (bottom_left_corner BETWEEN 0 AND 3),
            PRIMARY KEY (
                microscope_id, equipment_revision, stage_component_id, revision, slide_id
            ),
            FOREIGN KEY (
                microscope_id, equipment_revision, stage_component_id, revision
            ) REFERENCES stage_working_surface_revision (
                microscope_id, equipment_revision, stage_component_id, revision
            )
        );

        CREATE TABLE IF NOT EXISTS stage_working_surface_current (
            microscope_id TEXT NOT NULL,
            equipment_revision INTEGER NOT NULL,
            stage_component_id TEXT NOT NULL,
            revision INTEGER NOT NULL CHECK (revision > 0),
            PRIMARY KEY (microscope_id, equipment_revision, stage_component_id),
            FOREIGN KEY (
                microscope_id, equipment_revision, stage_component_id, revision
            ) REFERENCES stage_working_surface_revision (
                microscope_id, equipment_revision, stage_component_id, revision
            )
        );

        CREATE TRIGGER IF NOT EXISTS stage_working_surface_revision_sequential_insert
        BEFORE INSERT ON stage_working_surface_revision
        WHEN NEW.revision != COALESCE((
            SELECT revision FROM stage_working_surface_current
            WHERE microscope_id = NEW.microscope_id
              AND equipment_revision = NEW.equipment_revision
              AND stage_component_id = NEW.stage_component_id
        ), 0) + 1
        BEGIN SELECT RAISE(ABORT, 'Working-surface revisions must be appended sequentially'); END;

        CREATE TRIGGER IF NOT EXISTS stage_working_surface_revision_no_update
        BEFORE UPDATE ON stage_working_surface_revision
        BEGIN SELECT RAISE(ABORT, 'Working-surface revisions are immutable'); END;

        CREATE TRIGGER IF NOT EXISTS stage_working_surface_revision_no_delete
        BEFORE DELETE ON stage_working_surface_revision
        BEGIN SELECT RAISE(ABORT, 'Working-surface revisions are immutable'); END;

        CREATE TRIGGER IF NOT EXISTS stage_working_surface_slide_sequential_insert
        BEFORE INSERT ON stage_working_surface_slide
        WHEN NEW.revision != COALESCE((
            SELECT revision FROM stage_working_surface_current
            WHERE microscope_id = NEW.microscope_id
              AND equipment_revision = NEW.equipment_revision
              AND stage_component_id = NEW.stage_component_id
        ), 0) + 1
        BEGIN SELECT RAISE(ABORT, 'Working-surface slide rows cannot be added after their revision is published'); END;

        CREATE TRIGGER IF NOT EXISTS stage_working_surface_slide_no_update
        BEFORE UPDATE ON stage_working_surface_slide
        BEGIN SELECT RAISE(ABORT, 'Working-surface slide records are immutable'); END;

        CREATE TRIGGER IF NOT EXISTS stage_working_surface_slide_no_delete
        BEFORE DELETE ON stage_working_surface_slide
        BEGIN SELECT RAISE(ABORT, 'Working-surface slide records are immutable'); END;

        CREATE TRIGGER IF NOT EXISTS stage_working_surface_current_initial_revision
        BEFORE INSERT ON stage_working_surface_current
        WHEN NEW.revision != 1
        BEGIN SELECT RAISE(ABORT, 'Initial working-surface revision must be one'); END;

        CREATE TRIGGER IF NOT EXISTS stage_working_surface_current_monotonic
        BEFORE UPDATE ON stage_working_surface_current
        WHEN NEW.revision != OLD.revision + 1
        BEGIN SELECT RAISE(ABORT, 'Working-surface current revision must advance by one'); END;

        CREATE TRIGGER IF NOT EXISTS stage_working_surface_current_no_delete
        BEFORE DELETE ON stage_working_surface_current
        BEGIN SELECT RAISE(ABORT, 'Working-surface current pointer cannot be deleted'); END;
    )SQL";

    if (!execute(database_, schema)) {
        return core::Status<StageWorkingSurfaceStoreError>::failure(
            makeError(ErrorCode::SchemaMigrationFailed, database_));
    }

    Statement insertMetadata(database_,
        "INSERT OR IGNORE INTO stage_working_surface_store_metadata "
        "(singleton, schema_version) VALUES (1, 1)");
    if (!insertMetadata.prepared() || insertMetadata.step() != SQLITE_DONE) {
        return core::Status<StageWorkingSurfaceStoreError>::failure(
            makeError(ErrorCode::SchemaMigrationFailed, database_));
    }

    Statement readMetadata(database_,
        "SELECT schema_version FROM stage_working_surface_store_metadata "
        "WHERE singleton = 1");
    sqlite3_int64 version = 0;
    if (!readMetadata.prepared() || readMetadata.step() != SQLITE_ROW ||
        !readInt64(readMetadata.get(), 0, version) ||
        readMetadata.step() != SQLITE_DONE) {
        return core::Status<StageWorkingSurfaceStoreError>::failure(
            makeError(ErrorCode::SchemaMigrationFailed, database_));
    }
    if (version != kSchemaVersion) {
        return core::Status<StageWorkingSurfaceStoreError>::failure(
            makeError(ErrorCode::UnsupportedSchemaVersion, database_,
                "The stage working-surface schema version is unsupported."));
    }
    return core::Status<StageWorkingSurfaceStoreError>::success();
}

core::Result<std::uint64_t, StageWorkingSurfaceStoreError>
StageWorkingSurfaceSqliteStore::saveCurrentState(
    const core::MicroscopeId& microscopeId,
    std::uint64_t equipmentDefinitionRevision,
    const core::StageSpecification& installedStage,
    const core::StageWorkingSurface2D& workingSurface,
    const std::vector<StageWorkingSurfaceSlideBinding>& slideBindings,
    std::uint64_t expectedCurrentRevision) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!validPositiveRevision(equipmentDefinitionRevision) ||
        expectedCurrentRevision > kMaximumSqliteInteger ||
        expectedCurrentRevision == kMaximumSqliteInteger) {
        return core::Result<std::uint64_t, StageWorkingSurfaceStoreError>::failure(
            makeError(ErrorCode::InvalidRevision, database_));
    }
    if (!validateWorkingSurface(installedStage, workingSurface)) {
        return core::Result<std::uint64_t, StageWorkingSurfaceStoreError>::failure(
            makeError(ErrorCode::EquipmentBindingMismatch, database_,
                "The CAD working surface does not match the supplied installed stage component, axes, units or travel bounds."));
    }
    if (!validateBindings(workingSurface, slideBindings)) {
        return core::Result<std::uint64_t, StageWorkingSurfaceStoreError>::failure(
            makeError(ErrorCode::InvalidSlideCornerBindings, database_,
                "Every CAD slide rectangle must have exactly one explicit corner correspondence, with no extra or duplicate SlideId."));
    }

    Transaction transaction(database_);
    if (!transaction.active()) {
        return core::Result<std::uint64_t, StageWorkingSurfaceStoreError>::failure(
            makeError(ErrorCode::SqliteOperationFailed, database_));
    }

    std::optional<std::uint64_t> actualCurrent;
    if (!readCurrentRevision(database_, microscopeId, equipmentDefinitionRevision,
            workingSurface.stageComponentId(), actualCurrent)) {
        return core::Result<std::uint64_t, StageWorkingSurfaceStoreError>::failure(
            makeError(ErrorCode::SqliteOperationFailed, database_));
    }
    if (actualCurrent.value_or(0) != expectedCurrentRevision) {
        return core::Result<std::uint64_t, StageWorkingSurfaceStoreError>::failure(
            makeError(ErrorCode::RevisionConflict, database_,
                "The CAD working-surface revision changed since it was loaded."));
    }

    const std::uint64_t nextRevision = expectedCurrentRevision + 1;
    Statement insertSurface(database_,
        "INSERT INTO stage_working_surface_revision ("
        "microscope_id, equipment_revision, stage_component_id, revision, "
        "x_axis_id, y_axis_id, minimum_x, minimum_y, maximum_x, maximum_y) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)");
    if (!insertSurface.prepared() ||
        !insertSurface.bindText(1, microscopeId.value()) ||
        !insertSurface.bindInt64(2, equipmentDefinitionRevision) ||
        !insertSurface.bindText(3, workingSurface.stageComponentId().value()) ||
        !insertSurface.bindInt64(4, nextRevision) ||
        !insertSurface.bindText(5, workingSurface.xAxisId().value()) ||
        !insertSurface.bindText(6, workingSurface.yAxisId().value()) ||
        !insertSurface.bindDouble(7, workingSurface.minimumBoundary().xMicrometers) ||
        !insertSurface.bindDouble(8, workingSurface.minimumBoundary().yMicrometers) ||
        !insertSurface.bindDouble(9, workingSurface.maximumBoundary().xMicrometers) ||
        !insertSurface.bindDouble(10, workingSurface.maximumBoundary().yMicrometers) ||
        insertSurface.step() != SQLITE_DONE) {
        return core::Result<std::uint64_t, StageWorkingSurfaceStoreError>::failure(
            makeError(ErrorCode::SqliteOperationFailed, database_));
    }

    std::unordered_map<core::SlideId,
        const StageWorkingSurfaceSlideBinding*> bindingBySlide;
    bindingBySlide.reserve(slideBindings.size());
    for (const auto& binding : slideBindings) {
        bindingBySlide.emplace(binding.slideId, &binding);
    }

    for (const auto& rectangle : workingSurface.slideRectangles()) {
        const auto bindingIt = bindingBySlide.find(rectangle.slideId());
        if (bindingIt == bindingBySlide.end()) {
            return core::Result<std::uint64_t, StageWorkingSurfaceStoreError>::failure(
                makeError(ErrorCode::InvalidSlideCornerBindings, database_));
        }
        const auto& mapping = bindingIt->second->correspondence.mappings();
        const int axisTag = lengthAxisTag(rectangle.lengthAxis());
        const int tl = cornerTag(mapping[0]);
        const int tr = cornerTag(mapping[1]);
        const int br = cornerTag(mapping[2]);
        const int bl = cornerTag(mapping[3]);
        if (axisTag < 0 || tl < 0 || tr < 0 || br < 0 || bl < 0) {
            return core::Result<std::uint64_t, StageWorkingSurfaceStoreError>::failure(
                makeError(ErrorCode::InvalidSlideCornerBindings, database_));
        }

        Statement insertSlide(database_,
            "INSERT INTO stage_working_surface_slide ("
            "microscope_id, equipment_revision, stage_component_id, revision, slide_id, "
            "rectangle_revision_id, stop_x, stop_y, drawn_x, drawn_y, length_axis, "
            "length_micrometers, width_micrometers, top_left_corner, top_right_corner, "
            "bottom_right_corner, bottom_left_corner) "
            "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17)");
        if (!insertSlide.prepared() ||
            !insertSlide.bindText(1, microscopeId.value()) ||
            !insertSlide.bindInt64(2, equipmentDefinitionRevision) ||
            !insertSlide.bindText(3, workingSurface.stageComponentId().value()) ||
            !insertSlide.bindInt64(4, nextRevision) ||
            !insertSlide.bindText(5, rectangle.slideId().value()) ||
            !insertSlide.bindText(6, rectangle.registrationRevision().value()) ||
            !insertSlide.bindDouble(7, rectangle.stopPointEndpoint().xMicrometers) ||
            !insertSlide.bindDouble(8, rectangle.stopPointEndpoint().yMicrometers) ||
            !insertSlide.bindDouble(9, rectangle.drawnOppositeEndpoint().xMicrometers) ||
            !insertSlide.bindDouble(10, rectangle.drawnOppositeEndpoint().yMicrometers) ||
            !insertSlide.bindInt(11, axisTag) ||
            !insertSlide.bindDouble(12, rectangle.constrainedLengthMicrometers()) ||
            !insertSlide.bindDouble(13, rectangle.constrainedWidthMicrometers()) ||
            !insertSlide.bindInt(14, tl) ||
            !insertSlide.bindInt(15, tr) ||
            !insertSlide.bindInt(16, br) ||
            !insertSlide.bindInt(17, bl) ||
            insertSlide.step() != SQLITE_DONE) {
            return core::Result<std::uint64_t, StageWorkingSurfaceStoreError>::failure(
                makeError(ErrorCode::SqliteOperationFailed, database_));
        }
    }

    if (actualCurrent) {
        Statement updateCurrent(database_,
            "UPDATE stage_working_surface_current SET revision = ?1 "
            "WHERE microscope_id = ?2 AND equipment_revision = ?3 "
            "AND stage_component_id = ?4 AND revision = ?5");
        if (!updateCurrent.prepared() ||
            !updateCurrent.bindInt64(1, nextRevision) ||
            !updateCurrent.bindText(2, microscopeId.value()) ||
            !updateCurrent.bindInt64(3, equipmentDefinitionRevision) ||
            !updateCurrent.bindText(4, workingSurface.stageComponentId().value()) ||
            !updateCurrent.bindInt64(5, expectedCurrentRevision) ||
            updateCurrent.step() != SQLITE_DONE || sqlite3_changes(database_) != 1) {
            return core::Result<std::uint64_t, StageWorkingSurfaceStoreError>::failure(
                makeError(ErrorCode::SqliteOperationFailed, database_));
        }
    } else {
        Statement insertCurrent(database_,
            "INSERT INTO stage_working_surface_current ("
            "microscope_id, equipment_revision, stage_component_id, revision) "
            "VALUES (?1, ?2, ?3, ?4)");
        if (!insertCurrent.prepared() ||
            !insertCurrent.bindText(1, microscopeId.value()) ||
            !insertCurrent.bindInt64(2, equipmentDefinitionRevision) ||
            !insertCurrent.bindText(3, workingSurface.stageComponentId().value()) ||
            !insertCurrent.bindInt64(4, nextRevision) ||
            insertCurrent.step() != SQLITE_DONE) {
            return core::Result<std::uint64_t, StageWorkingSurfaceStoreError>::failure(
                makeError(ErrorCode::SqliteOperationFailed, database_));
        }
    }

    if (!transaction.commit()) {
        return core::Result<std::uint64_t, StageWorkingSurfaceStoreError>::failure(
            makeError(ErrorCode::SqliteOperationFailed, database_));
    }
    return core::Result<std::uint64_t, StageWorkingSurfaceStoreError>::success(
        nextRevision);
}

core::Result<std::optional<PersistedStageWorkingSurface>,
    StageWorkingSurfaceStoreError>
StageWorkingSurfaceSqliteStore::loadCurrentState(
    const core::MicroscopeId& microscopeId,
    std::uint64_t equipmentDefinitionRevision,
    const core::StageSpecification& installedStage) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!validPositiveRevision(equipmentDefinitionRevision)) {
        return core::Result<std::optional<PersistedStageWorkingSurface>,
            StageWorkingSurfaceStoreError>::failure(
                makeError(ErrorCode::InvalidRevision, database_));
    }

    std::optional<std::uint64_t> current;
    if (!readCurrentRevision(database_, microscopeId, equipmentDefinitionRevision,
            installedStage.componentId(), current)) {
        return core::Result<std::optional<PersistedStageWorkingSurface>,
            StageWorkingSurfaceStoreError>::failure(
                makeError(ErrorCode::SqliteOperationFailed, database_));
    }
    if (!current) {
        return core::Result<std::optional<PersistedStageWorkingSurface>,
            StageWorkingSurfaceStoreError>::success(std::nullopt);
    }
    return loadRevisionUnlocked(microscopeId, equipmentDefinitionRevision,
        installedStage, *current);
}

core::Result<std::optional<PersistedStageWorkingSurface>,
    StageWorkingSurfaceStoreError>
StageWorkingSurfaceSqliteStore::loadRevision(
    const core::MicroscopeId& microscopeId,
    std::uint64_t equipmentDefinitionRevision,
    const core::StageSpecification& installedStage,
    std::uint64_t revision) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!validPositiveRevision(equipmentDefinitionRevision) ||
        !validPositiveRevision(revision)) {
        return core::Result<std::optional<PersistedStageWorkingSurface>,
            StageWorkingSurfaceStoreError>::failure(
                makeError(ErrorCode::InvalidRevision, database_));
    }
    return loadRevisionUnlocked(microscopeId, equipmentDefinitionRevision,
        installedStage, revision);
}

core::Result<std::optional<PersistedStageWorkingSurface>,
    StageWorkingSurfaceStoreError>
StageWorkingSurfaceSqliteStore::loadRevisionUnlocked(
    const core::MicroscopeId& microscopeId,
    std::uint64_t equipmentDefinitionRevision,
    const core::StageSpecification& installedStage,
    std::uint64_t revision) {
    Statement surfaceStatement(database_,
        "SELECT x_axis_id, y_axis_id, minimum_x, minimum_y, maximum_x, maximum_y "
        "FROM stage_working_surface_revision "
        "WHERE microscope_id = ?1 AND equipment_revision = ?2 "
        "AND stage_component_id = ?3 AND revision = ?4");
    if (!surfaceStatement.prepared() ||
        !surfaceStatement.bindText(1, microscopeId.value()) ||
        !surfaceStatement.bindInt64(2, equipmentDefinitionRevision) ||
        !surfaceStatement.bindText(3, installedStage.componentId().value()) ||
        !surfaceStatement.bindInt64(4, revision)) {
        return core::Result<std::optional<PersistedStageWorkingSurface>,
            StageWorkingSurfaceStoreError>::failure(
                makeError(ErrorCode::SqliteOperationFailed, database_));
    }

    const int surfaceStep = surfaceStatement.step();
    if (surfaceStep == SQLITE_DONE) {
        return core::Result<std::optional<PersistedStageWorkingSurface>,
            StageWorkingSurfaceStoreError>::success(std::nullopt);
    }
    if (surfaceStep != SQLITE_ROW) {
        return core::Result<std::optional<PersistedStageWorkingSurface>,
            StageWorkingSurfaceStoreError>::failure(
                makeError(ErrorCode::SqliteOperationFailed, database_));
    }

    std::string xAxisText, yAxisText;
    core::StagePoint2D minimum{}, maximum{};
    if (!readText(surfaceStatement.get(), 0, xAxisText) ||
        !readText(surfaceStatement.get(), 1, yAxisText) ||
        !readDouble(surfaceStatement.get(), 2, minimum.xMicrometers) ||
        !readDouble(surfaceStatement.get(), 3, minimum.yMicrometers) ||
        !readDouble(surfaceStatement.get(), 4, maximum.xMicrometers) ||
        !readDouble(surfaceStatement.get(), 5, maximum.yMicrometers) ||
        surfaceStatement.step() != SQLITE_DONE) {
        return core::Result<std::optional<PersistedStageWorkingSurface>,
            StageWorkingSurfaceStoreError>::failure(
                invalidRecord(database_, "The stored working-surface boundary or axis fields are malformed."));
    }

    auto xAxis = core::StageAxisId::create(std::move(xAxisText));
    auto yAxis = core::StageAxisId::create(std::move(yAxisText));
    if (!xAxis || !yAxis) {
        return core::Result<std::optional<PersistedStageWorkingSurface>,
            StageWorkingSurfaceStoreError>::failure(
                invalidRecord(database_, "The stored working-surface stage-axis identity is invalid."));
    }

    Statement slideStatement(database_,
        "SELECT slide_id, rectangle_revision_id, stop_x, stop_y, drawn_x, drawn_y, "
        "length_axis, length_micrometers, width_micrometers, top_left_corner, "
        "top_right_corner, bottom_right_corner, bottom_left_corner "
        "FROM stage_working_surface_slide "
        "WHERE microscope_id = ?1 AND equipment_revision = ?2 "
        "AND stage_component_id = ?3 AND revision = ?4 ORDER BY slide_id");
    if (!slideStatement.prepared() ||
        !slideStatement.bindText(1, microscopeId.value()) ||
        !slideStatement.bindInt64(2, equipmentDefinitionRevision) ||
        !slideStatement.bindText(3, installedStage.componentId().value()) ||
        !slideStatement.bindInt64(4, revision)) {
        return core::Result<std::optional<PersistedStageWorkingSurface>,
            StageWorkingSurfaceStoreError>::failure(
                makeError(ErrorCode::SqliteOperationFailed, database_));
    }

    core::StageWorkingSurface2D::SlideRectangles rectangles;
    std::vector<StageWorkingSurfaceSlideBinding> bindings;
    int step = slideStatement.step();
    while (step == SQLITE_ROW) {
        if (rectangles.size() >= kMaximumSlideCount) {
            return core::Result<std::optional<PersistedStageWorkingSurface>,
                StageWorkingSurfaceStoreError>::failure(
                    invalidRecord(database_, "The stored working surface exceeds its slide-count limit."));
        }

        std::string slideText, rectangleRevisionText;
        core::StagePoint2D stop{}, drawn{};
        sqlite3_int64 axisTag = -1;
        sqlite3_int64 cornerTags[4] = {-1, -1, -1, -1};
        double length = 0.0, width = 0.0;
        if (!readText(slideStatement.get(), 0, slideText) ||
            !readText(slideStatement.get(), 1, rectangleRevisionText) ||
            !readDouble(slideStatement.get(), 2, stop.xMicrometers) ||
            !readDouble(slideStatement.get(), 3, stop.yMicrometers) ||
            !readDouble(slideStatement.get(), 4, drawn.xMicrometers) ||
            !readDouble(slideStatement.get(), 5, drawn.yMicrometers) ||
            !readInt64(slideStatement.get(), 6, axisTag) ||
            !readDouble(slideStatement.get(), 7, length) ||
            !readDouble(slideStatement.get(), 8, width) ||
            !readInt64(slideStatement.get(), 9, cornerTags[0]) ||
            !readInt64(slideStatement.get(), 10, cornerTags[1]) ||
            !readInt64(slideStatement.get(), 11, cornerTags[2]) ||
            !readInt64(slideStatement.get(), 12, cornerTags[3])) {
            return core::Result<std::optional<PersistedStageWorkingSurface>,
                StageWorkingSurfaceStoreError>::failure(
                    invalidRecord(database_, "A stored CAD slide rectangle or corner binding is malformed."));
        }

        const auto lengthAxis = parseLengthAxis(axisTag);
        std::array<navigation::SlideRectangleCorner2D, 4> cornerMappings{};
        bool validCorners = true;
        for (std::size_t i = 0; i < cornerMappings.size(); ++i) {
            const auto parsed = parseCorner(cornerTags[i]);
            if (!parsed) {
                validCorners = false;
                break;
            }
            cornerMappings[i] = *parsed;
        }
        auto slideId = core::SlideId::create(std::move(slideText));
        auto rectangleRevision = core::CalibrationRevisionId::create(
            std::move(rectangleRevisionText));
        if (!lengthAxis || !validCorners || !slideId || !rectangleRevision) {
            return core::Result<std::optional<PersistedStageWorkingSurface>,
                StageWorkingSurfaceStoreError>::failure(
                    invalidRecord(database_, "A stored CAD slide identity, axis assignment or corner correspondence is invalid."));
        }

        auto rectangle = core::SlideRectangleDefinition2D::create(
            slideId.value(), rectangleRevision.value(), stop, drawn,
            *lengthAxis, length, width);
        auto correspondence = navigation::SlideCornerCorrespondence::create(
            cornerMappings);
        if (!rectangle || !correspondence) {
            return core::Result<std::optional<PersistedStageWorkingSurface>,
                StageWorkingSurfaceStoreError>::failure(
                    invalidRecord(database_, "A stored CAD slide rectangle or corner correspondence violates its domain contract."));
        }

        rectangles.push_back(std::move(rectangle).value());
        bindings.push_back(StageWorkingSurfaceSlideBinding{
            std::move(slideId).value(), std::move(correspondence).value()});
        step = slideStatement.step();
    }
    if (step != SQLITE_DONE) {
        return core::Result<std::optional<PersistedStageWorkingSurface>,
            StageWorkingSurfaceStoreError>::failure(
                makeError(ErrorCode::SqliteOperationFailed, database_));
    }

    auto workingSurface = core::StageWorkingSurface2D::create(
        installedStage, std::move(xAxis).value(), std::move(yAxis).value(),
        minimum, maximum, std::move(rectangles));
    if (!workingSurface) {
        return core::Result<std::optional<PersistedStageWorkingSurface>,
            StageWorkingSurfaceStoreError>::failure(
                makeError(ErrorCode::EquipmentBindingMismatch, database_,
                    "The persisted CAD layout is not valid for the currently installed stage axes and travel bounds."));
    }
    if (!validateBindings(workingSurface.value(), bindings)) {
        return core::Result<std::optional<PersistedStageWorkingSurface>,
            StageWorkingSurfaceStoreError>::failure(
                invalidRecord(database_, "The persisted CAD layout does not have exactly one corner correspondence per SlideId."));
    }

    return core::Result<std::optional<PersistedStageWorkingSurface>,
        StageWorkingSurfaceStoreError>::success(
            PersistedStageWorkingSurface{
                revision, std::move(workingSurface).value(), std::move(bindings)});
}

} // namespace microscope::application::persistence
