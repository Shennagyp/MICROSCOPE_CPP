#pragma once

#include "microscope/core/motion/StageMotionPort.h"
#include "microscope/core/motion/StagePositionState.h"
#include "microscope/core/runtime/SerializedOperationRuntime.h"

#include <optional>
#include <utility>
#include <variant>

namespace microscope::application::motion {

enum class StagePositionFeedbackDisposition {
    PositionObservationApplied,
    IgnoredNonCurrentFeedback,
    IgnoredNonPositionPayload,
    IgnoredAxisOutsidePlane,
    InvalidatedAfterFeedbackLoss
};

enum class StagePositionFeedbackReducerErrorCode {
    MissingReceipt,
    PositionStateRejectedObservation
};

struct StagePositionFeedbackReducerError final {
    StagePositionFeedbackReducerErrorCode code;
    std::optional<core::StageAxisPositionStateError> positionStateError;
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

    // The control path must invalidate the plane even when no preserved receipt
    // remains to invoke reduce(). The runtime owner acknowledges feedback loss
    // only after the matching device readback/reconciliation has completed.
    [[nodiscard]] static core::StagePositionState invalidateForFeedbackLoss(
        const core::StagePositionState& current) {
        return current.invalidateMachineReference();
    }
};

} // namespace microscope::application::motion
