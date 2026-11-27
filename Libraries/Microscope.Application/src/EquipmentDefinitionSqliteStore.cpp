#include "microscope/application/persistence/EquipmentDefinitionSqliteStore.h"

#include <sqlite3.h>

#include <limits>
#include <utility>

namespace microscope::application::persistence {
namespace {

using Error = EquipmentDefinitionStoreError;
using ErrorCode = EquipmentDefinitionStoreErrorCode;

constexpr sqlite3_int64 kSchemaVersion = 1;
constexpr std::uint64_t kMaximumSqliteInteger =
    static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max());

Error makeError(
    ErrorCode code,
    sqlite3* database,
    std::string detail = {}) {
    if (detail.empty() && database != nullptr) detail = sqlite3_errmsg(database);
    return Error{
        code,
        database == nullptr ? 0 : sqlite3_errcode(database),
        std::move(detail),
        std::nullopt};
}

Error makeJsonError(configuration::EquipmentDefinitionJsonError error) {
    return Error{
        ErrorCode::InvalidDefinitionDocument,
        0,
        error.detail,
        std::move(error)};
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

    bool bindText(int index, std::string_view value) {
        if (value.empty() ||
            value.size() > configuration::EquipmentDefinitionJsonLoader::kMaximumDocumentBytes ||
            value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            return false;
        }
        return sqlite3_bind_text(
            statement_, index, value.data(),
            static_cast<int>(value.size()), SQLITE_TRANSIENT) == SQLITE_OK;
    }

    bool bindText(int index, const std::string& value) {
        return bindText(index, std::string_view(value));
    }

    bool bindInt64(int index, std::uint64_t value) {
        if (value > kMaximumSqliteInteger) return false;
        return sqlite3_bind_int64(
            statement_, index, static_cast<sqlite3_int64>(value)) == SQLITE_OK;
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

bool validRevision(std::uint64_t revision) noexcept {
    return revision > 0 && revision <= kMaximumSqliteInteger;
}

bool readCurrentRevision(
    sqlite3* database,
    const core::MicroscopeId& microscopeId,
    std::optional<std::uint64_t>& revision) {
    Statement statement(database,
        "SELECT revision FROM microscope_equipment_definition_current "
        "WHERE microscope_id = ?1");
    if (!statement.prepared() ||
        !statement.bindText(1, microscopeId.value())) {
        return false;
    }
    const int step = statement.step();
    if (step == SQLITE_DONE) {
        revision.reset();
        return true;
    }
    if (step != SQLITE_ROW ||
        sqlite3_column_type(statement.get(), 0) != SQLITE_INTEGER) return false;
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
                "The equipment-definition database path is empty.", std::nullopt});
    }

