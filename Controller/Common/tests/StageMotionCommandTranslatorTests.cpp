#include "controller/stage/StageMotionCommandTranslator.h"

#include "microscope/core/equipment/OpticalSpecifications.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using namespace microscope;
using namespace microscope::controller;
int failures=0;
void check(bool value,const char* message) {
    if(!value) { ++failures; std::cerr<<"FAIL: "<<message<<'\n'; }
}
template<class T,class E>
T take(core::Result<T,E> result) {
    if(!result) throw std::runtime_error("Unexpected test fixture factory failure.");
    return std::move(result).value();
}
core::StageAxisId axisId(const char* value) { return take(core::StageAxisId::create(value)); }
core::StageConfigurationId configId(const char* value) {
    return take(core::StageConfigurationId::create(value));
}
core::StageSlotId slotId(const char* value) { return take(core::StageSlotId::create(value)); }
core::EquipmentComponentId componentId(const char* value) {
    return take(core::EquipmentComponentId::create(value));
}
NanometreRange nmRange(Nanometres a,Nanometres b) {
    return take(NanometreRange::create(a,b));
}
RadianRange radRange(Radians a,Radians b) { return take(RadianRange::create(a,b)); }
core::StageMotionActionCompletion actionCompletion(const StageMotionPlan& plan) {
    std::vector<core::StageAxisId> axes;
    core::StageMotionAction action=core::StageMotionAction::MoveAxesTo;
    if(plan.action==StageMotionAction::Home) {
        action=core::StageMotionAction::HomeAxes;
        axes=plan.axesToHome;
    } else {
        for(const auto& target:plan.linearTargets) axes.push_back(target.axisId);
        for(const auto& target:plan.rotationTargets) axes.push_back(target.axisId);
    }
    return take(core::StageMotionActionCompletion::create(
        action,take(core::StageAxisSet::create(std::move(axes)))));
}

core::StageHomeReferenceObservation homeReference(const StageMotionPlan& plan) {
    std::vector<core::AxisPositionObservation> positions;
    positions.reserve(plan.axesToHome.size());
    for(const auto& id:plan.axesToHome) {
        const auto unit=id.value().find("theta")!=std::string::npos
            ? core::AxisCoordinateUnit::Radians
            : core::AxisCoordinateUnit::Micrometers;
        auto axisPosition=take(core::AxisPosition::create(id,0.0,unit));
        positions.push_back(take(core::AxisPositionObservation::create(
            std::move(axisPosition),0.0,core::StagePositionEvidence::PhysicalEndstop)));
    }
    return take(core::StageHomeReferenceObservation::create(std::move(positions)));
}



core::AxisSpecification equipmentAxis(
    core::StageAxisId id,core::AxisMotionType type,core::AxisCoordinateUnit unit,
    double minimum,double maximum) {
    auto range=take(core::AxisOperatingRange::create(minimum,maximum,unit));
    auto resolution=take(core::PositiveAxisResolution::create(0.1));
    return take(core::AxisSpecification::create(
        std::move(id),type,unit,std::move(range),resolution,
        std::nullopt,std::nullopt,std::nullopt,
        core::AxisPositionFeedbackCapability::None));
}

