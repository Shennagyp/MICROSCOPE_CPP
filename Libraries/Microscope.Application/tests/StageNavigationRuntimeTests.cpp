#include "microscope/application/motion/StageNavigationRuntime.h"
#include "microscope/application/runtime/RuntimeWakeSignal.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace {
using namespace microscope;

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

template <typename T, typename E>
T take(core::Result<T, E> result) {
    if (!result) {
        throw std::runtime_error("stage navigation runtime fixture rejected");
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
            id<core::EquipmentComponentId>("stage"), {axis("x"), axis("y")})),
        id<core::StageAxisId>("x"),
        id<core::StageAxisId>("y"),
        core::CarrierHorizontalDirection::PositiveStageXFromLeftToRight));
}

core::SlideRegistration registration(
    const char* slideId,
    core::DualSlideSlot slot,
    double originX,
    const char* revision) {
    const auto geometry = take(core::SlideGeometry::create(75000.0, 25000.0));
    const auto source = core::ObservationSource::HardwareReadback;
    const auto topLeft = take(core::SlideCornerMeasurement::create(
        core::SlideCorner::TopLeft, {originX, 20000.0}, source, 0.25));
    const auto topRight = take(core::SlideCornerMeasurement::create(
        core::SlideCorner::TopRight, {originX + 75000.0, 20000.0}, source, 0.25));
    const auto bottomRight = take(core::SlideCornerMeasurement::create(
        core::SlideCorner::BottomRight, {originX + 75000.0, 45000.0}, source, 0.25));
    const auto bottomLeft = take(core::SlideCornerMeasurement::create(
        core::SlideCorner::BottomLeft, {originX, 45000.0}, source, 0.25));
    return take(core::SlideRegistration::create(
        id<core::SlideId>(slideId),
        id<core::CalibrationRevisionId>(revision),
        slot, geometry, {topLeft, topRight, bottomRight, bottomLeft}, 0.5));
}

core::DualSlideCarrierState carrier() {
    const auto specification = carrierSpecification();
    const auto left = registration(
        "slide-left", core::DualSlideSlot::Left, 5000.0, "left-v1");
    const auto right = registration(
        "slide-right", core::DualSlideSlot::Right, 95000.0, "right-v1");
    return take(core::DualSlideCarrierState::create(
        specification,
        take(core::SlideSlotState::create(
            core::DualSlideSlot::Left, id<core::SlideId>("slide-left"), left)),
        take(core::SlideSlotState::create(
            core::DualSlideSlot::Right, id<core::SlideId>("slide-right"), right))));
}

core::AxisPositionObservation positionObservation(
    const char* axisId,
    double value,
    double uncertainty,
    core::StagePositionEvidence evidence = core::StagePositionEvidence::EncoderMeasured) {
    return take(core::AxisPositionObservation::create(
        take(core::AxisPosition::create(
            id<core::StageAxisId>(axisId), value,
            core::AxisCoordinateUnit::Micrometers)),
        uncertainty, evidence));
}

application::motion::StageMotionErrorBudget errorBudget() {
    auto horizontal = take(application::motion::StageAxisMotionErrorContribution::create(
        id<core::StageAxisId>("x"),
        core::AxisCoordinateUnit::Micrometers, 0.1));
    auto transverse = take(application::motion::StageAxisMotionErrorContribution::create(
        id<core::StageAxisId>("y"),
        core::AxisCoordinateUnit::Micrometers, 0.1));
    return take(application::motion::StageMotionErrorBudget::create(
        {horizontal, transverse}));
}

application::motion::StageNavigationVerificationPolicy verificationPolicy(
    application::motion::StageNavigationPositionVerificationMode mode) {
    return take(application::motion::StageNavigationVerificationPolicy::create(
        1.0, 1.0, 0.5, 0.5, mode));
}

core::StageHomingPolicy homingPolicy() {
    return take(core::StageHomingPolicy::create(5.0, 5.0));
}

core::StagePositionState referencedPosition(
    const core::DualSlideCarrierSpecification& specification) {
    auto position = take(core::StagePositionState::create(specification));
    position = take(position.afterVerifiedHomingObservation(
        positionObservation("x", 20000.0, 0.2),
        core::ObservationSource::HardwareReadback));
    position = take(position.afterVerifiedHomingObservation(
        positionObservation("y", 25000.0, 0.2),
        core::ObservationSource::HardwareReadback));
    return position;
}

