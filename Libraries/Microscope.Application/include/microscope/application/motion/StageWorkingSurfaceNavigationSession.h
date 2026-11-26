#pragma once

#include "microscope/application/motion/StageWorkingSurfaceNavigationControlQueue.h"
#include "microscope/application/runtime/SerializedRuntimeWorker.h"

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace microscope::application::motion {

// Owns the stage capability's in-process lifetime for one microscope runtime
// incarnation. The application composition owns this object and the injected
// physical driver adapter. The session never creates a driver per command.
enum class StageWorkingSurfaceNavigationSessionErrorCode {
    AlreadyRun,
    OperationRuntimeCreationFailed,
    NavigationRuntimeCreationFailed,
    ControlQueueCreationFailed,
    OwnerThreadBindingFailed,
    DeviceSessionOpenFailed,
    WorkerFailed,
    DeviceSessionCloseFailed
};

struct StageWorkingSurfaceNavigationSessionError final {
    StageWorkingSurfaceNavigationSessionErrorCode code;
    std::optional<core::SerializedOperationRuntimeError> operationRuntimeError;
    std::optional<StageWorkingSurfaceNavigationRuntimeError> navigationRuntimeError;
    std::optional<runtime::SerializedRuntimeWorkerError> workerError;
};

struct StageWorkingSurfaceNavigationSessionCallbacks final {
    // Callbacks run on the stage owner thread. Copy the supplied values into
    // the UI's own publication mechanism; never retain references to worker state.
    std::function<void(
        StageWorkingSurfaceNavigationRuntimeUpdate,
        core::StagePosition3D,
        core::StageWorkingSurface2D)> onStateChanged;

    std::function<void(StageWorkingSurfaceNavigationRuntimeError)> onNavigationError;
    std::function<void(StageWorkingSurfaceNavigationControlRequestOutcome)> onRequestOutcome;
    std::function<void(StageWorkingSurfaceNavigationSessionError)> onSessionError;
};

class StageWorkingSurfaceNavigationSession final {
public:
    using Pointer = std::unique_ptr<StageWorkingSurfaceNavigationSession>;
    using OperationRuntime = StageWorkingSurfaceNavigationRuntime::Runtime;
    using Result = core::Result<bool, StageWorkingSurfaceNavigationSessionError>;

    [[nodiscard]] static core::Result<
        Pointer, StageWorkingSurfaceNavigationSessionError> create(
            core::StageWorkingSurface2D surface,
            core::StagePosition3D initialPosition,
            core::StageXYHomingPolicy homingPolicy,
            StageMotionErrorBudget errorBudget,
            StageWorkingSurfaceNavigationVerificationPolicy verificationPolicy,
            core::RuntimeDeviceCorrelation device,
            core::IStageMotionPort& persistentDriver,
            std::size_t feedbackCapacity,
            std::size_t controlCapacity) {
        auto session = Pointer(new StageWorkingSurfaceNavigationSession(
            std::move(device), persistentDriver));

        auto operationRuntime = OperationRuntime::create(
            session->device_.microscopeId,
            session->device_.runtimeId,
            session->device_.runtimeGeneration,
            feedbackCapacity,
            session->wakeSignal_);
        if (!operationRuntime) {
            return createFailure(
                StageWorkingSurfaceNavigationSessionErrorCode::OperationRuntimeCreationFailed,
                operationRuntime.error());
        }
        session->operationRuntime_ = std::move(operationRuntime).value();

        auto navigation = StageWorkingSurfaceNavigationRuntime::create(
            std::move(surface),
            std::move(initialPosition),
            std::move(homingPolicy),
            std::move(errorBudget),
            std::move(verificationPolicy),
            session->device_,
            persistentDriver,
            *session->operationRuntime_);
        if (!navigation) {
            auto error = makeError(
                StageWorkingSurfaceNavigationSessionErrorCode::NavigationRuntimeCreationFailed);
            error.navigationRuntimeError = navigation.error();
            return core::Result<Pointer, StageWorkingSurfaceNavigationSessionError>::failure(
                std::move(error));
        }
        session->navigation_ =
            std::make_unique<StageWorkingSurfaceNavigationRuntime>(
                std::move(navigation).value());

        auto queue = StageWorkingSurfaceNavigationControlQueue::create(
            controlCapacity, session->wakeSignal_);
        if (!queue) {
            return createFailure(
                StageWorkingSurfaceNavigationSessionErrorCode::ControlQueueCreationFailed);
        }
        session->controlQueue_ = std::move(queue).value();

        return core::Result<Pointer, StageWorkingSurfaceNavigationSessionError>::success(
            std::move(session));
    }

    StageWorkingSurfaceNavigationSession(const StageWorkingSurfaceNavigationSession&) = delete;
    StageWorkingSurfaceNavigationSession& operator=(
        const StageWorkingSurfaceNavigationSession&) = delete;
    StageWorkingSurfaceNavigationSession(StageWorkingSurfaceNavigationSession&&) = delete;
    StageWorkingSurfaceNavigationSession& operator=(
        StageWorkingSurfaceNavigationSession&&) = delete;

    // Safe for front-end producer threads. The target is a value captured from
    // a particular canvas/slide-registration revision; no mutable stage object
    // or operation identity crosses this boundary.
    [[nodiscard]] StageWorkingSurfaceNavigationControlSubmitStatus submitNavigation(
        const core::SlideNavigationTarget2D& target) {
        if (shutdownRequested_.load(std::memory_order_acquire)) {
            return StageWorkingSurfaceNavigationControlSubmitStatus::Closed;
        }
        return controlQueue_->submit(
            StageWorkingSurfaceNavigationControlRequest{target});
    }

    // Closes the producer side immediately. Already accepted requests drain in
    // FIFO order. The persistent active operation is allowed to reach a terminal
    // result before its device-session binding and feedback ingress are retired.
    // If an active command loses all feedback, shutdown remains blocked rather
    // than pretending that the physical stage is idle.
    void requestShutdown() noexcept {
        if (!shutdownRequested_.exchange(true, std::memory_order_acq_rel)) {
            controlQueue_->close();
        }
    }

    [[nodiscard]] bool shutdownRequested() const noexcept {
        return shutdownRequested_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool closed() const noexcept {
        return closed_.load(std::memory_order_acquire);
    }

    // Blocking loop. The host starts this method on its dedicated stage owner
    // thread and joins that thread before destroying this session or the driver.
    // All mutable stage state and SerializedOperationRuntime transitions occur
    // on that same thread for the complete session lifetime.
    [[nodiscard]] Result run(StageWorkingSurfaceNavigationSessionCallbacks callbacks = {}) {
        bool expected = false;
        if (!runStarted_.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel)) {
            return Result::failure(makeError(
                StageWorkingSurfaceNavigationSessionErrorCode::AlreadyRun));
        }

        auto owner = operationRuntime_->bindOwnerThread();
        if (!owner) {
            auto error = makeError(
                StageWorkingSurfaceNavigationSessionErrorCode::OwnerThreadBindingFailed);
            error.operationRuntimeError = owner.error();
            return Result::failure(std::move(error));
        }

        auto opened = operationRuntime_->openDeviceSession(device_);
        if (!opened) {
            auto error = makeError(
                StageWorkingSurfaceNavigationSessionErrorCode::DeviceSessionOpenFailed);
            error.operationRuntimeError = opened.error();
            return Result::failure(std::move(error));
        }

        auto navigationOwner = navigation_->bindOwnerThread();
        if (!navigationOwner) {
            auto error = makeError(
                StageWorkingSurfaceNavigationSessionErrorCode::OwnerThreadBindingFailed);
            error.navigationRuntimeError = navigationOwner.error();
            return Result::failure(std::move(error));
        }

        auto callbackState =
            std::make_shared<StageWorkingSurfaceNavigationSessionCallbacks>(
                std::move(callbacks));

        auto worker = runtime::SerializedRuntimeWorker::run(
            *operationRuntime_,
            wakeSignal_,
            [this, callbackState](const auto& feedback, auto&) {
                auto update = navigation_->processFeedback(feedback);
                if (update) {
                    publish(*callbackState, std::move(update).value());
                } else {
                    notifyNavigationError(*callbackState, update.error());
                    requestShutdown();
                }
                finishShutdownIfIdle(*callbackState);
            },
            [this, callbackState](auto&) {
                auto drained = controlQueue_->drain(*navigation_);
                if (!drained) {
                    notifyNavigationError(*callbackState, drained.error());
                    requestShutdown();
                    finishShutdownIfIdle(*callbackState);
                    return;
                }

                auto result = std::move(drained).value();
                if (result.healthUpdate) {
                    publish(*callbackState, std::move(*result.healthUpdate));
                }
                for (auto& outcome : result.outcomes) {
                    if (outcome.update) {
                        publish(*callbackState, std::move(*outcome.update));
                    }
                    if (outcome.error && callbackState->onRequestOutcome) {
                        callbackState->onRequestOutcome(std::move(outcome));
                    } else if (outcome.update && callbackState->onRequestOutcome) {
                        callbackState->onRequestOutcome(std::move(outcome));
                    }
                }
                finishShutdownIfIdle(*callbackState);
            });

        if (!worker) {
            auto error = makeError(
                StageWorkingSurfaceNavigationSessionErrorCode::WorkerFailed);
            error.workerError = worker.error();
            notifySessionError(*callbackState, error);
            return Result::failure(std::move(error));
        }

        return Result::success(true);
    }

    // Immutable snapshots are returned by value. The active mutable objects
    // themselves remain private to the owner thread.
    [[nodiscard]] core::StageWorkingSurface2D workingSurfaceSnapshot() const {
        return navigation_->surface();
    }

private:
    StageWorkingSurfaceNavigationSession(
        core::RuntimeDeviceCorrelation device,
        core::IStageMotionPort& persistentDriver)
        : device_(std::move(device)), driver_(&persistentDriver) {}

    [[nodiscard]] static StageWorkingSurfaceNavigationSessionError makeError(
        StageWorkingSurfaceNavigationSessionErrorCode code) {
        return StageWorkingSurfaceNavigationSessionError{
            code, std::nullopt, std::nullopt, std::nullopt};
    }

    [[nodiscard]] static core::Result<
        Pointer, StageWorkingSurfaceNavigationSessionError> createFailure(
            StageWorkingSurfaceNavigationSessionErrorCode code,
            std::optional<core::SerializedOperationRuntimeError> runtimeError = std::nullopt) {
        auto error = makeError(code);
        error.operationRuntimeError = runtimeError;
        return core::Result<Pointer, StageWorkingSurfaceNavigationSessionError>::failure(
            std::move(error));
    }

    void publish(
        StageWorkingSurfaceNavigationSessionCallbacks& callbacks,
        StageWorkingSurfaceNavigationRuntimeUpdate update) {
        if (callbacks.onStateChanged) {
            callbacks.onStateChanged(
                std::move(update),
                navigation_->position(),
                navigation_->surface());
        }
    }

    static void notifyNavigationError(
        StageWorkingSurfaceNavigationSessionCallbacks& callbacks,
        const StageWorkingSurfaceNavigationRuntimeError& error) {
        if (callbacks.onNavigationError) callbacks.onNavigationError(error);
    }

    static void notifySessionError(
        StageWorkingSurfaceNavigationSessionCallbacks& callbacks,
        const StageWorkingSurfaceNavigationSessionError& error) {
        if (callbacks.onSessionError) callbacks.onSessionError(error);
    }

    void finishShutdownIfIdle(
        StageWorkingSurfaceNavigationSessionCallbacks& callbacks) {
        if (!shutdownRequested_.load(std::memory_order_acquire) ||
            closed_.load(std::memory_order_acquire) ||
            navigation_->navigationActive() ||
            controlQueue_->pendingCount() != 0) {
            return;
        }

        auto sessionClosed = operationRuntime_->closeDeviceSession(
            device_.deviceId, device_.deviceSessionGeneration);
        if (!sessionClosed) {
            auto error = makeError(
                StageWorkingSurfaceNavigationSessionErrorCode::DeviceSessionCloseFailed);
            error.operationRuntimeError = sessionClosed.error();
            notifySessionError(callbacks, error);
            return;
        }

        operationRuntime_->closeFeedbackIngress();
        closed_.store(true, std::memory_order_release);
    }

    core::RuntimeDeviceCorrelation device_;
    core::IStageMotionPort* driver_; // non-owning; application composition owns it
    runtime::RuntimeWakeSignal wakeSignal_;
    std::unique_ptr<OperationRuntime> operationRuntime_;
    std::unique_ptr<StageWorkingSurfaceNavigationRuntime> navigation_;
    std::unique_ptr<StageWorkingSurfaceNavigationControlQueue> controlQueue_;
    std::atomic<bool> runStarted_{false};
    std::atomic<bool> shutdownRequested_{false};
    std::atomic<bool> closed_{false};
};

} // namespace microscope::application::motion