    sqlite3* database = nullptr;
    const int opened = sqlite3_open_v2(
        databasePath.c_str(), &database,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr);
    if (opened != SQLITE_OK) {
        auto error = makeError(ErrorCode::DatabaseOpenFailed, database,
            "Could not open the equipment-definition SQLite database.");
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
        CREATE TABLE IF NOT EXISTS microscope_equipment_definition_metadata (
            singleton INTEGER PRIMARY KEY CHECK (singleton = 1),
            schema_version INTEGER NOT NULL
        );

        CREATE TABLE IF NOT EXISTS microscope_equipment_definition_revision (
            microscope_id TEXT NOT NULL,
            revision INTEGER NOT NULL CHECK (revision > 0),
            document_json TEXT NOT NULL,
            PRIMARY KEY (microscope_id, revision)
        );

        CREATE TABLE IF NOT EXISTS microscope_equipment_definition_current (
            microscope_id TEXT PRIMARY KEY,
            revision INTEGER NOT NULL CHECK (revision > 0),
            FOREIGN KEY (microscope_id, revision)
                REFERENCES microscope_equipment_definition_revision (
                    microscope_id, revision
                )
        );

        CREATE TRIGGER IF NOT EXISTS microscope_equipment_definition_revision_sequential_insert
        BEFORE INSERT ON microscope_equipment_definition_revision
        WHEN NEW.revision != COALESCE((
            SELECT revision FROM microscope_equipment_definition_current
            WHERE microscope_id = NEW.microscope_id
        ), 0) + 1
        BEGIN SELECT RAISE(ABORT, 'Equipment-definition revisions must be appended sequentially'); END;

        CREATE TRIGGER IF NOT EXISTS microscope_equipment_definition_revision_no_update
        BEFORE UPDATE ON microscope_equipment_definition_revision
        BEGIN SELECT RAISE(ABORT, 'Equipment-definition revisions are immutable'); END;

        CREATE TRIGGER IF NOT EXISTS microscope_equipment_definition_revision_no_delete
        BEFORE DELETE ON microscope_equipment_definition_revision
        BEGIN SELECT RAISE(ABORT, 'Equipment-definition revisions are immutable'); END;

        CREATE TRIGGER IF NOT EXISTS microscope_equipment_definition_current_initial
        BEFORE INSERT ON microscope_equipment_definition_current
        WHEN NEW.revision != 1
        BEGIN SELECT RAISE(ABORT, 'Initial equipment-definition revision must be one'); END;

        CREATE TRIGGER IF NOT EXISTS microscope_equipment_definition_current_monotonic
        BEFORE UPDATE ON microscope_equipment_definition_current
        WHEN NEW.revision != OLD.revision + 1
        BEGIN SELECT RAISE(ABORT, 'Equipment-definition current revision must advance by one'); END;

        CREATE TRIGGER IF NOT EXISTS microscope_equipment_definition_current_no_delete
        BEFORE DELETE ON microscope_equipment_definition_current
        BEGIN SELECT RAISE(ABORT, 'Equipment-definition current pointer cannot be deleted'); END;
    )SQL";

    if (!execute(database_, schema)) {
        return core::Status<Error>::failure(
            makeError(ErrorCode::SchemaMigrationFailed, database_));
    }

    Statement insertMetadata(database_,
        "INSERT OR IGNORE INTO microscope_equipment_definition_metadata "
        "(singleton, schema_version) VALUES (1, 1)");
    if (!insertMetadata.prepared() || insertMetadata.step() != SQLITE_DONE) {
        return core::Status<Error>::failure(
            makeError(ErrorCode::SchemaMigrationFailed, database_));
    }

    Statement readMetadata(database_,
        "SELECT schema_version FROM microscope_equipment_definition_metadata "
        "WHERE singleton = 1");
    if (!readMetadata.prepared() || readMetadata.step() != SQLITE_ROW ||
        sqlite3_column_type(readMetadata.get(), 0) != SQLITE_INTEGER) {
        return core::Status<Error>::failure(
            makeError(ErrorCode::SchemaMigrationFailed, database_));
    }
    const auto version = sqlite3_column_int64(readMetadata.get(), 0);
    if (readMetadata.step() != SQLITE_DONE) {
        return core::Status<Error>::failure(
            makeError(ErrorCode::SchemaMigrationFailed, database_));
    }
    if (version != kSchemaVersion) {
        return core::Status<Error>::failure(
            makeError(ErrorCode::UnsupportedSchemaVersion, database_,
                "The equipment-definition database schema version is unsupported."));
    }

    return core::Status<Error>::success();
}

