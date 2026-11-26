#include "microscope/application/persistence/CameraComponentModelBindingSqliteStore.h"

#include <sqlite3.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace microscope::application::persistence {
namespace {

using StoreError = CameraComponentModelBindingStoreError;
using StoreErrorCode = CameraComponentModelBindingStoreErrorCode;

constexpr std::uint64_t kMaximumSqliteInteger =
    static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max());
constexpr int kSchemaVersion = 1;

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

[[nodiscard]] bool bindOptionalText(
    sqlite3_stmt* statement,
    int index,
    const std::optional<std::string>& value) noexcept {
    if (!value) {
        return sqlite3_bind_null(statement, index) == SQLITE_OK;
    }
    return bindText(statement, index, *value);
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

[[nodiscard]] const core::EquipmentComponent* findEquipmentComponent(
    const core::MicroscopeEquipmentDefinition& equipment,
    const core::EquipmentComponentId& componentId) noexcept {
    const auto& components = equipment.components();
    const auto found = std::find_if(
        components.begin(), components.end(),
        [&componentId](const core::EquipmentComponent& component) {
            return component.id() == componentId;
        });
    return found == components.end() ? nullptr : &*found;
}

[[nodiscard]] const core::CameraSpecification* findCameraSpecification(
    const core::MicroscopeEquipmentDefinition& equipment,
    const core::EquipmentComponentId& componentId) noexcept {
    const auto& specifications = equipment.cameraSpecifications();
    const auto found = std::find_if(
        specifications.begin(), specifications.end(),
        [&componentId](const core::CameraSpecification& specification) {
            return specification.componentId() == componentId;
        });
    return found == specifications.end() ? nullptr : &*found;
}

[[nodiscard]] const ::camera::CameraModelProfile* findCatalogProfile(
    const ::camera::CameraModelCatalog& catalog,
    const ::camera::CameraModelKey& key) {
    ::camera::CameraIdentity identity;
    identity.manufacturer = key.manufacturer;
    identity.model = key.model;
    identity.modelVariant = key.variant;
    return catalog.findExact(identity);
}

[[nodiscard]] core::Status<StoreError> validateBindings(
    const core::MicroscopeEquipmentDefinition& equipment,
    const ::camera::CameraModelCatalog& catalog,
    const std::vector<CameraComponentModelBinding>& bindings) {
    std::set<core::EquipmentComponentId> cameraComponentIds;
    for (const auto& binding : bindings) {
        if (binding.modelProfileRevision == 0 ||
            binding.modelProfileRevision > kMaximumSqliteInteger) {
            return failureStatus(makeError(
                StoreErrorCode::InvalidRevision,
                nullptr,
                "A camera model profile revision is outside the persistent SQLite range."));
        }

        if (!cameraComponentIds.insert(binding.cameraComponentId).second) {
            return failureStatus(makeError(
                StoreErrorCode::DuplicateCameraComponent,
                nullptr,
                "A camera component appears more than once in the binding set."));
        }

        const auto* component = findEquipmentComponent(
            equipment, binding.cameraComponentId);
        if (component == nullptr) {
            return failureStatus(makeError(
                StoreErrorCode::CameraComponentNotInstalled,
                nullptr,
                "A camera model binding references a component not installed in this equipment definition."));
        }
        if (component->kind() != core::EquipmentComponentKind::ImagingDevice) {
            return failureStatus(makeError(
                StoreErrorCode::CameraComponentWrongKind,
                nullptr,
                "A camera model binding must reference an ImagingDevice component."));
        }
        if (findCameraSpecification(
                equipment, binding.cameraComponentId) == nullptr) {
            return failureStatus(makeError(
                StoreErrorCode::CameraSpecificationMissing,
                nullptr,
                "The bound ImagingDevice has no installed CameraSpecification."));
        }

        const auto* profile = findCatalogProfile(catalog, binding.modelKey);
        if (profile == nullptr) {
            return failureStatus(makeError(
                StoreErrorCode::CatalogProfileNotFound,
                nullptr,
                "The camera model key does not resolve to one exact profile in the current shared catalog."));
        }
        if (profile->profileRevision() != binding.modelProfileRevision) {
            return failureStatus(makeError(
                StoreErrorCode::CatalogProfileRevisionMismatch,
                nullptr,
                "The camera component binding references a stale camera model profile revision."));
        }
    }

    return core::Status<StoreError>::success();
}

[[nodiscard]] bool readCurrentBindingRevision(
    sqlite3* database,
    const core::MicroscopeId& microscopeId,
    std::uint64_t equipmentRevision,
    std::optional<std::uint64_t>& revision) {
    Statement statement(
        database,
        "SELECT binding_revision FROM camera_component_model_binding_set "
        "WHERE microscope_id = ? AND equipment_revision = ?");
    if (!statement.prepared() ||
        !bindText(statement.get(), 1, microscopeId.value()) ||
        sqlite3_bind_int64(
            statement.get(), 2,
            static_cast<sqlite3_int64>(equipmentRevision)) != SQLITE_OK) {
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
    if (stored < 0) {
        return false;
    }
    revision = static_cast<std::uint64_t>(stored);
    return true;
}

} // namespace

CameraComponentModelBindingSqliteStore::CameraComponentModelBindingSqliteStore(
    sqlite3* database) noexcept
    : database_(database) {}

core::Result<
    std::unique_ptr<CameraComponentModelBindingSqliteStore>,
    CameraComponentModelBindingStoreError>
CameraComponentModelBindingSqliteStore::open(
    const std::string& databasePath) {
    if (databasePath.empty()) {
        return core::Result<
            std::unique_ptr<CameraComponentModelBindingSqliteStore>,
            CameraComponentModelBindingStoreError>::failure(
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
            std::unique_ptr<CameraComponentModelBindingSqliteStore>,
            CameraComponentModelBindingStoreError>::failure(std::move(error));
    }

    sqlite3_busy_timeout(database, 5000);
    if (sqlite3_exec(
            database, "PRAGMA foreign_keys = ON", nullptr, nullptr, nullptr)
        != SQLITE_OK) {
        auto error = makeError(
            StoreErrorCode::DatabaseOpenFailed, database);
        sqlite3_close_v2(database);
        return core::Result<
            std::unique_ptr<CameraComponentModelBindingSqliteStore>,
            CameraComponentModelBindingStoreError>::failure(std::move(error));
    }

    auto store = std::unique_ptr<CameraComponentModelBindingSqliteStore>(
        new CameraComponentModelBindingSqliteStore(database));
    const auto initialized = store->initializeSchema();
    if (!initialized) {
        return core::Result<
            std::unique_ptr<CameraComponentModelBindingSqliteStore>,
            CameraComponentModelBindingStoreError>::failure(initialized.error());
    }

    return core::Result<
        std::unique_ptr<CameraComponentModelBindingSqliteStore>,
        CameraComponentModelBindingStoreError>::success(std::move(store));
}

CameraComponentModelBindingSqliteStore::~CameraComponentModelBindingSqliteStore() {
    if (database_ != nullptr) {
        sqlite3_close_v2(database_);
        database_ = nullptr;
    }
}

core::Status<CameraComponentModelBindingStoreError>
CameraComponentModelBindingSqliteStore::initializeSchema() {
    std::lock_guard<std::mutex> lock(mutex_);
    Transaction transaction(database_);
    if (!transaction.active()) {
        return failureStatus(makeError(
            StoreErrorCode::SchemaMigrationFailed, database_));
    }

    constexpr const char* schemaSql = R"SQL(
        CREATE TABLE IF NOT EXISTS camera_component_model_binding_schema (
            id INTEGER PRIMARY KEY CHECK (id = 1),
            schema_version INTEGER NOT NULL
        );

        INSERT OR IGNORE INTO camera_component_model_binding_schema (id, schema_version)
        VALUES (1, 1);

        CREATE TABLE IF NOT EXISTS camera_component_model_binding_set (
            microscope_id TEXT NOT NULL,
            equipment_revision INTEGER NOT NULL CHECK (equipment_revision > 0),
            binding_revision INTEGER NOT NULL CHECK (binding_revision > 0),
            PRIMARY KEY (microscope_id, equipment_revision)
        );

        CREATE TABLE IF NOT EXISTS camera_component_model_binding (
            microscope_id TEXT NOT NULL,
            equipment_revision INTEGER NOT NULL,
            camera_component_id TEXT NOT NULL,
            manufacturer TEXT NOT NULL,
            model TEXT NOT NULL,
            model_variant TEXT NULL,
            profile_revision INTEGER NOT NULL CHECK (profile_revision > 0),
            PRIMARY KEY (microscope_id, equipment_revision, camera_component_id)
        );

        CREATE INDEX IF NOT EXISTS idx_camera_model_binding_profile
        ON camera_component_model_binding (manufacturer, model, model_variant);
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
        "SELECT schema_version FROM camera_component_model_binding_schema WHERE id = 1");
    if (!version.prepared() || sqlite3_step(version.get()) != SQLITE_ROW) {
        return failureStatus(makeError(
            StoreErrorCode::SchemaMigrationFailed, database_));
    }
    const int schemaVersion = sqlite3_column_int(version.get(), 0);
    if (schemaVersion != kSchemaVersion) {
        return failureStatus(makeError(
            StoreErrorCode::UnsupportedSchemaVersion,
            database_,
            "Unsupported camera component/model binding schema version."));
    }

    if (!transaction.commit()) {
        return failureStatus(makeError(
            StoreErrorCode::SchemaMigrationFailed, database_));
    }
    return core::Status<CameraComponentModelBindingStoreError>::success();
}

core::Status<CameraComponentModelBindingStoreError>
CameraComponentModelBindingSqliteStore::replaceBindings(
    const core::MicroscopeEquipmentDefinition& equipment,
    const ::camera::CameraModelCatalog& catalog,
    const std::vector<CameraComponentModelBinding>& bindings,
    std::uint64_t expectedCurrentRevision,
    std::uint64_t newBindingRevision) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (equipment.revision() == 0 ||
        equipment.revision() > kMaximumSqliteInteger ||
        expectedCurrentRevision >= kMaximumSqliteInteger ||
        newBindingRevision != expectedCurrentRevision + 1 ||
        newBindingRevision > kMaximumSqliteInteger) {
        return failureStatus(makeError(
            StoreErrorCode::InvalidRevision,
            database_,
            "Equipment or binding revision is outside the persistent SQLite sequence."));
    }

    const auto valid = validateBindings(equipment, catalog, bindings);
    if (!valid) {
        return valid;
    }

    Transaction transaction(database_);
    if (!transaction.active()) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }

    std::optional<std::uint64_t> currentRevision;
    if (!readCurrentBindingRevision(
            database_,
            equipment.microscopeId(),
            equipment.revision(),
            currentRevision)) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }
    if (currentRevision.value_or(0) != expectedCurrentRevision) {
        return failureStatus(makeError(
            StoreErrorCode::RevisionConflict,
            database_,
            "Camera component model bindings changed since they were loaded."));
    }

    Statement deleteBindings(
        database_,
        "DELETE FROM camera_component_model_binding "
        "WHERE microscope_id = ? AND equipment_revision = ?");
    if (!deleteBindings.prepared() ||
        !bindText(deleteBindings.get(), 1, equipment.microscopeId().value()) ||
        sqlite3_bind_int64(
            deleteBindings.get(), 2,
            static_cast<sqlite3_int64>(equipment.revision())) != SQLITE_OK ||
        sqlite3_step(deleteBindings.get()) != SQLITE_DONE) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }

    Statement upsertSet(
        database_,
        "INSERT OR REPLACE INTO camera_component_model_binding_set "
        "(microscope_id, equipment_revision, binding_revision) VALUES (?, ?, ?)");
    if (!upsertSet.prepared() ||
        !bindText(upsertSet.get(), 1, equipment.microscopeId().value()) ||
        sqlite3_bind_int64(
            upsertSet.get(), 2,
            static_cast<sqlite3_int64>(equipment.revision())) != SQLITE_OK ||
        sqlite3_bind_int64(
            upsertSet.get(), 3,
            static_cast<sqlite3_int64>(newBindingRevision)) != SQLITE_OK ||
        sqlite3_step(upsertSet.get()) != SQLITE_DONE) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }

    Statement insertBinding(
        database_,
        "INSERT INTO camera_component_model_binding ("
        "microscope_id, equipment_revision, camera_component_id, "
        "manufacturer, model, model_variant, profile_revision"
        ") VALUES (?, ?, ?, ?, ?, ?, ?)");
    if (!insertBinding.prepared()) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }

    for (const auto& binding : bindings) {
        if (!bindText(insertBinding.get(), 1, equipment.microscopeId().value()) ||
            sqlite3_bind_int64(
                insertBinding.get(), 2,
                static_cast<sqlite3_int64>(equipment.revision())) != SQLITE_OK ||
            !bindText(insertBinding.get(), 3, binding.cameraComponentId.value()) ||
            !bindText(insertBinding.get(), 4, binding.modelKey.manufacturer) ||
            !bindText(insertBinding.get(), 5, binding.modelKey.model) ||
            !bindOptionalText(insertBinding.get(), 6, binding.modelKey.variant) ||
            sqlite3_bind_int64(
                insertBinding.get(), 7,
                static_cast<sqlite3_int64>(binding.modelProfileRevision)) != SQLITE_OK ||
            sqlite3_step(insertBinding.get()) != SQLITE_DONE) {
            return failureStatus(makeError(
                StoreErrorCode::SqliteOperationFailed, database_));
        }
        sqlite3_reset(insertBinding.get());
        sqlite3_clear_bindings(insertBinding.get());
    }

    if (!transaction.commit()) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }
    return core::Status<CameraComponentModelBindingStoreError>::success();
}

