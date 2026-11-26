#include "microscope/application/persistence/MicroscopeConfigurationSqliteStore.h"

#include <sqlite3.h>

#include <cstdint>
#include <limits>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace microscope::application::persistence {
namespace {

using StoreError = MicroscopeConfigurationStoreError;
using StoreErrorCode = MicroscopeConfigurationStoreErrorCode;

constexpr std::uint64_t kMaximumSqliteInteger =
    static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max());

[[nodiscard]] StoreError makeError(
    StoreErrorCode code,
    sqlite3* database,
    std::string detail = {}) {
    const int nativeCode = database == nullptr ? 0 : sqlite3_errcode(database);
    if (detail.empty() && database != nullptr) {
        detail = sqlite3_errmsg(database);
    }
    return StoreError{code, nativeCode, std::move(detail)};
}

[[nodiscard]] core::Status<StoreError> failureStatus(StoreError error) {
    return core::Status<StoreError>::failure(std::move(error));
}

class Statement final {
public:
    Statement(sqlite3* database, const char* sql) noexcept
        : database_(database) {
        result_ = sqlite3_prepare_v2(database_, sql, -1, &statement_, nullptr);
    }

    ~Statement() {
        if (statement_ != nullptr) {
            sqlite3_finalize(statement_);
        }
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    [[nodiscard]] bool prepared() const noexcept {
        return result_ == SQLITE_OK && statement_ != nullptr;
    }

    [[nodiscard]] sqlite3_stmt* get() const noexcept {
        return statement_;
    }

private:
    sqlite3* database_ = nullptr;
    sqlite3_stmt* statement_ = nullptr;
    int result_ = SQLITE_ERROR;
};

class Transaction final {
public:
    explicit Transaction(sqlite3* database) noexcept
        : database_(database),
          active_(sqlite3_exec(database_, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr)
                  == SQLITE_OK) {}

    ~Transaction() {
        if (active_) {
            sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
        }
    }

    [[nodiscard]] bool active() const noexcept {
        return active_;
    }

