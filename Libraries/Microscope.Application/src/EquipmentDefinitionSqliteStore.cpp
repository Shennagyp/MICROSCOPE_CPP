#include "microscope/application/persistence/EquipmentDefinitionSqliteStore.h"
#include "microscope/application/configuration/EquipmentDefinitionJsonLoader.h"

#include <sqlite3.h>

#include <limits>
#include <string>
#include <utility>

namespace microscope::application::persistence {
namespace {

using Error = EquipmentDefinitionStoreError;
using ErrorCode = EquipmentDefinitionStoreErrorCode;

constexpr sqlite3_int64 kStoreSchemaVersion = 1;
constexpr sqlite3_int64 kPayloadFormatVersion = 1;
constexpr std::uint64_t kMaximumSqliteInteger =
    static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max());
constexpr std::size_t kMaximumPayloadBytes =
    configuration::EquipmentDefinitionJsonLoader::kMaximumDocumentBytes;

Error makeError(ErrorCode code, sqlite3* database, std::string detail = {}) {
    if (detail.empty() && database != nullptr) detail = sqlite3_errmsg(database);
    return Error{
        code,
        database == nullptr ? SQLITE_MISUSE : sqlite3_errcode(database),
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
        prepared_ =
            sqlite3_prepare_v2(database_, sql, -1, &statement_, nullptr) == SQLITE_OK;
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
        if (value.empty() || value.find('\\0') != std::string::npos ||
            value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            return false;
        }
        return sqlite3_bind_text(
            statement_, index, value.data(), static_cast<int>(value.size()),
            SQLITE_TRANSIENT) == SQLITE_OK;
    }

    bool bindInt64(int index, std::uint64_t value) {
        if (value > kMaximumSqliteInteger) return false;
        return sqlite3_bind_int64(
            statement_, index, static_cast<sqlite3_int64>(value)) == SQLITE_OK;
    }

    bool bindBlob(int index, std::string_view value) {
        if (value.empty() || value.size() > kMaximumPayloadBytes ||
            value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            return false;
        }
        return sqlite3_bind_blob(
            statement_, index, value.data(), static_cast<int>(value.size()),
            SQLITE_TRANSIENT) == SQLITE_OK;
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

bool readCurrentRevision(
    sqlite3* database,
    const std::string& microscopeId,
    std::optional<std::uint64_t>& revision) {
    Statement statement(database,
        "SELECT revision FROM equipment_definition_current "
        "WHERE microscope_id = ?1");
    if (!statement.prepared() || !statement.bindText(1, microscopeId)) return false;

    const int step = statement.step();
    if (step == SQLITE_DONE) {
        revision.reset();
        return true;
    }
    if (step != SQLITE_ROW ||
        sqlite3_column_type(statement.get(), 0) != SQLITE_INTEGER) {
        return false;
    }

    const sqlite3_int64 value = sqlite3_column_int64(statement.get(), 0);
    if (value <= 0 || statement.step() != SQLITE_DONE) return false;
    revision = static_cast<std::uint64_t>(value);
    return true;
}

} // namespace

EquipmentDefinitionSqliteStore::EquipmentDefinitionSqliteStore(
    sqlite3* database) noexcept : database_(database) {}

EquipmentDefinitionSqliteStore::~EquipmentDefinitionSqliteStore() {
    if (database_ != nullptr) sqlite3_close_v2(database_);
}

core::Result<std::unique_ptr<EquipmentDefinitionSqliteStore>, Error>
EquipmentDefinitionSqliteStore::open(const std::string& databasePath) {
    if (databasePath.empty()) {
        return core::Result<std::unique_ptr<EquipmentDefinitionSqliteStore>, Error>::failure(
            Error{ErrorCode::EmptyDatabasePath, SQLITE_MISUSE,
                "The equipment-definition database path is empty."});
    }

    sqlite3* database = nullptr;
    const int opened = sqlite3_open_v2(
        databasePath.c_str(), &database,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (opened != SQLITE_OK) {
        auto error = makeError(ErrorCode::DatabaseOpenFailed, database,
            "Could not open the equipment-definition database.");
        if (database != nullptr) sqlite3_close_v2(database);
        return core::Result<std::unique_ptr<EquipmentDefinitionSqliteStore>, Error>::failure(
            std::move(error));
    }

    sqlite3_busy_timeout(database, 5000);
    if (!execute(database, "PRAGMA foreign_keys = ON")) {
        auto error = makeError(ErrorCode::DatabaseOpenFailed, database,
            "Could not enable SQLite foreign-key enforcement.");
        sqlite3_close_v2(database);
        return core::Result<std::unique_ptr<EquipmentDefinitionSqliteStore>, Error>::failure(
            std::move(error));
    }

    auto store = std::unique_ptr<EquipmentDefinitionSqliteStore>(
        new EquipmentDefinitionSqliteStore(database));
    const auto initialized = store->initializeSchema();
    if (!initialized) {
        return core::Result<std::unique_ptr<EquipmentDefinitionSqliteStore>, Error>::failure(
            initialized.error());
    }

    return core::Result<std::unique_ptr<EquipmentDefinitionSqliteStore>, Error>::success(
        std::move(store));
}

core::Status<Error> EquipmentDefinitionSqliteStore::initializeSchema() {
    const char* schema = R"SQL(
        CREATE TABLE IF NOT EXISTS equipment_definition_store_metadata (
            singleton INTEGER PRIMARY KEY CHECK (singleton = 1),
            schema_version INTEGER NOT NULL
        );

        INSERT OR IGNORE INTO equipment_definition_store_metadata
            (singleton, schema_version) VALUES (1, 1);

        CREATE TABLE IF NOT EXISTS equipment_definition_revision (
            microscope_id TEXT NOT NULL,
            revision INTEGER NOT NULL CHECK (revision > 0),
            payload_format_version INTEGER NOT NULL,
            payload BLOB NOT NULL,
            PRIMARY KEY (microscope_id, revision)
        );

        CREATE TABLE IF NOT EXISTS equipment_definition_current (
            microscope_id TEXT PRIMARY KEY,
            revision INTEGER NOT NULL CHECK (revision > 0),
            FOREIGN KEY (microscope_id, revision)
                REFERENCES equipment_definition_revision (microscope_id, revision)
                ON UPDATE RESTRICT ON DELETE RESTRICT
        );

        CREATE TRIGGER IF NOT EXISTS equipment_definition_revision_no_update
        BEFORE UPDATE ON equipment_definition_revision
        BEGIN
            SELECT RAISE(ABORT, 'equipment definition revisions are immutable');
        END;

        CREATE TRIGGER IF NOT EXISTS equipment_definition_revision_no_delete
        BEFORE DELETE ON equipment_definition_revision
        BEGIN
            SELECT RAISE(ABORT, 'equipment definition revisions are immutable');
        END;

        CREATE TRIGGER IF NOT EXISTS equipment_definition_current_no_rewind
        BEFORE UPDATE ON equipment_definition_current
        WHEN NEW.revision <= OLD.revision
        BEGIN
            SELECT RAISE(ABORT, 'current equipment definition revision must increase');
        END;

        CREATE TRIGGER IF NOT EXISTS equipment_definition_current_no_delete
        BEFORE DELETE ON equipment_definition_current
        BEGIN
            SELECT RAISE(ABORT, 'current equipment definition pointer cannot be deleted');
        END;

    )SQL";

    if (!execute(database_, schema)) {
        return core::Status<Error>::failure(makeError(
            ErrorCode::SchemaMigrationFailed, database_,
            "Could not create the equipment-definition revision schema."));
    }

    Statement metadata(database_,
        "SELECT schema_version FROM equipment_definition_store_metadata "
        "WHERE singleton = 1");
    if (!metadata.prepared() || metadata.step() != SQLITE_ROW ||
        sqlite3_column_type(metadata.get(), 0) != SQLITE_INTEGER) {
        return core::Status<Error>::failure(makeError(
            ErrorCode::SchemaMigrationFailed, database_,
            "Could not read equipment-definition schema metadata."));
    }

    const sqlite3_int64 version = sqlite3_column_int64(metadata.get(), 0);
    if (metadata.step() != SQLITE_DONE) {
        return core::Status<Error>::failure(makeError(
            ErrorCode::SchemaMigrationFailed, database_));
    }
    if (version != kStoreSchemaVersion) {
        return core::Status<Error>::failure(makeError(
            ErrorCode::UnsupportedSchemaVersion, database_,
            "The equipment-definition store schema version is unsupported."));
    }

    return core::Status<Error>::success();
}

core::Result<std::uint64_t, Error>
EquipmentDefinitionSqliteStore::saveCurrentDefinition(
    std::string_view sourceDocument,
    std::uint64_t expectedCurrentRevision) {
    if (sourceDocument.size() > kMaximumPayloadBytes) {
        return core::Result<std::uint64_t, Error>::failure(makeError(
            ErrorCode::PayloadTooLarge, database_,
            "The equipment-definition JSON payload exceeds the configured limit."));
    }
    if (expectedCurrentRevision >= kMaximumSqliteInteger) {
        return core::Result<std::uint64_t, Error>::failure(makeError(
            ErrorCode::InvalidRevision, database_,
            "The expected equipment-definition revision is outside SQLite's integer range."));
    }

    auto decoded = configuration::EquipmentDefinitionJsonLoader::parse(sourceDocument);
    if (!decoded) {
        const auto& parseError = decoded.error();
        return core::Result<std::uint64_t, Error>::failure(makeError(
            ErrorCode::InvalidEquipmentDefinition, database_,
            "Equipment definition rejected at " + parseError.path + ": " +
                parseError.detail));
    }

    auto equipment = std::move(decoded).value();
    const std::string microscopeId = equipment.microscopeId().value();
    const std::uint64_t revision = equipment.revision();
    if (revision == 0 || revision > kMaximumSqliteInteger ||
        revision <= expectedCurrentRevision) {
        return core::Result<std::uint64_t, Error>::failure(makeError(
            ErrorCode::InvalidRevision, database_,
            "The new equipment-definition revision must be positive, representable and greater than the expected current revision."));
    }

    std::lock_guard<std::mutex> lock(mutex_);
    Transaction transaction(database_);
    if (!transaction.active()) {
        return core::Result<std::uint64_t, Error>::failure(makeError(
            ErrorCode::SqliteOperationFailed, database_,
            "Could not begin the equipment-definition write transaction."));
    }

    std::optional<std::uint64_t> currentRevision;
    if (!readCurrentRevision(database_, microscopeId, currentRevision)) {
        return core::Result<std::uint64_t, Error>::failure(makeError(
            ErrorCode::SqliteOperationFailed, database_,
            "Could not read the current equipment-definition revision."));
    }
    const std::uint64_t actualCurrent = currentRevision.value_or(0);
    if (actualCurrent != expectedCurrentRevision) {
        return core::Result<std::uint64_t, Error>::failure(makeError(
            ErrorCode::RevisionConflict, database_,
            "The equipment definition changed since the supplied expected revision."));
    }

    Statement insert(database_,
        "INSERT INTO equipment_definition_revision "
        "(microscope_id, revision, payload_format_version, payload) "
        "VALUES (?1, ?2, ?3, ?4)");
    if (!insert.prepared() || !insert.bindText(1, microscopeId) ||
        !insert.bindInt64(2, revision) ||
        !insert.bindInt64(3, static_cast<std::uint64_t>(kPayloadFormatVersion)) ||
        !insert.bindBlob(4, sourceDocument)) {
        return core::Result<std::uint64_t, Error>::failure(makeError(
            ErrorCode::SqliteOperationFailed, database_,
            "Could not prepare the equipment-definition payload insert."));
    }
    if (insert.step() != SQLITE_DONE) {
        const int code = sqlite3_errcode(database_);
        const auto errorCode = code == SQLITE_CONSTRAINT
            ? ErrorCode::RevisionAlreadyExists : ErrorCode::SqliteOperationFailed;
        return core::Result<std::uint64_t, Error>::failure(makeError(
            errorCode, database_,
            "Could not append the immutable equipment-definition revision."));
    }

    Statement updateCurrent(database_,
        "INSERT INTO equipment_definition_current (microscope_id, revision) "
        "VALUES (?1, ?2) "
        "ON CONFLICT(microscope_id) DO UPDATE SET revision = excluded.revision");
    if (!updateCurrent.prepared() ||
        !updateCurrent.bindText(1, microscopeId) ||
        !updateCurrent.bindInt64(2, revision) ||
        updateCurrent.step() != SQLITE_DONE) {
        return core::Result<std::uint64_t, Error>::failure(makeError(
            ErrorCode::SqliteOperationFailed, database_,
            "Could not publish the new current equipment-definition revision."));
    }

    if (!transaction.commit()) {
        return core::Result<std::uint64_t, Error>::failure(makeError(
            ErrorCode::SqliteOperationFailed, database_,
            "Could not commit the equipment-definition revision."));
    }

    return core::Result<std::uint64_t, Error>::success(revision);
}

core::Result<std::optional<PersistedEquipmentDefinition>, Error>
EquipmentDefinitionSqliteStore::loadCurrentDefinition(
    const core::MicroscopeId& microscopeId) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::optional<std::uint64_t> revision;
    if (!readCurrentRevision(database_, microscopeId.value(), revision)) {
        return core::Result<std::optional<PersistedEquipmentDefinition>, Error>::failure(
            makeError(ErrorCode::InvalidPersistedRecord, database_,
                "The current equipment-definition pointer is malformed."));
    }
    if (!revision) {
        return core::Result<std::optional<PersistedEquipmentDefinition>, Error>::success(
            std::nullopt);
    }
    return loadRevisionUnlocked(microscopeId, *revision);
}

core::Result<std::optional<PersistedEquipmentDefinition>, Error>
EquipmentDefinitionSqliteStore::loadRevision(
    const core::MicroscopeId& microscopeId,
    std::uint64_t revision) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (revision == 0 || revision > kMaximumSqliteInteger) {
        return core::Result<std::optional<PersistedEquipmentDefinition>, Error>::failure(
            makeError(ErrorCode::InvalidRevision, database_,
                "The requested equipment-definition revision is invalid."));
    }
    return loadRevisionUnlocked(microscopeId, revision);
}

