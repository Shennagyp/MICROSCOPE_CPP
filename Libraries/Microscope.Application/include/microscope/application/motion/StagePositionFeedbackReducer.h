#pragma once

#include "microscope/core/motion/StageMotionPort.h"
#include "microscope/core/motion/StagePositionState.h"
#include "microscope/core/runtime/SerializedOperationRuntime.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <variant>

namespace microscope::application::motion {

enum class StagePositionFeedbackDisposition {
    PositionObservationApplied,
    IgnoredNonCurrentFeedback,
    IgnoredNonPositionPayload,
    IgnoredAxisOutsidePlane,
    InvalidatedAfterFeedbackLoss,
    HomeReferenceApplied
};

enum class StagePositionFeedbackReducerErrorCode {
    MissingReceipt,
    PositionStateRejectedObservation,
    FeedbackLossUnresolved,
    FeedbackNotAppliedToOperation,
    InvalidHomeReferenceFact,
    InvalidHomeReferencePayload,
    HomeStepNotVerified,
    RuntimeStepQueryFailed,
    RequestedAxesDoNotMatchStagePlane,
    HomeReferenceAxisSetMismatch
};

struct StagePositionFeedbackReducerError final {
    StagePositionFeedbackReducerErrorCode code;
    std::optional<core::StageAxisPositionStateError> positionStateError;
    std::optional<core::SerializedOperationRuntimeError> runtimeError;
};

struct StagePositionFeedbackReduction final {
    core::StagePositionState position;
    StagePositionFeedbackDisposition disposition;
};

// Applies only current-session position observations to the installed X/Y
// stage-plane state. Homing is deliberately separate: this reducer never
// promotes ordinary position telemetry into a verified machine reference.
class StagePositionFeedbackReducer final {
public:
    using FeedbackResult =
        core::FeedbackProcessingResult<core::StageMotionFeedbackPayload>;
    using Result = core::Result<
        StagePositionFeedbackReduction,
        StagePositionFeedbackReducerError>;

    [[nodiscard]] static Result reduce(
        const core::StagePositionState& current,
        const FeedbackResult& feedback,
        bool feedbackLossUnresolved) {
        if (feedbackLossUnresolved) {
            return Result::success(StagePositionFeedbackReduction{
                current.invalidateMachineReference(),
                StagePositionFeedbackDisposition::InvalidatedAfterFeedbackLoss});
        }

        if (feedback.disposition !=
                core::FeedbackProcessingDisposition::AppliedToOperation &&
            feedback.disposition !=
                core::FeedbackProcessingDisposition::UncorrelatedDeviceFeedback) {
            return Result::success(StagePositionFeedbackReduction{
                current, StagePositionFeedbackDisposition::IgnoredNonCurrentFeedback});
        }

        if (!feedback.receipt) {
            return Result::failure(StagePositionFeedbackReducerError{
                StagePositionFeedbackReducerErrorCode::MissingReceipt,
                std::nullopt});
        }

        const auto& envelope = feedback.receipt->feedback();
        const auto* observation =
            std::get_if<core::AxisPositionObservation>(&envelope.payload());
        if (observation == nullptr) {
            return Result::success(StagePositionFeedbackReduction{
                current, StagePositionFeedbackDisposition::IgnoredNonPositionPayload});
        }

        const auto& axisId = observation->position().axisId();
        if (axisId != current.horizontal().axisId() &&
            axisId != current.transverse().axisId()) {
            return Result::success(StagePositionFeedbackReduction{
                current, StagePositionFeedbackDisposition::IgnoredAxisOutsidePlane});
        }

        auto updated = current.afterPositionObservation(
            *observation, envelope.metadata().source());
        if (!updated) {
            return Result::failure(StagePositionFeedbackReducerError{
                StagePositionFeedbackReducerErrorCode::PositionStateRejectedObservation,
                updated.error()});
        }

        return Result::success(StagePositionFeedbackReduction{
            std::move(updated).value(),
            StagePositionFeedbackDisposition::PositionObservationApplied});
    }