core::AxisSpecification equipmentAxisFromController(
    const LinearAxis& axis) {
    const auto& range=axis.mechanicalTravelRange();
    auto operating=take(core::AxisOperatingRange::create(
        static_cast<double>(range.minimum())/1'000.0,
        static_cast<double>(range.maximum())/1'000.0,
        core::AxisCoordinateUnit::Micrometers));
    auto resolution=take(core::PositiveAxisResolution::create(0.1));
    const auto role=axis.role()==LinearAxisRole::X?core::StageLinearAxisRole::X:
        axis.role()==LinearAxisRole::Y?core::StageLinearAxisRole::Y:
        core::StageLinearAxisRole::Z;
    return take(core::AxisSpecification::create(
        axis.id(),core::AxisMotionType::Linear,core::AxisCoordinateUnit::Micrometers,
        std::move(operating),resolution,std::nullopt,std::nullopt,std::nullopt,
        core::AxisPositionFeedbackCapability::None,std::nullopt,role));
}
core::AxisSpecification equipmentAxisFromController(
    const RotationAxis& axis) {
    constexpr double degreesPerRadian=57.2957795130823208768;
    const auto& range=axis.mechanicalTravelRange();
    auto operating=take(core::AxisOperatingRange::create(
        range.minimum()*degreesPerRadian,range.maximum()*degreesPerRadian,
        core::AxisCoordinateUnit::Degrees));
    auto resolution=take(core::PositiveAxisResolution::create(0.01));
    const auto direction=axis.geometry().direction==RotationAxisDirection::X
        ?core::StageRotationAxisDirection::X
        :axis.geometry().direction==RotationAxisDirection::Y
            ?core::StageRotationAxisDirection::Y:core::StageRotationAxisDirection::Z;
    const auto& point=axis.geometry().pointOnAxis;
    return take(core::AxisSpecification::create(
        axis.id(),core::AxisMotionType::Rotary,core::AxisCoordinateUnit::Degrees,
        std::move(operating),resolution,std::nullopt,std::nullopt,std::nullopt,
        core::AxisPositionFeedbackCapability::None,std::nullopt,std::nullopt,
        core::StageRotationAxisGeometry{{point.x,point.y,point.z},direction}));
}
core::StageSpecification equipmentStageFromController(
    const StageSpecification& source,bool changeFirstSlotWidth=false) {
    std::vector<core::AxisSpecification> axes;
    for(const auto& axis:source.linearAxes())
        axes.push_back(equipmentAxisFromController(axis));
    for(const auto& axis:source.rotationAxes())
        axes.push_back(equipmentAxisFromController(axis));

    std::vector<core::StageConfigurationSpecification> configurations;
    for(const auto& config:source.configurations()) {
        std::vector<core::StageWorkingLinearAxisSpecification> linear;
        std::vector<core::StageWorkingRotationAxisSpecification> rotary;
        std::vector<core::StageSlotSpecification> slots;
        for(const auto& axis:config.workingLinearAxes()) {
            linear.push_back({axis.axisId,take(core::StageNanometreRange::create(
                axis.effectiveRange.minimum(),axis.effectiveRange.maximum()))});
        }
        for(const auto& axis:config.workingRotationAxes()) {
            rotary.push_back({axis.axisId,take(core::StageRadianRange::create(
                axis.effectiveRange.minimum(),axis.effectiveRange.maximum()))});
        }
        for(std::size_t index=0;index<config.slots().size();++index) {
            const auto& slot=config.slots()[index];
            auto xMaximum=slot.xRange().maximum();
            if(changeFirstSlotWidth&&config.id()==configId("slides")&&index==0)
                --xMaximum;
            const auto& origin=slot.originInStageCoordinates();
            slots.push_back(take(core::StageSlotSpecification::create(
                slot.id(),{origin.x,origin.y},
                take(core::StageNanometreRange::create(
                    slot.xRange().minimum(),xMaximum)),
                take(core::StageNanometreRange::create(
                    slot.yRange().minimum(),slot.yRange().maximum())))));
        }
        configurations.push_back(take(core::StageConfigurationSpecification::create(
            config.id(),std::move(linear),std::move(rotary),std::move(slots))));
    }
    return take(core::StageSpecification::create(
        source.componentId(),std::move(axes),std::move(configurations)));
}

struct Fixture final {
    Stage stage;
    core::StageSpecification installedStage;
    StageSlotId secondSlot;

    Fixture(Stage s,core::StageSpecification equipment,StageSlotId slot)
        :stage(std::move(s)),installedStage(std::move(equipment)),secondSlot(std::move(slot)){}
};

