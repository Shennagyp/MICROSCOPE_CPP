#include "camera/qt/FrameStream.h"

#include <QMetaObject>

namespace camera::qt {

FrameStream::FrameStream(QObject* parent)
    : QObject(parent) {}

::camera::FramePtr FrameStream::takeLatestFrame() {
    return mailbox_.takeLatestFrame(
        [this]() noexcept { queueNotification(); });
}

void FrameStream::clear() {
    mailbox_.clear();
}

void FrameStream::queueNotification() noexcept {
    (void)QMetaObject::invokeMethod(
        this,
        [this]() { emit frameAvailable(); },
        Qt::QueuedConnection);
}

} // namespace camera::qt