core::Result<std::optional<PersistedEquipmentDefinition>, Error>
EquipmentDefinitionSqliteStore::loadRevisionUnlocked(
    const core::MicroscopeId& microscopeId,
    std::uint64_t revision) {
    Statement statement(database_,
        "SELECT payload_format_version, payload "
        "FROM equipment_definition_revision "
        "WHERE microscope_id = ?1 AND revision = ?2");
    if (!statement.prepared() ||
        !statement.bindText(1, microscopeId.value()) ||
        !statement.bindInt64(2, revision)) {
        return core::Result<std::optional<PersistedEquipmentDefinition>, Error>::failure(
            makeError(ErrorCode::SqliteOperationFailed, database_));
    }

    const int step = statement.step();
    if (step == SQLITE_DONE) {
        return core::Result<std::optional<PersistedEquipmentDefinition>, Error>::success(
            std::nullopt);
    }
    if (step != SQLITE_ROW ||
        sqlite3_column_type(statement.get(), 0) != SQLITE_INTEGER ||
        sqlite3_column_type(statement.get(), 1) != SQLITE_BLOB) {
        return core::Result<std::optional<PersistedEquipmentDefinition>, Error>::failure(
            makeError(ErrorCode::InvalidPersistedRecord, database_,
                "The equipment-definition payload columns have invalid SQLite types."));
    }

    const sqlite3_int64 formatVersion = sqlite3_column_int64(statement.get(), 0);
    const int payloadSize = sqlite3_column_bytes(statement.get(), 1);
    const auto* payload = static_cast<const char*>(
        sqlite3_column_blob(statement.get(), 1));
    if (formatVersion != kPayloadFormatVersion) {
        return core::Result<std::optional<PersistedEquipmentDefinition>, Error>::failure(
            makeError(ErrorCode::UnsupportedSchemaVersion, database_,
                "The equipment-definition payload format version is unsupported."));
    }
    if (payload == nullptr || payloadSize <= 0 ||
        static_cast<std::size_t>(payloadSize) > kMaximumPayloadBytes) {
        return core::Result<std::optional<PersistedEquipmentDefinition>, Error>::failure(
            makeError(ErrorCode::InvalidPersistedRecord, database_,
                "The equipment-definition BLOB is empty or exceeds its size limit."));
    }

    // sqlite3_column_blob() is valid only while this statement remains on the
    // current row. Copy the bytes before stepping or finalizing the statement.
    std::string sourceDocument(payload, static_cast<std::size_t>(payloadSize));
    if (statement.step() != SQLITE_DONE) {
        return core::Result<std::optional<PersistedEquipmentDefinition>, Error>::failure(
            makeError(ErrorCode::InvalidPersistedRecord, database_,
                "The equipment-definition revision query returned more than one row."));
    }

    auto decoded = configuration::EquipmentDefinitionJsonLoader::parse(sourceDocument);
    if (!decoded) {
        const auto& parseError = decoded.error();
        return core::Result<std::optional<PersistedEquipmentDefinition>, Error>::failure(
            makeError(ErrorCode::InvalidPersistedRecord, database_,
                "Stored equipment definition is invalid at " + parseError.path + ": " +
                    parseError.detail));
    }

    auto equipment = std::move(decoded).value();
    if (equipment.microscopeId() != microscopeId || equipment.revision() != revision) {
        return core::Result<std::optional<PersistedEquipmentDefinition>, Error>::failure(
            makeError(ErrorCode::InvalidPersistedRecord, database_,
                "Stored equipment identity or revision does not match its SQLite key."));
    }

    std::optional<PersistedEquipmentDefinition> result;
    result.emplace(PersistedEquipmentDefinition{
        revision, std::move(sourceDocument), std::move(equipment)});
    return core::Result<std::optional<PersistedEquipmentDefinition>, Error>::success(
        std::move(result));
}

} // namespace microscope::application::persistence
