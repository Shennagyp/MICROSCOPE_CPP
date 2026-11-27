#pragma once

#include "microscope/application/navigation/SlideCornerCorrespondence.h"
#include "microscope/core/result/Result.h"
#include "microscope/core/stage/StageWorkingSurface2D.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace microscope::application::persistence {

enum class StageWorkingSurfaceStoreErrorCode {
    EmptyDatabasePath,
    DatabaseOpenFailed,
    SchemaMigrationFailed,
    UnsupportedSchemaVersion,
    InvalidRevision,
    RevisionConflict,
    InvalidWorkingSurface,
    InvalidSlideCornerBindings,
    EquipmentBindingMismatch,
    InvalidPersistedRecord,
    SqliteOperationFailed
};

struct StageWorkingSurfaceStoreError final {
    StageWorkingSurfaceStoreErrorCode code;
    int nativeCode = 0;
    std::string detail;
};

// One explicit per-slide corner correspondence persisted alongside a CAD
// layout revision. This is not the physical carrier's measured registration.
struct StageWorkingSurfaceSlideBinding final {
    core::SlideId slideId;
    navigation::SlideCornerCorrespondence correspondence;
};

struct PersistedStageWorkingSurface final {
    std::uint64_t revision = 0;
    core::StageWorkingSurface2D workingSurface;
    std::vector<StageWorkingSurfaceSlideBinding> slideBindings;
};

// Stores the CAD working-surface layout and its per-slide corner correspondence
// as an append-only, equipment-revision-bound snapshot. The measured physical
// registrations remain owned by DualSlideCarrierSqliteStore.
class StageWorkingSurfaceSqliteStore final {
public:
    [[nodiscard]] static core::Result<
        std::unique_ptr<StageWorkingSurfaceSqliteStore>,
        StageWorkingSurfaceStoreError> open(const std::string& databasePath);

    ~StageWorkingSurfaceSqliteStore();

    StageWorkingSurfaceSqliteStore(const StageWorkingSurfaceSqliteStore&) = delete;
    StageWorkingSurfaceSqliteStore& operator=(const StageWorkingSurfaceSqliteStore&) = delete;
    StageWorkingSurfaceSqliteStore(StageWorkingSurfaceSqliteStore&&) = delete;
    StageWorkingSurfaceSqliteStore& operator=(StageWorkingSurfaceSqliteStore&&) = delete;

    // expectedCurrentRevision is zero for a new layout under this exact
    // microscope/equipment-revision/stage-component identity.
    [[nodiscard]] core::Result<std::uint64_t, StageWorkingSurfaceStoreError>
    saveCurrentState(
        const core::MicroscopeId& microscopeId,
        std::uint64_t equipmentDefinitionRevision,
        const core::StageSpecification& installedStage,
        const core::StageWorkingSurface2D& workingSurface,
        const std::vector<StageWorkingSurfaceSlideBinding>& slideBindings,
        std::uint64_t expectedCurrentRevision);

    [[nodiscard]] core::Result<
        std::optional<PersistedStageWorkingSurface>,
        StageWorkingSurfaceStoreError> loadCurrentState(
            const core::MicroscopeId& microscopeId,
            std::uint64_t equipmentDefinitionRevision,
            const core::StageSpecification& installedStage);

    [[nodiscard]] core::Result<
        std::optional<PersistedStageWorkingSurface>,
        StageWorkingSurfaceStoreError> loadRevision(
            const core::MicroscopeId& microscopeId,
            std::uint64_t equipmentDefinitionRevision,
            const core::StageSpecification& installedStage,
            std::uint64_t revision);

private:
    explicit StageWorkingSurfaceSqliteStore(sqlite3* database) noexcept;

    [[nodiscard]] core::Status<StageWorkingSurfaceStoreError> initializeSchema();

    [[nodiscard]] core::Result<
        std::optional<PersistedStageWorkingSurface>,
        StageWorkingSurfaceStoreError> loadRevisionUnlocked(
            const core::MicroscopeId& microscopeId,
            std::uint64_t equipmentDefinitionRevision,
            const core::StageSpecification& installedStage,
            std::uint64_t revision);

    sqlite3* database_ = nullptr;
    std::mutex mutex_;
};

} // namespace microscope::application::persistence