core::FeedbackEnvelope<core::StageMotionFeedbackPayload> actionCompleted(
    const core::RuntimeDeviceCorrelation& device,
    const core::OperationId& operation,
    const core::StepId& step,
    const core::CommandId& command) {
    const auto axes = take(core::StageAxisSet::create({
        id<core::StageAxisId>("x"), id<core::StageAxisId>("y")}));
    const auto payload = take(core::StageMotionActionCompletion::create(
        core::StageMotionAction::MoveAxesTo, axes));
    const auto metadata = take(core::FeedbackMetadata::create(
        core::FeedbackCorrelation{
            device, core::OperationCorrelation{operation, step, command}},
        core::FeedbackKind::DeviceActionCompleted,
        core::ObservationSource::HardwareReadback));
    return core::FeedbackEnvelope<core::StageMotionFeedbackPayload>::create(
        metadata, core::StageMotionFeedbackPayload{payload});
}

core::FeedbackEnvelope<core::StageMotionFeedbackPayload> positionFeedback(
    const core::RuntimeDeviceCorrelation& device,
    const core::OperationId& operation,
    const core::StepId& step,
    const core::CommandId& command,
    const char* axisId,
    double position) {
    auto observation = positionObservation(axisId, position, 0.2);
    const auto metadata = take(core::FeedbackMetadata::create(
        core::FeedbackCorrelation{
            device, core::OperationCorrelation{operation, step, command}},
        core::FeedbackKind::DeviceStateObserved,
        core::ObservationSource::HardwareReadback));
    return core::FeedbackEnvelope<core::StageMotionFeedbackPayload>::create(
        metadata, core::StageMotionFeedbackPayload{std::move(observation)});
}

core::FeedbackEnvelope<core::StageMotionFeedbackPayload> homeReference(
    const core::RuntimeDeviceCorrelation& device,
    const core::OperationId& operation,
    const core::StepId& step,
    const core::CommandId& command) {
    auto payload = take(core::StageHomeReferenceObservation::create({
        positionObservation("x", 0.0, 0.2),
        positionObservation("y", 0.0, 0.2)}));
    const auto metadata = take(core::FeedbackMetadata::create(
        core::FeedbackCorrelation{
            device, core::OperationCorrelation{operation, step, command}},
        core::FeedbackKind::HomeReferenceEstablished,
        core::ObservationSource::HardwareReadback));
    return core::FeedbackEnvelope<core::StageMotionFeedbackPayload>::create(
        metadata, core::StageMotionFeedbackPayload{std::move(payload)});
}

class RecordingStagePort final : public core::IStageMotionPort {
public:
    unsigned moveToCalls{0};
    unsigned homeCalls{0};
    std::optional<core::MoveAxesTo> lastMove;

    core::StageMotionSubmissionResult moveAxesTo(
        const core::StageMotionCommandContext&,
        const core::MoveAxesTo& command,
        core::IFeedbackIngress<core::StageMotionFeedbackPayload>&) override {
        ++moveToCalls;
        lastMove = command;
        return core::StageMotionSubmissionResult::accepted();
    }

    core::StageMotionSubmissionResult moveAxesBy(
        const core::StageMotionCommandContext&,
        const core::MoveAxesBy&,
        core::IFeedbackIngress<core::StageMotionFeedbackPayload>&) override {
        return core::StageMotionSubmissionResult::rejected(
            core::StageMotionSubmissionRejection::UnsupportedOperation);
    }

    core::StageMotionSubmissionResult homeAxes(
        const core::StageMotionCommandContext&,
        const core::HomeAxes&,
        core::IFeedbackIngress<core::StageMotionFeedbackPayload>&) override {
        ++homeCalls;
        return core::StageMotionSubmissionResult::accepted();
    }

    core::StageMotionSubmissionResult stopAxes(
        const core::StageMotionCommandContext&,
        const core::StopAxes&,
        core::IFeedbackIngress<core::StageMotionFeedbackPayload>&) override {
        return core::StageMotionSubmissionResult::accepted();
    }
};

struct RuntimeFixture final {
    application::runtime::RuntimeWakeSignal wake;
    core::RuntimeDeviceCorrelation device{
        id<core::MicroscopeId>("scope"),
        id<core::RuntimeId>("runtime"),
        take(core::RuntimeGeneration::create(1)),
        take(core::DeviceSessionGeneration::create(1)),
        id<core::DeviceId>("stage-device")};
    std::unique_ptr<core::SerializedOperationRuntime<
        core::StageMotionFeedbackPayload>> runtime;

