#pragma once

#include "microscope/core/stage/StagePosition3D.h"
#include "microscope/core/motion/StageMotionPort.h"

#include <variant>

namespace microscope::core {

enum class StageXYHomingPolicyError { InvalidXThreshold, InvalidYThreshold };

class StageXYHomingPolicy final {
public:
    static Result<StageXYHomingPolicy, StageXYHomingPolicyError> create(
        double maximumXErrorMicrometers, double maximumYErrorMicrometers);
    double maximumXErrorMicrometers() const noexcept;
    double maximumYErrorMicrometers() const noexcept;
private:
    StageXYHomingPolicy(double, double) noexcept;
    double xThreshold_;
    double yThreshold_;
};

struct StageWorkingSurfaceDirectMovePlan final {
    SlideNavigationTarget2D target;
    MoveAxesTo command;
};

struct StageWorkingSurfaceHomeThenMovePlan final {
    SlideNavigationTarget2D target;
    HomeAxes home;
};

using StageWorkingSurfaceNavigationPlan = std::variant<
    StageWorkingSurfaceDirectMovePlan,
    StageWorkingSurfaceHomeThenMovePlan>;

enum class StageWorkingSurfaceNavigationPlanningError {
    StageComponentMismatch, XAxisMismatch, YAxisMismatch, XUnitMismatch, YUnitMismatch,
    SlideNotFound, SlidePointOutsideGeometry, RegistrationChanged, InvalidStageCoordinate,
    OutsideWorkingSurface, HomeAxisSetInvalid, HomingNotVerified, PositionUncertaintyExceeded
};

// Slot-independent planning over the user-authored, variable-count XY canvas.
// Focus Z is part of the machine state but is not a prerequisite for XY motion.
class StageWorkingSurfaceNavigationPlanner final {
public:
    static Result<StageWorkingSurfaceNavigationPlan,
        StageWorkingSurfaceNavigationPlanningError> plan(
            const StageWorkingSurface2D&, const SlideNavigationTarget2D&,
            const StagePosition3D&, const StageXYHomingPolicy&);

    static Result<StageWorkingSurfaceNavigationPlan,
        StageWorkingSurfaceNavigationPlanningError> planToSlidePoint(
            const StageWorkingSurface2D&, const SlideId&, SlidePoint2D,
            const StagePosition3D&, const StageXYHomingPolicy&);

    // The current layout must still contain the same slide registration revision.
    // Only X/Y must be referenced; focus Z is intentionally independent.
    static Result<MoveAxesTo, StageWorkingSurfaceNavigationPlanningError>
    resolveAfterHoming(
        const StageWorkingSurface2D&, const SlideNavigationTarget2D&,
        const StagePosition3D&, const StageXYHomingPolicy&);
};

} // namespace microscope::core
