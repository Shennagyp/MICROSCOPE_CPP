#pragma once

#include "microscope/application/motion/StageWorkingSurfaceNavigationRuntime.h"
#include "microscope/application/runtime/RuntimeWakeSignal.h"

#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace microscope::application::motion {

struct StageWorkingSurfaceNavigationControlRequest final {
    core::SlideId slideId;
    core::SlidePoint2D destination;
    StageWorkingSurfaceNavigationOperationIdentity identities;
};

enum class StageWorkingSurfaceNavigationControlSubmitStatus {
    Enqueued,
    QueueFull,
    Closed
};

enum class StageWorkingSurfaceNavigationControlQueueCreationError {
    ZeroCapacity
};

struct StageWorkingSurfaceNavigationControlRequestOutcome final {
    core::OperationId operationId;
    std::optional<StageWorkingSurfaceNavigationRuntimeUpdate> update;
    std::optional<StageWorkingSurfaceNavigationRuntimeError> error;
};

struct StageNavigationControlDrainResult final {
    std::vector<StageWorkingSurfaceNavigationControlRequestOutcome> outcomes;
    std::optional<StageWorkingSurfaceNavigationRuntimeUpdate> healthUpdate;
    bool queueClosed;
    std::size_t pendingCount;
};

// The wake signal is non-owning and must outlive this queue. A bounded,
// stage-specific producer/consumer queue. Front-end threads submit
// immutable slide-local requests; the SerializedRuntimeWorker control drain is
// the sole consumer and dispatches at most one active navigation at a time.
class StageWorkingSurfaceNavigationControlQueue final {
public:
    using Pointer = std::unique_ptr<StageWorkingSurfaceNavigationControlQueue>;

    [[nodiscard]] static core::Result<
        Pointer, StageWorkingSurfaceNavigationControlQueueCreationError> create(
            std::size_t capacity,
            ::microscope::application::runtime::RuntimeWakeSignal& wakeSignal) {
        if (capacity == 0) {
            return core::Result<Pointer, StageWorkingSurfaceNavigationControlQueueCreationError>::failure(
                StageWorkingSurfaceNavigationControlQueueCreationError::ZeroCapacity);
        }
        return core::Result<Pointer, StageWorkingSurfaceNavigationControlQueueCreationError>::success(
            Pointer(new StageWorkingSurfaceNavigationControlQueue(capacity, wakeSignal)));
    }

    StageWorkingSurfaceNavigationControlQueue(const StageWorkingSurfaceNavigationControlQueue&) = delete;
    StageWorkingSurfaceNavigationControlQueue& operator=(const StageWorkingSurfaceNavigationControlQueue&) = delete;

    [[nodiscard]] StageWorkingSurfaceNavigationControlSubmitStatus submit(
        const StageWorkingSurfaceNavigationControlRequest& request) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) {
                return StageWorkingSurfaceNavigationControlSubmitStatus::Closed;
            }
            if (count_ == capacity_) {
                return StageWorkingSurfaceNavigationControlSubmitStatus::QueueFull;
            }

            slots_[tail_].emplace(request);
            tail_ = (tail_ + 1) % capacity_;
            ++count_;
        }

        wakeSignal_->requestControlWake();
        return StageWorkingSurfaceNavigationControlSubmitStatus::Enqueued;
    }

    // Called only by the serialized runtime owner thread. If a navigation is
    // already executing, queued requests remain in FIFO order for the next
    // owner-thread iteration after that navigation reaches a terminal result.
    [[nodiscard]] core::Result<
        StageNavigationControlDrainResult,
        StageWorkingSurfaceNavigationRuntimeError> drain(StageWorkingSurfaceNavigationRuntime& navigation) {
        auto bound = navigation.bindOwnerThread();
        if (!bound) {
            return core::Result<StageNavigationControlDrainResult,
                StageWorkingSurfaceNavigationRuntimeError>::failure(bound.error());
        }

        auto health = navigation.synchronizeFeedbackLoss();
        if (!health) {
            return core::Result<StageNavigationControlDrainResult,
                StageWorkingSurfaceNavigationRuntimeError>::failure(health.error());
        }
        std::optional<StageWorkingSurfaceNavigationRuntimeUpdate> healthUpdate;
        if (health.value().disposition ==
            StageWorkingSurfaceNavigationRuntimeDisposition::FeedbackLossDetected) {
            healthUpdate = health.value();
        }
        StageNavigationControlDrainResult result{
            {}, std::move(healthUpdate), isClosed(), pendingCount()};
        if (navigation.feedbackLossUnresolved()) {
            result.pendingCount = pendingCount();
            return core::Result<StageNavigationControlDrainResult,
                StageWorkingSurfaceNavigationRuntimeError>::success(std::move(result));
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
                result.outcomes.push_back(StageWorkingSurfaceNavigationControlRequestOutcome{
                    operationId, std::move(dispatched).value(), std::nullopt});
            } else {
                result.outcomes.push_back(StageWorkingSurfaceNavigationControlRequestOutcome{
                    operationId, std::nullopt, dispatched.error()});
            }
        }

        result.queueClosed = isClosed();
        result.pendingCount = pendingCount();
        return core::Result<StageNavigationControlDrainResult,
            StageWorkingSurfaceNavigationRuntimeError>::success(std::move(result));
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
    StageWorkingSurfaceNavigationControlQueue(
        std::size_t capacity,
        ::microscope::application::runtime::RuntimeWakeSignal& wakeSignal)
        : capacity_(capacity),
          wakeSignal_(&wakeSignal),
          slots_(capacity) {}

    [[nodiscard]] std::optional<StageWorkingSurfaceNavigationControlRequest> tryPop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (count_ == 0) {
            return std::nullopt;
        }

        auto result = std::optional<StageWorkingSurfaceNavigationControlRequest>(
            std::move(*slots_[head_]));
        slots_[head_].reset();
        head_ = (head_ + 1) % capacity_;
        --count_;
        return result;
    }

    const std::size_t capacity_;
    ::microscope::application::runtime::RuntimeWakeSignal* wakeSignal_;
    mutable std::mutex mutex_;
    std::vector<std::optional<StageWorkingSurfaceNavigationControlRequest>> slots_;
    std::size_t head_{0};
    std::size_t tail_{0};
    std::size_t count_{0};
    bool closed_{false};
};

} // namespace microscope::application::motion