    RuntimeFixture() {
        runtime = take(core::SerializedOperationRuntime<
            core::StageMotionFeedbackPayload>::create(
                device.microscopeId, device.runtimeId,
                device.runtimeGeneration, 16, wake));
        take(runtime->bindOwnerThread());
        take(runtime->openDeviceSession(device));
    }

    void prepareOperation(const char* operationId, const char* stepId) {
        take(runtime->createOperation(id<core::OperationId>(operationId)));
        take(runtime->startOperation(id<core::OperationId>(operationId)));
        take(runtime->createStep(
            id<core::OperationId>(operationId), id<core::StepId>(stepId)));
    }

    core::FeedbackProcessingResult<core::StageMotionFeedbackPayload> process(
        core::FeedbackEnvelope<core::StageMotionFeedbackPayload> feedback) {
        if (runtime->feedbackIngress().submit(std::move(feedback)) !=
            core::FeedbackSubmitStatus::Enqueued) {
            throw std::runtime_error("stage feedback was not enqueued");
        }
        auto result = take(runtime->processNextFeedback());
        if (!result) {
            throw std::runtime_error("stage feedback queue unexpectedly empty");
        }
        return std::move(*result);
    }
};

application::motion::StageNavigationOperationIdentity navigationIds(
    const char* operationId,
    const char* stepId,
    const char* commandId,
    const char* restoredStepId,
    const char* restoredCommandId) {
    return application::motion::StageNavigationOperationIdentity{
        id<core::OperationId>(operationId),
        id<core::StepId>(stepId),
        id<core::CommandId>(commandId),
        id<core::StepId>(restoredStepId),
        id<core::CommandId>(restoredCommandId)};
}

void testOpenLoopBoundedEstimateCompletesNavigation() {
    RuntimeFixture fixture;
    const auto installedCarrier = carrier();
    RecordingStagePort port;
    auto navigation = take(application::motion::StageNavigationRuntime::create(
        installedCarrier,
        referencedPosition(installedCarrier.specification()),
        homingPolicy(),
        errorBudget(),
        verificationPolicy(
            application::motion::StageNavigationPositionVerificationMode::BoundedCommandEstimate),
        fixture.device, port, *fixture.runtime));
    take(navigation.bindOwnerThread());

    fixture.prepareOperation("open-loop-operation", "open-loop-step");
    auto started = take(navigation.beginNavigation(
        id<core::SlideId>("slide-left"), {20000.0, 10000.0},
        navigationIds("open-loop-operation", "open-loop-step", "open-loop-command",
                      "open-loop-restored-step", "open-loop-restored-command")));
    check(started.disposition ==
              application::motion::StageNavigationRuntimeDisposition::MoveDispatched &&
          port.moveToCalls == 1 && port.homeCalls == 0,
          "referenced stage dispatches one coordinated absolute move");

    auto completion = fixture.process(actionCompleted(
        fixture.device, id<core::OperationId>("open-loop-operation"),
        id<core::StepId>("open-loop-step"),
        id<core::CommandId>("open-loop-command")));
    auto completed = take(navigation.processFeedback(completion));
    check(completed.disposition ==
              application::motion::StageNavigationRuntimeDisposition::NavigationCompleted &&
          completed.operation &&
          completed.operation->status() == core::OperationStatus::Succeeded,
          "a bounded Core-derived estimate can verify open-loop movement without synthetic sensor feedback");
    check(navigation.position().horizontal().evidence() ==
              core::StagePositionEvidence::CommandDerivedEstimate &&
          std::abs(navigation.position().horizontal().position()->value() - 25000.0) < 1e-9 &&
          std::abs(navigation.position().horizontal().accumulatedPositionErrorBound().value() - 0.3) < 1e-9,
          "main stage state retains the command-derived coordinate and accumulated calibration bound");
}

