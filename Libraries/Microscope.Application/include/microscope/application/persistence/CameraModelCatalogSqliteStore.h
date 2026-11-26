#pragma once

#include "camera/CameraModelCatalog.h"
#include "microscope/core/result/Result.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

struct sqlite3;

namespace microscope::application::persistence {

enum class CameraModelCatalogStoreErrorCode {
    EmptyDatabasePath,
    DatabaseOpenFailed,
    SchemaMigrationFailed,
    UnsupportedSchemaVersion,
    InvalidCatalogRevision,
    RevisionConflict,
    InvalidPersistedProfile,
    SqliteOperationFailed
};

struct CameraModelCatalogStoreError {
    CameraModelCatalogStoreErrorCode code;
    int nativeCode = 0;
    std::string detail;
};

// SQLite-backed durable storage for the shared Camera::Common model catalog.
// One catalog is shared by microscope instances. Writes replace the catalog
// atomically and require optimistic revision matching. Connected-device IDs
// and serial numbers are never used as model-profile keys.
class CameraModelCatalogSqliteStore final {
public:
    [[nodiscard]] static core::Result<
        std::unique_ptr<CameraModelCatalogSqliteStore>,
        CameraModelCatalogStoreError> open(const std::string& databasePath);

    ~CameraModelCatalogSqliteStore();

    CameraModelCatalogSqliteStore(const CameraModelCatalogSqliteStore&) = delete;
    CameraModelCatalogSqliteStore& operator=(
        const CameraModelCatalogSqliteStore&) = delete;
    CameraModelCatalogSqliteStore(CameraModelCatalogSqliteStore&&) = delete;
    CameraModelCatalogSqliteStore& operator=(CameraModelCatalogSqliteStore&&) = delete;

    [[nodiscard]] core::Result<
        std::optional<camera::CameraModelCatalog>,
        CameraModelCatalogStoreError> load();

    // expectedCurrentRevision is zero for first insert. The supplied catalog
    // must have revision expectedCurrentRevision + 1. The replacement and
    // revision advance commit in one SQLite transaction.
    [[nodiscard]] core::Status<CameraModelCatalogStoreError> save(
        const camera::CameraModelCatalog& catalog,
        std::uint64_t expectedCurrentRevision);

private:
    explicit CameraModelCatalogSqliteStore(sqlite3* database) noexcept;

    [[nodiscard]] core::Status<CameraModelCatalogStoreError>
    initializeSchema();

    sqlite3* database_ = nullptr;
    std::mutex mutex_;
};

} // namespace microscope::application::persistence
