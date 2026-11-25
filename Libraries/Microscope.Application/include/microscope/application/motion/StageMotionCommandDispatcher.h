#pragma once

#include "microscope/core/motion/StageMotionPort.h"
#include "microscope/core/runtime/SerializedOperationRuntime.h"

#include <optional>
#include <utility>

namespace microscope::application::motion {

enum class StageMotionDispatchErrorCode {
    RuntimeDispatchRejected,
    AdapterSubmissionThrew,
    RuntimeRejectedSubmissionResult
};

struct StageMotionDispatchError final {
    StageMotionDispatchErrorCode code;
    std::optional<core::SerializedOperationRuntimeError> runtimeError;
};

struct StageMotionDispatchReceipt final {
    core::StageMotionAction action;
    core::StageMotionSubmissionResult submission;
    core::StepStatus stepStatus;
};

// Owner-thread adapter boundary for stage commands. The caller prepares the
// operation and step; this class records dispatch before invoking the typed
// port, then records only the adapter's synchronous acceptance/rejection.
// Device completion and physical verification remain feedback-driven.
class StageMotionCommandDispatcher final {
public:
    using Runtime = core::SerializedOperationRuntime<core::StageMotionFeedbackPayload>;
    using Result = core::Result<StageMotionDispatchReceipt, StageMotionDispatchError>;

    [[nodiscard]] static Result moveAxesTo(
        const core::StageMotionCommandContext& context,
        const core::MoveAxesTo& command,
        core::IStageMotionPort& port,
        Runtime& runtime) {
        return dispatch(
            core::StageMotionAction::MoveAxesTo, context, port, runtime,
            [&](core::IFeedbackIngress<core::StageMotionFeedbackPayload>& ingress) {
                return port.moveAxesTo(context, command, ingress);
            });
    }

    [[nodiscard]] static Result moveAxesBy(
        const core::StageMotionCommandContext& context,
        const core::MoveAxesBy& command,
        core::IStageMotionPort& port,
        Runtime& runtime) {
        return dispatch(
            core::StageMotionAction::MoveAxesBy, context, port, runtime,
            [&](core::IFeedbackIngress<core::StageMotionFeedbackPayload>& ingress) {
                return port.moveAxesBy(context, command, ingress);
            });
    }

    [[nodiscard]] static Result homeAxes(
        const core::StageMotionCommandContext& context,
        const core::HomeAxes& command,
        core::IStageMotionPort& port,
        Runtime& runtime) {
        return dispatch(
            core::StageMotionAction::HomeAxes, context, port, runtime,
            [&](core::IFeedbackIngress<core::StageMotionFeedbackPayload>& ingress) {
                return port.homeAxes(context, command, ingress);
            });
    }

    [[nodiscard]] static Result stopAxes(
        const core::StageMotionCommandContext& context,
        const core::StopAxes& command,
        core::IStageMotionPort& port,
        Runtime& runtime) {
        return dispatch(
            core::StageMotionAction::StopAxes, context, port, runtime,
            [&](core::IFeedbackIngress<core::StageMotionFeedbackPayload>& ingress) {
                return port.stopAxes(context, command, ingress);
            });
    }

private:
    template <typename TSubmit>
    [[nodiscard]] static Result dispatch(
        core::StageMotionAction action,
        const core::StageMotionCommandContext& context,
        core::IStageMotionPort&,
        Runtime& runtime,
        TSubmit&& submit) {
        const auto& operation = context.operation();
        if (!operation.commandId) {
            return Result::failure(StageMotionDispatchError{
                StageMotionDispatchErrorCode::RuntimeDispatchRejected,
                std::nullopt});
        }

        auto dispatched = runtime.dispatchCommand(
            operation.operationId,
            operation.stepId,
            *operation.commandId,
            context.runtimeDevice());
        if (!dispatched) {
            return Result::failure(StageMotionDispatchError{
                StageMotionDispatchErrorCode::RuntimeDispatchRejected,
                dispatched.error()});
        }

        std::optional<core::StageMotionSubmissionResult> submission;
        try {
            submission.emplace(submit(runtime.feedbackIngress()));
        } catch (...) {
            auto rejected = runtime.recordAdapterRejected(
                operation.operationId, operation.stepId);
            return Result::failure(StageMotionDispatchError{
                StageMotionDispatchErrorCode::AdapterSubmissionThrew,
                rejected ? std::nullopt
                         : std::optional<core::SerializedOperationRuntimeError>(rejected.error())});
        }

        if (submission->status() == core::StageMotionSubmissionStatus::Accepted) {
            auto accepted = runtime.recordAdapterAccepted(
                operation.operationId, operation.stepId);
            if (!accepted) {
                // The adapter may already be executing the command. The host
                // must treat this as a runtime-fatal consistency failure.
                return Result::failure(StageMotionDispatchError{
                    StageMotionDispatchErrorCode::RuntimeRejectedSubmissionResult,
                    accepted.error()});
            }
            return Result::success(StageMotionDispatchReceipt{
                action, std::move(*submission), accepted.value()});
        }

        auto rejected = runtime.recordAdapterRejected(
            operation.operationId, operation.stepId);
        if (!rejected) {
            return Result::failure(StageMotionDispatchError{
                StageMotionDispatchErrorCode::RuntimeRejectedSubmissionResult,
                rejected.error()});
        }
        return Result::success(StageMotionDispatchReceipt{
            action, std::move(*submission), rejected.value()});
    }
};

} // namespace microscope::application::motion
