#pragma once

#include "camera/FrameStreamMailbox.h"

#include <QObject>

#include <utility>

namespace camera::qt {

// Backend-neutral Qt frame handoff. Producers may publish from any thread;
// frameAvailable is always emitted on this object's Qt affinity thread.
// The component owns neither a camera session nor an acquisition worker.
class FrameStream final : public QObject {
    Q_OBJECT

public:
    explicit FrameStream(QObject* parent = nullptr);

    template <typename IsCurrent>
    void publishFrame(::camera::FramePtr&& frame, IsCurrent&& isCurrent) {
        mailbox_.publishFrame(
            std::move(frame),
            std::forward<IsCurrent>(isCurrent),
            [this]() noexcept { queueNotification(); });
    }

    // Consumers should take the current frame from the object's affinity
    // thread. Concurrent publication remains supported while the lease is read.
    [[nodiscard]] ::camera::FramePtr takeLatestFrame();

    // Removes the currently retained lease. Callers must invalidate their
    // producer generation before clearing when old producers may still run.
    void clear();

signals:
    void frameAvailable();

private:
    void queueNotification() noexcept;

    ::camera::FrameStreamMailbox mailbox_;
};

} // namespace camera::qt
