#include "microscope/application/motion/StagePositionCommandReducer.h"
#include "microscope/application/runtime/RuntimeWakeSignal.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using namespace microscope;

template <typename T, typename E>
T take(core::Result<T, E> result) {
    if (!result) {
        throw std::runtime_error("stage command reducer fixture rejected");
    }
    return std::move(result).value();
}

template <typename T>
T id(const char* value) {
    return take(T::create(value));
}

core::AxisSpecification axis(const char* name) {
    return take(core::AxisSpecification::create(
        id<core::StageAxisId>(name),
        core::AxisMotionType::Linear,
        core::AxisCoordinateUnit::Micrometers,
        take(core::AxisOperatingRange::create(
            -100000, 150000, core::AxisCoordinateUnit::Micrometers)),
        take(core::PositiveAxisResolution::create(0.1)),
        std::nullopt,
        std::nullopt,
        std::nullopt,
        core::AxisPositionFeedbackCapability::DeviceReported));
}

core::DualSlideCarrierSpecification carrierSpecification() {
    return take(core::DualSlideCarrierSpecification::create(
        take(core::StageSpecification::create(
            id<core::EquipmentComponentId>("stage"),
            {axis("x"), axis("y")})),
        id<core::StageAxisId>("x"),
        id<core::StageAxisId>("y"),
        core::CarrierHorizontalDirection::PositiveStageXFromLeftToRight));
}

core::StagePositionState referencedPosition(
    const core::DualSlideCarrierSpecification& specification) {
    auto state = take(core::StagePositionState::create(specification));
    const auto x = take(core::AxisPositionObservation::create(
        take(core::AxisPosition::create(
            id<core::StageAxisId>("x"), 10000.0,
            core::AxisCoordinateUnit::Micrometers)),
        0.5,
        core::StagePositionEvidence::EncoderMeasured));
    const auto y = take(core::AxisPositionObservation::create(
        take(core::AxisPosition::create(
            id<core::StageAxisId>("y"), 20000.0,
            core::AxisCoordinateUnit::Micrometers)),
        0.5,
        core::StagePositionEvidence::EncoderMeasured));
    state = take(state.afterVerifiedHomingObservation(
        x, core::ObservationSource::HardwareReadback));
    state = take(state.afterVerifiedHomingObservation(
        y, core::ObservationSource::HardwareReadback));
    return state;
}

struct RuntimeFixture final {
    application::runtime::RuntimeWakeSignal wake;
    core::RuntimeDeviceCorrelation device{
        id<core::MicroscopeId>("scope"),
        id<core::RuntimeId>("runtime"),
        take(core::RuntimeGeneration::create(2)),
        take(core::DeviceSessionGeneration::create(4)),
        id<core::DeviceId>("stage")};
    std::unique_ptr<core::SerializedOperationRuntime<
        core::StageMotionFeedbackPayload>> runtime;

    RuntimeFixture() {
        runtime = take(core::SerializedOperationRuntime<
            core::StageMotionFeedbackPayload>::create(
                device.microscopeId,
                device.runtimeId,
                device.runtimeGeneration,
                8,
                wake));
        take(runtime->bindOwnerThread());
        take(runtime->openDeviceSession(device));
    }

    core::StageMotionCommandContext prepare(
        const char* operationText,
        const char* stepText,
        const char* commandText) {
        const auto operation = id<core::OperationId>(operationText);
        const auto step = id<core::StepId>(stepText);
        const auto command = id<core::CommandId>(commandText);
        take(runtime->createOperation(operation));
        take(runtime->startOperation(operation));
        take(runtime->createStep(operation, step));
        take(runtime->dispatchCommand(operation, step, command, device));
        take(runtime->recordAdapterAccepted(operation, step));
        return take(core::StageMotionCommandContext::create(
            device, core::OperationCorrelation{operation, step, command}));
    }
};

core::MoveAxesTo targetCommand() {
    const auto x = take(core::AxisPosition::create(
        id<core::StageAxisId>("x"), 40000.0,
        core::AxisCoordinateUnit::Micrometers));
    const auto y = take(core::AxisPosition::create(
        id<core::StageAxisId>("y"), 45000.0,
        core::AxisCoordinateUnit::Micrometers));
    return take(core::MoveAxesTo::create({x, y}));
}

application::motion::StageMotionErrorBudget calibratedBudget(
    std::optional<double> xBound,
    std::optional<double> yBound) {
    auto x = take(application::motion::StageAxisMotionErrorContribution::create(
        id<core::StageAxisId>("x"),
        core::AxisCoordinateUnit::Micrometers,
        xBound));
    auto y = take(application::motion::StageAxisMotionErrorContribution::create(
        id<core::StageAxisId>("y"),
        core::AxisCoordinateUnit::Micrometers,
        yBound));
    return take(application::motion::StageMotionErrorBudget::create(
        {std::move(x), std::move(y)}));
}

core::FeedbackEnvelope<core::StageMotionFeedbackPayload> completedMove(
    const core::StageMotionCommandContext& context) {
    const auto axes = take(core::StageAxisSet::create({
        id<core::StageAxisId>("x"),
        id<core::StageAxisId>("y")}));
    const auto payload = take(core::StageMotionActionCompletion::create(
        core::StageMotionAction::MoveAxesTo, axes));
    const auto metadata = take(core::FeedbackMetadata::create(
        core::FeedbackCorrelation{
            context.runtimeDevice(), context.operation()},
        core::FeedbackKind::DeviceActionCompleted,
        core::ObservationSource::HardwareReadback));
    return core::FeedbackEnvelope<core::StageMotionFeedbackPayload>::create(
        metadata, core::StageMotionFeedbackPayload{payload});
}
} // namespace