    // Apply a complete HomeReferenceEstablished fact only after the Core runtime
    // reports the exact correlated step as Verified. The expected axes are
    // supplied from the HomeAxes request; no partial axis set can re-reference
    // the dual-slide plane.
    [[nodiscard]] static Result applyVerifiedHomeReference(
        const core::StagePositionState& current,
        const FeedbackResult& feedback,
        const core::StageAxisSet& requestedAxes,
        const core::SerializedOperationRuntime<core::StageMotionFeedbackPayload>& runtime,
        const core::OperationId& operationId,
        const core::StepId& stepId) {
        if (runtime.feedbackLossLatched()) {
            return Result::failure(StagePositionFeedbackReducerError{
                StagePositionFeedbackReducerErrorCode::FeedbackLossUnresolved,
                std::nullopt,
                std::nullopt});
        }

        if (feedback.disposition !=
                core::FeedbackProcessingDisposition::AppliedToOperation) {
            return Result::failure(StagePositionFeedbackReducerError{
                StagePositionFeedbackReducerErrorCode::FeedbackNotAppliedToOperation,
                std::nullopt,
                std::nullopt});
        }
        if (!feedback.receipt) {
            return Result::failure(StagePositionFeedbackReducerError{
                StagePositionFeedbackReducerErrorCode::MissingReceipt,
                std::nullopt,
                std::nullopt});
        }

        const auto& envelope = feedback.receipt->feedback();
        const auto& metadata = envelope.metadata();
        const auto& operation = metadata.correlation().operation;
        if (metadata.kind() != core::FeedbackKind::HomeReferenceEstablished ||
            !operation ||
            operation->operationId != operationId ||
            operation->stepId != stepId ||
            !operation->commandId ||
            feedback.operationId != std::optional<core::OperationId>(operationId) ||
            feedback.stepId != std::optional<core::StepId>(stepId)) {
            return Result::failure(StagePositionFeedbackReducerError{
                StagePositionFeedbackReducerErrorCode::InvalidHomeReferenceFact,
                std::nullopt,
                std::nullopt});
        }

        auto stepStatus = runtime.stepStatus(operationId, stepId);
        if (!stepStatus) {
            return Result::failure(StagePositionFeedbackReducerError{
                StagePositionFeedbackReducerErrorCode::RuntimeStepQueryFailed,
                std::nullopt,
                stepStatus.error()});
        }
        if (stepStatus.value() != core::StepStatus::Verified) {
            return Result::failure(StagePositionFeedbackReducerError{
                StagePositionFeedbackReducerErrorCode::HomeStepNotVerified,
                std::nullopt,
                std::nullopt});
        }

        const auto* homeReference =
            std::get_if<core::StageHomeReferenceObservation>(&envelope.payload());
        if (homeReference == nullptr) {
            return Result::failure(StagePositionFeedbackReducerError{
                StagePositionFeedbackReducerErrorCode::InvalidHomeReferencePayload,
                std::nullopt,
                std::nullopt});
        }

        const auto& axes = requestedAxes.axes();
        if (axes.size() != 2 ||
            std::find(axes.begin(), axes.end(), current.horizontal().axisId()) == axes.end() ||
            std::find(axes.begin(), axes.end(), current.transverse().axisId()) == axes.end()) {
            return Result::failure(StagePositionFeedbackReducerError{
                StagePositionFeedbackReducerErrorCode::RequestedAxesDoNotMatchStagePlane,
                std::nullopt,
                std::nullopt});
        }
        if (!homeReference->matchesAxes(requestedAxes)) {
            return Result::failure(StagePositionFeedbackReducerError{
                StagePositionFeedbackReducerErrorCode::HomeReferenceAxisSetMismatch,
                std::nullopt,
                std::nullopt});
        }

        auto updated = current;
        for (const auto& observation : homeReference->positions()) {
            auto next = updated.afterVerifiedHomingObservation(
                observation, metadata.source());
            if (!next) {
                return Result::failure(StagePositionFeedbackReducerError{
                    StagePositionFeedbackReducerErrorCode::PositionStateRejectedObservation,
                    next.error(),
                    std::nullopt});
            }
            updated = std::move(next).value();
        }

        return Result::success(StagePositionFeedbackReduction{
            std::move(updated),
            StagePositionFeedbackDisposition::HomeReferenceApplied});
    }

    // The control path must invalidate the plane even when no preserved receipt
    // remains to invoke reduce(). The runtime owner acknowledges feedback loss
    // only after the matching device readback/reconciliation has completed.
    [[nodiscard]] static core::StagePositionState invalidateForFeedbackLoss(
        const core::StagePositionState& current) {
        return current.invalidateMachineReference();
    }
};

} // namespace microscope::application::motion
