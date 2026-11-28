#include "microscope/application/persistence/StageSlotAssignmentsSqliteStore.h"
#include "microscope/application/persistence/SampleSlideSqliteStore.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace microscope::core;
using namespace microscope::application::navigation;
using namespace microscope::application::persistence;

int failures=0;
void check(bool value,const char* message) {
    if(!value) { ++failures; std::cerr<<"FAIL: "<<message<<'\n'; }
}
template<class T,class E>
T take(Result<T,E> result) {
    if(!result) throw std::runtime_error("StageSlot assignment persistence fixture failed.");
    return std::move(result).value();
}
template<class T>
T makeId(const char* value) { return take(T::create(value)); }

std::string uniqueDatabasePath(const char* prefix) {
    const auto nonce=std::chrono::steady_clock::now().time_since_epoch().count();
    return (std::filesystem::temp_directory_path()/
        (std::string(prefix)+std::to_string(nonce)+".sqlite")).string();
}
void removeDatabaseFiles(const std::string& path) {
    std::error_code ignored;
    std::filesystem::remove(path,ignored);
    std::filesystem::remove(path+"-wal",ignored);
    std::filesystem::remove(path+"-shm",ignored);
}

SampleType makeSampleType() {
    auto requirements=take(MicroscopyRequirements::create({
        {ContrastMethod::Brightfield,MicroscopyTechniqueConstraintKind::Required}}));
    return take(SampleType::create(
        makeId<SampleTypeId>("assignment-test-sample-type"),1,
        "Assignment test sample",SampleMaterialClass::Biological,
        std::move(requirements)));
}

Slide makeSlide(const SlideId& id,std::uint64_t revision,const SampleType& sampleType) {
    auto preparation=take(SlidePreparation::create(
        SpecimenPreparationMethod::Smear,SpecimenFixationState::Fixed));
    return take(Slide::create(
        id,revision,sampleType.id(),sampleType.revision(),
        "CODE-"+id.value(),"Assignment test slide",
        take(SlideGeometry::create(75'000.0,25'000.0)),
        std::move(preparation)));
}

StageConfigurationSpecification makeStageConfiguration() {
    const auto xId=makeId<StageAxisId>("assignment-stage-x");
    const auto yId=makeId<StageAxisId>("assignment-stage-y");
    const auto zId=makeId<StageAxisId>("assignment-stage-z");
    std::vector<StageSlotSpecification> slots;
    for(int i=0;i<10;++i) {
        const auto name="assignment-slot-"+std::to_string(i+1);
        slots.push_back(take(StageSlotSpecification::create(
            makeId<StageSlotId>(name.c_str()),
            {static_cast<std::int64_t>(i)*80'000'000,0},
            take(StageNanometreRange::create(0,75'000'000)),
            take(StageNanometreRange::create(0,25'000'000)))));
    }
    return take(StageConfigurationSpecification::create(
        makeId<StageConfigurationId>("assignment-slides"),
        {{xId,take(StageNanometreRange::create(0,900'000'000))},
         {yId,take(StageNanometreRange::create(0,300'000'000))},
         {zId,take(StageNanometreRange::create(0,100'000'000))}},
        {},std::move(slots)));
}