    [[nodiscard]] bool commit() noexcept {
        if (!active_ ||
            sqlite3_exec(database_, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
            return false;
        }
        active_ = false;
        return true;
    }

private:
    sqlite3* database_ = nullptr;
    bool active_ = false;
};

[[nodiscard]] bool bindText(
    sqlite3_stmt* statement,
    int index,
    const std::string& value) noexcept {
    if (value.find('\0') != std::string::npos ||
        value.size() > static_cast<std::size_t>(
                           std::numeric_limits<int>::max())) {
        return false;
    }
    return sqlite3_bind_text(
        statement, index, value.c_str(),
        static_cast<int>(value.size()), SQLITE_TRANSIENT) == SQLITE_OK;
}

[[nodiscard]] bool bindOptionalDouble(
    sqlite3_stmt* statement,
    int index,
    const std::optional<double>& value) noexcept {
    return value
        ? sqlite3_bind_double(statement, index, *value) == SQLITE_OK
        : sqlite3_bind_null(statement, index) == SQLITE_OK;
}

[[nodiscard]] std::optional<std::string> readText(
    sqlite3_stmt* statement,
    int column) {
    if (sqlite3_column_type(statement, column) == SQLITE_NULL) {
        return std::nullopt;
    }
    const auto* value = sqlite3_column_text(statement, column);
    const int length = sqlite3_column_bytes(statement, column);
    if (value == nullptr || length < 0) {
        return std::nullopt;
    }
    return std::string(
        reinterpret_cast<const char*>(value),
        static_cast<std::size_t>(length));
}

template <typename TId>
[[nodiscard]] std::optional<TId> readStrongId(
    sqlite3_stmt* statement,
    int column) {
    const auto value = readText(statement, column);
    if (!value) {
        return std::nullopt;
    }
    auto id = TId::create(*value);
    if (!id) {
        return std::nullopt;
    }
    return std::move(id).value();
}

[[nodiscard]] bool isCalibrationBindingMatch(
    const core::LightPathModeSetting& setting,
    const core::LightPathCalibration& calibration) noexcept {
    return setting.microscopeId() == calibration.microscopeId() &&
           setting.equipmentDefinitionRevision() ==
               calibration.equipmentDefinitionRevision() &&
           setting.operationModeId() == calibration.operationModeId() &&
           setting.operationModeRevision() == calibration.operationModeRevision() &&
           setting.lightPathId() == calibration.lightPathId() &&
           setting.calibrationRevisionId() == calibration.revisionId() &&
           setting.illuminationComponentId() ==
               calibration.illuminationComponentId() &&
           setting.illuminationChannelId() == calibration.illuminationChannelId() &&
           setting.configuredOpticalComponentIds() ==
               calibration.configuredOpticalComponentIds();
}

[[nodiscard]] bool readCurrentModeRevision(
    sqlite3* database,
    const core::MicroscopeId& microscopeId,
    const core::OperationModeId& operationModeId,
    std::optional<std::uint64_t>& revision) {
    Statement statement(
        database,
        "SELECT revision FROM operation_mode_current "
        "WHERE microscope_id = ? AND operation_mode_id = ?");
    if (!statement.prepared() ||
        !bindText(statement.get(), 1, microscopeId.value()) ||
        !bindText(statement.get(), 2, operationModeId.value())) {
        return false;
    }

    const int step = sqlite3_step(statement.get());
    if (step == SQLITE_DONE) {
        revision = std::nullopt;
        return true;
    }
    if (step != SQLITE_ROW) {
        return false;
    }

    const sqlite3_int64 stored = sqlite3_column_int64(statement.get(), 0);
    if (stored <= 0) {
        return false;
    }
    revision = static_cast<std::uint64_t>(stored);
    return true;
}

} // namespace

MicroscopeConfigurationSqliteStore::MicroscopeConfigurationSqliteStore(
    sqlite3* database) noexcept
    : database_(database) {}

core::Result<
    std::unique_ptr<MicroscopeConfigurationSqliteStore>,
    MicroscopeConfigurationStoreError> MicroscopeConfigurationSqliteStore::open(
        const std::string& databasePath) {
    if (databasePath.empty()) {
        return core::Result<
            std::unique_ptr<MicroscopeConfigurationSqliteStore>,
            MicroscopeConfigurationStoreError>::failure(
                makeError(StoreErrorCode::EmptyDatabasePath, nullptr));
    }

    sqlite3* database = nullptr;
    const int openResult = sqlite3_open_v2(
        databasePath.c_str(),
        &database,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr);
    if (openResult != SQLITE_OK) {
        auto error = makeError(
            StoreErrorCode::DatabaseOpenFailed,
            database,
            database == nullptr ? "SQLite could not allocate a database handle."
                                : sqlite3_errmsg(database));
        if (database != nullptr) {
            sqlite3_close_v2(database);
        }
        return core::Result<
            std::unique_ptr<MicroscopeConfigurationSqliteStore>,
            MicroscopeConfigurationStoreError>::failure(std::move(error));
    }

    sqlite3_busy_timeout(database, 5000);
    if (sqlite3_exec(
            database, "PRAGMA foreign_keys = ON", nullptr, nullptr, nullptr)
        != SQLITE_OK) {
        auto error = makeError(
            StoreErrorCode::DatabaseOpenFailed, database);
        sqlite3_close_v2(database);
        return core::Result<
            std::unique_ptr<MicroscopeConfigurationSqliteStore>,
            MicroscopeConfigurationStoreError>::failure(std::move(error));
    }

    auto store = std::unique_ptr<MicroscopeConfigurationSqliteStore>(
        new MicroscopeConfigurationSqliteStore(database));
    const auto initialized = store->initializeSchema();
    if (!initialized) {
        return core::Result<
            std::unique_ptr<MicroscopeConfigurationSqliteStore>,
            MicroscopeConfigurationStoreError>::failure(initialized.error());
    }

    return core::Result<
        std::unique_ptr<MicroscopeConfigurationSqliteStore>,
        MicroscopeConfigurationStoreError>::success(std::move(store));
}

MicroscopeConfigurationSqliteStore::~MicroscopeConfigurationSqliteStore() {
    if (database_ != nullptr) {
        sqlite3_close_v2(database_);
        database_ = nullptr;
    }
}

core::Status<MicroscopeConfigurationStoreError>
MicroscopeConfigurationSqliteStore::initializeSchema() {
    std::lock_guard<std::mutex> lock(mutex_);
    Transaction transaction(database_);
    if (!transaction.active()) {
        return failureStatus(makeError(
            StoreErrorCode::SchemaMigrationFailed, database_));
    }

    constexpr const char* schemaSql = R"SQL(
        CREATE TABLE IF NOT EXISTS microscope_configuration_store_schema (
            id INTEGER PRIMARY KEY CHECK (id = 1),
            schema_version INTEGER NOT NULL
        );

        INSERT OR IGNORE INTO microscope_configuration_store_schema (id, schema_version)
        VALUES (1, 1);

        CREATE TABLE IF NOT EXISTS light_path_calibration (
            microscope_id TEXT NOT NULL,
            calibration_revision_id TEXT NOT NULL,
            equipment_definition_revision INTEGER NOT NULL CHECK (equipment_definition_revision > 0),
            operation_mode_id TEXT NOT NULL,
            operation_mode_revision INTEGER NOT NULL CHECK (operation_mode_revision > 0),
            light_path_id TEXT NOT NULL,
            illumination_component_id TEXT NOT NULL,
            illumination_channel_id TEXT NOT NULL,
            drive_unit INTEGER NOT NULL,
            PRIMARY KEY (microscope_id, calibration_revision_id)
        );

        CREATE TABLE IF NOT EXISTS light_path_calibration_point (
            microscope_id TEXT NOT NULL,
            calibration_revision_id TEXT NOT NULL,
            point_index INTEGER NOT NULL CHECK (point_index >= 0),
            drive_value REAL NOT NULL,
            irradiance_uw_per_mm2 REAL NOT NULL,
            relative_uncertainty_percent REAL NOT NULL,
            PRIMARY KEY (microscope_id, calibration_revision_id, point_index),
            FOREIGN KEY (microscope_id, calibration_revision_id)
                REFERENCES light_path_calibration (microscope_id, calibration_revision_id)
                ON DELETE CASCADE
        );

        CREATE TABLE IF NOT EXISTS light_path_calibration_optical_component (
            microscope_id TEXT NOT NULL,
            calibration_revision_id TEXT NOT NULL,
            component_id TEXT NOT NULL,
            PRIMARY KEY (microscope_id, calibration_revision_id, component_id),
            FOREIGN KEY (microscope_id, calibration_revision_id)
                REFERENCES light_path_calibration (microscope_id, calibration_revision_id)
                ON DELETE CASCADE
        );

        CREATE TABLE IF NOT EXISTS operation_mode (
            microscope_id TEXT NOT NULL,
            operation_mode_id TEXT NOT NULL,
            revision INTEGER NOT NULL CHECK (revision > 0),
            name TEXT NOT NULL,
            contrast_method INTEGER NOT NULL,
            PRIMARY KEY (microscope_id, operation_mode_id, revision)
        );

        CREATE TABLE IF NOT EXISTS operation_mode_current (
            microscope_id TEXT NOT NULL,
            operation_mode_id TEXT NOT NULL,
            revision INTEGER NOT NULL CHECK (revision > 0),
            PRIMARY KEY (microscope_id, operation_mode_id),
            FOREIGN KEY (microscope_id, operation_mode_id, revision)
                REFERENCES operation_mode (microscope_id, operation_mode_id, revision)
        );

        CREATE TABLE IF NOT EXISTS operation_mode_light_path_setting (
            microscope_id TEXT NOT NULL,
            operation_mode_id TEXT NOT NULL,
            mode_revision INTEGER NOT NULL,
            light_path_id TEXT NOT NULL,
            equipment_definition_revision INTEGER NOT NULL CHECK (equipment_definition_revision > 0),
            calibration_revision_id TEXT NOT NULL,
            illumination_component_id TEXT NOT NULL,
            illumination_channel_id TEXT NOT NULL,
            target_irradiance_uw_per_mm2 REAL NOT NULL CHECK (target_irradiance_uw_per_mm2 > 0),
            PRIMARY KEY (microscope_id, operation_mode_id, mode_revision, light_path_id),
            FOREIGN KEY (microscope_id, operation_mode_id, mode_revision)
                REFERENCES operation_mode (microscope_id, operation_mode_id, revision)
                ON DELETE CASCADE,
            FOREIGN KEY (microscope_id, calibration_revision_id)
                REFERENCES light_path_calibration (microscope_id, calibration_revision_id)
        );

        CREATE TABLE IF NOT EXISTS operation_mode_setting_optical_component (
            microscope_id TEXT NOT NULL,
            operation_mode_id TEXT NOT NULL,
            mode_revision INTEGER NOT NULL,
            light_path_id TEXT NOT NULL,
            component_id TEXT NOT NULL,
            PRIMARY KEY (
                microscope_id, operation_mode_id, mode_revision,
                light_path_id, component_id
            ),
            FOREIGN KEY (microscope_id, operation_mode_id, mode_revision, light_path_id)
                REFERENCES operation_mode_light_path_setting (
                    microscope_id, operation_mode_id, mode_revision, light_path_id
                )
                ON DELETE CASCADE
        );

        CREATE INDEX IF NOT EXISTS idx_operation_mode_latest
        ON operation_mode_current (microscope_id, operation_mode_id);
    )SQL";

