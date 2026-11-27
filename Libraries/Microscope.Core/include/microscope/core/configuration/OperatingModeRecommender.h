#pragma once

#include "microscope/core/configuration/OperationMode.h"
#include "microscope/core/samples/SampleModels.h"

#include <cstdint>
#include <vector>

namespace microscope::core {

enum class OperatingModeRecommendationError {
    SlideSampleTypeMismatch,
    EmptyCandidateModes,
    CandidateMicroscopeMismatch,
    DuplicateOperationModeId
};

// Evidence for recommending one existing, microscope-scoped OperationMode.
// Stain evidence is advisory and never changes SampleType eligibility.
struct OperatingModeRecommendation final {
    OperationModeId operationModeId;
    std::uint64_t operationModeRevision;
    bool preferredBySampleType = false;
    std::vector<StainId> supportingStains;

    [[nodiscard]] bool recommendedByStain() const noexcept {
        return !supportingStains.empty();
    }
};

class OperatingModeRecommendationSet final {
public:
    [[nodiscard]] const std::vector<OperatingModeRecommendation>&
    candidates() const noexcept;

    // Returns a candidate only when the highest evidence rank is unique.
    // A tie remains a selection decision for the application/operator.
    [[nodiscard]] const OperatingModeRecommendation*
    unambiguousRecommendation() const noexcept;

private:
    friend class OperatingModeRecommender;
    explicit OperatingModeRecommendationSet(
        std::vector<OperatingModeRecommendation> candidates);

    std::vector<OperatingModeRecommendation> candidates_;
};

class OperatingModeRecommender final {
public:
    // Candidate modes must be the available modes for the requested microscope.
    // SampleType hard technique constraints determine eligibility first.
    // SampleType preferences outrank stain advice; stain suitability only ranks
    // modes that already satisfy the SampleType's hard constraints.
    [[nodiscard]] static Result<
        OperatingModeRecommendationSet,
        OperatingModeRecommendationError> recommend(
            const MicroscopeId& microscopeId,
            const SampleType& sampleType,
            const Slide& slide,
            const std::vector<OperationMode>& candidateModes);
};

} // namespace microscope::core
