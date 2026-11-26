#pragma once

#include "camera/CameraModelCatalog.h"
#include "microscope/core/equipment/OpticalTopology.h"
#include "microscope/core/result/Result.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct sqlite3;

namespace microscope::application::persistence {

struct CameraComponentModelBinding {
    core::EquipmentComponentId cameraComponentId;
    ::camera::CameraModelKey modelKey;
    std::uint64_t modelProfileRevision = 0;
};

struct PersistedCameraComponentModelBindings {
    std::uint64_t bindingRevision = 0;
    std::vector<CameraComponentModelBinding> bindings;
};

enum class CameraComponentModelBindingStoreErrorCode {
    EmptyDatabasePath,
    DatabaseOpenFailed,
    SchemaMigrationFailed,
    UnsupportedSchemaVersion,
    InvalidRevision,
    RevisionConflict,
    DuplicateCameraComponent,
    CameraComponentNotInstalled,
    CameraComponentWrongKind,
    CameraSpecificationMissing,
    CatalogProfileNotFound,
    CatalogProfileRevisionMismatch,
    InvalidPersistedBinding,
    SqliteOperationFailed
};

struct CameraComponentModelBindingStoreError {
    CameraComponentModelBindingStoreErrorCode code;
    int nativeCode = 0;
    std::string detail;
};

// Persists the stable model-profile identity associated with each installed
// camera component. It is scoped to an immutable equipment-definition
// revision. It deliberately does not persist transient SDK IDs or serials:
// physical device/session binding is resolved at runtime.
class CameraComponentModelBindingSqliteStore final {
public:
    [[nodiscard]] static core::Result<
        std::unique_ptr<CameraComponentModelBindingSqliteStore>,
        CameraComponentModelBindingStoreError> open(
            const std::string& databasePath);

    ~CameraComponentModelBindingSqliteStore();

    CameraComponentModelBindingSqliteStore(
        const CameraComponentModelBindingSqliteStore&) = delete;
    CameraComponentModelBindingSqliteStore& operator=(
        const CameraComponentModelBindingSqliteStore&) = delete;
    CameraComponentModelBindingSqliteStore(
        CameraComponentModelBindingSqliteStore&&) = delete;
    CameraComponentModelBindingSqliteStore& operator=(
        CameraComponentModelBindingSqliteStore&&) = delete;

    // Replace the binding set for this exact equipment revision. The new
    // binding revision must equal expectedCurrentRevision + 1.
    [[nodiscard]] core::Status<CameraComponentModelBindingStoreError>
    replaceBindings(
        const core::MicroscopeEquipmentDefinition& equipment,
        const ::camera::CameraModelCatalog& catalog,
        const std::vector<CameraComponentModelBinding>& bindings,
        std::uint64_t expectedCurrentRevision,
        std::uint64_t newBindingRevision);

    // Returns revision zero and an empty set when no bindings exist for the
    // supplied equipment revision. Stale/missing model profiles are errors.
    [[nodiscard]] core::Result<
        PersistedCameraComponentModelBindings,
        CameraComponentModelBindingStoreError> loadBindings(
            const core::MicroscopeEquipmentDefinition& equipment,
            const ::camera::CameraModelCatalog& catalog);

private:
    explicit CameraComponentModelBindingSqliteStore(sqlite3* database) noexcept;

    [[nodiscard]] core::Status<CameraComponentModelBindingStoreError>
    initializeSchema();

    sqlite3* database_ = nullptr;
    std::mutex mutex_;
};

} // namespace microscope::application::persistence
