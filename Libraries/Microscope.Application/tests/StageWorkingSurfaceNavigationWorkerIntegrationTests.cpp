#include "microscope/application/motion/StageWorkingSurfaceNavigationControlQueue.h"
#include "microscope/application/runtime/SerializedRuntimeWorker.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
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

template<class T, class E>
T take(core::Result<T, E> result) {
    if (!result) {
        throw std::runtime_error("stage worker integration fixture rejected a valid value");
    }
    return std::move(result).value();
}

template<class T>
T id(const char* value) {
    return take(T::create(value));
}

core::AxisSpecification axis(const char* name) {
    return take(core::AxisSpecification::create(
        id<core::StageAxisId>(name),
        core::AxisMotionType::Linear,
        core::AxisCoordinateUnit::Micrometers,
        take(core::AxisOperatingRange::create(
            -100000.0, 150000.0, core::AxisCoordinateUnit::Micrometers)),
        take(core::PositiveAxisResolution::create(0.1)),
        std::nullopt, std::nullopt, std::nullopt,
        core::AxisPositionFeedbackCapability::DeviceReported));
}

core::StageSpecification stageSpecification() {
    return take(core::StageSpecification::create(
        id<core::EquipmentComponentId>("stage"),
        {axis("x"), axis("y"), axis("focus-z")}));
}

core::StageWorkingSurface2D workingSurface(
    const core::StageSpecification& stage) {
    auto surface = take(core::StageWorkingSurface2D::createEmpty(
        stage,
        id<core::StageAxisId>("x"),
        id<core::StageAxisId>("y"),
        {0.0, 0.0},
        take(core::StageTravelRange2D::create(150000.0, 60000.0))));

    return take(surface.withSlideRectangle(take(
        core::SlideRectangleDefinition2D::create(
            id<core::SlideId>("slide-integration"),
            id<core::CalibrationRevisionId>("slide-integration-registration-r1"),
            {10000.0, 20000.0},
            {85000.0, 45000.0},
            core::SlideRectangleLengthAxis::StageX,
            75000.0,
            25000.0))));
}

core::AxisPositionObservation measuredHome(
    const char* axisId, double coordinate) {
    return take(core::AxisPositionObservation::create(
        take(core::AxisPosition::create(
            id<core::StageAxisId>(axisId),
            coordinate,
            core::AxisCoordinateUnit::Micrometers)),
        0.2,
        core::StagePositionEvidence::EncoderMeasured));
}

core::StagePosition3D referencedPosition(
    const core::StageWorkingSurface2D& surface,
    const core::StageSpecification& stage) {
    auto position = take(core::StagePosition3D::unknown(
        surface, stage, id<core::StageAxisId>("focus-z")));
    position = take(position.afterVerifiedHomingObservation(
        measuredHome("x", 20000.0),
        core::ObservationSource::HardwareReadback));
    position = take(position.afterVerifiedHomingObservation(
        measuredHome("y", 25000.0),
        core::ObservationSource::HardwareReadback));
    return position;
}

application::motion::StageMotionErrorBudget errorBudget() {
    const auto x = take(application::motion::StageAxisMotionErrorContribution::create(
        id<core::StageAxisId>("x"),
        core::AxisCoordinateUnit::Micrometers,
        0.1));
    const auto y = take(application::motion::StageAxisMotionErrorContribution::create(
        id<core::StageAxisId>("y"),
        core::AxisCoordinateUnit::Micrometers,
        0.1));
    return take(application::motion::StageMotionErrorBudget::create({x, y}));
}

class SimulatedStageController final : public core::IStageMotionPort {
public:
    unsigned moveToCalls{0};
    std::optional<core::MoveAxesTo> lastMove;
    std::optional<core::StageMotionCommandContext> pendingContext;
    core::IFeedbackIngress<core::StageMotionFeedbackPayload>* pendingIngress{nullptr};

    core::StageMotionSubmissionResult moveAxesTo(
        const core::StageMotionCommandContext& context,
        const core::MoveAxesTo& command,
        core::IFeedbackIngress<core::StageMotionFeedbackPayload>& ingress) override {
        ++moveToCalls;
        lastMove = command;
        pendingContext = context;
        pendingIngress = &ingress;
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
        return core::StageMotionSubmissionResult::rejected(
            core::StageMotionSubmissionRejection::UnsupportedOperation);
    }