    char* errorMessage = nullptr;
    const int result = sqlite3_exec(
        database_, schemaSql, nullptr, nullptr, &errorMessage);
    if (result != SQLITE_OK) {
        std::string detail = errorMessage == nullptr
            ? sqlite3_errmsg(database_)
            : errorMessage;
        sqlite3_free(errorMessage);
        return failureStatus(makeError(
            StoreErrorCode::SchemaMigrationFailed, database_, std::move(detail)));
    }

    Statement version(
        database_,
        "SELECT schema_version FROM microscope_configuration_store_schema WHERE id = 1");
    if (!version.prepared() || sqlite3_step(version.get()) != SQLITE_ROW) {
        return failureStatus(makeError(
            StoreErrorCode::SchemaMigrationFailed, database_));
    }
    const int schemaVersion = sqlite3_column_int(version.get(), 0);
    if (schemaVersion != 1) {
        return failureStatus(makeError(
            StoreErrorCode::UnsupportedSchemaVersion,
            database_,
            "Unsupported microscope configuration persistence schema version."));
    }

    if (!transaction.commit()) {
        return failureStatus(makeError(
            StoreErrorCode::SchemaMigrationFailed, database_));
    }
    return core::Status<MicroscopeConfigurationStoreError>::success();
}

core::Status<MicroscopeConfigurationStoreError>
MicroscopeConfigurationSqliteStore::saveCalibration(
    const core::LightPathCalibration& calibration) {
    std::lock_guard<std::mutex> lock(mutex_);
    Transaction transaction(database_);
    if (!transaction.active()) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }

    Statement insert(
        database_,
        "INSERT INTO light_path_calibration ("
        "microscope_id, calibration_revision_id, equipment_definition_revision, "
        "operation_mode_id, operation_mode_revision, light_path_id, "
        "illumination_component_id, illumination_channel_id, drive_unit"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
    if (!insert.prepared() ||
        !bindText(insert.get(), 1, calibration.microscopeId().value()) ||
        !bindText(insert.get(), 2, calibration.revisionId().value()) ||
        sqlite3_bind_int64(
            insert.get(), 3,
            static_cast<sqlite3_int64>(calibration.equipmentDefinitionRevision())) != SQLITE_OK ||
        !bindText(insert.get(), 4, calibration.operationModeId().value()) ||
        sqlite3_bind_int64(
            insert.get(), 5,
            static_cast<sqlite3_int64>(calibration.operationModeRevision())) != SQLITE_OK ||
        !bindText(insert.get(), 6, calibration.lightPathId().value()) ||
        !bindText(insert.get(), 7, calibration.illuminationComponentId().value()) ||
        !bindText(insert.get(), 8, calibration.illuminationChannelId().value()) ||
        sqlite3_bind_int(
            insert.get(), 9, static_cast<int>(calibration.driveUnit())) != SQLITE_OK) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }

    if (sqlite3_step(insert.get()) != SQLITE_DONE) {
        const int nativeCode = sqlite3_errcode(database_);
        return failureStatus(makeError(
            nativeCode == SQLITE_CONSTRAINT
                ? StoreErrorCode::DuplicateCalibrationRevision
                : StoreErrorCode::SqliteOperationFailed,
            database_));
    }

    Statement insertPoint(
        database_,
        "INSERT INTO light_path_calibration_point ("
        "microscope_id, calibration_revision_id, point_index, drive_value, "
        "irradiance_uw_per_mm2, relative_uncertainty_percent"
        ") VALUES (?, ?, ?, ?, ?, ?)");
    if (!insertPoint.prepared()) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }

    for (std::size_t index = 0; index < calibration.points().size(); ++index) {
        const auto& point = calibration.points()[index];
        if (!bindText(insertPoint.get(), 1, calibration.microscopeId().value()) ||
            !bindText(insertPoint.get(), 2, calibration.revisionId().value()) ||
            sqlite3_bind_int64(
                insertPoint.get(), 3, static_cast<sqlite3_int64>(index)) != SQLITE_OK ||
            sqlite3_bind_double(insertPoint.get(), 4, point.driveValue) != SQLITE_OK ||
            sqlite3_bind_double(
                insertPoint.get(), 5,
                point.irradianceMicrowattsPerSquareMillimeter) != SQLITE_OK ||
            sqlite3_bind_double(
                insertPoint.get(), 6, point.relativeUncertaintyPercent) != SQLITE_OK ||
            sqlite3_step(insertPoint.get()) != SQLITE_DONE) {
            return failureStatus(makeError(
                StoreErrorCode::SqliteOperationFailed, database_));
        }
        sqlite3_reset(insertPoint.get());
        sqlite3_clear_bindings(insertPoint.get());
    }

    Statement insertComponent(
        database_,
        "INSERT INTO light_path_calibration_optical_component ("
        "microscope_id, calibration_revision_id, component_id"
        ") VALUES (?, ?, ?)");
    if (!insertComponent.prepared()) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }

    for (const auto& componentId : calibration.configuredOpticalComponentIds()) {
        if (!bindText(insertComponent.get(), 1, calibration.microscopeId().value()) ||
            !bindText(insertComponent.get(), 2, calibration.revisionId().value()) ||
            !bindText(insertComponent.get(), 3, componentId.value()) ||
            sqlite3_step(insertComponent.get()) != SQLITE_DONE) {
            return failureStatus(makeError(
                StoreErrorCode::SqliteOperationFailed, database_));
        }
        sqlite3_reset(insertComponent.get());
        sqlite3_clear_bindings(insertComponent.get());
    }

    if (!transaction.commit()) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }
    return core::Status<MicroscopeConfigurationStoreError>::success();
}

