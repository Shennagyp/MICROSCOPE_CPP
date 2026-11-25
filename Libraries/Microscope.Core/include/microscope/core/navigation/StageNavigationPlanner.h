#pragma once
#include "microscope/core/navigation/DualSlideNavigation.h"
#include "microscope/core/motion/StagePositionState.h"
#include <variant>

namespace microscope::core {

enum class StageHomingPolicyError {
    InvalidHorizontalThreshold,
    InvalidTransverseThreshold
};
class StageHomingPolicy final {
public:
    static Result<StageHomingPolicy,StageHomingPolicyError> create(
        double maximumHorizontalErrorMicrometers,
        double maximumTransverseErrorMicrometers);
    double maximumHorizontalErrorMicrometers() const noexcept;
    double maximumTransverseErrorMicrometers() const noexcept;
private:
    StageHomingPolicy(double,double);
    double horizontalThreshold_;
    double transverseThreshold_;
};

struct StageDirectMovePlan final {
    SlideNavigationTarget target;
    MoveAxesTo command;
};
struct StageHomeThenMovePlan final {
    SlideNavigationTarget target;
    HomeAxes home;
};
using StageNavigationPlan = std::variant<StageDirectMovePlan,StageHomeThenMovePlan>;

enum class StageNavigationPlanningError {
    StageComponentMismatch,
    HorizontalAxisMismatch,
    TransverseAxisMismatch,
    HorizontalUnitMismatch,
    TransverseUnitMismatch,
    SlideNotInCarrier,
    SlideNotRegistered,
    SlidePointOutsideGeometry,
    RegistrationChanged,
    InvalidStageCoordinate,
    OutsideStageTravel,
    HomeAxisSetInvalid,
    HomingNotVerified,
    PositionUncertaintyExceeded
};

class StageNavigationPlanner final {
public:
    // Direct movement is allowed only while both axis references are valid and
    // their accumulated error bounds remain within policy. Otherwise both
    // axes are homed because they jointly define one stage-plane coordinate.
    static Result<StageNavigationPlan,StageNavigationPlanningError> plan(
        const DualSlideCarrierState& carrier,
        const SlideNavigationTarget& target,
        const StageAxisPositionState& horizontal,
        const StageAxisPositionState& transverse,
        const StageHomingPolicy& policy);

    // Application-facing entry point: creates the revision-bound target
    // from the selected SlideId and slide-local coordinate, then plans against
    // the one stage-scoped X/Y aggregate.
    static Result<StageNavigationPlan,StageNavigationPlanningError> planToSlidePoint(
        const DualSlideCarrierState& carrier,
        const SlideId& slideId,
        SlideLocalPoint point,
        const StagePositionState& position,
        const StageHomingPolicy& policy);

    // Aggregate overload prevents callers from combining X/Y states from
    // different installed stages after homing.
    static Result<MoveAxesTo,StageNavigationPlanningError> resolveAfterHoming(
        const DualSlideCarrierState& carrier,
        const SlideNavigationTarget& target,
        const StagePositionState& position,
        const StageHomingPolicy& policy);

    // Call only after the home operation has been verified and both axis states
    // have been replaced with fresh reference/readback evidence.
    static Result<MoveAxesTo,StageNavigationPlanningError> resolveAfterHoming(
        const DualSlideCarrierState& carrier,
        const SlideNavigationTarget& target,
        const StageAxisPositionState& horizontal,
        const StageAxisPositionState& transverse,
        const StageHomingPolicy& policy);
};

} // namespace microscope::core
