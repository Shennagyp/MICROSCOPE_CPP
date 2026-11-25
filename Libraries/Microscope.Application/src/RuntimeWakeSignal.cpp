#include "microscope/application/runtime/RuntimeWakeSignal.h"

#include <limits>

namespace microscope::application::runtime {

void RuntimeWakeSignal::feedbackAvailable() noexcept {
    publish(RuntimeWakeReason::FeedbackAvailable);
}

void RuntimeWakeSignal::feedbackLossDetected() noexcept {
    publish(RuntimeWakeReason::FeedbackLoss);
}

void RuntimeWakeSignal::feedbackIngressClosed() noexcept {
    publish(RuntimeWakeReason::FeedbackIngressClosed);
}

void RuntimeWakeSignal::requestControlWake() noexcept {
    publish(RuntimeWakeReason::ControlRequest);
}

void RuntimeWakeSignal::publish(RuntimeWakeReason reason) noexcept {
    try {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(generation_==std::numeric_limits<std::uint64_t>::max()) {
                wakeFault_.store(true,std::memory_order_release);
            } else {
                ++generation_;
            }
            lastReason_=reason;
            if(reason==RuntimeWakeReason::FeedbackLoss&&
               feedbackLossNotifications_!=std::numeric_limits<std::uint64_t>::max())
                ++feedbackLossNotifications_;
            if(reason==RuntimeWakeReason::FeedbackIngressClosed)
                feedbackIngressClosed_=true;
        }
    } catch(...) {
        wakeFault_.store(true,std::memory_order_release);
    }
    changed_.notify_all();
}

bool RuntimeWakeSignal::changedFrom(std::uint64_t observedGeneration) const noexcept {
    return generation_!=observedGeneration||
           feedbackIngressClosed_||
           wakeFault_.load(std::memory_order_acquire);
}

RuntimeWakeSnapshot RuntimeWakeSignal::snapshotLocked() const noexcept {
    return RuntimeWakeSnapshot{
        generation_,lastReason_,feedbackLossNotifications_,
        feedbackIngressClosed_,wakeFault_.load(std::memory_order_acquire)};
}

RuntimeWakeSnapshot RuntimeWakeSignal::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshotLocked();
}

std::optional<RuntimeWakeSnapshot> RuntimeWakeSignal::waitForChange(
    std::uint64_t observedGeneration,std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    const bool changed=changed_.wait_for(
        lock,timeout,[this,observedGeneration]{
            return changedFrom(observedGeneration);
        });
    if(!changed)
        return std::nullopt;
    return snapshotLocked();
}

} // namespace microscope::application::runtime
