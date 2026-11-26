#pragma once

// LEGACY COMPATIBILITY PATH.
// This bounded queue carries fixed-slot StageNavigationRuntime requests.
// New workflows must use StageWorkingSurfaceNavigationControlQueue and
// StageWorkingSurfaceNavigationRuntime, whose request identity is a SlideId
// in the variable-count XY working surface rather than a Left/Right slot.

#include "microscope/application/motion/StageNavigationRuntime.h"
#include "microscope/application/runtime/RuntimeWakeSignal.h"

#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace microscope::application::motion {

struct StageNavigationControlRequest final {
    core::SlideId slideId;
    core::SlideLocalPoint destination;
    StageNavigationOperationIdentity identities;
};

enum class StageNavigationControlSubmitStatus {
    Enqueued,
    QueueFull,
    Closed
};

enum class StageNavigationControlQueueCreationError {
    ZeroCapacity
};

struct StageNavigationControlRequestOutcome final {
    core::OperationId operationId;
    std::optional<StageNavigationRuntimeUpdate> update;
    std::optional<StageNavigationRuntimeError> error;
};

struct StageNavigationControlDrainResult final {
    std::vector<StageNavigationControlRequestOutcome> outcomes;
    std::optional<StageNavigationRuntimeUpdate> healthUpdate;
    bool queueClosed;
    std::size_t pendingCount;
};

// The wake signal is non-owning and must outlive this queue. A bounded,
// stage-specific producer/consumer queue. Front-end threads submit
// immutable slide-local requests; the SerializedRuntimeWorker control drain is
// the sole consumer and dispatches at most one active navigation at a time.
class StageNavigationControlQueue final {
public:
    using Pointer = std::unique_ptr<StageNavigationControlQueue>;

    [[nodiscard]] static core::Result<
        Pointer, StageNavigationControlQueueCreationError> create(
            std::size_t capacity,
            ::microscope::application::runtime::RuntimeWakeSignal& wakeSignal) {
        if (capacity == 0) {
            return core::Result<Pointer, StageNavigationControlQueueCreationError>::failure(
                StageNavigationControlQueueCreationError::ZeroCapacity);
        }
        return core::Result<Pointer, StageNavigationControlQueueCreationError>::success(
            Pointer(new StageNavigationControlQueue(capacity, wakeSignal)));
    }

    StageNavigationControlQueue(const StageNavigationControlQueue&) = delete;
    StageNavigationControlQueue& operator=(const StageNavigationControlQueue&) = delete;

    [[nodiscard]] StageNavigationControlSubmitStatus submit(
        const StageNavigationControlRequest& request) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) {
                return StageNavigationControlSubmitStatus::Closed;
            }
            if (count_ == capacity_) {
                return StageNavigationControlSubmitStatus::QueueFull;
            }

            slots_[tail_].emplace(request);
            tail_ = (tail_ + 1) % capacity_;
            ++count_;
        }

        wakeSignal_->requestControlWake();
        return StageNavigationControlSubmitStatus::Enqueued;
    }

    // Called only by the serialized runtime owner thread. If a navigation is
    // already executing, queued requests remain in FIFO order for the next
    // owner-thread iteration after that navigation reaches a terminal result.
    [[nodiscard]] core::Result<
        StageNavigationControlDrainResult,
        StageNavigationRuntimeError> drain(StageNavigationRuntime& navigation) {
        auto bound = navigation.bindOwnerThread();
        if (!bound) {
            return core::Result<StageNavigationControlDrainResult,
                StageNavigationRuntimeError>::failure(bound.error());
        }

        auto health = navigation.synchronizeFeedbackLoss();
        if (!health) {
            return core::Result<StageNavigationControlDrainResult,
                StageNavigationRuntimeError>::failure(health.error());
        }
        std::optional<StageNavigationRuntimeUpdate> healthUpdate;
        if (health.value().disposition ==
            StageNavigationRuntimeDisposition::FeedbackLossDetected) {
            healthUpdate = health.value();
        }
        StageNavigationControlDrainResult result{
            {}, std::move(healthUpdate), isClosed(), pendingCount()};
        if (navigation.feedbackLossUnresolved()) {
            result.pendingCount = pendingCount();
            return core::Result<StageNavigationControlDrainResult,
                StageNavigationRuntimeError>::success(std::move(result));
        }

        while (!navigation.navigationActive()) {
            auto request = tryPop();
            if (!request) {
                break;
            }

            const auto operationId = request->identities.operationId;
            auto dispatched = navigation.beginRequest(
                request->slideId, request->destination,
                std::move(request->identities));
            if (dispatched) {
                result.outcomes.push_back(StageNavigationControlRequestOutcome{
                    operationId, std::move(dispatched).value(), std::nullopt});
            } else {
                result.outcomes.push_back(StageNavigationControlRequestOutcome{
                    operationId, std::nullopt, dispatched.error()});
            }
        }

        result.queueClosed = isClosed();
        result.pendingCount = pendingCount();
        return core::Result<StageNavigationControlDrainResult,
            StageNavigationRuntimeError>::success(std::move(result));
    }

    void close() {
        bool notify = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            notify = !closed_;
            closed_ = true;
        }
        if (notify) {
            wakeSignal_->requestControlWake();
        }
    }

    [[nodiscard]] bool isClosed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    [[nodiscard]] std::size_t pendingCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_;
    }

private:
    StageNavigationControlQueue(
        std::size_t capacity,
        ::microscope::application::runtime::RuntimeWakeSignal& wakeSignal)
        : capacity_(capacity),
          wakeSignal_(&wakeSignal),
          slots_(capacity) {}

    [[nodiscard]] std::optional<StageNavigationControlRequest> tryPop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (count_ == 0) {
            return std::nullopt;
        }

        auto result = std::optional<StageNavigationControlRequest>(
            std::move(*slots_[head_]));
        slots_[head_].reset();
        head_ = (head_ + 1) % capacity_;
        --count_;
        return result;
    }

    const std::size_t capacity_;
    ::microscope::application::runtime::RuntimeWakeSignal* wakeSignal_;
    mutable std::mutex mutex_;
    std::vector<std::optional<StageNavigationControlRequest>> slots_;
    std::size_t head_{0};
    std::size_t tail_{0};
    std::size_t count_{0};
    bool closed_{false};
};

} // namespace microscope::application::motion
