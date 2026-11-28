#pragma once

#include "controller/stage/StageMotionCommandTranslator.h"
#include "microscope/application/motion/StageMotionCommandDispatcher.h"

#include <cstdint>
#include <type_traits>
#include <utility>
#include <variant>

namespace microscope::application::motion {

struct StageMotionPlanDispatchError final {
    std::variant<
        controller::StageMotionTranslationError,
        StageMotionDispatchError> cause;
};

struct StageMotionPlanDispatchReceipt final {
    std::uint64_t planSequence=0;
    StageMotionDispatchReceipt dispatch;
};

// Application-side bridge: translate one Controller plan and submit it through
// the existing correlated StageMotionCommandDispatcher. A receipt means only
// adapter submission was accepted/rejected; completion and home-reference
// evidence are reconciled separately by the Stage owner.
class StageMotionPlanDispatcher final {
public:
    using Runtime = StageMotionCommandDispatcher::Runtime;
    using Result = core::Result<
        StageMotionPlanDispatchReceipt,StageMotionPlanDispatchError>;

    [[nodiscard]] static Result dispatch(
        const controller::StageMotionPlan& plan,
        const controller::StageSpecification& controllerStage,
        const core::StageSpecification& installedStage,
        const core::StageMotionCommandContext& context,
        core::IStageMotionPort& port,
        Runtime& runtime) {
        auto translated=controller::StageMotionCommandTranslator::translate(
            plan,controllerStage,installedStage);
        if(!translated) {
            return Result::failure(StageMotionPlanDispatchError{
                translated.error()});
        }

        return std::visit(
            [&](const auto& command)->Result {
                using Command=std::decay_t<decltype(command)>;
                if constexpr(std::is_same_v<Command,core::HomeAxes>) {
                    auto receipt=StageMotionCommandDispatcher::homeAxes(
                        context,command,port,runtime);
                    if(!receipt) {
                        return Result::failure(StageMotionPlanDispatchError{
                            receipt.error()});
                    }
                    return Result::success(StageMotionPlanDispatchReceipt{
                        plan.sequence,std::move(receipt).value()});
                } else {
                    auto receipt=StageMotionCommandDispatcher::moveAxesTo(
                        context,command,port,runtime);
                    if(!receipt) {
                        return Result::failure(StageMotionPlanDispatchError{
                            receipt.error()});
                    }
                    return Result::success(StageMotionPlanDispatchReceipt{
                        plan.sequence,std::move(receipt).value()});
                }
            },
            translated.value().command);
    }
};

} // namespace microscope::application::motion