StageSpecification makeInstalledStage(const StageConfigurationSpecification& config) {
    const auto makeLinear=[](const char* id,StageLinearAxisRole role,double maximum) {
        auto operating=take(AxisOperatingRange::create(
            0,maximum,AxisCoordinateUnit::Micrometers));
        return take(AxisSpecification::create(
            makeId<StageAxisId>(id),AxisMotionType::Linear,
            AxisCoordinateUnit::Micrometers,std::move(operating),
            take(PositiveAxisResolution::create(0.1)),
            std::nullopt,std::nullopt,std::nullopt,
            AxisPositionFeedbackCapability::None,std::nullopt,role));
    };
    return take(StageSpecification::create(
        makeId<EquipmentComponentId>("assignment-stage-component"),
        {makeLinear("assignment-stage-x",StageLinearAxisRole::X,900'000),
         makeLinear("assignment-stage-y",StageLinearAxisRole::Y,300'000),
         makeLinear("assignment-stage-z",StageLinearAxisRole::Z,100'000)},
        {config}));
}

void testAppendOnlyNSlotAssignmentPersistenceAndSlideRevisionPinning() {
    const auto path=uniqueDatabasePath("microscope-stage-slot-assignments-");
    const auto microscopeId=makeId<MicroscopeId>("assignment-test-microscope");
    const auto slideAId=makeId<SlideId>("assignment-slide-A");
    const auto slideBId=makeId<SlideId>("assignment-slide-B");
    const auto slideA2Id=slideAId;
    const auto stageConfig=makeStageConfiguration();
    const auto installedStage=makeInstalledStage(stageConfig);
    const auto stageComponent=installedStage.componentId();
    const auto slot1=makeId<StageSlotId>("assignment-slot-1");
    const auto slot4=makeId<StageSlotId>("assignment-slot-4");
    {
        auto sampleOpened=SampleSlideSqliteStore::open(path);
        check(static_cast<bool>(sampleOpened),
            "opens SampleSlideSqliteStore first to provide immutable Slide revision tables");
        if(!sampleOpened) { removeDatabaseFiles(path); return; }
        auto sampleStore=std::move(sampleOpened).value();
        const auto sampleType=makeSampleType();
        check(sampleStore->saveSampleTypeRevision(sampleType,0).isSuccess(),
            "persists the SampleType required by Slide foreign keys");
        const auto slideA1=makeSlide(slideAId,1,sampleType);
        const auto slideB1=makeSlide(slideBId,1,sampleType);
        check(sampleStore->saveSlideRevision(slideA1,0).isSuccess()&&
              sampleStore->saveSlideRevision(slideB1,0).isSuccess(),
            "persists the immutable Slide revisions referenced by StageSlot assignments");

        auto assignmentsOpened=StageSlotAssignmentsSqliteStore::open(path);
        check(static_cast<bool>(assignmentsOpened),
            "opens the N-slot assignment store over the existing SampleSlide database");
        if(!assignmentsOpened) { removeDatabaseFiles(path); return; }
        auto assignmentsStore=std::move(assignmentsOpened).value();

        auto initial=take(StageSlotAssignments::create(
            stageComponent,12,stageConfig,
            {{slot1,slideAId,1},{slot4,slideBId,1}}));
        check(assignmentsStore->saveCurrentState(
            microscopeId,installedStage,initial,{slideA1,slideB1},0).isSuccess(),
            "creates assignment revision one using expected revision zero");

        auto loaded1=assignmentsStore->loadCurrentState(
            microscopeId,12,installedStage,stageConfig.id());
        check(loaded1&&loaded1.value().has_value()&&loaded1.value()->revision==1,
            "loads the current StageSlot assignment revision");
        if(loaded1&&loaded1.value()) {
            const auto* assignment=loaded1.value()->assignments.forSlot(slot1);
            check(assignment&&assignment->slideId==slideAId&&assignment->slideRevision==1,
                "round-trips the StageSlot to exact Slide identity and revision");
            check(loaded1.value()->assignments.assignments().size()==2&&
                  loaded1.value()->assignments.forSlot(
                      makeId<StageSlotId>("assignment-slot-2"))==nullptr,
                "persists sparse occupancy across a ten-slot holder");
        }

        const auto slideA2=makeSlide(slideA2Id,2,sampleType);
        check(sampleStore->saveSlideRevision(slideA2,1).isSuccess(),
            "appends a new immutable Slide revision");
        auto updated=take(StageSlotAssignments::create(
            stageComponent,12,stageConfig,
            {{slot1,slideAId,2},{slot4,slideBId,1}}));
        check(assignmentsStore->saveCurrentState(
            microscopeId,installedStage,updated,{slideA2,slideB1},1).isSuccess(),
            "appends assignment revision two pinned to the new Slide revision");

        auto historical=assignmentsStore->loadRevision(
            microscopeId,12,installedStage,stageConfig.id(),1);
        check(historical&&historical.value()&&historical.value()->revision==1&&
              historical.value()->assignments.forSlide(slideAId)->slideRevision==1,
            "historical assignment revision retains its original Slide revision pin");

        auto current=assignmentsStore->loadCurrentState(
            microscopeId,12,installedStage,stageConfig.id());
        check(current&&current.value()&&current.value()->revision==2&&
              current.value()->assignments.forSlide(slideAId)->slideRevision==2,
            "current assignment state resolves the latest immutable Slide revision");

        const auto staleSave=assignmentsStore->saveCurrentState(
            microscopeId,installedStage,initial,{slideA1,slideB1},2);
        check(!staleSave.isSuccess()&&
              staleSave.error().code==StageSlotAssignmentsStoreErrorCode::SlideRevisionMismatch,
            "rejects assignment writes that pin a Slide older than its persisted current revision");

        const auto conflictSave=assignmentsStore->saveCurrentState(
            microscopeId,installedStage,updated,{slideA2,slideB1},0);
        check(!conflictSave.isSuccess()&&
              conflictSave.error().code==StageSlotAssignmentsStoreErrorCode::RevisionConflict,
            "rejects stale optimistic assignment revision writers");

        const auto absent=assignmentsStore->loadRevision(
            microscopeId,12,installedStage,stageConfig.id(),99);
        check(absent&&!absent.value(),
            "returns an empty optional for an unknown assignment revision");

        const auto wrongConfig=assignmentsStore->loadCurrentState(
            microscopeId,12,installedStage,makeId<StageConfigurationId>("missing-config"));
        check(!wrongConfig&&
              wrongConfig.error().code==StageSlotAssignmentsStoreErrorCode::EquipmentBindingMismatch,
            "does not reinterpret assignments under a configuration absent from the installed Stage");
    }
    removeDatabaseFiles(path);
}

void testRequiresSampleSlideSchema() {
    const auto path=uniqueDatabasePath("microscope-stage-slot-assignments-no-slides-");
    {
        auto opened=StageSlotAssignmentsSqliteStore::open(path);
        check(!opened&&
              opened.error().code==StageSlotAssignmentsStoreErrorCode::SampleSlideStoreSchemaRequired,
            "requires the existing SampleSlide schema instead of creating a second Slide store");
    }
    removeDatabaseFiles(path);
}
}

int main() {
    testAppendOnlyNSlotAssignmentPersistenceAndSlideRevisionPinning();
    testRequiresSampleSlideSchema();
    if(failures) {
        std::cerr<<failures<<" StageSlot assignment SQLite test(s) failed.\n";
        return 1;
    }
    std::cout<<"All StageSlot assignment SQLite tests passed.\n";
    return 0;
}