core::Result<
    std::optional<core::LightPathCalibration>,
    MicroscopeConfigurationStoreError>
MicroscopeConfigurationSqliteStore::loadCalibrationUnlocked(
    const core::MicroscopeId& microscopeId,
    const core::CalibrationRevisionId& calibrationRevisionId) {
    Statement header(
        database_,
        "SELECT equipment_definition_revision, operation_mode_id, "
        "operation_mode_revision, light_path_id, illumination_component_id, "
        "illumination_channel_id, drive_unit "
        "FROM light_path_calibration "
        "WHERE microscope_id = ? AND calibration_revision_id = ?");
    if (!header.prepared() ||
        !bindText(header.get(), 1, microscopeId.value()) ||
        !bindText(header.get(), 2, calibrationRevisionId.value())) {
        return core::Result<
            std::optional<core::LightPathCalibration>,
            MicroscopeConfigurationStoreError>::failure(
                makeError(StoreErrorCode::SqliteOperationFailed, database_));
    }

    const int headerStep = sqlite3_step(header.get());
    if (headerStep == SQLITE_DONE) {
        return core::Result<
            std::optional<core::LightPathCalibration>,
            MicroscopeConfigurationStoreError>::success(std::nullopt);
    }
    if (headerStep != SQLITE_ROW) {
        return core::Result<
            std::optional<core::LightPathCalibration>,
            MicroscopeConfigurationStoreError>::failure(
                makeError(StoreErrorCode::SqliteOperationFailed, database_));
    }

    const sqlite3_int64 equipmentRevision =
        sqlite3_column_int64(header.get(), 0);
    const auto operationModeId =
        readStrongId<core::OperationModeId>(header.get(), 1);
    const sqlite3_int64 operationModeRevision =
        sqlite3_column_int64(header.get(), 2);
    const auto lightPathId =
        readStrongId<core::LightPathId>(header.get(), 3);
    const auto illuminationComponentId =
        readStrongId<core::EquipmentComponentId>(header.get(), 4);
    const auto illuminationChannelId =
        readStrongId<core::IlluminationChannelId>(header.get(), 5);
    const auto driveUnit =
        static_cast<core::IlluminationIntensityUnit>(
            sqlite3_column_int(header.get(), 6));

    if (equipmentRevision <= 0 || operationModeRevision <= 0 ||
        !operationModeId || !lightPathId ||
        !illuminationComponentId || !illuminationChannelId) {
        return core::Result<
            std::optional<core::LightPathCalibration>,
            MicroscopeConfigurationStoreError>::failure(
                makeError(StoreErrorCode::InvalidPersistedRecord, database_));
    }

    std::vector<core::LightPathCalibrationPoint> points;
    {
        Statement pointRows(
            database_,
            "SELECT drive_value, irradiance_uw_per_mm2, relative_uncertainty_percent "
            "FROM light_path_calibration_point "
            "WHERE microscope_id = ? AND calibration_revision_id = ? "
            "ORDER BY point_index");
        if (!pointRows.prepared() ||
            !bindText(pointRows.get(), 1, microscopeId.value()) ||
            !bindText(pointRows.get(), 2, calibrationRevisionId.value())) {
            return core::Result<
                std::optional<core::LightPathCalibration>,
                MicroscopeConfigurationStoreError>::failure(
                    makeError(StoreErrorCode::SqliteOperationFailed, database_));
        }

        for (;;) {
            const int step = sqlite3_step(pointRows.get());
            if (step == SQLITE_DONE) {
                break;
            }
            if (step != SQLITE_ROW) {
                return core::Result<
                    std::optional<core::LightPathCalibration>,
                    MicroscopeConfigurationStoreError>::failure(
                        makeError(StoreErrorCode::SqliteOperationFailed, database_));
            }
            points.push_back({
                sqlite3_column_double(pointRows.get(), 0),
                sqlite3_column_double(pointRows.get(), 1),
                sqlite3_column_double(pointRows.get(), 2)});
        }
    }

    std::vector<core::EquipmentComponentId> opticalComponentIds;
    {
        Statement componentRows(
            database_,
            "SELECT component_id FROM light_path_calibration_optical_component "
            "WHERE microscope_id = ? AND calibration_revision_id = ? "
            "ORDER BY component_id");
        if (!componentRows.prepared() ||
            !bindText(componentRows.get(), 1, microscopeId.value()) ||
            !bindText(componentRows.get(), 2, calibrationRevisionId.value())) {
            return core::Result<
                std::optional<core::LightPathCalibration>,
                MicroscopeConfigurationStoreError>::failure(
                    makeError(StoreErrorCode::SqliteOperationFailed, database_));
        }

        for (;;) {
            const int step = sqlite3_step(componentRows.get());
            if (step == SQLITE_DONE) {
                break;
            }
            if (step != SQLITE_ROW) {
                return core::Result<
                    std::optional<core::LightPathCalibration>,
                    MicroscopeConfigurationStoreError>::failure(
                        makeError(StoreErrorCode::SqliteOperationFailed, database_));
            }
            auto componentId =
                readStrongId<core::EquipmentComponentId>(componentRows.get(), 0);
            if (!componentId) {
                return core::Result<
                    std::optional<core::LightPathCalibration>,
                    MicroscopeConfigurationStoreError>::failure(
                        makeError(StoreErrorCode::InvalidPersistedRecord, database_));
            }
            opticalComponentIds.push_back(std::move(*componentId));
        }
    }

    auto calibration = core::LightPathCalibration::create(
        calibrationRevisionId,
        microscopeId,
        static_cast<std::uint64_t>(equipmentRevision),
        *operationModeId,
        static_cast<std::uint64_t>(operationModeRevision),
        *lightPathId,
        *illuminationComponentId,
        *illuminationChannelId,
        std::move(opticalComponentIds),
        driveUnit,
        std::move(points));
    if (!calibration) {
        return core::Result<
            std::optional<core::LightPathCalibration>,
            MicroscopeConfigurationStoreError>::failure(
                makeError(StoreErrorCode::InvalidPersistedRecord, database_));
    }

    return core::Result<
        std::optional<core::LightPathCalibration>,
        MicroscopeConfigurationStoreError>::success(
            std::optional<core::LightPathCalibration>{
                std::move(calibration).value()});
}

