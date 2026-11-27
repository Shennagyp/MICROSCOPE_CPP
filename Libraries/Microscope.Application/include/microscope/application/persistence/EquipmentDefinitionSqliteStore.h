#pragma once

#include "microscope/core/equipment/OpticalTopology.h"
#include "microscope/core/result/Result.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

struct sqlite3;

namespace microscope::application::persistence {

enum class EquipmentDefinitionStoreErrorCode {
    EmptyDatabasePath,
    DatabaseOpenFailed,
    SchemaMigrationFailed,
    UnsupportedSchemaVersion,
    InvalidRevision,
    RevisionConflict,
    RevisionAlreadyExists,
    PayloadTooLarge,
    InvalidEquipmentDefinition,
    InvalidPersistedRecord,
    SqliteOperationFailed
};

struct EquipmentDefinitionStoreError final {
    EquipmentDefinitionStoreErrorCode code;
    int nativeCode = 0;
    std::string detail;
};

struct PersistedEquipmentDefinition final {
    std::uint64_t revision = 0;
    // Exact UTF-8 JSON document that passed EquipmentDefinitionJsonLoader.
    // It remains available for diagnostics/export and is the durable source
    // used to reconstruct the Core graph.
    std::string sourceDocument;
    core::MicroscopeEquipmentDefinition equipment;
};

// Stores immutable equipment-definition revisions. SQLite stores the
// validated source JSON as a BLOB; loading always re-runs the strict JSON
// decoder and Core factories. This deliberately avoids a second serializer
// for the equipment graph and never persists runtime state or calibration.
class EquipmentDefinitionSqliteStore final {
public:
    [[nodiscard]] static core::Result<
        std::unique_ptr<EquipmentDefinitionSqliteStore>,
        EquipmentDefinitionStoreError> open(const std::string& databasePath);

    ~EquipmentDefinitionSqliteStore();

    EquipmentDefinitionSqliteStore(const EquipmentDefinitionSqliteStore&) = delete;
    EquipmentDefinitionSqliteStore& operator=(const EquipmentDefinitionSqliteStore&) = delete;
    EquipmentDefinitionSqliteStore(EquipmentDefinitionSqliteStore&&) = delete;
    EquipmentDefinitionSqliteStore& operator=(EquipmentDefinitionSqliteStore&&) = delete;

    // The equipment identity and revision are read from the validated
    // document. expectedCurrentRevision is zero for a new microscope and must
    // match the current persisted revision. New revisions must increase
    // monotonically; historical rows are immutable.
    [[nodiscard]] core::Result<std::uint64_t, EquipmentDefinitionStoreError>
    saveCurrentDefinition(
        std::string_view sourceDocument,
        std::uint64_t expectedCurrentRevision);

    [[nodiscard]] core::Result<
        std::optional<PersistedEquipmentDefinition>,
        EquipmentDefinitionStoreError> loadCurrentDefinition(
            const core::MicroscopeId& microscopeId);

    [[nodiscard]] core::Result<
        std::optional<PersistedEquipmentDefinition>,
        EquipmentDefinitionStoreError> loadRevision(
            const core::MicroscopeId& microscopeId,
            std::uint64_t revision);

private:
    explicit EquipmentDefinitionSqliteStore(sqlite3* database) noexcept;

    [[nodiscard]] core::Status<EquipmentDefinitionStoreError>
    initializeSchema();

    [[nodiscard]] core::Result<
        std::optional<PersistedEquipmentDefinition>,
        EquipmentDefinitionStoreError> loadRevisionUnlocked(
            const core::MicroscopeId& microscopeId,
            std::uint64_t revision);

    sqlite3* database_ = nullptr;
    std::mutex mutex_;
};

} // namespace microscope::application::persistence
