#pragma once

#include "microscope/core/runtime/Feedback.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>

namespace microscope::application::runtime {

enum class RuntimeWakeReason {
    FeedbackAvailable,
    FeedbackLoss,
    FeedbackIngressClosed,
    ControlRequest
};

struct RuntimeWakeSnapshot final {
    std::uint64_t generation;
    RuntimeWakeReason lastReason;
    std::uint64_t feedbackLossNotifications;
    bool feedbackIngressClosed;
    bool wakeFault;
};

// Application-owned wait primitive for the Core single-writer runtime.
// Producer methods are noexcept and never execute operation/state reducers.
class RuntimeWakeSignal final : public microscope::core::IFeedbackRuntimeSignal {
public:
    RuntimeWakeSignal() = default;
    RuntimeWakeSignal(const RuntimeWakeSignal&) = delete;
    RuntimeWakeSignal& operator=(const RuntimeWakeSignal&) = delete;

    void feedbackAvailable() noexcept override;
    void feedbackLossDetected() noexcept override;
    void feedbackIngressClosed() noexcept override;

    // Used by Application control requests that share the same owner thread.
    void requestControlWake() noexcept;

    RuntimeWakeSnapshot snapshot() const;

    // Returns nullopt only on timeout. The predicate is generation-based so a
    // signal arriving before the worker begins waiting cannot be lost.
    std::optional<RuntimeWakeSnapshot> waitForChange(
        std::uint64_t observedGeneration,
        std::chrono::milliseconds timeout);

private:
    void publish(RuntimeWakeReason reason) noexcept;
    bool changedFrom(std::uint64_t observedGeneration) const noexcept;
    RuntimeWakeSnapshot snapshotLocked() const noexcept;

    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::uint64_t generation_{0};
    std::uint64_t feedbackLossNotifications_{0};
    RuntimeWakeReason lastReason_{RuntimeWakeReason::ControlRequest};
    bool feedbackIngressClosed_{false};
    std::atomic<bool> wakeFault_{false};
};

} // namespace microscope::application::runtime
