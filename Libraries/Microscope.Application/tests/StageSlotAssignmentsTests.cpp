#include "microscope/application/navigation/StageSlotAssignments.h"

#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using namespace microscope::core;
using namespace microscope::application::navigation;

int failures=0;
void check(bool value,const char* message) {
    if(!value) { ++failures; std::cerr<<"FAIL: "<<message<<'\n'; }
}
template<class T,class E>
T take(Result<T,E> result) {
    if(!result) throw std::runtime_error("Stage-slot assignment fixture failed.");
    return std::move(result).value();
}
StageAxisId axisId(const char* value){return take(StageAxisId::create(value));}
StageSlotId slotId(const char* value){return take(StageSlotId::create(value));}
StageConfigurationId configurationId(const char* value){
    return take(StageConfigurationId::create(value));
}
EquipmentComponentId componentId(const char* value){
    return take(EquipmentComponentId::create(value));
}
SlideId slideId(const char* value){return take(SlideId::create(value));}
StageNanometreRange nmRange(std::int64_t a,std::int64_t b){
    return take(StageNanometreRange::create(a,b));
}

StageConfigurationSpecification makeTenSlotConfiguration() {
    const auto x=axisId("x"),y=axisId("y"),z=axisId("z");
    std::vector<StageSlotSpecification> slots;
    for(int i=0;i<10;++i) {
        const auto name="slot-"+std::to_string(i+1);
        slots.push_back(take(StageSlotSpecification::create(
            slotId(name.c_str()),
            {static_cast<std::int64_t>(i)*80'000'000,0},
            nmRange(0,75'000'000),nmRange(0,25'000'000))));
    }
    return take(StageConfigurationSpecification::create(
        configurationId("ten-slots"),
        {{x,nmRange(0,900'000'000)},
         {y,nmRange(0,300'000'000)},
         {z,nmRange(0,100'000'000)}},
        {},std::move(slots)));
}

void testGenericSparseAssignmentsAndIdentityLookups() {
    const auto configuration=makeTenSlotConfiguration();
    const auto first=slotId("slot-1");
    const auto fourth=slotId("slot-4");
    const auto slideA=slideId("slide-A");
    const auto slideB=slideId("slide-B");
    auto assignments=take(StageSlotAssignments::create(
        componentId("stage-main"),17,configuration,
        {{first,slideA,3},{fourth,slideB,8}}));

    check(assignments.stageComponentId()==componentId("stage-main")&&
          assignments.equipmentDefinitionRevision()==17&&
          assignments.configurationId()==configuration.id(),
        "assignment snapshot is scoped to one Stage equipment revision and configuration");
    check(assignments.assignments().size()==2,
        "an N-slot holder may have a sparse collection of occupied slot assignments");
    check(assignments.forSlot(first)&&assignments.forSlot(first)->slideId==slideA&&
          assignments.forSlot(first)->slideRevision==3,
        "resolves an occupied StageSlot to the exact Slide revision");
    check(assignments.forSlide(slideB)&&assignments.forSlide(slideB)->stageSlotId==fourth,
        "resolves a Slide identity back to its physical StageSlot");
    check(assignments.forSlot(slotId("slot-2"))==nullptr,
        "an unoccupied slot is represented by absence, not a synthetic Slide");
}

