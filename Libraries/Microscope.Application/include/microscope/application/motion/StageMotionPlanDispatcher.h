#pragma once

#include "controller/MicroscopeInstance.h"
#include "controller/stage/StageMotionCommandTranslator.h"
#include "microscope/application/motion/StageMotionCommandDispatcher.h"

#include <cstdint>
#include <type_traits>
#include <utility>
#include <variant>

namespace microscope::application::motion {

enum class StageMotionPlanIdentityError {
    MicroscopeIdentityMismatch,
    StageDeviceIdentityMismatch
};

struct StageMotionPlanDispatchError final {
    std::variant<
        StageMotionPlanIdentityError,
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
        controller::MicroscopeInstance& microscope,
        const controller::StageMotionPlan& plan,
        const core::StageSpecification& installedStage,
        const core::StageMotionCommandContext& context,
        core::IStageMotionPort& port,
        Runtime& runtime) {
        auto& stage=microscope.stage();
        if(context.runtimeDevice().microscopeId!=microscope.id()) {
            (void)stage.cancelUndispatchedPlan(plan);
            return Result::failure(StageMotionPlanDispatchError{
                StageMotionPlanIdentityError::MicroscopeIdentityMismatch});
        }
        if(context.runtimeDevice().deviceId.value()!=
           stage.specification().componentId().value()) {
            (void)stage.cancelUndispatchedPlan(plan);
            return Result::failure(StageMotionPlanDispatchError{
                StageMotionPlanIdentityError::StageDeviceIdentityMismatch});
        }

        auto translated=controller::StageMotionCommandTranslator::translate(
            plan,stage,installedStage);
        if(!translated) {
            (void)stage.cancelUndispatchedPlan(plan);
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
                        if(receipt.error().code==
                            StageMotionDispatchErrorCode::RuntimeDispatchRejected) {
                            (void)stage.cancelUndispatchedPlan(plan);
                        } else {
                            (void)stage.recordFailedMotion(plan);
                        }
                        return Result::failure(StageMotionPlanDispatchError{
                            receipt.error()});
                    }
                    auto dispatchReceipt=std::move(receipt).value();
                    if(dispatchReceipt.submission.status()==
                        core::StageMotionSubmissionStatus::Rejected) {
                        (void)stage.cancelUndispatchedPlan(plan);
                    }
                    return Result::success(StageMotionPlanDispatchReceipt{
                        plan.sequence,std::move(dispatchReceipt)});
                } else {
                    auto receipt=StageMotionCommandDispatcher::moveAxesTo(
                        context,command,port,runtime);
                    if(!receipt) {
                        if(receipt.error().code==
                            StageMotionDispatchErrorCode::RuntimeDispatchRejected) {
                            (void)stage.cancelUndispatchedPlan(plan);
                        } else {
                            (void)stage.recordFailedMotion(plan);
                        }
                        return Result::failure(StageMotionPlanDispatchError{
                            receipt.error()});
                    }
                    auto dispatchReceipt=std::move(receipt).value();
                    if(dispatchReceipt.submission.status()==
                        core::StageMotionSubmissionStatus::Rejected) {
                        (void)stage.cancelUndispatchedPlan(plan);
                    }
                    return Result::success(StageMotionPlanDispatchReceipt{
                        plan.sequence,std::move(dispatchReceipt)});
                }
            },
            translated.value().command);
    }
};

} // namespace microscope::application::motion