void testControllerPositionObservationsVerifyNavigation() {
    RuntimeFixture fixture;
    const auto installedCarrier = carrier();
    RecordingStagePort port;
    auto navigation = take(application::motion::StageNavigationRuntime::create(
        installedCarrier,
        referencedPosition(installedCarrier.specification()),
        homingPolicy(),
        errorBudget(),
        verificationPolicy(
            application::motion::StageNavigationPositionVerificationMode::DevicePositionObservations),
        fixture.device, port, *fixture.runtime));
    take(navigation.bindOwnerThread());

    fixture.prepareOperation("observed-operation", "observed-step");
    take(navigation.beginNavigation(
        id<core::SlideId>("slide-left"), {20000.0, 10000.0},
        navigationIds("observed-operation", "observed-step", "observed-command",
                      "observed-restored-step", "observed-restored-command")));

    auto completion = fixture.process(actionCompleted(
        fixture.device, id<core::OperationId>("observed-operation"),
        id<core::StepId>("observed-step"),
        id<core::CommandId>("observed-command")));
    auto awaiting = take(navigation.processFeedback(completion));
    check(awaiting.disposition ==
              application::motion::StageNavigationRuntimeDisposition::AwaitingPositionFeedback,
          "closed-loop policy waits for both fresh position observations");

    auto horizontal = fixture.process(positionFeedback(
        fixture.device, id<core::OperationId>("observed-operation"),
        id<core::StepId>("observed-step"), id<core::CommandId>("observed-command"),
        "x", 25000.2));
    auto horizontalUpdate = take(navigation.processFeedback(horizontal));
    check(horizontalUpdate.disposition ==
              application::motion::StageNavigationRuntimeDisposition::AwaitingPositionFeedback,
          "one axis observation cannot verify the coordinated two-axis move");

    auto transverse = fixture.process(positionFeedback(
        fixture.device, id<core::OperationId>("observed-operation"),
        id<core::StepId>("observed-step"), id<core::CommandId>("observed-command"),
        "y", 30000.2));
    auto completed = take(navigation.processFeedback(transverse));
    check(completed.disposition ==
              application::motion::StageNavigationRuntimeDisposition::NavigationCompleted &&
          completed.operation &&
          completed.operation->status() == core::OperationStatus::Succeeded,
          "both correlated axis observations within the calibrated tolerance verify navigation");
    check(navigation.position().horizontal().evidence() ==
              core::StagePositionEvidence::EncoderMeasured &&
          std::abs(navigation.position().horizontal().position()->value() - 25000.2) < 1e-9,
          "the owner retains measured position rather than replacing it with the command target");
}

void testHomingRestoresSlideLocalTarget() {
    RuntimeFixture fixture;
    const auto installedCarrier = carrier();
    RecordingStagePort port;
    auto unknownPosition = take(core::StagePositionState::create(
        installedCarrier.specification()));
    auto navigation = take(application::motion::StageNavigationRuntime::create(
        installedCarrier,
        unknownPosition,
        homingPolicy(),
        errorBudget(),
        verificationPolicy(
            application::motion::StageNavigationPositionVerificationMode::BoundedCommandEstimate),
        fixture.device, port, *fixture.runtime));
    take(navigation.bindOwnerThread());

    fixture.prepareOperation("home-operation", "home-step");
    auto homing = take(navigation.beginNavigation(
        id<core::SlideId>("slide-left"), {20000.0, 10000.0},
        navigationIds("home-operation", "home-step", "home-command",
                      "restored-move-step", "restored-move-command")));
    check(homing.disposition ==
              application::motion::StageNavigationRuntimeDisposition::HomingDispatched &&
          port.homeCalls == 1 && port.moveToCalls == 0,
          "unknown X/Y reference dispatches homing before any slide target");

    auto home = fixture.process(homeReference(
        fixture.device, id<core::OperationId>("home-operation"),
        id<core::StepId>("home-step"), id<core::CommandId>("home-command")));
    auto restored = take(navigation.processFeedback(home));
    check(restored.disposition ==
              application::motion::StageNavigationRuntimeDisposition::RestoredMoveDispatched &&
          port.homeCalls == 1 && port.moveToCalls == 1,
          "complete verified home evidence establishes the reference and dispatches the restored move");
    check(port.lastMove &&
          std::abs(port.lastMove->targets()[0].value() - 25000.0) < 1e-9 &&
          std::abs(port.lastMove->targets()[1].value() - 30000.0) < 1e-9,
          "restoration recalculates the absolute machine target from the saved slide-local point");

    auto completion = fixture.process(actionCompleted(
        fixture.device, id<core::OperationId>("home-operation"),
        id<core::StepId>("restored-move-step"),
        id<core::CommandId>("restored-move-command")));
    auto completed = take(navigation.processFeedback(completion));
    check(completed.disposition ==
              application::motion::StageNavigationRuntimeDisposition::NavigationCompleted &&
          completed.operation &&
          completed.operation->status() == core::OperationStatus::Succeeded,
          "home and restored move complete as one verified navigation operation");
}

} // namespace

int main() {
    testOpenLoopBoundedEstimateCompletesNavigation();
    testControllerPositionObservationsVerifyNavigation();
    testHomingRestoresSlideLocalTarget();

    if (failures != 0) {
        std::cerr << failures << " stage navigation runtime test(s) failed\n";
        return 1;
    }
    std::cout << "All stage navigation runtime tests passed\n";
    return 0;
}