core::Result<
    std::optional<core::LightPathCalibration>,
    MicroscopeConfigurationStoreError>
MicroscopeConfigurationSqliteStore::loadCalibration(
    const core::MicroscopeId& microscopeId,
    const core::CalibrationRevisionId& calibrationRevisionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    return loadCalibrationUnlocked(microscopeId, calibrationRevisionId);
}

core::Status<MicroscopeConfigurationStoreError>
MicroscopeConfigurationSqliteStore::saveOperationMode(
    const core::OperationMode& operationMode,
    std::uint64_t expectedCurrentRevision) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (expectedCurrentRevision >= kMaximumSqliteInteger ||
        operationMode.revision() != expectedCurrentRevision + 1 ||
        operationMode.revision() > kMaximumSqliteInteger) {
        return failureStatus(makeError(
            StoreErrorCode::InvalidRevision,
            database_,
            "The new operation mode revision must be exactly one greater than the expected current revision."));
    }

    Transaction transaction(database_);
    if (!transaction.active()) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }

    std::optional<std::uint64_t> currentRevision;
    if (!readCurrentModeRevision(
            database_,
            operationMode.microscopeId(),
            operationMode.id(),
            currentRevision)) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }
    if (currentRevision.value_or(0) != expectedCurrentRevision) {
        return failureStatus(makeError(
            StoreErrorCode::RevisionConflict,
            database_,
            "The operation mode changed since it was loaded."));
    }

    for (const auto& setting : operationMode.lightPathSettings()) {
        auto calibration = loadCalibrationUnlocked(
            operationMode.microscopeId(),
            setting.calibrationRevisionId());
        if (!calibration) {
            return failureStatus(calibration.error());
        }
        if (!calibration.value()) {
            return failureStatus(makeError(
                StoreErrorCode::CalibrationNotFound,
                database_,
                "An operation mode references a calibration revision that is not persisted."));
        }
        if (!isCalibrationBindingMatch(setting, *calibration.value())) {
            return failureStatus(makeError(
                StoreErrorCode::CalibrationBindingMismatch,
                database_,
                "An operation mode LightPath setting does not match its referenced calibration revision."));
        }
    }

    Statement insertMode(
        database_,
        "INSERT INTO operation_mode ("
        "microscope_id, operation_mode_id, revision, name, contrast_method"
        ") VALUES (?, ?, ?, ?, ?)");
    if (!insertMode.prepared() ||
        !bindText(insertMode.get(), 1, operationMode.microscopeId().value()) ||
        !bindText(insertMode.get(), 2, operationMode.id().value()) ||
        sqlite3_bind_int64(
            insertMode.get(), 3,
            static_cast<sqlite3_int64>(operationMode.revision())) != SQLITE_OK ||
        !bindText(insertMode.get(), 4, operationMode.name()) ||
        sqlite3_bind_int(
            insertMode.get(), 5,
            static_cast<int>(operationMode.contrastMethod())) != SQLITE_OK ||
        sqlite3_step(insertMode.get()) != SQLITE_DONE) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }

    Statement insertSetting(
        database_,
        "INSERT INTO operation_mode_light_path_setting ("
        "microscope_id, operation_mode_id, mode_revision, light_path_id, "
        "equipment_definition_revision, calibration_revision_id, "
        "illumination_component_id, illumination_channel_id, "
        "target_irradiance_uw_per_mm2"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
    Statement insertComponent(
        database_,
        "INSERT INTO operation_mode_setting_optical_component ("
        "microscope_id, operation_mode_id, mode_revision, light_path_id, component_id"
        ") VALUES (?, ?, ?, ?, ?)");
    if (!insertSetting.prepared() || !insertComponent.prepared()) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }

    for (const auto& setting : operationMode.lightPathSettings()) {
        if (!bindText(insertSetting.get(), 1, operationMode.microscopeId().value()) ||
            !bindText(insertSetting.get(), 2, operationMode.id().value()) ||
            sqlite3_bind_int64(
                insertSetting.get(), 3,
                static_cast<sqlite3_int64>(operationMode.revision())) != SQLITE_OK ||
            !bindText(insertSetting.get(), 4, setting.lightPathId().value()) ||
            sqlite3_bind_int64(
                insertSetting.get(), 5,
                static_cast<sqlite3_int64>(setting.equipmentDefinitionRevision())) != SQLITE_OK ||
            !bindText(insertSetting.get(), 6, setting.calibrationRevisionId().value()) ||
            !bindText(insertSetting.get(), 7, setting.illuminationComponentId().value()) ||
            !bindText(insertSetting.get(), 8, setting.illuminationChannelId().value()) ||
            sqlite3_bind_double(
                insertSetting.get(), 9,
                setting.targetIrradianceMicrowattsPerSquareMillimeter()) != SQLITE_OK ||
            sqlite3_step(insertSetting.get()) != SQLITE_DONE) {
            return failureStatus(makeError(
                StoreErrorCode::SqliteOperationFailed, database_));
        }
        sqlite3_reset(insertSetting.get());
        sqlite3_clear_bindings(insertSetting.get());

        for (const auto& componentId : setting.configuredOpticalComponentIds()) {
            if (!bindText(insertComponent.get(), 1, operationMode.microscopeId().value()) ||
                !bindText(insertComponent.get(), 2, operationMode.id().value()) ||
                sqlite3_bind_int64(
                    insertComponent.get(), 3,
                    static_cast<sqlite3_int64>(operationMode.revision())) != SQLITE_OK ||
                !bindText(insertComponent.get(), 4, setting.lightPathId().value()) ||
                !bindText(insertComponent.get(), 5, componentId.value()) ||
                sqlite3_step(insertComponent.get()) != SQLITE_DONE) {
                return failureStatus(makeError(
                    StoreErrorCode::SqliteOperationFailed, database_));
            }
            sqlite3_reset(insertComponent.get());
            sqlite3_clear_bindings(insertComponent.get());
        }
    }

    if (currentRevision) {
        Statement updateCurrent(
            database_,
            "UPDATE operation_mode_current SET revision = ? "
            "WHERE microscope_id = ? AND operation_mode_id = ? AND revision = ?");
        if (!updateCurrent.prepared() ||
            sqlite3_bind_int64(
                updateCurrent.get(), 1,
                static_cast<sqlite3_int64>(operationMode.revision())) != SQLITE_OK ||
            !bindText(updateCurrent.get(), 2, operationMode.microscopeId().value()) ||
            !bindText(updateCurrent.get(), 3, operationMode.id().value()) ||
            sqlite3_bind_int64(
                updateCurrent.get(), 4,
                static_cast<sqlite3_int64>(*currentRevision)) != SQLITE_OK ||
            sqlite3_step(updateCurrent.get()) != SQLITE_DONE ||
            sqlite3_changes(database_) != 1) {
            return failureStatus(makeError(
                StoreErrorCode::RevisionConflict, database_));
        }
    } else {
        Statement insertCurrent(
            database_,
            "INSERT INTO operation_mode_current (microscope_id, operation_mode_id, revision) "
            "VALUES (?, ?, ?)");
        if (!insertCurrent.prepared() ||
            !bindText(insertCurrent.get(), 1, operationMode.microscopeId().value()) ||
            !bindText(insertCurrent.get(), 2, operationMode.id().value()) ||
            sqlite3_bind_int64(
                insertCurrent.get(), 3,
                static_cast<sqlite3_int64>(operationMode.revision())) != SQLITE_OK ||
            sqlite3_step(insertCurrent.get()) != SQLITE_DONE) {
            return failureStatus(makeError(
                StoreErrorCode::SqliteOperationFailed, database_));
        }
    }

    if (!transaction.commit()) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }
    return core::Status<MicroscopeConfigurationStoreError>::success();
}

