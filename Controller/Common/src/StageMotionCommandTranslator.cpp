#include "controller/stage/StageMotionCommandTranslator.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace microscope::controller {
namespace {

const core::AxisSpecification* findAxis(
    const core::StageSpecification& stage,const StageAxisId& id) noexcept {
    const auto it=std::find_if(stage.axes().begin(),stage.axes().end(),
        [&id](const core::AxisSpecification& axis){return axis.id()==id;});
    return it==stage.axes().end()?nullptr:&*it;
}

core::Result<double,StageMotionTranslationError> toLinearUnit(
    Nanometres nanometres,core::AxisCoordinateUnit unit) {
    switch(unit) {
    case core::AxisCoordinateUnit::Micrometers:
        return core::Result<double,StageMotionTranslationError>::success(
            static_cast<double>(nanometres)/1'000.0);
    case core::AxisCoordinateUnit::Millimeters:
        return core::Result<double,StageMotionTranslationError>::success(
            static_cast<double>(nanometres)/1'000'000.0);
    default:
        return core::Result<double,StageMotionTranslationError>::failure(
            StageMotionTranslationError::UnsupportedCoordinateUnit);
    }
}

core::Result<double,StageMotionTranslationError> toRotationUnit(
    Radians radians,core::AxisCoordinateUnit unit) {
    constexpr double degreesPerRadian=57.2957795130823208768;
    switch(unit) {
    case core::AxisCoordinateUnit::Radians:
        return core::Result<double,StageMotionTranslationError>::success(radians);
    case core::AxisCoordinateUnit::Degrees:
        return core::Result<double,StageMotionTranslationError>::success(
            radians*degreesPerRadian);
    default:
        return core::Result<double,StageMotionTranslationError>::failure(
            StageMotionTranslationError::UnsupportedCoordinateUnit);
    }
}

} // namespace

core::Result<StageMotionCommandTranslation,StageMotionTranslationError>
StageMotionCommandTranslator::translate(
    const StageMotionPlan& plan,const core::StageSpecification& installedStage) {
    if(!plan.configurationId)
        return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
            StageMotionTranslationError::MissingPlanConfiguration);

    if(plan.action==StageMotionAction::Home) {
        if(plan.axesToHome.empty()||!plan.linearTargets.empty()||
           !plan.rotationTargets.empty()||plan.refreshRestoration)
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::InvalidPlanShape);

        for(const auto& id:plan.axesToHome) {
            if(findAxis(installedStage,id)==nullptr)
                return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                    StageMotionTranslationError::UnknownEquipmentAxis);
        }
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

    for(const auto& target:plan.linearTargets) {
        const auto* axis=findAxis(installedStage,target.axisId);
        if(axis==nullptr)
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::UnknownEquipmentAxis);
        if(axis->motionType()!=core::AxisMotionType::Linear)
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::AxisMotionTypeMismatch);
        const auto converted=toLinearUnit(target.position,axis->coordinateUnit());
        if(!converted)
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                converted.error());
        if(converted.value()<axis->operatingRange().minimum()||
           converted.value()>axis->operatingRange().maximum())
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::TargetOutsideEquipmentRange);
        auto position=core::AxisPosition::create(
            target.axisId,converted.value(),axis->coordinateUnit());
        if(!position)
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::InvalidCoreMotionCommand);
        targets.push_back(std::move(position).value());
    }

    for(const auto& target:plan.rotationTargets) {
        const auto* axis=findAxis(installedStage,target.axisId);
        if(axis==nullptr)
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::UnknownEquipmentAxis);
        if(axis->motionType()!=core::AxisMotionType::Rotary)
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::AxisMotionTypeMismatch);
        const auto converted=toRotationUnit(target.angle,axis->coordinateUnit());
        if(!converted)
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                converted.error());
        if(converted.value()<axis->operatingRange().minimum()||
           converted.value()>axis->operatingRange().maximum())
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::TargetOutsideEquipmentRange);
        auto position=core::AxisPosition::create(
            target.axisId,converted.value(),axis->coordinateUnit());
        if(!position)
            return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
                StageMotionTranslationError::InvalidCoreMotionCommand);
        targets.push_back(std::move(position).value());
    }

    auto command=core::MoveAxesTo::create(std::move(targets));
    if(!command)
        return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::failure(
            StageMotionTranslationError::InvalidCoreMotionCommand);
    return core::Result<StageMotionCommandTranslation,StageMotionTranslationError>::success(
        StageMotionCommandTranslation{plan.sequence,
            TranslatedStageMotion{std::in_place_type<core::MoveAxesTo>,
                std::move(command).value()}});
}

} // namespace microscope::controller
