#include "microscope/application/runtime/SerializedRuntimeWorker.h"
#include "microscope/core/motion/StageMotionPort.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <optional>
#include <thread>
#include <utility>
#include <variant>

namespace {
using namespace microscope;

template <typename T, typename E>
T take(core::Result<T, E> result) {
    if (!result) {
        throw std::runtime_error("runtime worker test fixture rejected");
    }
    return std::move(result).value();
}

template <typename T>
T id(const char* value) {
    return take(T::create(value));
}

bool waitUntil(const auto& predicate) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::yield();
    }
    return predicate();
}

core::FeedbackEnvelope<core::StageMotionFeedbackPayload> telemetry() {
    const core::RuntimeDeviceCorrelation device{
        id<core::MicroscopeId>("scope"),
        id<core::RuntimeId>("runtime"),
        take(core::RuntimeGeneration::create(1)),
        take(core::DeviceSessionGeneration::create(1)),
        id<core::DeviceId>("stage")};

    const auto payload = take(core::AxisMotionStateObservation::create(
        id<core::StageAxisId>("x"), core::StageAxisMotionState::Idle));
    const auto metadata = take(core::FeedbackMetadata::create(
        core::FeedbackCorrelation{device, std::nullopt},
        core::FeedbackKind::DeviceStateObserved,
        core::ObservationSource::DeviceEvent));

    return core::FeedbackEnvelope<core::StageMotionFeedbackPayload>::create(
        metadata, core::StageMotionFeedbackPayload{payload});
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
    auto runtime = take(core::SerializedOperationRuntime<
        core::StageMotionFeedbackPayload>::create(
            id<core::MicroscopeId>("scope"),
            id<core::RuntimeId>("runtime"),
            take(core::RuntimeGeneration::create(1)),
            8,
            wakeSignal));

    std::atomic<unsigned> controlDrains{0};
    std::atomic<unsigned> feedbackCallbacks{0};
    std::atomic<bool> feedbackWasValidated{false};
    std::thread::id callbackThread;
    std::thread::id ownerThread;
    std::optional<core::Result<
        bool,
        application::runtime::SerializedRuntimeWorkerError>> runResult;

    std::thread owner([&] {
        ownerThread = std::this_thread::get_id();
        runResult.emplace(application::runtime::SerializedRuntimeWorker::run(
            *runtime,
            wakeSignal,
            [&](const auto& result, auto&) {
                callbackThread = std::this_thread::get_id();
                feedbackWasValidated.store(
                    result.disposition ==
                        core::FeedbackProcessingDisposition::UnknownDeviceSession &&
                    result.receipt.has_value() &&
                    std::holds_alternative<core::AxisMotionStateObservation>(
                        result.receipt->feedback().payload()),
                    std::memory_order_release);
                feedbackCallbacks.fetch_add(1, std::memory_order_release);
            },
            [&](auto&) {
                controlDrains.fetch_add(1, std::memory_order_release);
            }));
    });

    const bool entered = waitUntil([&] {
        return controlDrains.load(std::memory_order_acquire) >= 1;
    });
    check(entered, "worker binds its owner thread and enters the control drain");

    const auto submitted = runtime->feedbackIngress().submit(telemetry());
    check(submitted == core::FeedbackSubmitStatus::Enqueued,
          "driver-thread telemetry enters the typed feedback inbox");
    const bool processed = waitUntil([&] {
        return feedbackCallbacks.load(std::memory_order_acquire) == 1;
    });
    check(processed, "feedback notification wakes the owner and drains the receipt");
    check(feedbackWasValidated.load(std::memory_order_acquire),
          "worker forwards the Core freshness disposition and typed receipt");

    wakeSignal.requestControlWake();
    const bool controlWoke = waitUntil([&] {
        return controlDrains.load(std::memory_order_acquire) >= 3;
    });
    check(controlWoke,
          "control wake is observed after the current feedback batch is drained");

    runtime->closeFeedbackIngress();
    owner.join();

    check(runResult.has_value() && *runResult &&
          runResult->value(),
          "worker exits successfully after closed ingress is fully drained");
    check(callbackThread == ownerThread,
          "capability feedback reducer executes on the serialized owner thread");
    auto wrongThreadBind = runtime->bindOwnerThread();
    check(!wrongThreadBind &&
          wrongThreadBind.error() ==
              core::SerializedOperationRuntimeError::WrongOwnerThread,
          "the same runtime cannot be rebound to a second thread");

    if (failures != 0) {
        std::cerr << failures << " serialized runtime worker test(s) failed\n";
        return 1;
    }

    std::cout << "All serialized runtime worker tests passed\n";
    return 0;
}
