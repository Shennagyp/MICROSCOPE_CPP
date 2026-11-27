#pragma once

#include "microscope/application/configuration/EquipmentDefinitionJsonLoader.h"
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
    RevisionMismatch,
    RevisionConflict,
    DocumentTooLarge,
    InvalidDefinitionDocument,
    InvalidPersistedRecord,
    SqliteOperationFailed
};

struct EquipmentDefinitionStoreError final {
    EquipmentDefinitionStoreErrorCode code;
    int nativeCode = 0;
    std::string detail;
    std::optional<configuration::EquipmentDefinitionJsonError> jsonError;
};

struct PersistedEquipmentDefinition final {
    std::uint64_t revision = 0;
    std::string sourceDocument;
    core::MicroscopeEquipmentDefinition equipment;
};

// Stores each validated equipment definition as its original versioned JSON
// source document. The loader reconstructs the canonical Core graph on both
// save and load; the database does not maintain a second, partial equipment
// schema. Revisions are append-only and scoped by the Core MicroscopeId.
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

    // The document must contain the expected MicroscopeId and exactly
    // expectedCurrentRevision + 1. expectedCurrentRevision is zero for a
    // microscope whose first equipment-definition revision is being saved.
    [[nodiscard]] core::Result<std::uint64_t, EquipmentDefinitionStoreError>
    saveCurrentDefinition(
        const core::MicroscopeId& microscopeId,
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
