#pragma once

#include "controller/stage/Stage.h"
#include "microscope/core/equipment/StageSpecifications.h"
#include "microscope/core/motion/StageMotionPort.h"

#include <cstdint>
#include <variant>

namespace microscope::controller {

enum class StageMotionTranslationError {
    MissingPlanConfiguration,
    StalePlan,
    StageComponentMismatch,
    ConfigurationNotFound,
    ControllerAxisNotFound,
    WorkingAxisMismatch,
    InvalidPlanShape,
    UnknownEquipmentAxis,
    AxisMotionTypeMismatch,
    UnsupportedCoordinateUnit,
    TargetOutsideEquipmentRange,
    InvalidCoreMotionCommand
};

using TranslatedStageMotion = std::variant<core::HomeAxes,core::MoveAxesTo>;

struct StageMotionCommandTranslation final {
    std::uint64_t planSequence;
    TranslatedStageMotion command;
};

// Bridges the Controller's canonical nm/radian plans to the existing Core
// motion contract, using the installed equipment axis declarations as the
// authoritative device-coordinate unit and travel range.
class StageMotionCommandTranslator final {
public:
    static core::Result<StageMotionCommandTranslation,StageMotionTranslationError>
    translate(
        const StageMotionPlan& plan,
        const Stage& stage,
        const core::StageSpecification& installedStage);
};

} // namespace microscope::controller
