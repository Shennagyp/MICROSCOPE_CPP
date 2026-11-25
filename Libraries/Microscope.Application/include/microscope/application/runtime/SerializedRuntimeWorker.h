#pragma once

#include "microscope/application/runtime/RuntimeWakeSignal.h"
#include "microscope/core/runtime/SerializedOperationRuntime.h"

#include <utility>

namespace microscope::application::runtime {

// The host starts this loop on its dedicated runtime-owner thread. It does not
// create a thread, own a device adapter, or define a universal command bus.
// The host supplies its capability-specific feedback reducer and drains its
// own typed control-request queue through the second callback.
enum class SerializedRuntimeWorkerError {
    OwnerThreadBindingFailed,
    FeedbackProcessingFailed,
    FeedbackHandlerFailed,
    ControlHandlerFailed,
    WakeSignalFault
};

class SerializedRuntimeWorker final {
public:
    template <typename TPayload, typename TFeedbackHandler, typename TControlHandler>
    [[nodiscard]] static core::Result<bool, SerializedRuntimeWorkerError> run(
        core::SerializedOperationRuntime<TPayload>& runtime,
        RuntimeWakeSignal& wakeSignal,
        TFeedbackHandler&& handleFeedback,
        TControlHandler&& drainControlRequests) {
        const auto owner = runtime.bindOwnerThread();
        if (!owner) {
            return core::Result<bool, SerializedRuntimeWorkerError>::failure(
                SerializedRuntimeWorkerError::OwnerThreadBindingFailed);
        }

        for (;;) {
            // Capture before either queue is drained. A producer that enqueues
            // during draining advances the generation; waitForChange() then
            // returns immediately instead of losing that wakeup.
            const auto observedGeneration = wakeSignal.snapshot().generation;
            if (wakeSignal.snapshot().wakeFault) {
                return core::Result<bool, SerializedRuntimeWorkerError>::failure(
                    SerializedRuntimeWorkerError::WakeSignalFault);
            }

            for (;;) {
                auto next = runtime.processNextFeedback();
                if (!next) {
                    return core::Result<bool, SerializedRuntimeWorkerError>::failure(
                        SerializedRuntimeWorkerError::FeedbackProcessingFailed);
                }
                if (!next.value()) {
                    break;
                }

                try {
                    handleFeedback(*next.value(), runtime);
                } catch (...) {
                    return core::Result<bool, SerializedRuntimeWorkerError>::failure(
                        SerializedRuntimeWorkerError::FeedbackHandlerFailed);
                }
            }

            // The control queue is host-defined and typed by the host's
            // capability composition. It is drained only on this owner thread.
            try {
                drainControlRequests(runtime);
            } catch (...) {
                return core::Result<bool, SerializedRuntimeWorkerError>::failure(
                    SerializedRuntimeWorkerError::ControlHandlerFailed);
            }

            if (runtime.feedbackIngressClosed() &&
                runtime.pendingFeedbackCount() == 0) {
                return core::Result<bool, SerializedRuntimeWorkerError>::success(true);
            }

            const auto wake = wakeSignal.waitForChange(observedGeneration);
            if (wake.wakeFault) {
                return core::Result<bool, SerializedRuntimeWorkerError>::failure(
                    SerializedRuntimeWorkerError::WakeSignalFault);
            }
        }
    }
};

} // namespace microscope::application::runtime
