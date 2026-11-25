#pragma once

#include "microscope/core/runtime/Feedback.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace microscope::core {

enum class BoundedFeedbackInboxCreationError {
    ZeroCapacity,
    MissingFeedbackLossCallback
};

// Thread-safe, typed FIFO ingress for one runtime incarnation. The callback
// is an independent control path: it must wake the serialized runtime and
// latch the affected device/operations as unknown. It must not enqueue another
// feedback item into this inbox.
template <typename TPayload>
class BoundedFeedbackInbox final : public IFeedbackIngress<TPayload> {
public:
    using Pointer = std::unique_ptr<BoundedFeedbackInbox>;
    using FeedbackLossCallback = std::function<void()>;

    static Result<Pointer,BoundedFeedbackInboxCreationError> create(
        MicroscopeId microscopeId,
        RuntimeId runtimeId,
        RuntimeGeneration runtimeGeneration,
        std::size_t capacity,
        FeedbackLossCallback onFeedbackLoss) {
        if(capacity==0)
            return Result<Pointer,BoundedFeedbackInboxCreationError>::failure(
                BoundedFeedbackInboxCreationError::ZeroCapacity);
        if(!onFeedbackLoss)
            return Result<Pointer,BoundedFeedbackInboxCreationError>::failure(
                BoundedFeedbackInboxCreationError::MissingFeedbackLossCallback);
        return Result<Pointer,BoundedFeedbackInboxCreationError>::success(
            Pointer(new BoundedFeedbackInbox(
                std::move(microscopeId),std::move(runtimeId),runtimeGeneration,
                capacity,std::move(onFeedbackLoss))));
    }

    FeedbackSubmitStatus submit(FeedbackEnvelope<TPayload> feedback) override {
        bool notifyFeedbackLoss=false;
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
                if(!feedbackLossLatched_) {
                    feedbackLossLatched_=true;
                    notifyFeedbackLoss=true;
                }
                status=FeedbackSubmitStatus::Closed;
            } else if(queue_.size()>=capacity_) {
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
                        if(!feedbackLossLatched_) {
                            feedbackLossLatched_=true;
                            notifyFeedbackLoss=true;
                        }
                        status=FeedbackSubmitStatus::Closed;
                    } else {
                        queue_.push_back(std::move(receipt).value());
                        nextSequence_=
                            nextSequence_==std::numeric_limits<std::uint64_t>::max()?0:nextSequence_+1;
                        status=FeedbackSubmitStatus::Enqueued;
                    }
                }
            }
        }

        if(status==FeedbackSubmitStatus::Enqueued)
            available_.notify_one();

        if(notifyFeedbackLoss) {
            // Do not let adapter callback exceptions escape a native driver
            // thread. The loss remains latched and observable by the runtime.
            try { onFeedbackLoss_(); } catch(...) {}
        }
        return status;
    }

    [[nodiscard]] std::optional<FeedbackReceipt<TPayload>> tryPop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if(queue_.empty())
            return std::nullopt;
        auto result=std::optional<FeedbackReceipt<TPayload>>(
            std::move(queue_.front()));
        queue_.pop_front();
        return result;
    }

    [[nodiscard]] std::optional<FeedbackReceipt<TPayload>> waitAndPop() {
        std::unique_lock<std::mutex> lock(mutex_);
        available_.wait(lock,[this]{return closed_||!queue_.empty();});
        if(queue_.empty())
            return std::nullopt;
        auto result=std::optional<FeedbackReceipt<TPayload>>(
            std::move(queue_.front()));
        queue_.pop_front();
        return result;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_=true;
        }
        available_.notify_all();
    }

    [[nodiscard]] bool isClosed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    [[nodiscard]] bool feedbackLossLatched() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return feedbackLossLatched_;
    }

    // Only the serialized runtime may acknowledge loss, and only after it
    // has reconciled all operations/device state affected by the dropped fact.
    void acknowledgeFeedbackLoss() {
        std::lock_guard<std::mutex> lock(mutex_);
        feedbackLossLatched_=false;
    }

    [[nodiscard]] std::size_t pendingCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    BoundedFeedbackInbox(
        MicroscopeId microscopeId,RuntimeId runtimeId,
        RuntimeGeneration runtimeGeneration,std::size_t capacity,
        FeedbackLossCallback onFeedbackLoss)
        :microscopeId_(std::move(microscopeId)),
         runtimeId_(std::move(runtimeId)),
         runtimeGeneration_(runtimeGeneration),
         capacity_(capacity),
         onFeedbackLoss_(std::move(onFeedbackLoss)) {}

    MicroscopeId microscopeId_;
    RuntimeId runtimeId_;
    RuntimeGeneration runtimeGeneration_;
    const std::size_t capacity_;
    FeedbackLossCallback onFeedbackLoss_;

    mutable std::mutex mutex_;
    std::condition_variable available_;
    std::deque<FeedbackReceipt<TPayload>> queue_;
    std::uint64_t nextSequence_{1};
    bool closed_{false};
    bool feedbackLossLatched_{false};
};

} // namespace microscope::core
