#pragma once

#include "microscope/core/configuration/LightPathCalibration.h"
#include "microscope/core/configuration/OperationMode.h"
#include "microscope/core/result/Result.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <mutex>
#include <optional>
#include <string>

struct sqlite3;

namespace microscope::application::persistence {

enum class MicroscopeConfigurationStoreErrorCode {
    EmptyDatabasePath,
    DatabaseOpenFailed,
    SchemaMigrationFailed,
    UnsupportedSchemaVersion,
    InvalidRevision,
    RevisionConflict,
    DuplicateCalibrationRevision,
    CalibrationNotFound,
    CalibrationBindingMismatch,
    InvalidPersistedRecord,
    SqliteOperationFailed
};

struct MicroscopeConfigurationStoreError {
    MicroscopeConfigurationStoreErrorCode code;
    int nativeCode = 0;
    std::string detail;
};

// Durable immutable calibration records and revisioned operation-mode
// aggregates. This store can share the application's SQLite file with other
// Application persistence stores; its schema version is namespaced in its own
// metadata table and does not own SQLite PRAGMA user_version.
class MicroscopeConfigurationSqliteStore final {
public:
    [[nodiscard]] static core::Result<
        std::unique_ptr<MicroscopeConfigurationSqliteStore>,
        MicroscopeConfigurationStoreError> open(const std::string& databasePath);

    ~MicroscopeConfigurationSqliteStore();

    MicroscopeConfigurationSqliteStore(
        const MicroscopeConfigurationSqliteStore&) = delete;
    MicroscopeConfigurationSqliteStore& operator=(
        const MicroscopeConfigurationSqliteStore&) = delete;
    MicroscopeConfigurationSqliteStore(
        MicroscopeConfigurationSqliteStore&&) = delete;
    MicroscopeConfigurationSqliteStore& operator=(
        MicroscopeConfigurationSqliteStore&&) = delete;

    // Calibration revisions are append-only. Reusing a revision identity is
    // rejected; create a new CalibrationRevisionId for every recalibration.
    [[nodiscard]] core::Status<MicroscopeConfigurationStoreError>
    saveCalibration(const core::LightPathCalibration& calibration);

    [[nodiscard]] core::Result<
        std::optional<core::LightPathCalibration>,
        MicroscopeConfigurationStoreError> loadCalibration(
            const core::MicroscopeId& microscopeId,
            const core::CalibrationRevisionId& calibrationRevisionId);

    // Saves a new immutable OperationMode revision. expectedCurrentRevision
    // is zero when creating the first revision. Each contained LightPath
    // setting must reference a persisted calibration matching the mode's
    // microscope, equipment revision, mode revision, path, source and optics.
    [[nodiscard]] core::Status<MicroscopeConfigurationStoreError>
    saveOperationMode(
        const core::OperationMode& operationMode,
        std::uint64_t expectedCurrentRevision);

    [[nodiscard]] core::Result<
        std::optional<core::OperationMode>,
        MicroscopeConfigurationStoreError> loadCurrentOperationMode(
            const core::MicroscopeId& microscopeId,
            const core::OperationModeId& operationModeId);

private:
    explicit MicroscopeConfigurationSqliteStore(sqlite3* database) noexcept;

    [[nodiscard]] core::Status<MicroscopeConfigurationStoreError>
    initializeSchema();

    [[nodiscard]] core::Result<
        std::optional<core::LightPathCalibration>,
        MicroscopeConfigurationStoreError> loadCalibrationUnlocked(
            const core::MicroscopeId& microscopeId,
            const core::CalibrationRevisionId& calibrationRevisionId);

    sqlite3* database_ = nullptr;
    std::mutex mutex_;
};

} // namespace microscope::application::persistence