core::Result<
    PersistedCameraComponentModelBindings,
    CameraComponentModelBindingStoreError>
CameraComponentModelBindingSqliteStore::loadBindings(
    const core::MicroscopeEquipmentDefinition& equipment,
    const ::camera::CameraModelCatalog& catalog) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (equipment.revision() == 0 ||
        equipment.revision() > kMaximumSqliteInteger) {
        return core::Result<
            PersistedCameraComponentModelBindings,
            CameraComponentModelBindingStoreError>::failure(
                makeError(StoreErrorCode::InvalidRevision, database_));
    }

    std::optional<std::uint64_t> currentRevision;
    if (!readCurrentBindingRevision(
            database_,
            equipment.microscopeId(),
            equipment.revision(),
            currentRevision)) {
        return core::Result<
            PersistedCameraComponentModelBindings,
            CameraComponentModelBindingStoreError>::failure(
                makeError(StoreErrorCode::SqliteOperationFailed, database_));
    }

    PersistedCameraComponentModelBindings result;
    result.bindingRevision = currentRevision.value_or(0);
    if (!currentRevision) {
        return core::Result<
            PersistedCameraComponentModelBindings,
            CameraComponentModelBindingStoreError>::success(std::move(result));
    }

    Statement rows(
        database_,
        "SELECT camera_component_id, manufacturer, model, model_variant, profile_revision "
        "FROM camera_component_model_binding "
        "WHERE microscope_id = ? AND equipment_revision = ? "
        "ORDER BY camera_component_id");
    if (!rows.prepared() ||
        !bindText(rows.get(), 1, equipment.microscopeId().value()) ||
        sqlite3_bind_int64(
            rows.get(), 2,
            static_cast<sqlite3_int64>(equipment.revision())) != SQLITE_OK) {
        return core::Result<
            PersistedCameraComponentModelBindings,
            CameraComponentModelBindingStoreError>::failure(
                makeError(StoreErrorCode::SqliteOperationFailed, database_));
    }

    for (;;) {
        const int step = sqlite3_step(rows.get());
        if (step == SQLITE_DONE) {
            break;
        }
        if (step != SQLITE_ROW) {
            return core::Result<
                PersistedCameraComponentModelBindings,
                CameraComponentModelBindingStoreError>::failure(
                    makeError(StoreErrorCode::SqliteOperationFailed, database_));
        }

        const auto componentText = readText(rows.get(), 0);
        const auto manufacturer = readText(rows.get(), 1);
        const auto model = readText(rows.get(), 2);
        const auto variant = readText(rows.get(), 3);
        const sqlite3_int64 profileRevision =
            sqlite3_column_int64(rows.get(), 4);

        if (!componentText || !manufacturer || !model ||
            profileRevision <= 0) {
            return core::Result<
                PersistedCameraComponentModelBindings,
                CameraComponentModelBindingStoreError>::failure(
                    makeError(StoreErrorCode::InvalidPersistedBinding, database_));
        }

        auto componentId =
            core::EquipmentComponentId::create(*componentText);
        if (!componentId) {
            return core::Result<
                PersistedCameraComponentModelBindings,
                CameraComponentModelBindingStoreError>::failure(
                    makeError(StoreErrorCode::InvalidPersistedBinding, database_));
        }

        CameraComponentModelBinding binding{
            std::move(componentId).value(),
            ::camera::CameraModelKey{
                *manufacturer,
                *model,
                variant},
            static_cast<std::uint64_t>(profileRevision)};

        const auto* component = findEquipmentComponent(
            equipment, binding.cameraComponentId);
        if (component == nullptr) {
            return core::Result<
                PersistedCameraComponentModelBindings,
                CameraComponentModelBindingStoreError>::failure(
                    makeError(
                        StoreErrorCode::CameraComponentNotInstalled,
                        database_,
                        "A persisted camera binding references a component absent from the loaded equipment revision."));
        }
        if (component->kind() != core::EquipmentComponentKind::ImagingDevice) {
            return core::Result<
                PersistedCameraComponentModelBindings,
                CameraComponentModelBindingStoreError>::failure(
                    makeError(StoreErrorCode::CameraComponentWrongKind, database_));
        }
        if (findCameraSpecification(
                equipment, binding.cameraComponentId) == nullptr) {
            return core::Result<
                PersistedCameraComponentModelBindings,
                CameraComponentModelBindingStoreError>::failure(
                    makeError(StoreErrorCode::CameraSpecificationMissing, database_));
        }

        const auto* profile = findCatalogProfile(catalog, binding.modelKey);
        if (profile == nullptr) {
            return core::Result<
                PersistedCameraComponentModelBindings,
                CameraComponentModelBindingStoreError>::failure(
                    makeError(
                        StoreErrorCode::CatalogProfileNotFound,
                        database_,
                        "A persisted camera binding's model profile is absent from the current catalog."));
        }
        if (profile->profileRevision() != binding.modelProfileRevision) {
            return core::Result<
                PersistedCameraComponentModelBindings,
                CameraComponentModelBindingStoreError>::failure(
                    makeError(
                        StoreErrorCode::CatalogProfileRevisionMismatch,
                        database_,
                        "A persisted camera binding references an older or newer model profile revision."));
        }

        result.bindings.push_back(std::move(binding));
    }

    return core::Result<
        PersistedCameraComponentModelBindings,
        CameraComponentModelBindingStoreError>::success(std::move(result));
}

} // namespace microscope::application::persistence
