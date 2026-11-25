#include "microscope/application/motion/StagePositionFeedbackReducer.h"
#include "microscope/application/runtime/RuntimeWakeSignal.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace {
using namespace microscope;

template <typename T, typename E>
T take(core::Result<T, E> result) {
    if (!result) {
        throw std::runtime_error("stage position reducer fixture rejected");
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

core::AxisPositionObservation position(
    const char* axisId,
    double value,
    double uncertainty,
    core::StagePositionEvidence evidence) {
    return take(core::AxisPositionObservation::create(
        take(core::AxisPosition::create(
            id<core::StageAxisId>(axisId),
            value,
            core::AxisCoordinateUnit::Micrometers)),
        uncertainty,
        evidence));
}

core::FeedbackEnvelope<core::StageMotionFeedbackPayload> positionFeedback(
    const core::RuntimeDeviceCorrelation& device,
    const char* axisId,
    double value,
    core::StagePositionEvidence evidence,
    core::ObservationSource source = core::ObservationSource::HardwareReadback) {
    auto observation = position(axisId, value, 0.2, evidence);
    auto metadata = take(core::FeedbackMetadata::create(
        core::FeedbackCorrelation{device, std::nullopt},
        core::FeedbackKind::DeviceStateObserved,
        source));
    return core::FeedbackEnvelope<core::StageMotionFeedbackPayload>::create(
        metadata, core::StageMotionFeedbackPayload{std::move(observation)});
}

core::FeedbackEnvelope<core::StageMotionFeedbackPayload> homeReferenceFeedback(
    const core::RuntimeDeviceCorrelation& device,
    const core::OperationId& operation,
    const core::StepId& step,
    const core::CommandId& command) {
    auto x = take(core::AxisPosition::create(
        id<core::StageAxisId>("x"), 0.0, core::AxisCoordinateUnit::Micrometers));
    auto y = take(core::AxisPosition::create(
        id<core::StageAxisId>("y"), 0.0, core::AxisCoordinateUnit::Micrometers));
    auto xObservation = take(core::AxisPositionObservation::create(
        x, 0.5, core::StagePositionEvidence::ControllerReported));
    auto yObservation = take(core::AxisPositionObservation::create(
        y, 0.25, core::StagePositionEvidence::EncoderMeasured));
    auto payload = take(core::StageHomeReferenceObservation::create(
        {xObservation, yObservation}));
    auto metadata = take(core::FeedbackMetadata::create(
        core::FeedbackCorrelation{
            device, core::OperationCorrelation{operation, step, command}},
        core::FeedbackKind::HomeReferenceEstablished,
        core::ObservationSource::HardwareReadback));
    return core::FeedbackEnvelope<core::StageMotionFeedbackPayload>::create(
        metadata, core::StageMotionFeedbackPayload{std::move(payload)});
}

core::FeedbackEnvelope<core::StageMotionFeedbackPayload> motionStateFeedback(
    const core::RuntimeDeviceCorrelation& device) {
    auto observation = take(core::AxisMotionStateObservation::create(
        id<core::StageAxisId>("x"), core::StageAxisMotionState::Idle));
    auto metadata = take(core::FeedbackMetadata::create(
        core::FeedbackCorrelation{device, std::nullopt},
        core::FeedbackKind::DeviceStateObserved,
        core::ObservationSource::HardwareReadback));
    return core::FeedbackEnvelope<core::StageMotionFeedbackPayload>::create(
        metadata, core::StageMotionFeedbackPayload{std::move(observation)});
}
} // namespace

int main() {
    int failures = 0;
    const auto check = [&failures](bool value, const char* message) {
        if (!value) {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    };

    application::runtime::RuntimeWakeSignal wakeSignal;
    const auto microscopeId = id<core::MicroscopeId>("scope");
    const auto runtimeId = id<core::RuntimeId>("runtime");
    const auto runtimeGeneration = take(core::RuntimeGeneration::create(3));
    const auto currentSession = core::RuntimeDeviceCorrelation{
        microscopeId,
        runtimeId,
        runtimeGeneration,
        take(core::DeviceSessionGeneration::create(7)),
        id<core::DeviceId>("stage-device")};

    auto runtime = take(core::SerializedOperationRuntime<
        core::StageMotionFeedbackPayload>::create(
            microscopeId, runtimeId, runtimeGeneration, 1, wakeSignal));
    take(runtime->bindOwnerThread());
    take(runtime->openDeviceSession(currentSession));

    auto state = take(core::StagePositionState::create(carrierSpecification()));
    state = take(state.afterVerifiedHomingObservation(
        position("x", 0.0, 0.5, core::StagePositionEvidence::ControllerReported),
        core::ObservationSource::SdkStatus));
    state = take(state.afterVerifiedHomingObservation(
        position("y", 0.0, 0.5, core::StagePositionEvidence::ControllerReported),
        core::ObservationSource::SdkStatus));

    const auto process = [&](core::FeedbackEnvelope<
        core::StageMotionFeedbackPayload> feedback) {
        if (runtime->feedbackIngress().submit(std::move(feedback)) !=
            core::FeedbackSubmitStatus::Enqueued) {
            throw std::runtime_error("stage feedback was not enqueued");
        }
        auto next = take(runtime->processNextFeedback());
        if (!next) {
            throw std::runtime_error("stage feedback queue unexpectedly empty");
        }
        return std::move(*next);
    };

    auto measured = process(positionFeedback(
        currentSession, "x", 123.25, core::StagePositionEvidence::EncoderMeasured));
    auto applied = take(application::motion::StagePositionFeedbackReducer::reduce(
        state, measured, runtime->feedbackLossLatched()));
    check(applied.disposition ==
              application::motion::StagePositionFeedbackDisposition::PositionObservationApplied &&
          std::abs(applied.position.horizontal().position()->value() - 123.25) < 1e-9 &&
          std::abs(applied.position.horizontal().accumulatedPositionErrorBound().value() - 0.2) < 1e-9,
          "fresh encoder position feedback updates the matching stage axis and uncertainty");
    state = std::move(applied.position);

    auto nonPosition = process(motionStateFeedback(currentSession));
    auto ignoredMotion = take(application::motion::StagePositionFeedbackReducer::reduce(
        state, nonPosition, runtime->feedbackLossLatched()));
    check(ignoredMotion.disposition ==
              application::motion::StagePositionFeedbackDisposition::IgnoredNonPositionPayload &&
          std::abs(ignoredMotion.position.horizontal().position()->value() - 123.25) < 1e-9,
          "motion-state feedback cannot overwrite a physical position coordinate");

    auto outsidePlane = process(positionFeedback(
        currentSession, "z", 900.0, core::StagePositionEvidence::EncoderMeasured));
    auto ignoredAxis = take(application::motion::StagePositionFeedbackReducer::reduce(
        state, outsidePlane, runtime->feedbackLossLatched()));
    check(ignoredAxis.disposition ==
              application::motion::StagePositionFeedbackDisposition::IgnoredAxisOutsidePlane &&
          std::abs(ignoredAxis.position.horizontal().position()->value() - 123.25) < 1e-9,
          "a valid but non-planar axis observation does not mutate the X/Y aggregate");

    auto previousSession = currentSession;
    previousSession.deviceSessionGeneration =
        take(core::DeviceSessionGeneration::create(6));
    auto stale = process(positionFeedback(
        previousSession, "x", 777.0, core::StagePositionEvidence::EncoderMeasured));
    check(stale.disposition ==
              core::FeedbackProcessingDisposition::StaleDeviceSession,
          "Core rejects feedback from a previous stage-device session");
    auto ignoredStale = take(application::motion::StagePositionFeedbackReducer::reduce(
        state, stale, runtime->feedbackLossLatched()));
    check(ignoredStale.disposition ==
              application::motion::StagePositionFeedbackDisposition::IgnoredNonCurrentFeedback &&
          std::abs(ignoredStale.position.horizontal().position()->value() - 123.25) < 1e-9,
          "stale feedback cannot update the current physical stage position");

    auto queuedBeforeLoss = positionFeedback(
        currentSession, "x", 888.0, core::StagePositionEvidence::EncoderMeasured);
    auto droppedOnOverflow = positionFeedback(
        currentSession, "x", 999.0, core::StagePositionEvidence::EncoderMeasured);
    check(runtime->feedbackIngress().submit(std::move(queuedBeforeLoss)) ==
              core::FeedbackSubmitStatus::Enqueued,
          "the first pre-loss receipt remains available for ordered draining");
    check(runtime->feedbackIngress().submit(std::move(droppedOnOverflow)) ==
              core::FeedbackSubmitStatus::QueueFull &&
          runtime->feedbackLossLatched(),
          "queue overflow latches the runtime feedback-loss condition");

    auto preservedButUncertain = take(runtime->processNextFeedback());
    check(preservedButUncertain &&
          preservedButUncertain->disposition ==
              core::FeedbackProcessingDisposition::UncorrelatedDeviceFeedback,
          "the preserved receipt is still drained after overflow");
    auto invalidated = take(application::motion::StagePositionFeedbackReducer::reduce(
        state, *preservedButUncertain, runtime->feedbackLossLatched()));
    check(invalidated.disposition ==
              application::motion::StagePositionFeedbackDisposition::InvalidatedAfterFeedbackLoss &&
          !invalidated.position.horizontal().positionIsUsable() &&
          !invalidated.position.transverse().positionIsUsable() &&
          std::abs(invalidated.position.horizontal().position()->value() - 123.25) < 1e-9,
          "feedback loss invalidates both plane references without applying possibly stale queued position");
    state = std::move(invalidated.position);

    check(bool(runtime->acknowledgeFeedbackLoss()),
          "the owner can clear the loss latch only after draining the queue; host policy requires readback reconciliation first");
    auto freshAfterReconciliation = process(positionFeedback(
        currentSession, "x", 130.0, core::StagePositionEvidence::EncoderMeasured));
    auto reapplied = take(application::motion::StagePositionFeedbackReducer::reduce(
        state, freshAfterReconciliation, runtime->feedbackLossLatched()));
    check(reapplied.disposition ==
              application::motion::StagePositionFeedbackDisposition::PositionObservationApplied &&
          std::abs(reapplied.position.horizontal().position()->value() - 130.0) < 1e-9 &&
          !reapplied.position.horizontal().positionIsUsable(),
          "fresh readback updates the coordinate after reconciliation but does not silently re-home the stage");

    const auto homeAxes = take(core::StageAxisSet::create({
        id<core::StageAxisId>("x"), id<core::StageAxisId>("y")}));
    const auto requestedHome = core::HomeAxes::create(homeAxes);
    const auto homeOperation = id<core::OperationId>("home-op");
    const auto homeStep = id<core::StepId>("home-step");
    const auto homeCommand = id<core::CommandId>("home-command");
    take(runtime->createOperation(homeOperation));
    take(runtime->startOperation(homeOperation));
    take(runtime->createStep(homeOperation, homeStep));
    take(runtime->dispatchCommand(homeOperation, homeStep, homeCommand, currentSession));
    take(runtime->recordAdapterAccepted(homeOperation, homeStep));

    const auto homeCommandContext = take(
        core::StageMotionCommandContext::create(
            currentSession,
            core::OperationCorrelation{homeOperation, homeStep, homeCommand}));
    auto homeResult = process(homeReferenceFeedback(
        currentSession, homeOperation, homeStep, homeCommand));
    auto prematureHome = application::motion::StagePositionFeedbackReducer::
        applyVerifiedHomeReference(
            state, homeResult, homeCommandContext, requestedHome, *runtime);
    check(!prematureHome &&
          prematureHome.error().code ==
              application::motion::StagePositionFeedbackReducerErrorCode::HomeStepNotVerified &&
          !state.horizontal().positionIsUsable() &&
          !state.transverse().positionIsUsable(),
          "a home-reference fact cannot restore machine coordinates before the runtime verifies its step");

    auto verifiedHomeStep = take(
        runtime->recordStepVerification(homeOperation, homeStep, true));
    auto referenced = take(
        application::motion::StagePositionFeedbackReducer::applyVerifiedHomeReference(
            state, homeResult, homeCommandContext, requestedHome, *runtime));
    check(verifiedHomeStep == core::StepStatus::Verified &&
          referenced.disposition ==
              application::motion::StagePositionFeedbackDisposition::HomeReferenceApplied &&
          referenced.position.horizontal().positionIsUsable() &&
          referenced.position.transverse().positionIsUsable() &&
          referenced.position.horizontal().position()->value() == 0.0 &&
          referenced.position.transverse().position()->value() == 0.0,
          "only the verified complete X/Y home-reference fact restores the stage plane");

    auto ignoredAsTelemetry = take(
        application::motion::StagePositionFeedbackReducer::reduce(
            state, homeResult, runtime->feedbackLossLatched()));
    check(ignoredAsTelemetry.disposition ==
              application::motion::StagePositionFeedbackDisposition::IgnoredNonPositionPayload &&
          !ignoredAsTelemetry.position.horizontal().positionIsUsable(),
          "home-reference evidence cannot be applied through the ordinary position-telemetry path");

    runtime->closeFeedbackIngress();

    if (failures != 0) {
        std::cerr << failures << " stage position feedback reducer test(s) failed\n";
        return 1;
    }

    std::cout << "All stage position feedback reducer tests passed\n";
    return 0;
}
