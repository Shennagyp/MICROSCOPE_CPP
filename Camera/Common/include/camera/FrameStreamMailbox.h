#pragma once

#include "camera/CameraFrames.h"

#include <atomic>
#include <memory>
#include <utility>

namespace camera {

// Single-slot latest-frame mailbox shared by application frontends.
// It owns no camera, session, worker, event loop, queue, or UI notification
// policy. The adapter supplies synchronous generation validation and a
// coalesced notification callback.
class FrameStreamMailbox final {
public:
    FrameStreamMailbox() = default;
    FrameStreamMailbox(const FrameStreamMailbox&) = delete;
    FrameStreamMailbox& operator=(const FrameStreamMailbox&) = delete;

    template <typename IsCurrent, typename QueueNotification>
    void publishFrame(
        FramePtr&& frame,
        IsCurrent&& isCurrent,
        QueueNotification&& queueNotification) {
        if (!isCurrent())
            return;

        const FramePtr published = std::move(frame);
        std::atomic_store_explicit(
            &latestFrame_, published, std::memory_order_release);

        if (!isCurrent()) {
            FramePtr expected = published;
            (void)std::atomic_compare_exchange_strong_explicit(
                &latestFrame_,
                &expected,
                FramePtr{},
                std::memory_order_acq_rel,
                std::memory_order_acquire);
            return;
        }

        if (!notificationPending_.exchange(true, std::memory_order_acq_rel))
            queueNotification();
    }

    template <typename QueueNotification>
    [[nodiscard]] FramePtr takeLatestFrame(
        QueueNotification&& queueNotification) {
        FramePtr frame = std::atomic_exchange_explicit(
            &latestFrame_, FramePtr{}, std::memory_order_acq_rel);

        notificationPending_.store(false, std::memory_order_release);

        if (std::atomic_load_explicit(
                &latestFrame_, std::memory_order_acquire)) {
            bool expected = false;
            if (notificationPending_.compare_exchange_strong(
                    expected,
                    true,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                queueNotification();
            }
        }

        return frame;
    }

    void clear() {
        std::atomic_store_explicit(
            &latestFrame_, FramePtr{}, std::memory_order_release);
        notificationPending_.store(false, std::memory_order_release);
    }

private:
    FramePtr latestFrame_;
    std::atomic<bool> notificationPending_{false};
};

} // namespace camera