core::Result<std::uint64_t, Error>
EquipmentDefinitionSqliteStore::saveCurrentDefinition(
    const core::MicroscopeId& microscopeId,
    std::string_view sourceDocument,
    std::uint64_t expectedCurrentRevision) {
    if (sourceDocument.empty()) {
        return core::Result<std::uint64_t, Error>::failure(
            makeError(ErrorCode::InvalidDefinitionDocument, database_,
                "Equipment-definition JSON document is empty."));
    }
    if (sourceDocument.size() >
        configuration::EquipmentDefinitionJsonLoader::kMaximumDocumentBytes) {
        return core::Result<std::uint64_t, Error>::failure(
            makeError(ErrorCode::DocumentTooLarge, database_,
                "Equipment-definition JSON document exceeds the configured size limit."));
    }
    if (expectedCurrentRevision >= kMaximumSqliteInteger) {
        return core::Result<std::uint64_t, Error>::failure(
            makeError(ErrorCode::InvalidRevision, database_));
    }

    auto parsed = configuration::EquipmentDefinitionJsonLoader::parse(sourceDocument);
    if (!parsed) {
        auto error = makeJsonError(parsed.error());
        return core::Result<std::uint64_t, Error>::failure(std::move(error));
    }

    if (parsed.value().microscopeId() != microscopeId) {
        return core::Result<std::uint64_t, Error>::failure(
            makeError(ErrorCode::InvalidDefinitionDocument, database_,
                "The JSON microscopeId does not match the requested persistence identity."));
    }

    const std::uint64_t nextRevision = expectedCurrentRevision + 1U;
    if (parsed.value().revision() != nextRevision) {
        return core::Result<std::uint64_t, Error>::failure(
            makeError(ErrorCode::RevisionMismatch, database_,
                "The JSON equipment revision must equal expectedCurrentRevision + 1."));
    }

    std::lock_guard<std::mutex> lock(mutex_);
    Transaction transaction(database_);
    if (!transaction.active()) {
        return core::Result<std::uint64_t, Error>::failure(
            makeError(ErrorCode::SqliteOperationFailed, database_));
    }

    std::optional<std::uint64_t> actualCurrent;
    if (!readCurrentRevision(database_, microscopeId, actualCurrent)) {
        return core::Result<std::uint64_t, Error>::failure(
            makeError(ErrorCode::SqliteOperationFailed, database_));
    }
    if (actualCurrent.value_or(0) != expectedCurrentRevision) {
        return core::Result<std::uint64_t, Error>::failure(
            makeError(ErrorCode::RevisionConflict, database_,
                "The microscope equipment definition changed since it was loaded."));
    }

    Statement insertRevision(database_,
        "INSERT INTO microscope_equipment_definition_revision "
        "(microscope_id, revision, document_json) VALUES (?1, ?2, ?3)");
    if (!insertRevision.prepared() ||
        !insertRevision.bindText(1, microscopeId.value()) ||
        !insertRevision.bindInt64(2, nextRevision) ||
        !insertRevision.bindText(3, sourceDocument) ||
        insertRevision.step() != SQLITE_DONE) {
        return core::Result<std::uint64_t, Error>::failure(
            makeError(ErrorCode::SqliteOperationFailed, database_));
    }

    if (actualCurrent) {
        Statement updateCurrent(database_,
            "UPDATE microscope_equipment_definition_current SET revision = ?1 "
            "WHERE microscope_id = ?2 AND revision = ?3");
        if (!updateCurrent.prepared() ||
            !updateCurrent.bindInt64(1, nextRevision) ||
            !updateCurrent.bindText(2, microscopeId.value()) ||
            !updateCurrent.bindInt64(3, expectedCurrentRevision) ||
            updateCurrent.step() != SQLITE_DONE ||
            sqlite3_changes(database_) != 1) {
            return core::Result<std::uint64_t, Error>::failure(
                makeError(ErrorCode::SqliteOperationFailed, database_));
        }
    } else {
        Statement insertCurrent(database_,
            "INSERT INTO microscope_equipment_definition_current "
            "(microscope_id, revision) VALUES (?1, ?2)");
        if (!insertCurrent.prepared() ||
            !insertCurrent.bindText(1, microscopeId.value()) ||
            !insertCurrent.bindInt64(2, nextRevision) ||
            insertCurrent.step() != SQLITE_DONE) {
            return core::Result<std::uint64_t, Error>::failure(
                makeError(ErrorCode::SqliteOperationFailed, database_));
        }
    }

    if (!transaction.commit()) {
        return core::Result<std::uint64_t, Error>::failure(
            makeError(ErrorCode::SqliteOperationFailed, database_));
    }
    return core::Result<std::uint64_t, Error>::success(nextRevision);
}

