#include "NativeCameraController.h"

#include <exception>
#include <limits>
#include <optional>
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

bool NativeCameraController::startAcquisitionForGeneration(
    std::uint64_t generation,
    CameraError* error) noexcept {
    if (error)
        *error = {};

    std::shared_ptr<detail::AcquisitionFrameGate> frameGate;
    try {
        frameGate =
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
            error);

        if (started) {
            acquisitionFrameGate_ = std::move(frameGate);
            return true;
        }

        if (camera_.state() == CameraState::Streaming) {
            if (acquisitionFrameGate_) {
                acquisitionFrameGate_->activateFor(generation);
            } else if (frameGate) {
                frameGate->activateFor(generation);
                acquisitionFrameGate_ = std::move(frameGate);
            }
        } else {
            acquisitionFrameGate_.reset();
        }
        return false;
    } catch (const std::exception& exception) {
        if (error)
            *error = CameraError{0, "camera start", exception.what()};
    } catch (...) {
        if (error) {
            *error = CameraError{
                0, "camera start",
                "Unknown exception while preparing acquisition."};
        }
    }

    if (camera_.state() == CameraState::Streaming) {
        if (acquisitionFrameGate_) {
            acquisitionFrameGate_->activateFor(generation);
        } else if (frameGate) {
            frameGate->activateFor(generation);
            acquisitionFrameGate_ = std::move(frameGate);
        }
    } else {
        acquisitionFrameGate_.reset();
    }
    return false;
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
            if (!startAcquisitionForGeneration(generation, &error))
                result = std::move(error);
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

NativeCameraController::RequestId NativeCameraController::requestFeatureDescriptors(
    FeatureDescriptorsCallback callback) {
    if (!callback)
        return 0;

    return enqueueFeatureRequest(
        RequestSemantics::FeatureSnapshot,
        [this, callback = std::move(callback)](
            RequestId requestId,
            std::uint64_t sessionGeneration,
            std::uint64_t featureStateGeneration) mutable {
            FeatureDescriptorsResult result;
            try {
                result = camera_.featureDescriptors();
            } catch (const std::exception& exception) {
                result = camera::CameraFeatureAccessError{
                    camera::CameraFeatureAccessError::Code::BackendFailure,
                    {},
                    exception.what(),
                    0,
                    std::nullopt,
                    std::nullopt
                };
            } catch (...) {
                result = camera::CameraFeatureAccessError{
                    camera::CameraFeatureAccessError::Code::BackendFailure,
                    {},
                    "Unknown exception while reading camera feature descriptors.",
                    0,
                    std::nullopt,
                    std::nullopt
                };
            }

            if (isFeatureSnapshotCurrent(
                    sessionGeneration, featureStateGeneration)) {
                invokeSafely(
                    callback,
                    requestId,
                    sessionGeneration,
                    featureStateGeneration,
                    std::move(result));
            }
        });
}

NativeCameraController::RequestId NativeCameraController::requestReadFeature(
    std::string name,
    FeatureReadCallback callback) {
    if (!callback || name.empty())
        return 0;

    return enqueueFeatureRequest(
        RequestSemantics::FeatureSnapshot,
        [this, name = std::move(name), callback = std::move(callback)](
            RequestId requestId,
            std::uint64_t sessionGeneration,
            std::uint64_t featureStateGeneration) mutable {
            FeatureReadResult result;
            try {
                result = camera_.readFeature(name);
            } catch (const std::exception& exception) {
                result = camera::CameraFeatureAccessError{
                    camera::CameraFeatureAccessError::Code::BackendFailure,
                    name,
                    exception.what(),
                    0,
                    std::nullopt,
                    std::nullopt
                };
            } catch (...) {
                result = camera::CameraFeatureAccessError{
                    camera::CameraFeatureAccessError::Code::BackendFailure,
                    name,
                    "Unknown exception while reading camera feature.",
                    0,
                    std::nullopt,
                    std::nullopt
                };
            }

            if (isFeatureSnapshotCurrent(
                    sessionGeneration, featureStateGeneration)) {
                invokeSafely(
                    callback,
                    requestId,
                    sessionGeneration,
                    featureStateGeneration,
                    std::move(result));
            }
        });
}

