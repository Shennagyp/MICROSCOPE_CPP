#pragma once

#include "amscope/AmcamCamera.h"
#include "amscope/detail/AcquisitionFrameGate.h"
#include "camera/FrameStreamMailbox.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace amscope::native_mac {

// Native-frontend application controller. AmcamCamera remains the only owner
// of the vendor session and acquisition worker. This class serializes
// application lifecycle requests; it is not an acquisition backend.
class NativeCameraController final {
public:
    using RequestId = std::uint64_t;
    using LifecycleResult = std::variant<std::monostate, CameraError>;
    using CameraListResult = std::variant<std::vector<CameraInfo>, CameraError>;

    // These callbacks execute on the controller's command worker. UI adapters
    // must marshal results to their UI thread. FrameAvailableCallback is the
    // exception: it is invoked on the acquisition worker only when the
    // single-slot mailbox transitions to notification-pending.
    using LifecycleCallback =
        std::function<void(RequestId, std::uint64_t, LifecycleResult)>;
    using CameraListCallback =
        std::function<void(RequestId, std::uint64_t, CameraListResult)>;
    using ErrorCallback =
        std::function<void(std::uint64_t, CameraError)>;
    using FrameAvailableCallback = std::function<void()>;

    explicit NativeCameraController(
        FrameAvailableCallback onFrameAvailable,
        ErrorCallback onAcquisitionError = {});
    ~NativeCameraController();

    NativeCameraController(const NativeCameraController&) = delete;
    NativeCameraController& operator=(const NativeCameraController&) = delete;
    NativeCameraController(NativeCameraController&&) = delete;
    NativeCameraController& operator=(NativeCameraController&&) = delete;

    [[nodiscard]] RequestId requestEnumerateCameras(CameraListCallback callback);
    [[nodiscard]] RequestId requestOpen(
        std::string deviceId, LifecycleCallback callback);
    [[nodiscard]] RequestId requestClose(LifecycleCallback callback);
    [[nodiscard]] RequestId requestStart(LifecycleCallback callback);
    [[nodiscard]] RequestId requestStop(LifecycleCallback callback);

    [[nodiscard]] CameraState state() const noexcept;
    [[nodiscard]] std::uint64_t droppedFrames() const noexcept;
    [[nodiscard]] std::uint64_t framesPulled() const noexcept;
    [[nodiscard]] std::uint64_t imageEventsReceived() const noexcept;

    // Transfers the latest frame lease to the consumer. There is no frame
    // history and no FIFO of presentation work.
    [[nodiscard]] camera::FramePtr takeLatestFrame();

private:
    struct PendingRequest {
        RequestId id = 0;
        std::uint64_t sessionGeneration = 0;
        bool lifecycleMutation = false;
        std::function<void(RequestId, std::uint64_t)> execute;
    };

    static constexpr std::size_t kMaximumPendingRequests = 128;

    [[nodiscard]] RequestId enqueue(
        bool lifecycleMutation,
        std::function<void(RequestId, std::uint64_t)> execute);
    [[nodiscard]] std::uint64_t nextSessionGenerationLocked() noexcept;
    void runCommandWorker();
    void notifyFrameAvailable() noexcept;
    void handleAcquisitionFailure(
        const std::shared_ptr<detail::AcquisitionFrameGate>& frameGate,
        const CameraError& error) noexcept;

    // Keep the session owner alive until the destructor has stopped/closed it.
    AmcamCamera camera_;
    camera::FrameStreamMailbox frameMailbox_;

    FrameAvailableCallback onFrameAvailable_;
    ErrorCallback onAcquisitionError_;

    mutable std::mutex requestMutex_;
    std::condition_variable requestCondition_;
    std::deque<PendingRequest> pendingRequests_;
    std::thread commandWorker_;
    RequestId nextRequestId_ = 1;
    std::uint64_t scheduledSessionGeneration_ = 0;
    std::atomic<std::uint64_t> activeSessionGeneration_{0};
    bool stopping_ = false;

    // Accessed only by the serialized command worker (and after join in dtor).
    std::shared_ptr<detail::AcquisitionFrameGate> acquisitionFrameGate_;
};

} // namespace amscope::native_mac
