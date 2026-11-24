#include "NativeCameraController.h"

#include <exception>
#include <limits>
#include <utility>

namespace amscope::native_mac {

namespace {

template <typename Callback, typename... Args>
void invokeSafely(const Callback& callback, Args&&... args) noexcept {
    if (!callback)
        return;
    try {
        callback(std::forward<Args>(args)...);
    } catch (...) {
        // A frontend callback must never unwind through a camera worker.
    }
}

} // namespace

NativeCameraController::NativeCameraController(
    FrameAvailableCallback onFrameAvailable,
    ErrorCallback onAcquisitionError)
    : onFrameAvailable_(std::move(onFrameAvailable)),
      onAcquisitionError_(std::move(onAcquisitionError)),
      commandWorker_([this]() { runCommandWorker(); }) {}

NativeCameraController::~NativeCameraController() {
    {
        std::lock_guard<std::mutex> lock(requestMutex_);
        stopping_ = true;
        pendingRequests_.clear();
    }
    requestCondition_.notify_all();

    if (commandWorker_.joinable())
        commandWorker_.join();

    activeSessionGeneration_.fetch_add(1, std::memory_order_acq_rel);
    acquisitionFrameGate_.reset();

    CameraError ignored;
    (void)camera_.close(&ignored);
    frameMailbox_.clear();
}

NativeCameraController::RequestId NativeCameraController::requestEnumerateCameras(
    CameraListCallback callback) {
    if (!callback)
        return 0;

    return enqueue(
        false,
        [callback = std::move(callback)](RequestId requestId,
                                         std::uint64_t generation) mutable {
            CameraListResult result;
            try {
                CameraError error;
                auto cameras = AmcamCamera::enumerate(&error);
                if (!error.message.empty())
                    result = std::move(error);
                else
                    result = std::move(cameras);
            } catch (const std::exception& exception) {
                result = CameraError{0, "camera enumeration", exception.what()};
            } catch (...) {
                result = CameraError{
                    0, "camera enumeration",
                    "Unknown exception during camera enumeration."};
            }
            invokeSafely(callback, requestId, generation, std::move(result));
        });
}

NativeCameraController::RequestId NativeCameraController::requestOpen(
    std::string deviceId,
    LifecycleCallback callback) {
    if (!callback)
        return 0;

    return enqueue(
        true,
        [this, deviceId = std::move(deviceId),
         callback = std::move(callback)](RequestId requestId,
                                         std::uint64_t generation) mutable {
            LifecycleResult result{std::monostate{}};
            CameraError error;
            try {
                const bool opened = camera_.open(deviceId, &error);
                if (!opened) {
                    result = std::move(error);
                    if (camera_.state() == CameraState::Streaming &&
                        acquisitionFrameGate_) {
                        acquisitionFrameGate_->activateFor(generation);
                    } else {
                        acquisitionFrameGate_.reset();
                    }
                } else {
                    acquisitionFrameGate_.reset();
                    frameMailbox_.clear();
                }
            } catch (const std::exception& exception) {
                result = CameraError{0, "camera open", exception.what()};
            } catch (...) {
                result = CameraError{
                    0, "camera open", "Unknown exception while opening the camera."};
            }
            invokeSafely(callback, requestId, generation, std::move(result));
        });
}

NativeCameraController::RequestId NativeCameraController::requestClose(
    LifecycleCallback callback) {
    if (!callback)
        return 0;

    return enqueue(
        true,
        [this, callback = std::move(callback)](RequestId requestId,
                                               std::uint64_t generation) mutable {
            LifecycleResult result{std::monostate{}};
            CameraError error;
            try {
                if (!camera_.close(&error)) {
                    result = std::move(error);
                    if (camera_.state() == CameraState::Streaming &&
                        acquisitionFrameGate_) {
                        acquisitionFrameGate_->activateFor(generation);
                    } else {
                        acquisitionFrameGate_.reset();
                    }
                } else {
                    acquisitionFrameGate_.reset();
                }
                frameMailbox_.clear();
            } catch (const std::exception& exception) {
                result = CameraError{0, "camera close", exception.what()};
            } catch (...) {
                result = CameraError{
                    0, "camera close", "Unknown exception while closing the camera."};
            }
            invokeSafely(callback, requestId, generation, std::move(result));
        });
}

NativeCameraController::RequestId NativeCameraController::requestStart(
    LifecycleCallback callback) {
    if (!callback)
        return 0;

    return enqueue(
        true,
        [this, callback = std::move(callback)](RequestId requestId,
                                               std::uint64_t generation) mutable {
            LifecycleResult result{std::monostate{}};
            CameraError error;
            try {
                auto frameGate =
                    std::make_shared<detail::AcquisitionFrameGate>(generation);
                const bool started = camera_.start(
                    [this, frameGate](camera::FramePtr frame) {
                        frameMailbox_.publishFrame(
                            std::move(frame),
                            [this, &frameGate]() noexcept {
                                return frameGate &&
                                    frameGate->accepts(
                                        activeSessionGeneration_.load(
                                            std::memory_order_acquire));
                            },
                            [this]() noexcept { notifyFrameAvailable(); });
                    },
                    [this, frameGate](const CameraError& acquisitionError) {
                        handleAcquisitionFailure(frameGate, acquisitionError);
                    },
                    &error);

                if (started) {
                    acquisitionFrameGate_ = std::move(frameGate);
                } else {
                    result = std::move(error);
                    if (camera_.state() == CameraState::Streaming &&
                        acquisitionFrameGate_) {
                        acquisitionFrameGate_->activateFor(generation);
                    } else {
                        acquisitionFrameGate_.reset();
                    }
                }
            } catch (const std::exception& exception) {
                result = CameraError{0, "camera start", exception.what()};
                if (camera_.state() != CameraState::Streaming)
                    acquisitionFrameGate_.reset();
                else if (acquisitionFrameGate_)
                    acquisitionFrameGate_->activateFor(generation);
            } catch (...) {
                result = CameraError{
                    0, "camera start",
                    "Unknown exception while preparing acquisition."};
                if (camera_.state() != CameraState::Streaming)
                    acquisitionFrameGate_.reset();
                else if (acquisitionFrameGate_)
                    acquisitionFrameGate_->activateFor(generation);
            }
            invokeSafely(callback, requestId, generation, std::move(result));
        });
}

NativeCameraController::RequestId NativeCameraController::requestStop(
    LifecycleCallback callback) {
    if (!callback)
        return 0;

    return enqueue(
        true,
        [this, callback = std::move(callback)](RequestId requestId,
                                               std::uint64_t generation) mutable {
            LifecycleResult result{std::monostate{}};
            CameraError error;
            try {
                if (!camera_.stop(&error)) {
                    result = std::move(error);
                    if (camera_.state() == CameraState::Streaming &&
                        acquisitionFrameGate_) {
                        acquisitionFrameGate_->activateFor(generation);
                    } else {
                        acquisitionFrameGate_.reset();
                    }
                } else {
                    acquisitionFrameGate_.reset();
                }
                frameMailbox_.clear();
            } catch (const std::exception& exception) {
                result = CameraError{0, "camera stop", exception.what()};
            } catch (...) {
                result = CameraError{
                    0, "camera stop", "Unknown exception while stopping acquisition."};
            }
            invokeSafely(callback, requestId, generation, std::move(result));
        });
}

CameraState NativeCameraController::state() const noexcept {
    return camera_.state();
}

std::uint64_t NativeCameraController::droppedFrames() const noexcept {
    return camera_.droppedFrames();
}

std::uint64_t NativeCameraController::framesPulled() const noexcept {
    return camera_.framesPulled();
}

std::uint64_t NativeCameraController::imageEventsReceived() const noexcept {
    return camera_.imageEventsReceived();
}

camera::FramePtr NativeCameraController::takeLatestFrame() {
    return frameMailbox_.takeLatestFrame(
        [this]() noexcept { notifyFrameAvailable(); });
}

NativeCameraController::RequestId NativeCameraController::enqueue(
    bool lifecycleMutation,
    std::function<void(RequestId, std::uint64_t)> execute) {
    if (!execute)
        return 0;

    RequestId requestId = 0;
    {
        std::lock_guard<std::mutex> lock(requestMutex_);
        if (stopping_ || pendingRequests_.size() >= kMaximumPendingRequests)
            return 0;

        if (nextRequestId_ == 0)
            return 0;
        requestId = nextRequestId_++;

        const std::uint64_t generation = lifecycleMutation
            ? nextSessionGenerationLocked()
            : scheduledSessionGeneration_;
        if (generation == 0)
            return 0;

        pendingRequests_.push_back(PendingRequest{
            requestId,
            generation,
            lifecycleMutation,
            std::move(execute)
        });
    }

    requestCondition_.notify_one();
    return requestId;
}

std::uint64_t NativeCameraController::nextSessionGenerationLocked() noexcept {
    if (scheduledSessionGeneration_ == std::numeric_limits<std::uint64_t>::max())
        return 0;
    return ++scheduledSessionGeneration_;
}

void NativeCameraController::runCommandWorker() {
    for (;;) {
        PendingRequest request;
        {
            std::unique_lock<std::mutex> lock(requestMutex_);
            requestCondition_.wait(lock, [this]() {
                return stopping_ || !pendingRequests_.empty();
            });

            if (stopping_)
                return;

            request = std::move(pendingRequests_.front());
            pendingRequests_.pop_front();
        }

        if (request.lifecycleMutation) {
            activeSessionGeneration_.store(
                request.sessionGeneration, std::memory_order_release);
        }

        try {
            request.execute(request.id, request.sessionGeneration);
        } catch (...) {
            // Individual operations translate failures to CameraError. This
            // final boundary prevents an unexpected callback or allocation
            // exception from terminating the command worker.
        }
    }
}

void NativeCameraController::notifyFrameAvailable() noexcept {
    invokeSafely(onFrameAvailable_);
}

void NativeCameraController::handleAcquisitionFailure(
    const std::shared_ptr<detail::AcquisitionFrameGate>& frameGate,
    const CameraError& error) noexcept {
    if (!frameGate)
        return;

    try {
        const std::uint64_t acquisitionGeneration = frameGate->generation();
        if (acquisitionGeneration == 0 ||
            !frameGate->invalidateIfCurrent(acquisitionGeneration)) {
            return;
        }

        // Stop publication immediately. The queued error notification is kept
        // off the acquisition callback and serialized with lifecycle operations.
        frameMailbox_.clear();

        const CameraError errorCopy = error;
        (void)enqueue(
            false,
            [this, acquisitionGeneration, errorCopy](
                RequestId, std::uint64_t) {
                const bool stillOwnsAcquisition =
                    activeSessionGeneration_.load(std::memory_order_acquire) ==
                        acquisitionGeneration &&
                    camera_.state() == CameraState::Faulted;
                if (stillOwnsAcquisition)
                    invokeSafely(onAcquisitionError_, acquisitionGeneration, errorCopy);
            });
    } catch (...) {
        // Acquisition-failure reporting is best-effort under allocation failure;
        // exceptions must never escape into the vendor acquisition worker.
    }
}

} // namespace amscope::native_mac