core::Result<
    std::optional<core::OperationMode>,
    MicroscopeConfigurationStoreError>
MicroscopeConfigurationSqliteStore::loadCurrentOperationMode(
    const core::MicroscopeId& microscopeId,
    const core::OperationModeId& operationModeId) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::optional<std::uint64_t> currentRevision;
    if (!readCurrentModeRevision(
            database_, microscopeId, operationModeId, currentRevision)) {
        return core::Result<
            std::optional<core::OperationMode>,
            MicroscopeConfigurationStoreError>::failure(
                makeError(StoreErrorCode::SqliteOperationFailed, database_));
    }
    if (!currentRevision) {
        return core::Result<
            std::optional<core::OperationMode>,
            MicroscopeConfigurationStoreError>::success(std::nullopt);
    }

    std::string name;
    core::ContrastMethod contrastMethod = core::ContrastMethod::Unknown;
    {
        Statement mode(
            database_,
            "SELECT name, contrast_method FROM operation_mode "
            "WHERE microscope_id = ? AND operation_mode_id = ? AND revision = ?");
        if (!mode.prepared() ||
            !bindText(mode.get(), 1, microscopeId.value()) ||
            !bindText(mode.get(), 2, operationModeId.value()) ||
            sqlite3_bind_int64(
                mode.get(), 3,
                static_cast<sqlite3_int64>(*currentRevision)) != SQLITE_OK) {
            return core::Result<
                std::optional<core::OperationMode>,
                MicroscopeConfigurationStoreError>::failure(
                    makeError(StoreErrorCode::SqliteOperationFailed, database_));
        }
        if (sqlite3_step(mode.get()) != SQLITE_ROW) {
            return core::Result<
                std::optional<core::OperationMode>,
                MicroscopeConfigurationStoreError>::failure(
                    makeError(StoreErrorCode::InvalidPersistedRecord, database_));
        }
        auto storedName = readText(mode.get(), 0);
        if (!storedName) {
            return core::Result<
                std::optional<core::OperationMode>,
                MicroscopeConfigurationStoreError>::failure(
                    makeError(StoreErrorCode::InvalidPersistedRecord, database_));
        }
        name = std::move(*storedName);
        contrastMethod = static_cast<core::ContrastMethod>(
            sqlite3_column_int(mode.get(), 1));
    }

    std::vector<core::LightPathModeSetting> settings;
    {
        Statement settingRows(
            database_,
            "SELECT light_path_id, equipment_definition_revision, "
            "calibration_revision_id, illumination_component_id, "
            "illumination_channel_id, target_irradiance_uw_per_mm2 "
            "FROM operation_mode_light_path_setting "
            "WHERE microscope_id = ? AND operation_mode_id = ? AND mode_revision = ? "
            "ORDER BY light_path_id");
        if (!settingRows.prepared() ||
            !bindText(settingRows.get(), 1, microscopeId.value()) ||
            !bindText(settingRows.get(), 2, operationModeId.value()) ||
            sqlite3_bind_int64(
                settingRows.get(), 3,
                static_cast<sqlite3_int64>(*currentRevision)) != SQLITE_OK) {
            return core::Result<
                std::optional<core::OperationMode>,
                MicroscopeConfigurationStoreError>::failure(
                    makeError(StoreErrorCode::SqliteOperationFailed, database_));
        }

        for (;;) {
            const int step = sqlite3_step(settingRows.get());
            if (step == SQLITE_DONE) {
                break;
            }
            if (step != SQLITE_ROW) {
                return core::Result<
                    std::optional<core::OperationMode>,
                    MicroscopeConfigurationStoreError>::failure(
                        makeError(StoreErrorCode::SqliteOperationFailed, database_));
            }

            const auto lightPathId =
                readStrongId<core::LightPathId>(settingRows.get(), 0);
            const sqlite3_int64 equipmentRevision =
                sqlite3_column_int64(settingRows.get(), 1);
            const auto calibrationRevisionId =
                readStrongId<core::CalibrationRevisionId>(settingRows.get(), 2);
            const auto illuminationComponentId =
                readStrongId<core::EquipmentComponentId>(settingRows.get(), 3);
            const auto illuminationChannelId =
                readStrongId<core::IlluminationChannelId>(settingRows.get(), 4);
            const double targetIrradiance =
                sqlite3_column_double(settingRows.get(), 5);
            if (!lightPathId || equipmentRevision <= 0 ||
                !calibrationRevisionId || !illuminationComponentId ||
                !illuminationChannelId) {
                return core::Result<
                    std::optional<core::OperationMode>,
                    MicroscopeConfigurationStoreError>::failure(
                        makeError(StoreErrorCode::InvalidPersistedRecord, database_));
            }

            std::vector<core::EquipmentComponentId> opticalComponentIds;
            Statement componentRows(
                database_,
                "SELECT component_id FROM operation_mode_setting_optical_component "
                "WHERE microscope_id = ? AND operation_mode_id = ? AND mode_revision = ? "
                "AND light_path_id = ? ORDER BY component_id");
            if (!componentRows.prepared() ||
                !bindText(componentRows.get(), 1, microscopeId.value()) ||
                !bindText(componentRows.get(), 2, operationModeId.value()) ||
                sqlite3_bind_int64(
                    componentRows.get(), 3,
                    static_cast<sqlite3_int64>(*currentRevision)) != SQLITE_OK ||
                !bindText(componentRows.get(), 4, lightPathId->value())) {
                return core::Result<
                    std::optional<core::OperationMode>,
                    MicroscopeConfigurationStoreError>::failure(
                        makeError(StoreErrorCode::SqliteOperationFailed, database_));
            }

            for (;;) {
                const int componentStep = sqlite3_step(componentRows.get());
                if (componentStep == SQLITE_DONE) {
                    break;
                }
                if (componentStep != SQLITE_ROW) {
                    return core::Result<
                        std::optional<core::OperationMode>,
                        MicroscopeConfigurationStoreError>::failure(
                            makeError(StoreErrorCode::SqliteOperationFailed, database_));
                }
                auto componentId =
                    readStrongId<core::EquipmentComponentId>(componentRows.get(), 0);
                if (!componentId) {
                    return core::Result<
                        std::optional<core::OperationMode>,
                        MicroscopeConfigurationStoreError>::failure(
                            makeError(StoreErrorCode::InvalidPersistedRecord, database_));
                }
                opticalComponentIds.push_back(std::move(*componentId));
            }

            auto setting = core::LightPathModeSetting::create(
                microscopeId,
                static_cast<std::uint64_t>(equipmentRevision),
                operationModeId,
                *currentRevision,
                *lightPathId,
                *calibrationRevisionId,
                *illuminationComponentId,
                *illuminationChannelId,
                std::move(opticalComponentIds),
                targetIrradiance);
            if (!setting) {
                return core::Result<
                    std::optional<core::OperationMode>,
                    MicroscopeConfigurationStoreError>::failure(
                        makeError(StoreErrorCode::InvalidPersistedRecord, database_));
            }

            auto calibration = loadCalibrationUnlocked(
                microscopeId, *calibrationRevisionId);
            if (!calibration) {
                return core::Result<
                    std::optional<core::OperationMode>,
                    MicroscopeConfigurationStoreError>::failure(calibration.error());
            }
            if (!calibration.value()) {
                return core::Result<
                    std::optional<core::OperationMode>,
                    MicroscopeConfigurationStoreError>::failure(
                        makeError(StoreErrorCode::CalibrationNotFound, database_));
            }
            if (!isCalibrationBindingMatch(
                    setting.value(), *calibration.value())) {
                return core::Result<
                    std::optional<core::OperationMode>,
                    MicroscopeConfigurationStoreError>::failure(
                        makeError(StoreErrorCode::CalibrationBindingMismatch, database_));
            }

            settings.push_back(std::move(setting).value());
        }
    }

    auto operationMode = core::OperationMode::create(
        microscopeId,
        operationModeId,
        *currentRevision,
        std::move(name),
        contrastMethod,
        std::move(settings));
    if (!operationMode) {
        return core::Result<
            std::optional<core::OperationMode>,
            MicroscopeConfigurationStoreError>::failure(
                makeError(StoreErrorCode::InvalidPersistedRecord, database_));
    }

    return core::Result<
        std::optional<core::OperationMode>,
        MicroscopeConfigurationStoreError>::success(
            std::optional<core::OperationMode>{std::move(operationMode).value()});
}

} // namespace microscope::application::persistence