NativeCameraController::RequestId NativeCameraController::requestWriteFeature(
    std::string name,
    camera::CameraFeatureValue value,
    FeatureWriteCallback callback) {
    if (!callback || name.empty())
        return 0;

    return enqueueFeatureRequest(
        RequestSemantics::FeatureMutation,
        [this, name = std::move(name), value = std::move(value),
         callback = std::move(callback)](
            RequestId requestId,
            std::uint64_t sessionGeneration,
            std::uint64_t) mutable {
            FeatureWriteResult result;
            try {
                result = camera_.writeFeature(name, value);
            } catch (const std::exception& exception) {
                result = camera::CameraFeatureAccessError{
                    camera::CameraFeatureAccessError::Code::BackendFailure,
                    name,
                    exception.what(),
                    0,
                    std::nullopt,
                    std::nullopt
                };
            } catch (...) {
                result = camera::CameraFeatureAccessError{
                    camera::CameraFeatureAccessError::Code::BackendFailure,
                    name,
                    "Unknown exception while writing camera feature.",
                    0,
                    std::nullopt,
                    std::nullopt
                };
            }

            if (isSessionCurrent(sessionGeneration)) {
                invokeSafely(callback, requestId, sessionGeneration, std::move(result));
            }
        });
}

NativeCameraController::RequestId
NativeCameraController::requestChangePreviewResolution(
    std::string resolutionSymbol,
    FeatureWriteCallback callback) {
    if (!callback || resolutionSymbol.empty())
        return 0;

    return enqueueFeatureRequest(
        RequestSemantics::ResolutionMutation,
        [this, resolutionSymbol = std::move(resolutionSymbol),
         callback = std::move(callback)](
            RequestId requestId,
            std::uint64_t sessionGeneration,
            std::uint64_t) mutable {
            FeatureWriteResult result{std::monostate{}};
            const CameraState initialState = camera_.state();
            if (initialState != CameraState::Open &&
                initialState != CameraState::Streaming) {
                result = camera::CameraFeatureAccessError{
                    camera::CameraFeatureAccessError::Code::BackendFailure,
                    "AmcamResolutionIndex",
                    "Open the camera before changing preview resolution.",
                    0,
                    std::nullopt,
                    std::nullopt
                };
                invokeSafely(callback, requestId, sessionGeneration, std::move(result));
                return;
            }

            const bool resumeAcquisition =
                initialState == CameraState::Streaming;
            if (resumeAcquisition) {
                CameraError stopError;
                if (!camera_.stop(&stopError)) {
                    if (camera_.state() == CameraState::Streaming &&
                        acquisitionFrameGate_) {
                        acquisitionFrameGate_->activateFor(sessionGeneration);
                    } else {
                        acquisitionFrameGate_.reset();
                        frameMailbox_.clear();
                    }
                    result = camera::CameraFeatureAccessError{
                        camera::CameraFeatureAccessError::Code::BackendFailure,
                        "AmcamResolutionIndex",
                        stopError.message.empty()
                            ? "Acquisition could not stop for the resolution change."
                            : "Acquisition could not stop for the resolution change: " +
                                  stopError.message,
                        stopError.vendorCode,
                        std::nullopt,
                        std::nullopt
                    };
                    invokeSafely(
                        callback, requestId, sessionGeneration, std::move(result));
                    return;
                }

                acquisitionFrameGate_.reset();
                frameMailbox_.clear();
            }

            try {
                result = camera_.writeFeature(
                    "AmcamResolutionIndex",
                    camera::CameraFeatureValue{
                        camera::FeatureEnumeration{resolutionSymbol}});
            } catch (const std::exception& exception) {
                result = camera::CameraFeatureAccessError{
                    camera::CameraFeatureAccessError::Code::BackendFailure,
                    "AmcamResolutionIndex",
                    exception.what(),
                    0,
                    std::nullopt,
                    std::nullopt
                };
            } catch (...) {
                result = camera::CameraFeatureAccessError{
                    camera::CameraFeatureAccessError::Code::BackendFailure,
                    "AmcamResolutionIndex",
                    "Unknown exception while applying the preview resolution.",
                    0,
                    std::nullopt,
                    std::nullopt
                };
            }

            if (resumeAcquisition) {
                CameraError restartError;
                if (!startAcquisitionForGeneration(
                        sessionGeneration, &restartError)) {
                    const std::string restartMessage =
                        restartError.message.empty()
                            ? "Acquisition could not restart."
                            : restartError.message;
                    if (auto* writeError =
                            std::get_if<camera::CameraFeatureAccessError>(&result)) {
                        writeError->message +=
                            " The previous acquisition could not be restarted: " +
                            restartMessage;
                    } else {
                        result = camera::CameraFeatureAccessError{
                            camera::CameraFeatureAccessError::Code::BackendFailure,
                            "AmcamResolutionIndex",
                            "The resolution was applied, but acquisition could not "
                            "restart: " + restartMessage,
                            restartError.vendorCode,
                            std::nullopt,
                            std::nullopt
                        };
                    }
                }
            }

            if (isSessionCurrent(sessionGeneration)) {
                invokeSafely(
                    callback, requestId, sessionGeneration, std::move(result));
            }
        });
}

