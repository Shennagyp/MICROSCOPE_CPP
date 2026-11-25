#pragma once

#include "microscope/application/motion/StageMotionCommandDispatcher.h"
#include "microscope/core/navigation/StageNavigationPlanner.h"

#include <variant>

namespace microscope::application::motion {

using StageNavigationMotionCommand =
    std::variant<core::MoveAxesTo, core::HomeAxes>;

struct StageNavigationPlanDispatchReceipt final {
    // Keep the exact slide-local destination and calibration revision with
    // the command that was submitted. A home-first caller must retain this
    // value and re-resolve it after the verified reference is established.
    core::SlideNavigationTarget target;
    StageNavigationMotionCommand command;
    core::StageMotionCommandContext context;
    StageMotionDispatchReceipt dispatch;
};

class StageNavigationPlanDispatcher final {
public:
    using Runtime = core::SerializedOperationRuntime<core::StageMotionFeedbackPayload>;
    using Result = core::Result<
        StageNavigationPlanDispatchReceipt,
        StageMotionDispatchError>;

    // Operation and step creation remain host-owned. The supplied context must
    // identify the already-created, active operation step and its CommandId.
    [[nodiscard]] static Result dispatch(
        const core::StageNavigationPlan& plan,
        const core::StageMotionCommandContext& context,
        core::IStageMotionPort& port,
        Runtime& runtime) {
        if (const auto* direct = std::get_if<core::StageDirectMovePlan>(&plan)) {
            auto dispatched = StageMotionCommandDispatcher::moveAxesTo(
                context, direct->command, port, runtime);
            if (!dispatched) {
                return Result::failure(dispatched.error());
            }
            return Result::success(StageNavigationPlanDispatchReceipt{
                direct->target,
                StageNavigationMotionCommand{direct->command},
                context,
                std::move(dispatched).value()});
        }

        const auto& home = std::get<core::StageHomeThenMovePlan>(plan);
        auto dispatched = StageMotionCommandDispatcher::homeAxes(
            context, home.home, port, runtime);
        if (!dispatched) {
            return Result::failure(dispatched.error());
        }
        return Result::success(StageNavigationPlanDispatchReceipt{
            home.target,
            StageNavigationMotionCommand{home.home},
            context,
            std::move(dispatched).value()});
    }
};

} // namespace microscope::application::motion
