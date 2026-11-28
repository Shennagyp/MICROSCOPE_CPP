#include "controller/stage/StageMotionCommandTranslator.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

namespace microscope::controller {
namespace {

const core::AxisSpecification* findEquipmentAxis(
    const core::StageSpecification& stage,const StageAxisId& id) noexcept {
    const auto it=std::find_if(stage.axes().begin(),stage.axes().end(),
        [&id](const core::AxisSpecification& value){return value.id()==id;});
    return it==stage.axes().end()?nullptr:&*it;
}
const WorkingLinearAxis* findWorkingLinear(
    const StageConfiguration& configuration,const StageAxisId& id) noexcept {
    const auto it=std::find_if(configuration.workingLinearAxes().begin(),
        configuration.workingLinearAxes().end(),
        [&id](const WorkingLinearAxis& value){return value.axisId==id;});
    return it==configuration.workingLinearAxes().end()?nullptr:&*it;
}
const WorkingRotationAxis* findWorkingRotation(
    const StageConfiguration& configuration,const StageAxisId& id) noexcept {
    const auto it=std::find_if(configuration.workingRotationAxes().begin(),
        configuration.workingRotationAxes().end(),
        [&id](const WorkingRotationAxis& value){return value.axisId==id;});
    return it==configuration.workingRotationAxes().end()?nullptr:&*it;
}

core::Result<double,StageMotionTranslationError> toLinearUnit(
    Nanometres value,core::AxisCoordinateUnit unit) {
    switch(unit) {
    case core::AxisCoordinateUnit::Micrometers:
        return core::Result<double,StageMotionTranslationError>::success(
            static_cast<double>(value)/1'000.0);
    case core::AxisCoordinateUnit::Millimeters:
        return core::Result<double,StageMotionTranslationError>::success(
            static_cast<double>(value)/1'000'000.0);
    default:
        return core::Result<double,StageMotionTranslationError>::failure(
            StageMotionTranslationError::UnsupportedCoordinateUnit);
    }
}
core::Result<double,StageMotionTranslationError> toRotationUnit(
    Radians value,core::AxisCoordinateUnit unit) {
    constexpr double degreesPerRadian=57.2957795130823208768;
    switch(unit) {
    case core::AxisCoordinateUnit::Radians:
        return core::Result<double,StageMotionTranslationError>::success(value);
    case core::AxisCoordinateUnit::Degrees:
        return core::Result<double,StageMotionTranslationError>::success(
            value*degreesPerRadian);
    default:
        return core::Result<double,StageMotionTranslationError>::failure(
            StageMotionTranslationError::UnsupportedCoordinateUnit);
    }
}

} // namespace

core::Result<StageMotionCommandTranslation,StageMotionTranslationError>
StageMotionCommandTranslator::translate(
    const StageMotionPlan& plan,
    const Stage& stage,
    const core::StageSpecification& installedStage) {
    const auto validPlan=stage.validateForDispatch(plan);
    if(!validPlan)
        return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
            StageMotionTranslationError::StalePlan);
    const auto& controllerStage=stage.specification();
    if(!plan.configurationId)
        return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
            StageMotionTranslationError::MissingPlanConfiguration);
    if(controllerStage.componentId()!=installedStage.componentId())
        return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
            StageMotionTranslationError::StageComponentMismatch);

    const auto* configuration=controllerStage.configuration(*plan.configurationId);
    if(configuration==nullptr)
        return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
            StageMotionTranslationError::ConfigurationNotFound);

    if(plan.action==StageMotionAction::Home) {
        if(plan.axesToHome.empty()||!plan.linearTargets.empty()||
           !plan.rotationTargets.empty()||plan.refreshRestoration)
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::InvalidPlanShape);

        std::set<StageAxisId> expected;
        for(const auto& working:configuration->workingLinearAxes()) {
            const auto* controllerAxis=controllerStage.linearAxis(working.axisId);
            const auto* equipmentAxis=findEquipmentAxis(installedStage,working.axisId);
            if(controllerAxis==nullptr)
                return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                    StageMotionTranslationError::ControllerAxisNotFound);
            if(equipmentAxis==nullptr)
                return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                    StageMotionTranslationError::UnknownEquipmentAxis);
            if(equipmentAxis->motionType()!=core::AxisMotionType::Linear)
                return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                    StageMotionTranslationError::AxisMotionTypeMismatch);
            if(!toLinearUnit(0,equipmentAxis->coordinateUnit()))
                return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                    StageMotionTranslationError::UnsupportedCoordinateUnit);
            expected.insert(working.axisId);
        }
        for(const auto& working:configuration->workingRotationAxes()) {
            const auto* controllerAxis=controllerStage.rotationAxis(working.axisId);
            const auto* equipmentAxis=findEquipmentAxis(installedStage,working.axisId);
            if(controllerAxis==nullptr)
                return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                    StageMotionTranslationError::ControllerAxisNotFound);
            if(equipmentAxis==nullptr)
                return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                    StageMotionTranslationError::UnknownEquipmentAxis);
            if(equipmentAxis->motionType()!=core::AxisMotionType::Rotary)
                return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                    StageMotionTranslationError::AxisMotionTypeMismatch);
            if(!toRotationUnit(0.0,equipmentAxis->coordinateUnit()))
                return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                    StageMotionTranslationError::UnsupportedCoordinateUnit);
            expected.insert(working.axisId);
        }
        const std::set<StageAxisId> requested(plan.axesToHome.begin(),plan.axesToHome.end());
        if(requested.size()!=plan.axesToHome.size()||requested!=expected)
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::WorkingAxisMismatch);

        auto axisSet=core::StageAxisSet::create(plan.axesToHome);
        if(!axisSet)
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::InvalidCoreMotionCommand);
        auto command=core::HomeAxes::create(std::move(axisSet).value());
        return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::success(
            StageMotionCommandTranslation{plan.sequence,
                TranslatedStageMotion{std::in_place_type<core::HomeAxes>,std::move(command)}});
    }

    if(plan.action!=StageMotionAction::MoveTo||!plan.axesToHome.empty()||
       plan.linearTargets.empty())
        return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
            StageMotionTranslationError::InvalidPlanShape);

    std::vector<core::AxisPosition> targets;
    targets.reserve(plan.linearTargets.size()+plan.rotationTargets.size());
    std::set<StageAxisId> linearIds;
    std::set<LinearAxisRole> linearRoles;
    for(const auto& target:plan.linearTargets) {
        const auto* controllerAxis=controllerStage.linearAxis(target.axisId);
        const auto* working=findWorkingLinear(*configuration,target.axisId);
        const auto* equipmentAxis=findEquipmentAxis(installedStage,target.axisId);
        if(controllerAxis==nullptr)
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::ControllerAxisNotFound);
        if(working==nullptr)
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::WorkingAxisMismatch);
        if(equipmentAxis==nullptr)
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::UnknownEquipmentAxis);
        if(equipmentAxis->motionType()!=core::AxisMotionType::Linear)
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::AxisMotionTypeMismatch);
        if(!controllerAxis->mechanicalTravelRange().contains(target.position)||
           (!plan.refreshRestoration&&!working->effectiveRange.contains(target.position))||
           !linearIds.insert(target.axisId).second||
           !linearRoles.insert(controllerAxis->role()).second)
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::WorkingAxisMismatch);

        const auto converted=toLinearUnit(target.position,equipmentAxis->coordinateUnit());
        if(!converted)
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                converted.error());
        if(converted.value()<equipmentAxis->operatingRange().minimum()||
           converted.value()>equipmentAxis->operatingRange().maximum())
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::TargetOutsideEquipmentRange);
        auto position=core::AxisPosition::create(
            target.axisId,converted.value(),equipmentAxis->coordinateUnit());
        if(!position)
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::InvalidCoreMotionCommand);
        targets.push_back(std::move(position).value());
    }

    const bool isSlotXY=linearRoles.size()==2&&linearRoles.count(LinearAxisRole::X)&&
        linearRoles.count(LinearAxisRole::Y);
    const bool isFullXYZ=linearRoles.size()==3&&linearRoles.count(LinearAxisRole::X)&&
        linearRoles.count(LinearAxisRole::Y)&&linearRoles.count(LinearAxisRole::Z);
    if((!isSlotXY&&!isFullXYZ)||(plan.refreshRestoration&&!isFullXYZ)||
       (isSlotXY&&!plan.rotationTargets.empty()))
        return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
            StageMotionTranslationError::InvalidPlanShape);

    std::set<StageAxisId> rotationIds;
    for(const auto& target:plan.rotationTargets) {
        const auto* controllerAxis=controllerStage.rotationAxis(target.axisId);
        const auto* working=findWorkingRotation(*configuration,target.axisId);
        const auto* equipmentAxis=findEquipmentAxis(installedStage,target.axisId);
        if(controllerAxis==nullptr)
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::ControllerAxisNotFound);
        if(working==nullptr||!rotationIds.insert(target.axisId).second)
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::WorkingAxisMismatch);
        if(equipmentAxis==nullptr)
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::UnknownEquipmentAxis);
        if(equipmentAxis->motionType()!=core::AxisMotionType::Rotary)
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::AxisMotionTypeMismatch);
        if(!controllerAxis->mechanicalTravelRange().contains(target.angle)||
           (!plan.refreshRestoration&&!working->effectiveRange.contains(target.angle)))
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::WorkingAxisMismatch);

        const auto converted=toRotationUnit(target.angle,equipmentAxis->coordinateUnit());
        if(!converted)
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                converted.error());
        if(converted.value()<equipmentAxis->operatingRange().minimum()||
           converted.value()>equipmentAxis->operatingRange().maximum())
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::TargetOutsideEquipmentRange);
        auto position=core::AxisPosition::create(
            target.axisId,converted.value(),equipmentAxis->coordinateUnit());
        if(!position)
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::InvalidCoreMotionCommand);
        targets.push_back(std::move(position).value());
    }

    if(isFullXYZ&&rotationIds.size()!=configuration->workingRotationAxes().size())
        return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
            StageMotionTranslationError::WorkingAxisMismatch);

    auto command=core::MoveAxesTo::create(std::move(targets));
    if(!command)
        return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
            StageMotionTranslationError::InvalidCoreMotionCommand);
    return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::success(
        StageMotionCommandTranslation{plan.sequence,
            TranslatedStageMotion{std::in_place_type<core::MoveAxesTo>,std::move(command).value()}});
}

} // namespace microscope::controller