    core::StageMotionSubmissionResult stopAxes(
        const core::StageMotionCommandContext&,
        const core::StopAxes&,
        core::IFeedbackIngress<core::StageMotionFeedbackPayload>&) override {
        return core::StageMotionSubmissionResult::accepted();
    }

    core::FeedbackSubmitStatus publishCompletedMove() {
        if (!pendingContext || pendingIngress == nullptr) {
            throw std::runtime_error("no accepted stage move is awaiting simulated feedback");
        }

        const auto axes = take(core::StageAxisSet::create({
            id<core::StageAxisId>("x"),
            id<core::StageAxisId>("y")}));
        const auto completed = take(core::StageMotionActionCompletion::create(
            core::StageMotionAction::MoveAxesTo,
            axes));
        const auto metadata = take(core::FeedbackMetadata::create(
            core::FeedbackCorrelation{
                pendingContext->runtimeDevice(),
                pendingContext->operation()},
            core::FeedbackKind::DeviceActionCompleted,
            core::ObservationSource::HardwareReadback));
        auto envelope = core::FeedbackEnvelope<core::StageMotionFeedbackPayload>::create(
            metadata,
            core::StageMotionFeedbackPayload{completed});

        auto* ingress = pendingIngress;
        pendingContext.reset();
        pendingIngress = nullptr;
        if (ingress == nullptr) {
            throw std::runtime_error(\"the accepted stage command lost its feedback ingress\");
        }
        return ingress->submit(std::move(envelope));
    }
};
} // namespace