core::Result<std::optional<PersistedEquipmentDefinition>, Error>
EquipmentDefinitionSqliteStore::loadCurrentDefinition(
    const core::MicroscopeId& microscopeId) {
    std::lock_guard<std::mutex> lock(mutex_);
    Statement statement(database_,
        "SELECT revision FROM microscope_equipment_definition_current "
        "WHERE microscope_id = ?1");
    if (!statement.prepared() ||
        !statement.bindText(1, microscopeId.value())) {
        return core::Result<std::optional<PersistedEquipmentDefinition>, Error>::failure(
            makeError(ErrorCode::SqliteOperationFailed, database_));
    }
    const int step = statement.step();
    if (step == SQLITE_DONE) {
        return core::Result<std::optional<PersistedEquipmentDefinition>, Error>::success(
            std::nullopt);
    }
    if (step != SQLITE_ROW ||
        sqlite3_column_type(statement.get(), 0) != SQLITE_INTEGER) {
        return core::Result<std::optional<PersistedEquipmentDefinition>, Error>::failure(
            makeError(ErrorCode::InvalidPersistedRecord, database_));
    }
    const sqlite3_int64 revision = sqlite3_column_int64(statement.get(), 0);
    if (revision <= 0 || statement.step() != SQLITE_DONE) {
        return core::Result<std::optional<PersistedEquipmentDefinition>, Error>::failure(
            makeError(ErrorCode::InvalidPersistedRecord, database_));
    }
    return loadRevisionUnlocked(microscopeId, static_cast<std::uint64_t>(revision));
}

core::Result<std::optional<PersistedEquipmentDefinition>, Error>
EquipmentDefinitionSqliteStore::loadRevision(
    const core::MicroscopeId& microscopeId,
    std::uint64_t revision) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!validRevision(revision)) {
        return core::Result<std::optional<PersistedEquipmentDefinition>, Error>::failure(
            makeError(ErrorCode::InvalidRevision, database_));
    }
    return loadRevisionUnlocked(microscopeId, revision);
}

core::Result<std::optional<PersistedEquipmentDefinition>, Error>
EquipmentDefinitionSqliteStore::loadRevisionUnlocked(
    const core::MicroscopeId& microscopeId,
    std::uint64_t revision) {
    Statement statement(database_,
        "SELECT document_json FROM microscope_equipment_definition_revision "
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
        sqlite3_column_type(statement.get(), 0) != SQLITE_TEXT) {
        return core::Result<std::optional<PersistedEquipmentDefinition>, Error>::failure(
            makeError(ErrorCode::InvalidPersistedRecord, database_));
    }

    const auto* data = sqlite3_column_text(statement.get(), 0);
    const int byteCount = sqlite3_column_bytes(statement.get(), 0);
    if (data == nullptr || byteCount <= 0 ||
        static_cast<std::size_t>(byteCount) >
            configuration::EquipmentDefinitionJsonLoader::kMaximumDocumentBytes) {
        return core::Result<std::optional<PersistedEquipmentDefinition>, Error>::failure(
            makeError(ErrorCode::InvalidPersistedRecord, database_,
                "Stored equipment-definition document is empty or exceeds the size limit."));
    }

    // Copy SQLite's column data before stepping/finalizing the statement.
    std::string sourceDocument(
        reinterpret_cast<const char*>(data), static_cast<std::size_t>(byteCount));
    if (statement.step() != SQLITE_DONE) {
        return core::Result<std::optional<PersistedEquipmentDefinition>, Error>::failure(
            makeError(ErrorCode::InvalidPersistedRecord, database_,
                "An equipment-definition revision key returned multiple rows."));
    }

    auto parsed = configuration::EquipmentDefinitionJsonLoader::parse(sourceDocument);
    if (!parsed) {
        auto error = makeError(ErrorCode::InvalidPersistedRecord, database_,
            "Persisted equipment-definition JSON failed schema or Core validation.");
        error.jsonError = parsed.error();
        return core::Result<std::optional<PersistedEquipmentDefinition>, Error>::failure(
            std::move(error));
    }
    if (parsed.value().microscopeId() != microscopeId ||
        parsed.value().revision() != revision) {
        return core::Result<std::optional<PersistedEquipmentDefinition>, Error>::failure(
            makeError(ErrorCode::InvalidPersistedRecord, database_,
                "Persisted equipment document identity/revision does not match its database key."));
    }

    return core::Result<std::optional<PersistedEquipmentDefinition>, Error>::success(
        PersistedEquipmentDefinition{
            revision, std::move(sourceDocument), std::move(parsed).value()});
}

} // namespace microscope::application::persistence