Fixture makeFixture() {
    const auto xId=axisId("x"),yId=axisId("y"),zId=axisId("z"),thetaId=axisId("theta");
    auto x=take(LinearAxis::create(xId,LinearAxisRole::X,nmRange(0,1'000'000'000),100,1'000,500));
    auto y=take(LinearAxis::create(yId,LinearAxisRole::Y,nmRange(0,500'000'000),100,1'000,500));
    auto z=take(LinearAxis::create(zId,LinearAxisRole::Z,nmRange(0,100'000'000),50,500,250));
    auto theta=take(RotationAxis::create(thetaId,
        RotationAxisGeometry{{0,0,0},RotationAxisDirection::Z},
        radRange(-3.141592653589793,3.141592653589793),0.0001,0.001,0.0002));

    const auto slidesId=configId("slides"),petriId=configId("petri");
    const auto second=slotId("slot-2");
    std::vector<StageSlot> slots;
    for(int i=0;i<10;++i) {
        const auto name="slot-"+std::to_string(i+1);
        slots.push_back(take(StageSlot::create(slotId(name.c_str()),
            {20'000'000+static_cast<Nanometres>(i)*80'000'000,20'000'000},
            nmRange(0,75'000'000),nmRange(0,25'000'000))));
    }
    auto slides=take(StageConfiguration::create(slidesId,
        {{xId,nmRange(0,900'000'000)},{yId,nmRange(0,300'000'000)},
         {zId,nmRange(8'000'000,80'000'000)}},{},std::move(slots)));
    auto petriSlot=take(StageSlot::create(slotId("petri"),{0,0},
        nmRange(0,150'000'000),nmRange(0,150'000'000)));
    auto petri=take(StageConfiguration::create(petriId,
        {{xId,nmRange(0,900'000'000)},{yId,nmRange(0,300'000'000)},
         {zId,nmRange(2'000'000,60'000'000)}},
        {{thetaId,radRange(-1.5707963267948966,1.5707963267948966)}},
        {std::move(petriSlot)}));

    auto controllerSpec=take(StageSpecification::create(
        componentId("stage"),{std::move(x),std::move(y),std::move(z)},
        {std::move(theta)},{std::move(slides),std::move(petri)}));
    auto stage=take(Stage::create(std::move(controllerSpec),slidesId));

    auto installed=take(core::StageSpecification::create(componentId("stage"),{
        equipmentAxis(xId,core::AxisMotionType::Linear,core::AxisCoordinateUnit::Micrometers,0,1'000'000),
        equipmentAxis(yId,core::AxisMotionType::Linear,core::AxisCoordinateUnit::Micrometers,0,500'000),
        equipmentAxis(zId,core::AxisMotionType::Linear,core::AxisCoordinateUnit::Micrometers,0,100'000),
        equipmentAxis(thetaId,core::AxisMotionType::Rotary,core::AxisCoordinateUnit::Degrees,-180,180)
    }));
    return Fixture(std::move(stage),std::move(installed),second);
}

void testHomeTranslationUsesCoreMotionContract() {
    auto fixture=makeFixture();
    auto plan=take(fixture.stage.home());
    auto translated=StageMotionCommandTranslator::translate(plan,fixture.stage,fixture.installedStage);
    check(static_cast<bool>(translated),"translates Controller home plan");
    if(!translated) return;
    check(translated.value().planSequence==plan.sequence,"preserves plan correlation sequence");
    check(std::holds_alternative<core::HomeAxes>(translated.value().command),
        "home plan maps to existing Core HomeAxes command");
    if(const auto* home=std::get_if<core::HomeAxes>(&translated.value().command))
        check(home->axes().axes().size()==3,"home translates the complete configured axis set");
}

void testLinearNanometresConvertToInstalledEquipmentUnit() {
    auto fixture=makeFixture();
    auto home=take(fixture.stage.home());
    check(static_cast<bool>(fixture.stage.recordCompletedHome(home,actionCompletion(home),homeReference(home))),"home plan completion");
    auto plan=take(fixture.stage.moveTo({12'000'000,10'000'000},fixture.secondSlot));
    auto translated=StageMotionCommandTranslator::translate(plan,fixture.stage,fixture.installedStage);
    check(static_cast<bool>(translated),"translates slot motion plan");
    if(!translated) return;
    const auto* move=std::get_if<core::MoveAxesTo>(&translated.value().command);
    check(move!=nullptr,"move plan maps to Core MoveAxesTo");
    if(!move) return;
    check(move->targets().size()==2,"translates the two XY axis targets");
    if(move->targets().size()==2) {
        check(move->targets()[0].unit()==core::AxisCoordinateUnit::Micrometers,
            "uses the equipment-declared coordinate unit");
        check(std::abs(move->targets()[0].value()-112'000.0)<1e-9,
            "converts 112,000,000 nm to 112,000 um");
        check(std::abs(move->targets()[1].value()-30'000.0)<1e-9,
            "converts 30,000,000 nm to 30,000 um");
    }
}

void testRadiansConvertToInstalledDegrees() {
    auto fixture=makeFixture();
    check(static_cast<bool>(fixture.stage.activateConfiguration(configId("petri"))),
        "switches to Petri working configuration");
    auto home=take(fixture.stage.home());
    check(static_cast<bool>(fixture.stage.recordCompletedHome(home,actionCompletion(home),homeReference(home))),"Petri home completion");

    StageAbsolutePosition target;
    target.linear={10'000'000,10'000'000,10'000'000};
    target.rotations.push_back({axisId("theta"),0.25});
    auto plan=take(fixture.stage.moveTo(target));
    auto translated=StageMotionCommandTranslator::translate(plan,fixture.stage,fixture.installedStage);
    check(static_cast<bool>(translated),"translates rotary motion plan");
    if(!translated) return;
    const auto* move=std::get_if<core::MoveAxesTo>(&translated.value().command);
    check(move&&move->targets().size()==4,"linear and rotary targets are combined");
    if(move&&move->targets().size()==4) {
        check(move->targets().back().unit()==core::AxisCoordinateUnit::Degrees,
            "uses the equipment-declared degree unit");
        check(std::abs(move->targets().back().value()-14.32394487827058)<1e-9,
            "converts radians to degrees at the existing Core adapter boundary");
    }
}

void testRejectsControllerEquipmentAxisMismatch() {
    auto fixture=makeFixture();
    auto plan=take(fixture.stage.home());
    const auto missingAxis=take(core::StageSpecification::create(
        componentId("stage"),{
            equipmentAxis(axisId("x"),core::AxisMotionType::Linear,
                core::AxisCoordinateUnit::Micrometers,0,1'000'000)
        }));
    const auto result=StageMotionCommandTranslator::translate(plan,fixture.stage,missingAxis);
    check(!result&&result.error()==StageMotionTranslationError::UnknownEquipmentAxis,
        "rejects plans containing an axis absent from the installed equipment definition");
}

void testCanonicalEquipmentConfigurationMustMatchRuntimeStage() {
    auto fixture=makeFixture();
    auto home=take(fixture.stage.home());
    const auto equipment=equipmentStageFromController(fixture.stage.specification());
    const auto accepted=StageMotionCommandTranslator::translate(
        home,fixture.stage,equipment);
    check(static_cast<bool>(accepted),
        "accepts runtime Stage configuration projected from the same equipment definition");

    const auto altered=equipmentStageFromController(fixture.stage.specification(),true);
    const auto rejected=StageMotionCommandTranslator::translate(
        home,fixture.stage,altered);
    check(!rejected&&
          rejected.error()==StageMotionTranslationError::EquipmentConfigurationMismatch,
        "rejects a persisted equipment slot whose usable width differs from runtime geometry");
    check(static_cast<bool>(fixture.stage.cancelUndispatchedPlan(home)),
        "configuration mismatch leaves the pending Stage plan available for explicit cancellation");
}

void testRejectsMechanicalRangeMismatch() {
    auto fixture=makeFixture();
    auto home=take(fixture.stage.home());
    const auto xId=axisId("x");
    auto changedX=equipmentAxis(
        xId,core::AxisMotionType::Linear,core::AxisCoordinateUnit::Micrometers,0,900'000);
    const auto mismatched=take(core::StageSpecification::create(
        componentId("stage"),{
            std::move(changedX),
            equipmentAxis(axisId("y"),core::AxisMotionType::Linear,
                core::AxisCoordinateUnit::Micrometers,0,500'000),
            equipmentAxis(axisId("z"),core::AxisMotionType::Linear,
                core::AxisCoordinateUnit::Micrometers,0,100'000),
            equipmentAxis(axisId("theta"),core::AxisMotionType::Rotary,
                core::AxisCoordinateUnit::Degrees,-180,180)
        }));
    const auto translated=StageMotionCommandTranslator::translate(
        home,fixture.stage,mismatched);
    check(!translated&&translated.error()==StageMotionTranslationError::MechanicalRangeMismatch,
        "rejects an equipment file whose X mechanical travel differs from the Stage model");
    check(static_cast<bool>(fixture.stage.cancelUndispatchedPlan(home)),
        "mismatched equipment binding does not consume the Stage plan");
}

void testRejectsTamperedPlanBeforeEquipmentTranslation() {
    auto fixture=makeFixture();
    auto home=take(fixture.stage.home());
    auto forged=home;
    forged.axesToHome.pop_back();
    const auto translated=StageMotionCommandTranslator::translate(
        forged,fixture.stage,fixture.installedStage);
    check(!translated&&translated.error()==StageMotionTranslationError::StalePlan,
        "translator rejects a tampered plan with the same sequence number");
    check(static_cast<bool>(fixture.stage.cancelUndispatchedPlan(home)),
        "original unsubmitted plan can be cancelled after forged-plan rejection");
}

} // namespace

int main() {
    testHomeTranslationUsesCoreMotionContract();
    testLinearNanometresConvertToInstalledEquipmentUnit();
    testRadiansConvertToInstalledDegrees();
    testRejectsControllerEquipmentAxisMismatch();
    testCanonicalEquipmentConfigurationMustMatchRuntimeStage();
    testRejectsMechanicalRangeMismatch();
    testRejectsTamperedPlanBeforeEquipmentTranslation();
    if(failures) {
        std::cerr<<failures<<" Stage motion translation tests failed.\n";
        return 1;
    }
    std::cout<<"All Stage motion translation tests passed.\n";
    return 0;
}
