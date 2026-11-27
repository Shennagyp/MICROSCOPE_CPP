#include "microscope/core/configuration/OperatingModeRecommender.h"

#include <algorithm>
#include <set>
#include <utility>

namespace microscope::core {
namespace {

[[nodiscard]] bool isEligibleTechnique(
    const std::vector<MicroscopyTechniqueConstraint>& constraints,
    ContrastMethod candidate) noexcept {
    const MicroscopyTechniqueConstraint* required = nullptr;
    bool hasSupportedSet = false;
    bool candidateIsSupported = false;

    for (const auto& constraint : constraints) {
        switch (constraint.kind) {
        case MicroscopyTechniqueConstraintKind::Required:
            required = &constraint;
            break;
        case MicroscopyTechniqueConstraintKind::Supported:
            hasSupportedSet = true;
            candidateIsSupported |= constraint.method == candidate;
            break;
        case MicroscopyTechniqueConstraintKind::Preferred:
            // Preferred is an explicitly allowed technique as well as a
            // ranking signal; it remains eligible alongside a Supported set.
            candidateIsSupported |= constraint.method == candidate;
            break;
        case MicroscopyTechniqueConstraintKind::Prohibited:
            if (constraint.method == candidate) {
                return false;
            }
            break;
        }
    }

    if (required != nullptr && required->method != candidate) {
        return false;
    }

    if (hasSupportedSet &&
        !candidateIsSupported &&
        (required == nullptr || required->method != candidate)) {
        return false;
    }

    return true;
}

[[nodiscard]] bool isPreferredBySampleType(
    const std::vector<MicroscopyTechniqueConstraint>& constraints,
    ContrastMethod candidate) noexcept {
    return std::any_of(
        constraints.begin(), constraints.end(),
        [candidate](const MicroscopyTechniqueConstraint& constraint) {
            return constraint.kind == MicroscopyTechniqueConstraintKind::Preferred &&
                   constraint.method == candidate;
        });
}

[[nodiscard]] std::vector<StainId> stainSupportFor(
    const Slide& slide,
    ContrastMethod candidate) {
    std::vector<StainId> result;
    for (const auto& stain : slide.stains()) {
        const auto& suitability = stain.microscopySuitability();
        if (!suitability) {
            continue;
        }
        const auto& methods = suitability->recommendedContrastMethods();
        if (std::find(methods.begin(), methods.end(), candidate) != methods.end()) {
            result.push_back(stain.id());
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

[[nodiscard]] bool hasSameRank(
    const OperatingModeRecommendation& left,
    const OperatingModeRecommendation& right) noexcept {
    return left.preferredBySampleType == right.preferredBySampleType &&
           left.recommendedByStain() == right.recommendedByStain();
}

} // namespace

OperatingModeRecommendationSet::OperatingModeRecommendationSet(
    std::vector<OperatingModeRecommendation> candidates)
    : candidates_(std::move(candidates)) {}

const std::vector<OperatingModeRecommendation>&
OperatingModeRecommendationSet::candidates() const noexcept {
    return candidates_;
}

const OperatingModeRecommendation*
OperatingModeRecommendationSet::unambiguousRecommendation() const noexcept {
    if (candidates_.empty()) {
        return nullptr;
    }
    if (candidates_.size() == 1 || !hasSameRank(candidates_[0], candidates_[1])) {
        return &candidates_.front();
    }
    return nullptr;
}

Result<
    OperatingModeRecommendationSet,
    OperatingModeRecommendationError>
OperatingModeRecommender::recommend(
    const MicroscopeId& microscopeId,
    const SampleType& sampleType,
    const Slide& slide,
    const std::vector<OperationMode>& candidateModes) {
    if (slide.sampleTypeId() != sampleType.id() ||
        slide.sampleTypeRevision() != sampleType.revision()) {
        return Result<
            OperatingModeRecommendationSet,
            OperatingModeRecommendationError>::failure(
                OperatingModeRecommendationError::SlideSampleTypeMismatch);
    }
    if (candidateModes.empty()) {
        return Result<
            OperatingModeRecommendationSet,
            OperatingModeRecommendationError>::failure(
                OperatingModeRecommendationError::EmptyCandidateModes);
    }

    std::set<OperationModeId> modeIds;
    for (const auto& mode : candidateModes) {
        if (mode.microscopeId() != microscopeId) {
            return Result<
                OperatingModeRecommendationSet,
                OperatingModeRecommendationError>::failure(
                    OperatingModeRecommendationError::CandidateMicroscopeMismatch);
        }
        if (!modeIds.insert(mode.id()).second) {
            return Result<
                OperatingModeRecommendationSet,
                OperatingModeRecommendationError>::failure(
                    OperatingModeRecommendationError::DuplicateOperationModeId);
        }
    }

    const auto& constraints = sampleType.microscopyRequirements().techniques();
    // A Stain cannot independently select a contrast mode. It contributes
    // advisory ranking only when the SampleType supplies technique context.
    const bool hasSampleTypeTechniqueContext = !constraints.empty();
    std::vector<OperatingModeRecommendation> recommendations;
    recommendations.reserve(candidateModes.size());

    for (const auto& mode : candidateModes) {
        const auto method = mode.contrastMethod();
        if (!isEligibleTechnique(constraints, method)) {
            continue;
        }

        recommendations.push_back(OperatingModeRecommendation{
            mode.id(),
            mode.revision(),
            isPreferredBySampleType(constraints, method),
            hasSampleTypeTechniqueContext
                ? stainSupportFor(slide, method)
                : std::vector<StainId>{}});
    }

    // Stable, explainable precedence:
    // 1) SampleType preference
    // 2) Optional Stain suitability recommendation
    // 3) Stable mode ID (display ordering only; does not break a semantic tie)
    std::sort(
        recommendations.begin(), recommendations.end(),
        [](const OperatingModeRecommendation& left,
           const OperatingModeRecommendation& right) {
            if (left.preferredBySampleType != right.preferredBySampleType) {
                return left.preferredBySampleType > right.preferredBySampleType;
            }
            if (left.recommendedByStain() != right.recommendedByStain()) {
                return left.recommendedByStain() > right.recommendedByStain();
            }
            return left.operationModeId < right.operationModeId;
        });

    return Result<
        OperatingModeRecommendationSet,
        OperatingModeRecommendationError>::success(
            OperatingModeRecommendationSet(std::move(recommendations)));
}

} // namespace microscope::core