int main() {
    int failures = 0;
    const auto check = [&failures](bool condition, const char* message) {
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    };

    const auto specification = carrierSpecification();
    const auto initial = referencedPosition(specification);
    const auto command = targetCommand();
    const auto budget = calibratedBudget(2.0, 3.0);

    RuntimeFixture fixture;
    const auto context = fixture.prepare(
        "move-operation", "move-step", "move-command");
    auto envelope = completedMove(context);
    check(fixture.runtime->feedbackIngress().submit(std::move(envelope)) ==
              core::FeedbackSubmitStatus::Enqueued,
          "device action completion is accepted by the scoped feedback ingress");
    auto processed = take(fixture.runtime->processNextFeedback());
    check(processed &&
          processed->disposition ==
              core::FeedbackProcessingDisposition::AppliedToOperation &&
          processed->stepStatus == core::StepStatus::AwaitingObservation,
          "the exact completed move advances to AwaitingObservation, not Verified");

    auto reduced = take(
        application::motion::StagePositionCommandReducer::reduceCompletedMoveAxesTo(
            initial, *processed, context, command, budget,
            fixture.runtime->feedbackLossLatched()));
    check(std::abs(reduced.position.horizontal().position()->value() - 40000.0) < 1e-9 &&
          std::abs(reduced.position.transverse().position()->value() - 45000.0) < 1e-9,
          "confirmed MoveAxesTo updates the two Core-derived target coordinates");
    check(reduced.position.horizontal().evidence() ==
              core::StagePositionEvidence::CommandDerivedEstimate &&
          reduced.position.transverse().evidence() ==
              core::StagePositionEvidence::CommandDerivedEstimate,
          "command completion creates estimates, not measured coordinates");
    check(reduced.position.horizontal().accumulatedPositionErrorBound() ==
              std::optional<double>(2.5) &&
          reduced.position.transverse().accumulatedPositionErrorBound() ==
              std::optional<double>(3.5),
          "the per-axis calibrated motion bounds accumulate with prior position uncertainty");

    auto wrongCommand = take(core::StageMotionCommandContext::create(
        fixture.device,
        core::OperationCorrelation{
            id<core::OperationId>("move-operation"),
            id<core::StepId>("move-step"),
            id<core::CommandId>("different-command")}));
    auto wrongCorrelation =
        application::motion::StagePositionCommandReducer::reduceCompletedMoveAxesTo(
            initial, *processed, wrongCommand, command, budget,
            fixture.runtime->feedbackLossLatched());
    check(!wrongCorrelation &&
          wrongCorrelation.error().code ==
              application::motion::StagePositionCommandReductionErrorCode::CommandContextMismatch,
          "a completion receipt cannot update position for another command");

    auto uncalibratedBudget = calibratedBudget(std::nullopt, 3.0);
    auto uncalibrated = take(
        application::motion::StagePositionCommandReducer::reduceCompletedMoveAxesTo(
            initial, *processed, context, command, uncalibratedBudget,
            fixture.runtime->feedbackLossLatched()));
    check(!uncalibrated.position.horizontal().accumulatedPositionErrorBound() &&
          uncalibrated.position.horizontal().requiresHoming(5.0) &&
          uncalibrated.position.transverse().accumulatedPositionErrorBound() ==
              std::optional<double>(3.5),
          "a missing calibrated bound remains unknown and forces homing instead of becoming zero");

    auto lossBlocked =
        application::motion::StagePositionCommandReducer::reduceCompletedMoveAxesTo(
            initial, *processed, context, command, budget, true);
    check(!lossBlocked &&
          lossBlocked.error().code ==
              application::motion::StagePositionCommandReductionErrorCode::FeedbackLossUnresolved,
          "an unresolved feedback-loss latch blocks command-derived position updates");

    auto duplicateX = take(
        application::motion::StageAxisMotionErrorContribution::create(
            id<core::StageAxisId>("x"),
            core::AxisCoordinateUnit::Micrometers,
            1.0));
    auto duplicateBudget =
        application::motion::StageMotionErrorBudget::create(
            {duplicateX, duplicateX});
    check(!duplicateBudget &&
          duplicateBudget.error() ==
              application::motion::StageAxisMotionErrorContributionError::DuplicateAxisId,
          "an error budget cannot define the same physical axis more than once");

    auto invalidBound =
        application::motion::StageAxisMotionErrorContribution::create(
            id<core::StageAxisId>("x"),
            core::AxisCoordinateUnit::Micrometers,
            -0.1);
    check(!invalidBound &&
          invalidBound.error() ==
              application::motion::StageAxisMotionErrorContributionError::InvalidBound,
          "negative conservative error bounds are rejected");

    fixture.runtime->closeFeedbackIngress();

    if (failures != 0) {
        std::cerr << failures << " stage position command reducer test(s) failed\n";
        return 1;
    }
    std::cout << "All stage position command reducer tests passed\n";
    return 0;
}
