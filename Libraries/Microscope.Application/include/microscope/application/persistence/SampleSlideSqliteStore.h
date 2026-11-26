#pragma once

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

enum class SampleSlideStoreErrorCode {
    EmptyDatabasePath,
    DatabaseOpenFailed,
    SchemaMigrationFailed,
    UnsupportedSchemaVersion,
    InvalidRevision,
    PayloadTooLarge,
    RevisionConflict,
    DuplicateSampleTypeRevision,
    SampleTypeRevisionNotFound,
    DuplicateSlideRevision,
    SlideRevisionNotFound,
    SlideSampleTypeRevisionNotFound,
    InvalidPersistedRecord,
    SqliteOperationFailed
};

struct SampleSlideStoreError final {
    SampleSlideStoreErrorCode code;
    int nativeCode = 0;
    std::string detail;
};

// A coherent read of one immutable Slide revision and the exact immutable
// SampleType definition it pins. This must not substitute the current
// SampleType revision when the Slide references historical requirements.
struct PersistedSlideWithSampleType final {
    core::Slide slide;
    core::SampleType sampleType;
};

// Stores immutable SampleType and Slide revisions. A Slide is linked to the
// exact SampleTypeId/revision it was prepared against; updating a SampleType
// never rewrites or invalidates that historical reference.
//
// Aggregate payloads use an explicit, little-endian, versioned codec rather
// than serializing C++ object memory. SQLite owns revision identity, current
// pointers and referential integrity; the codec owns the complete domain value.
class SampleSlideSqliteStore final {
public:
    [[nodiscard]] static core::Result<
        std::unique_ptr<SampleSlideSqliteStore>,
        SampleSlideStoreError> open(const std::string& databasePath);

    ~SampleSlideSqliteStore();

    SampleSlideSqliteStore(const SampleSlideSqliteStore&) = delete;
    SampleSlideSqliteStore& operator=(const SampleSlideSqliteStore&) = delete;
    SampleSlideSqliteStore(SampleSlideSqliteStore&&) = delete;
    SampleSlideSqliteStore& operator=(SampleSlideSqliteStore&&) = delete;

    // Append-only revision write. expectedCurrentRevision is zero when the
    // SampleType is first created; subsequent writes must advance it by one.
    [[nodiscard]] core::Status<SampleSlideStoreError> saveSampleTypeRevision(
        const core::SampleType& sampleType,
        std::uint64_t expectedCurrentRevision);

    [[nodiscard]] core::Result<
        std::optional<core::SampleType>,
        SampleSlideStoreError> loadSampleTypeRevision(
            const core::SampleTypeId& sampleTypeId,
            std::uint64_t revision);

    [[nodiscard]] core::Result<
        std::optional<core::SampleType>,
        SampleSlideStoreError> loadCurrentSampleType(
            const core::SampleTypeId& sampleTypeId);

    // Current definitions for workspace/catalog display, ordered by stable ID.
    [[nodiscard]] core::Result<
        std::vector<core::SampleType>,
        SampleSlideStoreError> loadCurrentSampleTypes();

    // A Slide revision is committed only when the exact referenced SampleType
    // revision already exists. The first Slide revision uses expected=0.
    [[nodiscard]] core::Status<SampleSlideStoreError> saveSlideRevision(
        const core::Slide& slide,
        std::uint64_t expectedCurrentRevision);

    [[nodiscard]] core::Result<
        std::optional<core::Slide>,
        SampleSlideStoreError> loadSlideRevision(
            const core::SlideId& slideId,
            std::uint64_t revision);

    [[nodiscard]] core::Result<
        std::optional<core::Slide>,
        SampleSlideStoreError> loadCurrentSlide(
            const core::SlideId& slideId);

    // Resolves the exact SampleType revision pinned by a persisted Slide.
    [[nodiscard]] core::Result<
        std::optional<PersistedSlideWithSampleType>,
        SampleSlideStoreError> loadSlideWithPinnedSampleType(
            const core::SlideId& slideId,
            std::uint64_t slideRevision);

    [[nodiscard]] core::Result<
        std::optional<PersistedSlideWithSampleType>,
        SampleSlideStoreError> loadCurrentSlideWithPinnedSampleType(
            const core::SlideId& slideId);

    // Current Slides and the exact SampleType revisions they pin, ordered by
    // stable SlideId. This is the workspace's coherent discovery read.
    [[nodiscard]] core::Result<
        std::vector<PersistedSlideWithSampleType>,
        SampleSlideStoreError> loadCurrentSlidesWithPinnedSampleTypes();

private:
    explicit SampleSlideSqliteStore(sqlite3* database) noexcept;

    [[nodiscard]] core::Status<SampleSlideStoreError> initializeSchema();

    [[nodiscard]] core::Result<
        std::optional<core::SampleType>,
        SampleSlideStoreError> loadSampleTypeRevisionUnlocked(
            const core::SampleTypeId& sampleTypeId,
            std::uint64_t revision);

    [[nodiscard]] core::Result<
        std::optional<core::Slide>,
        SampleSlideStoreError> loadSlideRevisionUnlocked(
            const core::SlideId& slideId,
            std::uint64_t revision);

    [[nodiscard]] core::Result<
        std::optional<PersistedSlideWithSampleType>,
        SampleSlideStoreError> loadSlideWithPinnedSampleTypeUnlocked(
            const core::SlideId& slideId,
            std::uint64_t slideRevision);

    sqlite3* database_ = nullptr;
    std::mutex mutex_;
};

} // namespace microscope::application::persistence
