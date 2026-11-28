#include "microscope/application/navigation/StageSlotAssignments.h"

#include <algorithm>
#include <set>
#include <utility>

namespace microscope::application::navigation {

StageSlotAssignments::StageSlotAssignments(
    core::EquipmentComponentId stageComponentId,
    std::uint64_t equipmentDefinitionRevision,
    core::StageConfigurationId configurationId,
    std::vector<StageSlotAssignment> assignments)
    :stageComponentId_(std::move(stageComponentId)),
     equipmentDefinitionRevision_(equipmentDefinitionRevision),
     configurationId_(std::move(configurationId)),
     assignments_(std::move(assignments)) {}

core::Result<StageSlotAssignments,StageSlotAssignmentsError>
StageSlotAssignments::create(
    core::EquipmentComponentId stageComponentId,
    std::uint64_t equipmentDefinitionRevision,
    const core::StageConfigurationSpecification& configuration,
    std::vector<StageSlotAssignment> assignments) {
    if(equipmentDefinitionRevision==0)
        return core::Result<StageSlotAssignments,StageSlotAssignmentsError>::failure(
            StageSlotAssignmentsError::ZeroEquipmentRevision);

    std::set<core::StageSlotId> configuredSlotIds;
    for(const auto& slot:configuration.slots()) configuredSlotIds.insert(slot.id());

    std::set<core::StageSlotId> assignedSlotIds;
    std::set<core::SlideId> assignedSlideIds;
    for(const auto& assignment:assignments) {
        if(configuredSlotIds.find(assignment.stageSlotId)==configuredSlotIds.end())
            return core::Result<StageSlotAssignments,StageSlotAssignmentsError>::failure(
                StageSlotAssignmentsError::SlotNotInConfiguration);
        if(assignment.slideRevision==0)
            return core::Result<StageSlotAssignments,StageSlotAssignmentsError>::failure(
                StageSlotAssignmentsError::ZeroSlideRevision);
        if(!assignedSlotIds.insert(assignment.stageSlotId).second)
            return core::Result<StageSlotAssignments,StageSlotAssignmentsError>::failure(
                StageSlotAssignmentsError::DuplicateStageSlotAssignment);
        if(!assignedSlideIds.insert(assignment.slideId).second)
            return core::Result<StageSlotAssignments,StageSlotAssignmentsError>::failure(
                StageSlotAssignmentsError::SlideAssignedToMultipleSlots);
    }

    return core::Result<StageSlotAssignments,StageSlotAssignmentsError>::success(
        StageSlotAssignments(std::move(stageComponentId),equipmentDefinitionRevision,
            configuration.id(),std::move(assignments)));
}

const core::EquipmentComponentId&
StageSlotAssignments::stageComponentId() const noexcept {
    return stageComponentId_;
}
std::uint64_t StageSlotAssignments::equipmentDefinitionRevision() const noexcept {
    return equipmentDefinitionRevision_;
}
const core::StageConfigurationId&
StageSlotAssignments::configurationId() const noexcept {
    return configurationId_;
}
const std::vector<StageSlotAssignment>&
StageSlotAssignments::assignments() const noexcept {
    return assignments_;
}
const StageSlotAssignment* StageSlotAssignments::forSlot(
    const core::StageSlotId& slotId) const noexcept {
    const auto it=std::find_if(assignments_.begin(),assignments_.end(),
        [&slotId](const StageSlotAssignment& item){return item.stageSlotId==slotId;});
    return it==assignments_.end()?nullptr:&*it;
}
const StageSlotAssignment* StageSlotAssignments::forSlide(
    const core::SlideId& slideId) const noexcept {
    const auto it=std::find_if(assignments_.begin(),assignments_.end(),
        [&slideId](const StageSlotAssignment& item){return item.slideId==slideId;});
    return it==assignments_.end()?nullptr:&*it;
}

} // namespace microscope::application::navigation
