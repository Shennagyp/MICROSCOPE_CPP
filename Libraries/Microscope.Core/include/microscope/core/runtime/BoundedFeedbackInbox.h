#pragma once

#include "microscope/core/runtime/Feedback.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace microscope::core {

enum class BoundedFeedbackInboxCreationError { ZeroCapacity };

// Thread-safe fixed-capacity FIFO for one runtime incarnation. The runtime
// signal is non-owning and must outlive this inbox. It is the independent,
// noexcept control path used to wake the serialized owner on enqueue, loss,
// and closure.
template <typename TPayload>
class BoundedFeedbackInbox final : public IFeedbackIngress<TPayload> {
public:
    using Pointer = std::unique_ptr<BoundedFeedbackInbox>;

    static Result<Pointer,BoundedFeedbackInboxCreationError> create(
        MicroscopeId microscopeId,
        RuntimeId runtimeId,
        RuntimeGeneration runtimeGeneration,
        std::size_t capacity,
        IFeedbackRuntimeSignal& runtimeSignal) {
        if(capacity==0)
            return Result<Pointer,BoundedFeedbackInboxCreationError>::failure(
                BoundedFeedbackInboxCreationError::ZeroCapacity);
        return Result<Pointer,BoundedFeedbackInboxCreationError>::success(
            Pointer(new BoundedFeedbackInbox(
                std::move(microscopeId),std::move(runtimeId),runtimeGeneration,
                capacity,runtimeSignal)));
    }

    FeedbackSubmitStatus submit(FeedbackEnvelope<TPayload> feedback) override {
        bool notifyFeedbackLoss=false;
        bool notifyClosed=false;
        FeedbackSubmitStatus status=FeedbackSubmitStatus::Closed;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(closed_)
                return FeedbackSubmitStatus::Closed;

            const auto& runtime=feedback.metadata().correlation().runtimeDevice;
            if(runtime.microscopeId!=microscopeId_ ||
               runtime.runtimeId!=runtimeId_ ||
               runtime.runtimeGeneration!=runtimeGeneration_) {
                return FeedbackSubmitStatus::WrongRuntimeScope;
            }

            if(nextSequence_==0) {
                closed_=true;
                notifyClosed=true;
                if(!feedbackLossLatched_) {
                    feedbackLossLatched_=true;
                    notifyFeedbackLoss=true;
                }
                status=FeedbackSubmitStatus::Closed;
            } else if(count_>=capacity_) {
                if(!feedbackLossLatched_) {
                    feedbackLossLatched_=true;
                    notifyFeedbackLoss=true;
                }
                status=FeedbackSubmitStatus::QueueFull;
            } else {
                auto sequence=FeedbackIngressSequence::create(
                    microscopeId_,runtimeId_,runtimeGeneration_,nextSequence_);
                if(!sequence) {
                    closed_=true;
                    notifyClosed=true;
                    if(!feedbackLossLatched_) {
                        feedbackLossLatched_=true;
                        notifyFeedbackLoss=true;
                    }
                    status=FeedbackSubmitStatus::Closed;
                } else {
                    auto receipt=FeedbackReceipt<TPayload>::create(
                        std::move(feedback),std::chrono::steady_clock::now(),
                        std::move(sequence).value());
                    if(!receipt) {
                        closed_=true;
                        notifyClosed=true;
                        if(!feedbackLossLatched_) {
                            feedbackLossLatched_=true;
                            notifyFeedbackLoss=true;
                        }
                        status=FeedbackSubmitStatus::Closed;
                    } else {
                        slots_[tail_].emplace(std::move(receipt).value());
                        tail_=(tail_+1)%capacity_;
                        ++count_;
                        nextSequence_=
                            nextSequence_==std::numeric_limits<std::uint64_t>::max()
                                ? 0 : nextSequence_+1;
                        status=FeedbackSubmitStatus::Enqueued;
                    }
                }
            }
        }

        if(status==FeedbackSubmitStatus::Enqueued) {
            available_.notify_one();
            runtimeSignal_->feedbackAvailable();
        } else if(notifyClosed) {
            available_.notify_all();
            runtimeSignal_->feedbackIngressClosed();
        }

        if(notifyFeedbackLoss)
            runtimeSignal_->feedbackLossDetected();

        return status;
    }

    [[nodiscard]] std::optional<FeedbackReceipt<TPayload>> tryPop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if(count_==0)
            return std::nullopt;
        auto result=std::optional<FeedbackReceipt<TPayload>>(
            std::move(*slots_[head_]));
        slots_[head_].reset();
        head_=(head_+1)%capacity_;
        --count_;
        return result;
    }

    [[nodiscard]] std::optional<FeedbackReceipt<TPayload>> waitAndPop() {
        std::unique_lock<std::mutex> lock(mutex_);
        available_.wait(lock,[this]{return closed_||count_!=0;});
        if(count_==0)
            return std::nullopt;
        auto result=std::optional<FeedbackReceipt<TPayload>>(
            std::move(*slots_[head_]));
        slots_[head_].reset();
        head_=(head_+1)%capacity_;
        --count_;
        return result;
    }

    void close() {
        bool notify=false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            notify=!closed_;
            closed_=true;
        }
        if(notify) {
            available_.notify_all();
            runtimeSignal_->feedbackIngressClosed();
        }
    }

    [[nodiscard]] bool isClosed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    [[nodiscard]] bool feedbackLossLatched() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return feedbackLossLatched_;
    }

    // Only the serialized runtime may acknowledge loss, after reconciling
    // every operation/device state affected by the rejected feedback.
    void acknowledgeFeedbackLoss() {
        std::lock_guard<std::mutex> lock(mutex_);
        feedbackLossLatched_=false;
    }

    [[nodiscard]] std::size_t pendingCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_;
    }

private:
    BoundedFeedbackInbox(
        MicroscopeId microscopeId,RuntimeId runtimeId,
        RuntimeGeneration runtimeGeneration,std::size_t capacity,
        IFeedbackRuntimeSignal& runtimeSignal)
        :microscopeId_(std::move(microscopeId)),
         runtimeId_(std::move(runtimeId)),
         runtimeGeneration_(runtimeGeneration),
         capacity_(capacity),
         runtimeSignal_(&runtimeSignal),
         slots_(capacity) {}

    MicroscopeId microscopeId_;
    RuntimeId runtimeId_;
    RuntimeGeneration runtimeGeneration_;
    const std::size_t capacity_;
    IFeedbackRuntimeSignal* runtimeSignal_;

    mutable std::mutex mutex_;
    std::condition_variable available_;
    std::vector<std::optional<FeedbackReceipt<TPayload>>> slots_;
    std::size_t head_{0};
    std::size_t tail_{0};
    std::size_t count_{0};
    std::uint64_t nextSequence_{1};
    bool closed_{false};
    bool feedbackLossLatched_{false};
};

} // namespace microscope::core
