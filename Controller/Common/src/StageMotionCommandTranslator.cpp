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

bool approximatelyEqual(double left,double right) noexcept;

const core::StageConfigurationSpecification* findEquipmentConfiguration(
    const core::StageSpecification& stage,const StageConfigurationId& id) noexcept {
    const auto it=std::find_if(stage.configurations().begin(),stage.configurations().end(),
        [&id](const core::StageConfigurationSpecification& value){return value.id()==id;});
    return it==stage.configurations().end()?nullptr:&*it;
}

core::StageLinearAxisRole toCoreRole(LinearAxisRole role) noexcept {
    switch(role) {
    case LinearAxisRole::X:return core::StageLinearAxisRole::X;
    case LinearAxisRole::Y:return core::StageLinearAxisRole::Y;
    case LinearAxisRole::Z:return core::StageLinearAxisRole::Z;
    }
    return core::StageLinearAxisRole::X;
}
core::StageRotationAxisDirection toCoreDirection(RotationAxisDirection direction) noexcept {
    switch(direction) {
    case RotationAxisDirection::X:return core::StageRotationAxisDirection::X;
    case RotationAxisDirection::Y:return core::StageRotationAxisDirection::Y;
    case RotationAxisDirection::Z:return core::StageRotationAxisDirection::Z;
    }
    return core::StageRotationAxisDirection::Z;
}

bool configurationMatches(
    const StageConfiguration& controller,
    const core::StageConfigurationSpecification& equipment) noexcept {
    if(controller.id()!=equipment.id()||
       controller.workingLinearAxes().size()!=equipment.workingLinearAxes().size()||
       controller.workingRotationAxes().size()!=equipment.workingRotationAxes().size()||
       controller.slots().size()!=equipment.slots().size()) return false;

    for(const auto& axis:controller.workingLinearAxes()) {
        const auto it=std::find_if(equipment.workingLinearAxes().begin(),
            equipment.workingLinearAxes().end(),
            [&axis](const core::StageWorkingLinearAxisSpecification& value){
                return value.axisId==axis.axisId;
            });
        if(it==equipment.workingLinearAxes().end()||
           it->effectiveRange.minimum()!=axis.effectiveRange.minimum()||
           it->effectiveRange.maximum()!=axis.effectiveRange.maximum()) return false;
    }
    for(const auto& axis:controller.workingRotationAxes()) {
        const auto it=std::find_if(equipment.workingRotationAxes().begin(),
            equipment.workingRotationAxes().end(),
            [&axis](const core::StageWorkingRotationAxisSpecification& value){
                return value.axisId==axis.axisId;
            });
        if(it==equipment.workingRotationAxes().end()||
           !approximatelyEqual(it->effectiveRange.minimum(),axis.effectiveRange.minimum())||
           !approximatelyEqual(it->effectiveRange.maximum(),axis.effectiveRange.maximum())) return false;
    }
    for(const auto& slot:controller.slots()) {
        const auto it=std::find_if(equipment.slots().begin(),equipment.slots().end(),
            [&slot](const core::StageSlotSpecification& value){return value.id()==slot.id();});
        if(it==equipment.slots().end()||
           it->originInStageCoordinates().x!=slot.originInStageCoordinates().x||
           it->originInStageCoordinates().y!=slot.originInStageCoordinates().y||
           it->xRange().minimum()!=slot.xRange().minimum()||
           it->xRange().maximum()!=slot.xRange().maximum()||
           it->yRange().minimum()!=slot.yRange().minimum()||
           it->yRange().maximum()!=slot.yRange().maximum()) return false;
    }
    return true;
}

core::Result<double,StageMotionTranslationError> toRotationUnit(
    Radians value,core::AxisCoordinateUnit unit);

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
bool approximatelyEqual(double left,double right) noexcept {
    const double scale=std::max({1.0,std::abs(left),std::abs(right)});
    return std::abs(left-right)<=scale*1e-12;
}