void testRejectsInvalidAndAmbiguousAssignments() {
    const auto configuration=makeTenSlotConfiguration();
    const auto s1=slotId("slot-1"),s2=slotId("slot-2");
    const auto a=slideId("slide-A"),b=slideId("slide-B");

    auto zeroEquipment=StageSlotAssignments::create(
        componentId("stage-main"),0,configuration,{});
    check(!zeroEquipment&&zeroEquipment.error()==StageSlotAssignmentsError::ZeroEquipmentRevision,
        "rejects an assignment snapshot without an equipment revision");

    auto unknownSlot=StageSlotAssignments::create(
        componentId("stage-main"),17,configuration,
        {{slotId("slot-11"),a,1}});
    check(!unknownSlot&&unknownSlot.error()==StageSlotAssignmentsError::SlotNotInConfiguration,
        "cannot assign a Slide to a StageSlot absent from the active equipment configuration");

    auto zeroSlideRevision=StageSlotAssignments::create(
        componentId("stage-main"),17,configuration,{{s1,a,0}});
    check(!zeroSlideRevision&&
          zeroSlideRevision.error()==StageSlotAssignmentsError::ZeroSlideRevision,
        "pins only positive immutable Slide revisions");

    auto duplicateSlot=StageSlotAssignments::create(
        componentId("stage-main"),17,configuration,{{s1,a,1},{s1,b,1}});
    check(!duplicateSlot&&
          duplicateSlot.error()==StageSlotAssignmentsError::DuplicateStageSlotAssignment,
        "one StageSlot cannot contain two Slide assignments in one snapshot");

    auto duplicateSlide=StageSlotAssignments::create(
        componentId("stage-main"),17,configuration,{{s1,a,1},{s2,a,2}});
    check(!duplicateSlide&&
          duplicateSlide.error()==StageSlotAssignmentsError::SlideAssignedToMultipleSlots,
        "one physical Slide cannot be assigned to two StageSlots at once");

    const auto geometry=take(SlideGeometry::create(75'000.0,25'000.0));
    const std::array<StageSlotCornerMeasurement,4> corners{
        take(StageSlotCornerMeasurement::create(StageSlotRegistrationCorner::TopLeft,
            {0,0},ObservationSource::HardwareReadback,1)),
        take(StageSlotCornerMeasurement::create(StageSlotRegistrationCorner::TopRight,
            {75'000'000,0},ObservationSource::HardwareReadback,1)),
        take(StageSlotCornerMeasurement::create(StageSlotRegistrationCorner::BottomRight,
            {75'000'000,25'000'000},ObservationSource::HardwareReadback,1)),
        take(StageSlotCornerMeasurement::create(StageSlotRegistrationCorner::BottomLeft,
            {0,25'000'000},ObservationSource::HardwareReadback,1))};
    const auto registration=take(StageSlotRegistration::create(
        s1,a,1,makeId<CalibrationRevisionId>("assignment-calibration"),
        geometry,corners,1));
    auto matchingRegistration=StageSlotAssignments::create(
        componentId("stage-main"),17,configuration,{{s1,a,1,registration}});
    check(static_cast<bool>(matchingRegistration),
        "accepts registration whose StageSlot, Slide and immutable revision match the assignment");
    auto wrongSlideRegistration=StageSlotRegistration::create(
        s1,b,1,makeId<CalibrationRevisionId>("wrong-slide-calibration"),
        geometry,corners,1);
    check(static_cast<bool>(wrongSlideRegistration),"constructs a distinct Slide registration fixture");
    if(wrongSlideRegistration) {
        auto rejected=StageSlotAssignments::create(
            componentId("stage-main"),17,configuration,
            {{s1,a,1,std::move(wrongSlideRegistration).value()}});
        check(!rejected&&rejected.error()==StageSlotAssignmentsError::RegistrationSlideMismatch,
            "does not attach a different Slide's calibration to an assignment");
    }
    auto wrongRevisionRegistration=StageSlotRegistration::create(
        s1,a,2,makeId<CalibrationRevisionId>("wrong-slide-revision-calibration"),
        geometry,corners,1);
    check(static_cast<bool>(wrongRevisionRegistration),"constructs a newer Slide revision registration fixture");
    if(wrongRevisionRegistration) {
        auto rejected=StageSlotAssignments::create(
            componentId("stage-main"),17,configuration,
            {{s1,a,1,std::move(wrongRevisionRegistration).value()}});
        check(!rejected&&rejected.error()==StageSlotAssignmentsError::RegistrationSlideRevisionMismatch,
            "does not reuse calibration measured against another Slide revision");
    }
}
}

int main() {
    testGenericSparseAssignmentsAndIdentityLookups();
    testRejectsInvalidAndAmbiguousAssignments();
    if(failures) {
        std::cerr<<failures<<" StageSlotAssignments test(s) failed.\n";
        return 1;
    }
    std::cout<<"All StageSlotAssignments tests passed.\n";
    return 0;
}
