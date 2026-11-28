#pragma once

#include "microscope/core/equipment/StageSpecifications.h"
#include "microscope/application/navigation/StageSlotRegistration.h"
#include "microscope/core/result/Result.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace microscope::application::navigation {

// An occupied physical StageSlot references an existing application-owned
// Slide identity and the exact immutable Slide revision used for registration.
// Empty slots have no assignment record.
struct StageSlotAssignment final {
    core::StageSlotId stageSlotId;
    core::SlideId slideId;
    std::uint64_t slideRevision = 0;
    std::optional<StageSlotRegistration> registration = std::nullopt;
};

enum class StageSlotAssignmentsError {
    ZeroEquipmentRevision,
    ConfigurationIdentityMismatch,
    SlotNotInConfiguration,
    ZeroSlideRevision,
    DuplicateStageSlotAssignment,
    SlideAssignedToMultipleSlots,
    RegistrationStageSlotMismatch,
    RegistrationSlideMismatch,
    RegistrationSlideRevisionMismatch
};

// Application-owned mapping between physical StageSlots and existing Slide
// identities. It does not own or duplicate Slide data. The snapshot is scoped
// to one installed Stage component, equipment revision and Stage configuration.
class StageSlotAssignments final {
public:
    [[nodiscard]] static core::Result<StageSlotAssignments,
        StageSlotAssignmentsError> create(
            core::EquipmentComponentId stageComponentId,
            std::uint64_t equipmentDefinitionRevision,
            const core::StageConfigurationSpecification& configuration,
            std::vector<StageSlotAssignment> assignments);

    [[nodiscard]] const core::EquipmentComponentId& stageComponentId() const noexcept;
    [[nodiscard]] std::uint64_t equipmentDefinitionRevision() const noexcept;
    [[nodiscard]] const core::StageConfigurationId& configurationId() const noexcept;
    [[nodiscard]] const std::vector<StageSlotAssignment>& assignments() const noexcept;

    [[nodiscard]] const StageSlotAssignment* forSlot(
        const core::StageSlotId&) const noexcept;
    [[nodiscard]] const StageSlotAssignment* forSlide(
        const core::SlideId&) const noexcept;

private:
    StageSlotAssignments(core::EquipmentComponentId,std::uint64_t,
        core::StageConfigurationId,std::vector<StageSlotAssignment>);

    core::EquipmentComponentId stageComponentId_;
    std::uint64_t equipmentDefinitionRevision_;
    core::StageConfigurationId configurationId_;
    std::vector<StageSlotAssignment> assignments_;
};

} // namespace microscope::application::navigation