core::Result<bool,StageMotionTranslationError> axisRangeMatches(
    const LinearAxis& axis,const core::AxisSpecification& installed) {
    if(installed.motionType()!=core::AxisMotionType::Linear)
        return core::Result<bool,StageMotionTranslationError>::failure(
            StageMotionTranslationError::AxisMotionTypeMismatch);
    const auto minimum=toLinearUnit(
        axis.mechanicalTravelRange().minimum(),installed.coordinateUnit());
    const auto maximum=toLinearUnit(
        axis.mechanicalTravelRange().maximum(),installed.coordinateUnit());
    if(!minimum) return core::Result<bool,StageMotionTranslationError>::failure(minimum.error());
    if(!maximum) return core::Result<bool,StageMotionTranslationError>::failure(maximum.error());
    return core::Result<bool,StageMotionTranslationError>::success(
        approximatelyEqual(minimum.value(),installed.operatingRange().minimum())&&
        approximatelyEqual(maximum.value(),installed.operatingRange().maximum()));
}
core::Result<bool,StageMotionTranslationError> axisRangeMatches(
    const RotationAxis& axis,const core::AxisSpecification& installed) {
    if(installed.motionType()!=core::AxisMotionType::Rotary)
        return core::Result<bool,StageMotionTranslationError>::failure(
            StageMotionTranslationError::AxisMotionTypeMismatch);
    const auto minimum=toRotationUnit(
        axis.mechanicalTravelRange().minimum(),installed.coordinateUnit());
    const auto maximum=toRotationUnit(
        axis.mechanicalTravelRange().maximum(),installed.coordinateUnit());
    if(!minimum) return core::Result<bool,StageMotionTranslationError>::failure(minimum.error());
    if(!maximum) return core::Result<bool,StageMotionTranslationError>::failure(maximum.error());
    return core::Result<bool,StageMotionTranslationError>::success(
        approximatelyEqual(minimum.value(),installed.operatingRange().minimum())&&
        approximatelyEqual(maximum.value(),installed.operatingRange().maximum()));
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
    const bool hasCanonicalConfigurations=!installedStage.configurations().empty();
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
    if(hasCanonicalConfigurations) {
        if(installedStage.configurations().size()!=controllerStage.configurations().size())
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::EquipmentConfigurationMismatch);
        const auto* installedConfiguration=findEquipmentConfiguration(
            installedStage,*plan.configurationId);
        if(installedConfiguration==nullptr||
           !configurationMatches(*configuration,*installedConfiguration))
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::EquipmentConfigurationMismatch);
    }

    for(const auto& axis:controllerStage.linearAxes()) {
        const auto* equipment=findEquipmentAxis(installedStage,axis.id());
        if(equipment==nullptr)
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::UnknownEquipmentAxis);
        const auto rangeMatches=axisRangeMatches(axis,*equipment);
        if(!rangeMatches)
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                rangeMatches.error());
        if(!rangeMatches.value())
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::MechanicalRangeMismatch);
        if(hasCanonicalConfigurations&&
           (!equipment->linearRole()||*equipment->linearRole()!=toCoreRole(axis.role())))
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::EquipmentConfigurationMismatch);
    }
    for(const auto& axis:controllerStage.rotationAxes()) {
        const auto* equipment=findEquipmentAxis(installedStage,axis.id());
        if(equipment==nullptr)
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::UnknownEquipmentAxis);
        const auto rangeMatches=axisRangeMatches(axis,*equipment);
        if(!rangeMatches)
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                rangeMatches.error());
        if(!rangeMatches.value())
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::MechanicalRangeMismatch);
        if(hasCanonicalConfigurations) {
            if(!equipment->rotationGeometry())
                return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                    StageMotionTranslationError::EquipmentConfigurationMismatch);
            const auto& expected=axis.geometry();
            const auto& actual=*equipment->rotationGeometry();
            if(actual.pointOnAxis.x!=expected.pointOnAxis.x||
               actual.pointOnAxis.y!=expected.pointOnAxis.y||
               actual.pointOnAxis.z!=expected.pointOnAxis.z||
               actual.direction!=toCoreDirection(expected.direction))
                return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                    StageMotionTranslationError::EquipmentConfigurationMismatch);
        }
    }

    if(controllerStage.linearAxes().size()+controllerStage.rotationAxes().size()!=
       installedStage.axes().size())
        return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
            StageMotionTranslationError::AxisInventoryMismatch);

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
