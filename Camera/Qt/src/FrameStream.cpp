#include "camera/qt/FrameStream.h"
#include <QMetaType>
namespace camera::qt {
FrameStream::FrameStream(QObject* parent) : QObject(parent) {
    qRegisterMetaType<camera::FramePtr>("camera::FramePtr");
}
void FrameStream::publishFrame(FramePtr frame) {
    publishFrame(std::move(frame), []() noexcept { return true; });
}
FramePtr FrameStream::takeLatestFrame() {
    FramePtr frame = std::atomic_exchange_explicit(
        &latestFrame_, FramePtr{}, std::memory_order_acq_rel);
    notificationPending_.store(false, std::memory_order_release);
    // Preserve the original race-closing re-arm behavior.
    if (std::atomic_load_explicit(&latestFrame_, std::memory_order_acquire)) {
        bool expected = false;
        if (notificationPending_.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel, std::memory_order_acquire))
            queueFrameNotification();
    }
    return frame;
}
void FrameStream::clear() {
    std::atomic_store_explicit(&latestFrame_, FramePtr{}, std::memory_order_release);
    notificationPending_.store(false, std::memory_order_release);
}
} // namespace camera::qt
