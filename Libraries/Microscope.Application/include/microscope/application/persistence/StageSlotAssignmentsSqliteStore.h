#pragma once

#include "microscope/application/navigation/StageSlotAssignments.h"
#include "microscope/core/result/Result.h"
#include "microscope/core/samples/SampleModels.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace microscope::application::persistence {

enum class StageSlotAssignmentsStoreErrorCode {
    EmptyDatabasePath,
    DatabaseOpenFailed,
    SampleSlideStoreSchemaRequired,
    SchemaMigrationFailed,
    UnsupportedSchemaVersion,
    InvalidRevision,
    RevisionConflict,
    SlideNotPersisted,
    SlideRevisionMismatch,
    EquipmentBindingMismatch,
    InvalidPersistedRecord,
    SqliteOperationFailed
};

struct StageSlotAssignmentsStoreError final {
    StageSlotAssignmentsStoreErrorCode code;
    int nativeCode = 0;
    std::string detail;
};

struct PersistedStageSlotAssignments final {
    std::uint64_t revision = 0;
    navigation::StageSlotAssignments assignments;
};

// Append-only assignment snapshots. The Slide object and its immutable
// revision remain owned by SampleSlideSqliteStore; this store persists only
// StageSlotId -> SlideId/revision relationships.
class StageSlotAssignmentsSqliteStore final {
public:
    [[nodiscard]] static core::Result<
        std::unique_ptr<StageSlotAssignmentsSqliteStore>,
        StageSlotAssignmentsStoreError> open(const std::string& databasePath);

    ~StageSlotAssignmentsSqliteStore();

    StageSlotAssignmentsSqliteStore(const StageSlotAssignmentsSqliteStore&) = delete;
    StageSlotAssignmentsSqliteStore& operator=(const StageSlotAssignmentsSqliteStore&) = delete;
    StageSlotAssignmentsSqliteStore(StageSlotAssignmentsSqliteStore&&) = delete;
    StageSlotAssignmentsSqliteStore& operator=(StageSlotAssignmentsSqliteStore&&) = delete;

    // Every assigned Slide must be supplied as its current persisted value.
    // The store checks its current revision and pins that exact revision.
    // expectedCurrentRevision is zero for a new Stage/configuration assignment
    // set; subsequent writes append exactly one revision.
    [[nodiscard]] core::Status<StageSlotAssignmentsStoreError> saveCurrentState(
        const core::MicroscopeId& microscopeId,
        const navigation::StageSlotAssignments& assignments,
        const std::vector<core::Slide>& currentSlides,
        std::uint64_t expectedCurrentRevision);

    [[nodiscard]] core::Result<
        std::optional<PersistedStageSlotAssignments>,
        StageSlotAssignmentsStoreError> loadCurrentState(
            const core::MicroscopeId& microscopeId,
            std::uint64_t equipmentDefinitionRevision,
            const core::StageSpecification& installedStage,
            const core::StageConfigurationId& configurationId);

    [[nodiscard]] core::Result<
        std::optional<PersistedStageSlotAssignments>,
        StageSlotAssignmentsStoreError> loadRevision(
            const core::MicroscopeId& microscopeId,
            std::uint64_t equipmentDefinitionRevision,
            const core::StageSpecification& installedStage,
            const core::StageConfigurationId& configurationId,
            std::uint64_t revision);

private:
    explicit StageSlotAssignmentsSqliteStore(sqlite3* database) noexcept;

    [[nodiscard]] core::Status<StageSlotAssignmentsStoreError> initializeSchema();

    [[nodiscard]] core::Result<
        std::optional<PersistedStageSlotAssignments>,
        StageSlotAssignmentsStoreError> loadRevisionUnlocked(
            const core::MicroscopeId& microscopeId,
            std::uint64_t equipmentDefinitionRevision,
            const core::StageSpecification& installedStage,
            const core::StageConfigurationId& configurationId,
            std::uint64_t revision);

    sqlite3* database_ = nullptr;
    std::mutex mutex_;
};

} // namespace microscope::application::persistence
