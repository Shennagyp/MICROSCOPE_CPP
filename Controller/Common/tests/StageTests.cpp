#include "controller/stage/Stage.h"
#include "controller/MicroscopeInstance.h"
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
    if(!result) throw std::runtime_error("Unexpected factory failure.");
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
NanometreRange nmRange(Nanometres min,Nanometres max) {
    return take(NanometreRange::create(min,max));
}
RadianRange radRange(Radians min,Radians max) {
    return take(RadianRange::create(min,max));
}
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



StageSpecification makeSpecification(
    StageConfigurationId& slides,StageConfigurationId& petri,StageSlotId& secondSlot) {
    const auto xId=axisId("stage-x"),yId=axisId("stage-y");
    const auto zId=axisId("stage-z"),thetaId=axisId("stage-theta");
    auto x=take(LinearAxis::create(xId,LinearAxisRole::X,
        nmRange(0,1'000'000'000),100,2'000,500));
    auto y=take(LinearAxis::create(yId,LinearAxisRole::Y,
        nmRange(0,500'000'000),100,2'000,500));
    auto z=take(LinearAxis::create(zId,LinearAxisRole::Z,
        nmRange(0,100'000'000),50,1'000,250));
    auto theta=take(RotationAxis::create(thetaId,
        RotationAxisGeometry{{10'000,20'000,30'000},RotationAxisDirection::Z},
        radRange(-3.141592653589793,3.141592653589793),
        0.0001,0.001,0.0002));

    slides=configId("slides-holder");petri=configId("petri-holder");
    secondSlot=slotId("slide-slot-2");
    std::vector<StageSlot> slots;
    for(int i=0;i<10;++i) {
        const auto name="slide-slot-"+std::to_string(i+1);
        slots.push_back(take(StageSlot::create(slotId(name.c_str()),
            {20'000'000+static_cast<Nanometres>(i)*80'000'000,20'000'000},
            nmRange(0,75'000'000),nmRange(0,25'000'000))));
    }
    auto slideConfig=take(StageConfiguration::create(slides,
        {{xId,nmRange(0,900'000'000)},
         {yId,nmRange(0,300'000'000)},
         {zId,nmRange(8'000'000,80'000'000)}},
        {},std::move(slots)));
    auto petriSlot=take(StageSlot::create(slotId("petri-slot"),{0,0},
        nmRange(0,150'000'000),nmRange(0,150'000'000)));
    auto petriConfig=take(StageConfiguration::create(petri,
        {{xId,nmRange(0,900'000'000)},
         {yId,nmRange(0,300'000'000)},
         {zId,nmRange(2'000'000,60'000'000)}},
        {{thetaId,radRange(-1.5707963267948966,1.5707963267948966)}},
        {std::move(petriSlot)}));
    return take(StageSpecification::create(componentId("microscope-stage"),
        {std::move(x),std::move(y),std::move(z)},{std::move(theta)},
        {std::move(slideConfig),std::move(petriConfig)}));
}

void testGenericConfigurationAndAxisCommandConstruction() {
    StageConfigurationId slides=configId("tmp-slides"),petri=configId("tmp-petri");
    StageSlotId second=slotId("tmp-slot");
    auto spec=makeSpecification(slides,petri,second);
    check(spec.configurations().size()==2,"one Stage definition contains multiple configurations");
    check(spec.configuration(slides)->slots().size()==10,"supports ten generic StageSlots");
    check(spec.configuration(petri)->slots().size()==1,"Petri holder is one generic StageSlot");
    check(spec.configuration(slides)->workingRotationAxes().empty(),"rotation is optional per configuration");
    check(spec.configuration(petri)->workingRotationAxes().size()==1,"another config enables rotation");
    check(spec.configuration(slides)->workingLinearAxes()[2].effectiveRange.minimum()==8'000'000,
        "Z working range is configuration-specific");

    auto duplicateX=take(LinearAxis::create(
        axisId("duplicate-stage-x"),LinearAxisRole::X,nmRange(0,1'000'000'000),
        100,2'000,500));
    auto duplicateAxes=spec.linearAxes();
    duplicateAxes.push_back(std::move(duplicateX));
    auto duplicateRoleDefinition=StageSpecification::create(
        componentId("duplicate-role-stage"),std::move(duplicateAxes),spec.rotationAxes(),
        {*spec.configuration(slides)});
    check(!duplicateRoleDefinition&&
          duplicateRoleDefinition.error()==StageModelError::DuplicateLinearAxisRole,
        "a physical Stage cannot contain two linear axes assigned the same X/Y/Z role");

    const auto* x=spec.linearAxis(LinearAxisRole::X);
    check(x&&x->homeAxisId()==axisId("stage-x"),"LinearAxis builds its own home intent");
    auto absolute=x->moveTo(5'000'000);
    auto relative=x->moveBy(5'000'000,2'000);
    check(absolute&&absolute.value().position==5'000'000,"LinearAxis uses nanometre absolute targets");
    check(relative&&relative.value().position==5'002'000,"LinearAxis uses nanometre relative targets");
    check(!x->moveTo(2'000'000'000),"LinearAxis rejects targets beyond mechanical travel");
    const auto measuredX=take(LinearAxis::create(
        axisId("measured-x"),LinearAxisRole::X,nmRange(0,100'000),10,100,25,40));
    check(measuredX.accuracy()==std::optional<Nanometres>(100)&&
          measuredX.precision()==std::optional<Nanometres>(25)&&
          measuredX.measurementUncertainty()==std::optional<Nanometres>(40),
        "linear accuracy, precision and measurement uncertainty remain independent");
    check(!LinearAxis::create(
        axisId("invalid-uncertainty-x"),LinearAxisRole::X,nmRange(0,100'000),10,100,25,-1),
        "linear measurement uncertainty cannot be negative");

    const auto* theta=spec.rotationAxis(axisId("stage-theta"));
    check(theta&&theta->homeAxisId()==axisId("stage-theta"),"RotationAxis builds its own home intent");
    auto angularAbsolute=theta->rotateTo(0.25);
    auto angularRelative=theta->rotateBy(0.25,0.1);
    check(angularAbsolute&&std::abs(angularAbsolute.value().angle-0.25)<1e-12,
        "RotationAxis uses radian absolute targets");
    check(angularRelative&&std::abs(angularRelative.value().angle-0.35)<1e-12,
        "RotationAxis uses radian relative targets");
    const auto measuredTheta=take(RotationAxis::create(
        axisId("measured-theta"),RotationAxisGeometry{{0,0,0},RotationAxisDirection::Z},
        radRange(-1.0,1.0),0.001,0.01,0.002,0.0005));
    check(measuredTheta.accuracy()==std::optional<Radians>(0.01)&&
          measuredTheta.precision()==std::optional<Radians>(0.002)&&
          measuredTheta.measurementUncertainty()==std::optional<Radians>(0.0005),
        "rotary accuracy, precision and measurement uncertainty remain independent");
    check(!RotationAxis::create(
        axisId("invalid-uncertainty-theta"),
        RotationAxisGeometry{{0,0,0},RotationAxisDirection::Z},
        radRange(-1.0,1.0),0.001,0.01,0.002,-0.0005),
        "rotary measurement uncertainty cannot be negative");
    const auto unspecifiedAxis=take(LinearAxis::create(
        axisId("unspecified-axis-quality"),LinearAxisRole::X,
        nmRange(0,100'000),10,std::nullopt,std::nullopt));
    check(!unspecifiedAxis.accuracy()&&!unspecifiedAxis.precision()&&
          !unspecifiedAxis.measurementUncertainty(),
        "unspecified accuracy, precision and measurement uncertainty remain absent");
}

void testHomeRequiresExactCompletionAndReferenceAxes() {
    StageConfigurationId slides=configId("slides");
    StageConfigurationId petri=configId("petri");
    StageSlotId second=slotId("slide-slot-2");
    auto spec=makeSpecification(slides,petri,second);
    auto stage=take(Stage::create(std::move(spec),slides));
    auto home=take(stage.home());

    const auto wrongAction=take(core::StageMotionActionCompletion::create(
        core::StageMotionAction::MoveAxesTo,
        take(core::StageAxisSet::create(home.axesToHome))));
    const auto correctReference=homeReference(home);
    check(!stage.recordCompletedHome(home,wrongAction,correctReference),
        "a MoveAxesTo completion cannot establish the Stage home reference");
    check(!stage.currentPosition().xyzKnown(),
        "rejected completion leaves the position unknown");

    auto wrongAxes=home.axesToHome;
    wrongAxes.pop_back();
    const auto partialCompletion=take(core::StageMotionActionCompletion::create(
        core::StageMotionAction::HomeAxes,
        take(core::StageAxisSet::create(std::move(wrongAxes)))));
    check(!stage.recordCompletedHome(home,partialCompletion,correctReference),
        "partial axis completion cannot establish a complete XYZ reference");

    std::vector<core::AxisPositionObservation> wrongUnitPositions;
    for(const auto& id:home.axesToHome) {
        const auto unit=id==axisId("stage-x")
            ? core::AxisCoordinateUnit::Radians
            : core::AxisCoordinateUnit::Micrometers;
        auto position=take(core::AxisPosition::create(id,0.0,unit));
        wrongUnitPositions.push_back(take(core::AxisPositionObservation::create(
            std::move(position),0.0,core::StagePositionEvidence::PhysicalEndstop)));
    }
    const auto wrongUnitReference=take(
        core::StageHomeReferenceObservation::create(std::move(wrongUnitPositions)));
    const auto correctCompletion=actionCompletion(home);
    check(!stage.recordCompletedHome(home,correctCompletion,wrongUnitReference),
        "a linear axis cannot accept a rotary coordinate unit as home evidence");
    check(static_cast<bool>(stage.recordCompletedHome(
        home,correctCompletion,correctReference)),
        "the same pending home plan can complete with exact action and complete reference evidence");
    check(stage.currentPosition().xyzKnown(),
        "only complete home evidence establishes the XYZ reference");
}

void testCorrelatedMotionPlansAndCurrentSlot() {
    StageConfigurationId slides=configId("slides"),petri=configId("petri");
    StageSlotId second=slotId("slide-slot-2");
    auto spec=makeSpecification(slides,petri,second);
    auto stage=take(Stage::create(std::move(spec),slides));

    auto home=take(stage.home());
    check(home.action==StageMotionAction::Home&&home.axesToHome.size()==3,
        "home plan includes every active linear axis");
    check(!stage.home(),"does not create a second plan while one is pending");
    check(static_cast<bool>(stage.recordCompletedHome(home,actionCompletion(home),homeReference(home))),
        "home state is recorded only after correlated completion");

    auto move=take(stage.moveTo({12'000'000,10'000'000},second));
    check(move.action==StageMotionAction::MoveTo&&move.linearTargets.size()==2,
        "slot-relative move produces grouped XY motion");
    check(move.linearTargets[0].position==112'000'000&&
          move.linearTargets[1].position==30'000'000,
        "translates the local slot point into global stage coordinates");
    check(!stage.currentSlotId().has_value(),
        "creating a motion plan does not optimistically change current position");
    check(static_cast<bool>(stage.recordCompletedMotion(move,actionCompletion(move))),
        "matching completion updates the command-derived position");
    check(stage.currentSlotId()==second,"current slot is derived from global XY position");
    check(!stage.recordCompletedMotion(move,actionCompletion(move)),"duplicate completion is rejected");
    check(!stage.moveTo({76'000'000,1},second),"rejects movement outside slot usable range");

    auto touchingSlot=take(StageSlot::create(
        slotId("touching-slot"),{95'000'000,20'000'000},
        nmRange(0,75'000'000),nmRange(0,25'000'000)));
    auto touchingDefinition=StageConfiguration::create(
        configId("touching-slots"),
        {{axisId("stage-x"),nmRange(0,1'000'000'000)},
         {axisId("stage-y"),nmRange(0,500'000'000)},
         {axisId("stage-z"),nmRange(0,100'000'000)}},{},
        {take(StageSlot::create(slotId("first-slot"),{20'000'000,20'000'000},
            nmRange(0,75'000'000),nmRange(0,25'000'000))),std::move(touchingSlot)});
    check(!touchingDefinition&&touchingDefinition.error()==StageModelError::OverlappingSlots,
        "runtime configuration rejects slots whose inclusive rectangles share an edge");
}

void testFailedMotionInvalidatesPosition() {
    StageConfigurationId slides=configId("slides"),petri=configId("petri");
    StageSlotId second=slotId("slide-slot-2");
    auto spec=makeSpecification(slides,petri,second);
    auto stage=take(Stage::create(std::move(spec),slides));

    auto home=take(stage.home());
    check(static_cast<bool>(stage.recordCompletedHome(home,actionCompletion(home),homeReference(home))),"initial home completes");
    auto move=take(stage.moveTo({10'000'000,10'000'000},second));
    check(static_cast<bool>(stage.recordFailedMotion(move)),
        "failed or indeterminate motion is reconciled explicitly");
    check(!stage.currentPosition().xyKnown(),
        "failed motion invalidates position rather than assuming rollback");
    check(!stage.recordFailedMotion(move),"duplicate failure is rejected");
    check(!stage.moveTo({1,1},second),"slot move is blocked while XY reference is unknown");
}

void testRefreshPositionHomeAndRestoreWorkflow() {
    StageConfigurationId slides=configId("slides"),petri=configId("petri");
    StageSlotId second=slotId("slide-slot-2");
    auto spec=makeSpecification(slides,petri,second);
    auto stage=take(Stage::create(std::move(spec),slides));

    auto home=take(stage.home());
    check(static_cast<bool>(stage.recordCompletedHome(home,actionCompletion(home),homeReference(home))),"initial home completes");
    auto xy=take(stage.moveTo({4'000'000,5'000'000},second));
    check(static_cast<bool>(stage.recordCompletedMotion(xy,actionCompletion(xy))),"slot XY movement completes");

    StageAbsolutePosition target;
    target.linear={104'000'000,25'000'000,12'000'000};
    auto absolute=take(stage.moveTo(target));
    check(static_cast<bool>(stage.recordCompletedMotion(absolute,actionCompletion(absolute))),"absolute XYZ movement completes");

    auto refresh=take(stage.refreshPosition());
    check(refresh.homePlan.action==StageMotionAction::Home,"refresh begins with a home plan");
    check(refresh.restorePosition.linear.x==104'000'000&&
          refresh.restorePosition.linear.y==25'000'000&&
          refresh.restorePosition.linear.z==12'000'000,
        "refresh saves the exact global position");
    check(static_cast<bool>(stage.recordCompletedHome(refresh.homePlan,actionCompletion(refresh.homePlan),homeReference(refresh.homePlan))),
        "refresh home completion is recorded");
    auto restore=take(stage.restorePositionAfterRefresh(refresh));
    check(restore.refreshRestoration&&restore.linearTargets.size()==3,
        "restore plan is a separate coordinated move");
    check(restore.linearTargets[0].position==104'000'000&&
          restore.linearTargets[1].position==25'000'000&&
          restore.linearTargets[2].position==12'000'000,
        "restore plan retains the saved global coordinates");
    check(static_cast<bool>(stage.recordCompletedMotion(restore,actionCompletion(restore))),
        "restoration updates state only after completion");
    check(stage.currentPosition().x==104'000'000&&stage.currentPosition().y==25'000'000&&
          stage.currentPosition().z==12'000'000,"refresh ends at the saved position");
}

void testConfigurationActivationAndRotation() {
    StageConfigurationId slides=configId("slides"),petri=configId("petri");
    StageSlotId second=slotId("slide-slot-2");
    auto spec=makeSpecification(slides,petri,second);
    auto stage=take(Stage::create(std::move(spec),slides));
    auto home=take(stage.home());
    check(static_cast<bool>(stage.recordCompletedHome(home,actionCompletion(home),homeReference(home))),"home slide configuration");
    check(static_cast<bool>(stage.activateConfiguration(petri)),
        "activates Petri configuration on same Stage");
    check(stage.currentPosition().xyzKnown(),
        "enabling an optional rotary axis preserves the known global XYZ position");
    check(stage.currentPosition().rotations.empty(),
        "a newly enabled rotary axis remains unknown until that configuration is homed");

    StageAbsolutePosition beforeRotaryHome;
    beforeRotaryHome.linear={10'000'000,10'000'000,10'000'000};
    beforeRotaryHome.rotations.push_back({axisId("stage-theta"),0.25});
    check(!stage.moveTo(beforeRotaryHome),
        "absolute motion waits until every newly enabled rotary axis has a reference");

    auto petriHome=take(stage.home());
    check(petriHome.axesToHome.size()==4,"Petri config homes XYZ plus rotation");
    check(static_cast<bool>(stage.recordCompletedHome(petriHome,actionCompletion(petriHome),homeReference(petriHome))),"Petri home completes");

    StageAbsolutePosition target;
    target.linear={10'000'000,10'000'000,10'000'000};
    target.rotations.push_back({axisId("stage-theta"),0.25});
    auto move=take(stage.moveTo(target));
    check(move.rotationTargets.size()==1&&std::abs(move.rotationTargets[0].angle-0.25)<1e-12,
        "absolute move includes its rotation target in radians");
    check(static_cast<bool>(stage.recordCompletedMotion(move,actionCompletion(move))),"completed rotary move is recorded");

    check(static_cast<bool>(stage.activateConfiguration(slides)),
        "switches back to the slide holder configuration");
    check(stage.currentPosition().xyzKnown()&&stage.currentPosition().rotations.empty(),
        "disabling the optional rotary axis preserves XYZ and drops only its inactive state");
}

void testMicroscopeInstanceOwnsStage() {
    StageConfigurationId slides=configId("slides"),petri=configId("petri");
    StageSlotId second=slotId("slide-slot-2");
    auto spec=makeSpecification(slides,petri,second);
    auto stage=take(Stage::create(std::move(spec),slides));
    auto microscopeId=take(core::MicroscopeId::create("optika-b293-instance"));
    MicroscopeInstance instance(std::move(microscopeId),
        take(core::DeviceId::create("optika-b293-stage-device")),std::move(stage));
    check(instance.stage().specification().configurations().size()==2,
        "MicroscopeInstance owns one Stage with generic configurations");
    auto home=take(instance.stage().home());
    check(home.axesToHome.size()==3,"Stage is accessed through MicroscopeInstance");
    check(instance.id().value()=="optika-b293-instance","instance keeps stable identity");
}

} // namespace

int main() {
    testGenericConfigurationAndAxisCommandConstruction();
    testHomeRequiresExactCompletionAndReferenceAxes();
    testCorrelatedMotionPlansAndCurrentSlot();
    testFailedMotionInvalidatesPosition();
    testRefreshPositionHomeAndRestoreWorkflow();
    testConfigurationActivationAndRotation();
    testMicroscopeInstanceOwnsStage();
    if(failures) {
        std::cerr<<failures<<" Controller Stage tests failed.\n";
        return 1;
    }
    std::cout<<"All Controller Stage tests passed.\n";
    return 0;
}