int main() {
    int localFailures = 0;
    const auto fail = [&localFailures](const char* message) {
        ++localFailures;
        std::cerr << "FAIL: " << message << '\n';
    };

    application::runtime::RuntimeWakeSignal wake;
    const auto device = core::RuntimeDeviceCorrelation{
        id<core::MicroscopeId>("integration-scope"),
        id<core::RuntimeId>("integration-runtime"),
        take(core::RuntimeGeneration::create(1)),
        take(core::DeviceSessionGeneration::create(1)),
        id<core::DeviceId>("stage-controller")};

    auto runtime = take(core::SerializedOperationRuntime<
        core::StageMotionFeedbackPayload>::create(
            device.microscopeId,
            device.runtimeId,
            device.runtimeGeneration,
            8,
            wake));

    const auto stage = stageSpecification();
    const auto surface = workingSurface(stage);
    auto position = referencedPosition(surface, stage);
    const auto homing = take(core::StageXYHomingPolicy::create(5.0, 5.0));
    const auto verification = take(
        application::motion::StageWorkingSurfaceNavigationVerificationPolicy::create(
            2.0, 2.0, 0.5, 0.5,
            application::motion::StageWorkingSurfaceNavigationPositionVerificationMode::
                BoundedCommandEstimate));

    SimulatedStageController controller;
    auto navigation = take(
        application::motion::StageWorkingSurfaceNavigationRuntime::create(
            surface,
            std::move(position),
            homing,
            errorBudget(),
            verification,
            device,
            controller,
            *runtime));

    auto queue = take(
        application::motion::StageWorkingSurfaceNavigationControlQueue::create(
            2, wake));

    const auto target = take(surface.createNavigationTarget(
        id<core::SlideId>("slide-integration"),
        {20000.0, 10000.0}));
    const application::motion::StageWorkingSurfaceNavigationControlRequest request{target};
    if (queue->submit(request) !=
        application::motion::StageWorkingSurfaceNavigationControlSubmitStatus::Enqueued) {
        fail("the application control queue accepts the revision-bound slide target");
        return 1;
    }

    std::optional<application::motion::StageWorkingSurfaceNavigationRuntimeUpdate>
        terminalUpdate;
    std::optional<core::StagePosition3D> terminalPosition;
    std::optional<std::string> callbackFailure;
    bool sessionOpened = false;

    using WorkerResult = core::Result<
        bool, application::runtime::SerializedRuntimeWorkerError>;
    std::optional<WorkerResult> workerResult;

    std::thread worker([&] {
        workerResult.emplace(application::runtime::SerializedRuntimeWorker::run(
            *runtime,
            wake,
            [&](const core::FeedbackProcessingResult<core::StageMotionFeedbackPayload>& feedback,
                auto& ownerRuntime) {
                auto update = navigation.processFeedback(feedback);
                if (!update) {
                    callbackFailure = "the stage owner rejected simulated controller feedback";
                    ownerRuntime.closeFeedbackIngress();
                    return;
                }

                const auto disposition = update.value().disposition;
                if (disposition ==
                        application::motion::StageWorkingSurfaceNavigationRuntimeDisposition::
                            NavigationCompleted ||
                    disposition ==
                        application::motion::StageWorkingSurfaceNavigationRuntimeDisposition::
                            NavigationFailed ||
                    disposition ==
                        application::motion::StageWorkingSurfaceNavigationRuntimeDisposition::
                            NavigationPartiallyCompleted) {
                    terminalUpdate = update.value();
                    auto snapshot = navigation.positionSnapshot();
                    if (snapshot) {
                        terminalPosition = std::move(snapshot).value();
                    } else {
                        callbackFailure = "the stage owner could not publish its terminal position";
                    }
                    ownerRuntime.closeFeedbackIngress();
                }
            },
            [&](auto& ownerRuntime) {
                if (!sessionOpened) {
                    auto opened = ownerRuntime.openDeviceSession(device);
                    if (!opened) {
                        callbackFailure = "the simulated stage device session could not be opened";
                        ownerRuntime.closeFeedbackIngress();
                        return;
                    }
                    auto bound = navigation.bindOwnerThread();
                    if (!bound) {
                        callbackFailure = "the persistent stage owner could not bind to the worker";
                        ownerRuntime.closeFeedbackIngress();
                        return;
                    }
                    sessionOpened = true;
                }

                auto drained = queue->drain(navigation);
                if (!drained) {
                    callbackFailure = "the stage control queue could not be drained";
                    ownerRuntime.closeFeedbackIngress();
                    return;
                }

                if (controller.pendingContext) {
                    auto feedbackStatus = controller.publishCompletedMove();
                    if (feedbackStatus != core::FeedbackSubmitStatus::Enqueued) {
                        callbackFailure = "the simulated controller could not enqueue completion feedback";
                        ownerRuntime.closeFeedbackIngress();
                    }
                }
            }));
    });

    worker.join();

    check(!callbackFailure, callbackFailure ? callbackFailure->c_str() :
          "the worker callbacks completed without an integration error");
    check(workerResult && bool(*workerResult) && workerResult->value(),
          "the serialized worker drains controller feedback and exits after ingress closure");
    check(controller.moveToCalls == 1 && controller.lastMove &&
          controller.lastMove->targets().size() == 2,
          "one coordinated XY command reaches the injected stage controller port");
    check(terminalUpdate &&
          terminalUpdate->disposition ==
              application::motion::StageWorkingSurfaceNavigationRuntimeDisposition::
                  NavigationCompleted &&
          terminalUpdate->operation &&
          terminalUpdate->operation->status() == core::OperationStatus::Succeeded,
          "controller completion flows through Core verification into a successful operation");
    check(terminalPosition &&
          terminalPosition->x().positionIsUsable() &&
          terminalPosition->y().positionIsUsable() &&
          !terminalPosition->z().positionIsUsable(),
          "the verified XY result updates the retained machine state without fabricating focus-Z");
    if (terminalPosition && terminalPosition->x().position() &&
        terminalPosition->y().position()) {
        check(std::abs(terminalPosition->x().position()->value() - 30000.0) < 1e-9 &&
              std::abs(terminalPosition->y().position()->value() - 30000.0) < 1e-9,
              "the command-derived machine position matches the registered slide-local target");
    } else {
        fail("the terminal XY position is available");
    }

    if (localFailures != 0) {
        std::cerr << localFailures << " stage worker integration assertion(s) failed\n";
        return 1;
    }
    std::cout << "Stage worker integration test completed\n";
    return 0;
}