bool NativeCameraController::isSessionCurrent(
    std::uint64_t sessionGeneration) const noexcept {
    return activeSessionGeneration_.load(std::memory_order_acquire) ==
        sessionGeneration;
}

bool NativeCameraController::isFeatureSnapshotCurrent(
    std::uint64_t sessionGeneration,
    std::uint64_t featureStateGeneration) const noexcept {
    return isSessionCurrent(sessionGeneration) &&
        featureStateGeneration_.load(std::memory_order_acquire) ==
            featureStateGeneration;
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

    const RequestSemantics semantics = lifecycleMutation
        ? RequestSemantics::LifecycleMutation
        : RequestSemantics::SessionBound;
    return enqueueFeatureRequest(
        semantics,
        [execute = std::move(execute)](
            RequestId requestId,
            std::uint64_t sessionGeneration,
            std::uint64_t) mutable {
            execute(requestId, sessionGeneration);
        });
}

NativeCameraController::RequestId NativeCameraController::enqueueFeatureRequest(
    RequestSemantics semantics,
    std::function<void(RequestId, std::uint64_t, std::uint64_t)> execute) {
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

        const bool lifecycleMutation =
            semantics == RequestSemantics::LifecycleMutation ||
            semantics == RequestSemantics::ResolutionMutation;
        const bool featureMutation =
            semantics == RequestSemantics::FeatureMutation ||
            semantics == RequestSemantics::ResolutionMutation;

        const std::uint64_t sessionGeneration = lifecycleMutation
            ? nextSessionGenerationLocked()
            : scheduledSessionGeneration_;
        if (lifecycleMutation && sessionGeneration == 0)
            return 0;

        if (featureMutation &&
            scheduledFeatureStateGeneration_ ==
                std::numeric_limits<std::uint64_t>::max()) {
            return 0;
        }
        if (featureMutation) {
            ++scheduledFeatureStateGeneration_;
            featureStateGeneration_.store(
                scheduledFeatureStateGeneration_, std::memory_order_release);
        }

        const std::uint64_t featureStateGeneration =
            scheduledFeatureStateGeneration_;
        auto task = [execute = std::move(execute), featureStateGeneration](
            RequestId id, std::uint64_t session) mutable {
            execute(id, session, featureStateGeneration);
        };

        pendingRequests_.push_back(PendingRequest{
            requestId,
            sessionGeneration,
            featureStateGeneration,
            semantics,
            std::move(task)
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

        if (request.semantics == RequestSemantics::LifecycleMutation ||
            request.semantics == RequestSemantics::ResolutionMutation) {
            activeSessionGeneration_.store(
                request.sessionGeneration, std::memory_order_release);
        } else if (!isSessionCurrent(request.sessionGeneration)) {
            continue;
        }

        if (request.semantics == RequestSemantics::FeatureSnapshot &&
            featureStateGeneration_.load(std::memory_order_acquire) !=
                request.featureStateGeneration) {
            continue;
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
