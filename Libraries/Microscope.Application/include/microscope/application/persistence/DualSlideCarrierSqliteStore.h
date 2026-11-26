#pragma once

#include "microscope/core/navigation/DualSlideNavigation.h"
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

enum class DualSlideCarrierStoreErrorCode {
    EmptyDatabasePath,
    DatabaseOpenFailed,
    SampleSlideStoreSchemaRequired,
    SchemaMigrationFailed,
    UnsupportedSchemaVersion,
    InvalidRevision,
    PayloadTooLarge,
    RevisionConflict,
    SlideNotPersisted,
    SlideRevisionMismatch,
    SlideGeometryMismatch,
    EquipmentBindingMismatch,
    InvalidPersistedRecord,
    SqliteOperationFailed
};

struct DualSlideCarrierStoreError final {
    DualSlideCarrierStoreErrorCode code;
    int nativeCode = 0;
    std::string detail;
};

// The physical carrier snapshot is bound to one exact microscope equipment
// revision. Each mounted Slide is also pinned to the exact immutable Slide
// revision whose physical geometry was checked against its four-corner map.
struct PersistedDualSlideCarrierState final {
    std::uint64_t revision = 0;
    core::DualSlideCarrierState state;
    std::optional<std::uint64_t> leftSlideRevision;
    std::optional<std::uint64_t> rightSlideRevision;
};

// Persists the two physical horizontal carrier slots and the independently
// measured four-corner affine registration for each mounted Slide.
//
// This store requires SampleSlideSqliteStore to have initialized the same
// SQLite database first. Carrier revisions reference immutable slide_revision
// rows by exact SlideId/revision; changing a Slide creates a new carrier-state
// revision rather than rewriting the registration's historical association.
class DualSlideCarrierSqliteStore final {
public:
    [[nodiscard]] static core::Result<
        std::unique_ptr<DualSlideCarrierSqliteStore>,
        DualSlideCarrierStoreError> open(const std::string& databasePath);

    ~DualSlideCarrierSqliteStore();

    DualSlideCarrierSqliteStore(const DualSlideCarrierSqliteStore&) = delete;
    DualSlideCarrierSqliteStore& operator=(const DualSlideCarrierSqliteStore&) = delete;
    DualSlideCarrierSqliteStore(DualSlideCarrierSqliteStore&&) = delete;
    DualSlideCarrierSqliteStore& operator=(DualSlideCarrierSqliteStore&&) = delete;

    // currentSlides must contain the latest persisted Slide value for each
    // occupied slot. The store checks the current revision and requires the
    // measured registration geometry to match that Slide's physical geometry.
    // expectedCurrentRevision is zero for a new equipment-bound carrier state.
    [[nodiscard]] core::Status<DualSlideCarrierStoreError> saveCurrentState(
        const core::MicroscopeId& microscopeId,
        std::uint64_t equipmentDefinitionRevision,
        const core::DualSlideCarrierState& state,
        const std::vector<core::Slide>& currentSlides,
        std::uint64_t expectedCurrentRevision);

    [[nodiscard]] core::Result<
        std::optional<PersistedDualSlideCarrierState>,
        DualSlideCarrierStoreError> loadCurrentState(
            const core::MicroscopeId& microscopeId,
            std::uint64_t equipmentDefinitionRevision,
            const core::DualSlideCarrierSpecification& installedSpecification);

    [[nodiscard]] core::Result<
        std::optional<PersistedDualSlideCarrierState>,
        DualSlideCarrierStoreError> loadRevision(
            const core::MicroscopeId& microscopeId,
            std::uint64_t equipmentDefinitionRevision,
            const core::DualSlideCarrierSpecification& installedSpecification,
            std::uint64_t revision);

private:
    explicit DualSlideCarrierSqliteStore(sqlite3* database) noexcept;

    [[nodiscard]] core::Status<DualSlideCarrierStoreError> initializeSchema();

    [[nodiscard]] core::Result<
        std::optional<PersistedDualSlideCarrierState>,
        DualSlideCarrierStoreError> loadRevisionUnlocked(
            const core::MicroscopeId& microscopeId,
            std::uint64_t equipmentDefinitionRevision,
            const core::DualSlideCarrierSpecification& installedSpecification,
            std::uint64_t revision);

    sqlite3* database_ = nullptr;
    std::mutex mutex_;
};

} // namespace microscope::application::persistence
