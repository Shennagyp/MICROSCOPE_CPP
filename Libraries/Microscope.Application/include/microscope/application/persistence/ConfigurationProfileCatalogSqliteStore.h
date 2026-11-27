#pragma once

#include "microscope/core/configuration/Configuration.h"
#include "microscope/core/result/Result.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

struct sqlite3;

namespace microscope::application::persistence {

enum class ConfigurationProfileCatalogStoreErrorCode {
    EmptyDatabasePath,
    DatabaseOpenFailed,
    SchemaMigrationFailed,
    UnsupportedSchemaVersion,
    InvalidRevision,
    RevisionConflict,
    PayloadTooLarge,
    InvalidCatalog,
    InvalidPersistedRecord,
    SqliteOperationFailed
};

struct ConfigurationProfileCatalogStoreError final {
    ConfigurationProfileCatalogStoreErrorCode code;
    int nativeCode = 0;
    std::string detail;
};

struct PersistedConfigurationProfileCatalog final {
    std::uint64_t revision = 0;
    std::uint64_t equipmentDefinitionRevision = 0;
    core::ConfigurationProfileCatalog catalog;
};

// Persists declarative compatibility profiles as immutable catalog revisions.
// A catalog revision is scoped to one microscope and one exact equipment
// definition revision. It does not persist equipment, calibration, selected
// runtime settings or device state.
//
// The binary payload is an explicit little-endian codec with a format version;
// it never serializes native C++ object memory. Decoding reconstructs every
// profile through Core factories so corrupt or unknown enum values are rejected.
class ConfigurationProfileCatalogSqliteStore final {
public:
    [[nodiscard]] static core::Result<
        std::unique_ptr<ConfigurationProfileCatalogSqliteStore>,
        ConfigurationProfileCatalogStoreError> open(const std::string& databasePath);

    ~ConfigurationProfileCatalogSqliteStore();

    ConfigurationProfileCatalogSqliteStore(
        const ConfigurationProfileCatalogSqliteStore&) = delete;
    ConfigurationProfileCatalogSqliteStore& operator=(
        const ConfigurationProfileCatalogSqliteStore&) = delete;
    ConfigurationProfileCatalogSqliteStore(
        ConfigurationProfileCatalogSqliteStore&&) = delete;
    ConfigurationProfileCatalogSqliteStore& operator=(
        ConfigurationProfileCatalogSqliteStore&&) = delete;

    // expectedCurrentRevision is zero for the first catalog under this exact
    // microscope/equipment-definition identity. Returns the appended revision.
    [[nodiscard]] core::Result<std::uint64_t,
        ConfigurationProfileCatalogStoreError> saveCurrentCatalog(
            const core::MicroscopeId& microscopeId,
            std::uint64_t equipmentDefinitionRevision,
            const core::ConfigurationProfileCatalog& catalog,
            std::uint64_t expectedCurrentRevision);

    [[nodiscard]] core::Result<
        std::optional<PersistedConfigurationProfileCatalog>,
        ConfigurationProfileCatalogStoreError> loadCurrentCatalog(
            const core::MicroscopeId& microscopeId,
            std::uint64_t equipmentDefinitionRevision);

    [[nodiscard]] core::Result<
        std::optional<PersistedConfigurationProfileCatalog>,
        ConfigurationProfileCatalogStoreError> loadRevision(
            const core::MicroscopeId& microscopeId,
            std::uint64_t equipmentDefinitionRevision,
            std::uint64_t revision);

private:
    explicit ConfigurationProfileCatalogSqliteStore(sqlite3* database) noexcept;

    [[nodiscard]] core::Status<ConfigurationProfileCatalogStoreError>
    initializeSchema();

    [[nodiscard]] core::Result<
        std::optional<PersistedConfigurationProfileCatalog>,
        ConfigurationProfileCatalogStoreError> loadRevisionUnlocked(
            const core::MicroscopeId& microscopeId,
            std::uint64_t equipmentDefinitionRevision,
            std::uint64_t revision);

    sqlite3* database_ = nullptr;
    std::mutex mutex_;
};

} // namespace microscope::application::persistence
