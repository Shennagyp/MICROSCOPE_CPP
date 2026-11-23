#pragma once
#include "camera/CameraFrames.h"
#include <QMetaType>
#include <QMetaObject>
#include <QObject>
#include <atomic>
#include <memory>
#include <utility>
namespace camera::qt {
// Backend-neutral preview handoff. No camera, session, worker, or operation-queue ownership.
// Publishes immutable FramePtr leases; notifications are coalesced and freshness-first.
class FrameStream final : public QObject {
    Q_OBJECT
public:
    explicit FrameStream(QObject* parent = nullptr);
    FrameStream(const FrameStream&) = delete;
    FrameStream& operator=(const FrameStream&) = delete;
    void publishFrame(FramePtr frame);
    // Lifecycle predicate is checked before and after publication. If invalidated,
    // remove only this frame if it is still the current mailbox value.
    template <typename IsCurrent>
    void publishFrame(FramePtr frame, IsCurrent&& isCurrent) {
        if (!isCurrent()) return;
        const FramePtr published = std::move(frame);
        std::atomic_store_explicit(&latestFrame_, published, std::memory_order_release);
        if (!isCurrent()) {
            FramePtr expected = published;
            (void)std::atomic_compare_exchange_strong_explicit(
                &latestFrame_, &expected, FramePtr{},
                std::memory_order_acq_rel, std::memory_order_acquire);
            return;
        }
        if (!notificationPending_.exchange(true, std::memory_order_acq_rel))
            queueFrameNotification();
    }
    [[nodiscard]] FramePtr takeLatestFrame();
    void clear();
signals:
    void frameAvailable();
private:
    // Inline to keep the notification hot path from making an out-of-library call.
    void queueFrameNotification() {
        (void)QMetaObject::invokeMethod(this, [this]() {
            emit frameAvailable();
        }, Qt::QueuedConnection);
    }
    FramePtr latestFrame_;
    std::atomic<bool> notificationPending_{false};
};
} // namespace camera::qt
Q_DECLARE_METATYPE(camera::FramePtr)
