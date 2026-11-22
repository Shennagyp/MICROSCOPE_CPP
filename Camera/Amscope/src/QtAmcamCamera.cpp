#include "amscope/QtAmcamCamera.h"

#include <QByteArray>
#include <QDebug>
#include <QMetaObject>
#include <QMetaType>

#include <exception>
#include <optional>
#include <utility>

namespace amscope {

namespace {

camera::CameraFeatureAccessError featureWorkerException(
    const std::string& feature,
    const char* message) {
    return {
        camera::CameraFeatureAccessError::Code::BackendFailure,
        feature,
        message,
        0,
        std::nullopt,
        std::nullopt
    };
}

camera::CameraFeatureAccessError featureWorkerException(
    const std::string& feature,
    const std::exception& exception) {
    return featureWorkerException(feature, exception.what());
}

camera::CameraFeatureAccessError executionModeFeatureError(
    const std::string& feature,
    const char* operation) {
    return {
        camera::CameraFeatureAccessError::Code::BackendFailure,
        feature,
        std::string("QtAmcamCamera instance is committed to queued execution; ") +
            "cannot call synchronous " + operation + ".",
        0,
        std::nullopt,
        std::nullopt
    };
}

} // namespace

QtAmcamCamera::QtAmcamCamera(QObject* parent)
    : QtAmcamCamera(AmcamCameraProfileRegistry{}, parent) {}

QtAmcamCamera::QtAmcamCamera(
    AmcamCameraProfileRegistry profiles,
    QObject* parent)
    : QObject(parent), camera_(std::move(profiles)) {
    qRegisterMetaType<amscope::CameraState>("amscope::CameraState");
    qRegisterMetaType<amscope::FramePtr>("amscope::FramePtr");
    featureWorker_ = std::thread([this]() { runFeatureWorker(); });
}

QtAmcamCamera::~QtAmcamCamera() {
    {
        std::lock_guard<std::mutex> queueLock(featureQueueMutex_);
        stoppingFeatureWorker_ = true;
        featureQueue_.clear();
    }
    featureQueueCondition_.notify_all();

    // Join before touching camera_ or allowing QObject destruction. The worker
    // cannot retain a reference to the facade after this point.
    if (featureWorker_.joinable()) {
        featureWorker_.join();
    }

    {
        std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
        advanceSessionGeneration();
        acquisitionFrameGate_.reset();
        CameraError ignored;
        camera_.close(&ignored);
    }
    clearFrameMailbox();
}

std::vector<CameraInfo> QtAmcamCamera::enumerate(CameraError* error) {
    return AmcamCamera::enumerate(error);
}

QtAmcamCamera::CameraRequestId QtAmcamCamera::requestEnumerateCameras(
    CameraListCallback callback) {
    if (!callback) return 0;

    return enqueueFeatureRequest(
        [this, callback = std::move(callback)](
            CameraRequestId requestId,
            const FeatureRequestContext& requestContext) mutable {
            std::optional<CameraListResult> result;
            try {
                CameraError error;
                auto cameras = AmcamCamera::enumerate(&error);
                if (!error.message.empty())
                    result.emplace(std::move(error));
                else
                    result.emplace(std::move(cameras));
            } catch (const std::exception& exception) {
                result.emplace(CameraError{0, "camera enumeration", exception.what()});
            } catch (...) {
                result.emplace(CameraError{0, "camera enumeration",
                                           "Unknown exception during camera enumeration."});
            }

            dispatchFeatureCallback(
                requestContext,
                [callback = std::move(callback),
                 requestId,
                 result = std::move(*result)]() mutable {
                    callback(requestId, std::move(result));
                });
        },
        FeatureRequestSemantics::SessionBound);
}

bool QtAmcamCamera::open(const QString& deviceId, CameraError* error) {
    if (!requireSynchronousExecutionMode("open", error))
        return false;
    const QByteArray id = deviceId.toUtf8();
    CameraError local;
    bool ok = false;
    {
        std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
        advanceSessionGeneration();
        ok = camera_.open(
            std::string(id.constData(), static_cast<std::size_t>(id.size())),
            &local);
        if (ok) {
            acquisitionFrameGate_.reset();
        } else if (camera_.state() == CameraState::Streaming && acquisitionFrameGate_) {
            acquisitionFrameGate_->activateFor(
                sessionGeneration_.load(std::memory_order_acquire));
        } else {
            acquisitionFrameGate_.reset();
        }
    }

    if (!ok) {
        if (error) *error = local;
        emitError(local);
        return false;
    }
    clearFrameMailbox();
    emit stateChanged(camera_.state());
    return true;
}

bool QtAmcamCamera::close(CameraError* error) {
    if (!requireSynchronousExecutionMode("close", error))
        return false;
    CameraError local;
    bool ok = false;
    {
        std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
        advanceSessionGeneration();
        ok = camera_.close(&local);
        if (!ok && camera_.state() == CameraState::Streaming && acquisitionFrameGate_) {
            acquisitionFrameGate_->activateFor(
                sessionGeneration_.load(std::memory_order_acquire));
        } else {
            acquisitionFrameGate_.reset();
        }
    }
    clearFrameMailbox();
    if (!ok) {
        if (error) *error = local;
        emitError(local);
    }
    emit stateChanged(camera_.state());
    return ok;
}

bool QtAmcamCamera::start(CameraError* error) {
    if (!requireSynchronousExecutionMode("start", error))
        return false;
    CameraError local;
    bool ok = false;
    {
        std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
        advanceSessionGeneration();
        std::shared_ptr<detail::AcquisitionFrameGate> frameGate;
        try {
            frameGate = std::make_shared<detail::AcquisitionFrameGate>(
                sessionGeneration_.load(std::memory_order_acquire));
            ok = camera_.start(
                [this, frameGate](FramePtr frame) {
                    publishLatestFrame(std::move(frame), frameGate);
                },
                [this, frameGate](const CameraError& acquisitionError) {
                    handleAcquisitionFailure(frameGate, acquisitionError);
                },
                &local);
            if (ok) {
                acquisitionFrameGate_ = std::move(frameGate);
            } else if (camera_.state() == CameraState::Streaming &&
                       acquisitionFrameGate_) {
                acquisitionFrameGate_->activateFor(
                    sessionGeneration_.load(std::memory_order_acquire));
            } else {
                acquisitionFrameGate_.reset();
            }
        } catch (const std::exception& exception) {
            if (camera_.state() == CameraState::Streaming && acquisitionFrameGate_) {
                acquisitionFrameGate_->activateFor(
                    sessionGeneration_.load(std::memory_order_acquire));
            } else {
                acquisitionFrameGate_.reset();
            }
            local = {0, "camera start", exception.what()};
            ok = false;
        } catch (...) {
            if (camera_.state() == CameraState::Streaming && acquisitionFrameGate_) {
                acquisitionFrameGate_->activateFor(
                    sessionGeneration_.load(std::memory_order_acquire));
            } else {
                acquisitionFrameGate_.reset();
            }
            local = {0, "camera start",
                     "Unknown exception while preparing acquisition frame delivery."};
            ok = false;
        }
    }

    if (!ok) {
        if (error) *error = local;
        emitError(local);
    }
    emit stateChanged(camera_.state());
    return ok;
}

bool QtAmcamCamera::stop(CameraError* error) {
    if (!requireSynchronousExecutionMode("stop", error))
        return false;
    CameraError local;
    bool ok = false;
    {
        std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
        advanceSessionGeneration();
        ok = camera_.stop(&local);
        if (!ok && camera_.state() == CameraState::Streaming && acquisitionFrameGate_) {
            acquisitionFrameGate_->activateFor(
                sessionGeneration_.load(std::memory_order_acquire));
        } else {
            acquisitionFrameGate_.reset();
        }
    }
    clearFrameMailbox();
    if (!ok) {
        if (error) *error = local;
        emitError(local);
    }
    emit stateChanged(camera_.state());
    return ok;
}

QtAmcamCamera::CameraRequestId QtAmcamCamera::requestOpen(
    QString deviceId,
    LifecycleCallback callback) {
    if (!callback) return 0;
    const QByteArray utf8Id = deviceId.toUtf8();
    std::string id(utf8Id.constData(), static_cast<std::size_t>(utf8Id.size()));

    return enqueueFeatureRequest(
        [this, id = std::move(id), callback = std::move(callback)](
            CameraRequestId requestId,
            const FeatureRequestContext& requestContext) mutable {
            LifecycleResult result{std::monostate{}};
            CameraError error;
            try {
                {
                    std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
                    advanceSessionGenerationTo(requestContext.sessionGeneration);
                    if (!camera_.open(id, &error)) {
                        result = std::move(error);
                        if (camera_.state() == CameraState::Streaming &&
                            acquisitionFrameGate_) {
                            acquisitionFrameGate_->activateFor(
                                sessionGeneration_.load(std::memory_order_acquire));
                        } else {
                            acquisitionFrameGate_.reset();
                        }
                    } else {
                        acquisitionFrameGate_.reset();
                    }
                }
                if (std::holds_alternative<std::monostate>(result))
                    clearFrameMailbox();
            } catch (const std::exception& exception) {
                result = CameraError{0, "camera open", exception.what()};
            } catch (...) {
                result = CameraError{0, "camera open",
                                     "Unknown exception while opening the camera."};
            }
            dispatchLifecycleCallback(
                requestId, std::move(callback), std::move(result));
        },
        FeatureRequestSemantics::LifecycleMutation);
}

QtAmcamCamera::CameraRequestId QtAmcamCamera::requestClose(
    LifecycleCallback callback) {
    if (!callback) return 0;

    return enqueueFeatureRequest(
        [this, callback = std::move(callback)](
            CameraRequestId requestId,
            const FeatureRequestContext& requestContext) mutable {
            LifecycleResult result{std::monostate{}};
            CameraError error;
            try {
                {
                    std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
                    advanceSessionGenerationTo(requestContext.sessionGeneration);
                    if (!camera_.close(&error)) {
                        result = std::move(error);
                        if (camera_.state() == CameraState::Streaming &&
                            acquisitionFrameGate_) {
                            acquisitionFrameGate_->activateFor(
                                sessionGeneration_.load(std::memory_order_acquire));
                        } else {
                            acquisitionFrameGate_.reset();
                        }
                    } else {
                        acquisitionFrameGate_.reset();
                    }
                }
                clearFrameMailbox();
            } catch (const std::exception& exception) {
                result = CameraError{0, "camera close", exception.what()};
            } catch (...) {
                result = CameraError{0, "camera close",
                                     "Unknown exception while closing the camera."};
            }
            dispatchLifecycleCallback(
                requestId, std::move(callback), std::move(result));
        },
        FeatureRequestSemantics::LifecycleMutation);
}

QtAmcamCamera::CameraRequestId QtAmcamCamera::requestStart(
    LifecycleCallback callback) {
    if (!callback) return 0;

    return enqueueFeatureRequest(
        [this, callback = std::move(callback)](
            CameraRequestId requestId,
            const FeatureRequestContext& requestContext) mutable {
            LifecycleResult result{std::monostate{}};
            CameraError error;
            try {
                std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
                advanceSessionGenerationTo(requestContext.sessionGeneration);
                std::shared_ptr<detail::AcquisitionFrameGate> frameGate =
                    std::make_shared<detail::AcquisitionFrameGate>(
                        requestContext.sessionGeneration);
                const bool started = camera_.start(
                    [this, frameGate](FramePtr frame) {
                        publishLatestFrame(std::move(frame), frameGate);
                    },
                [this, frameGate](const CameraError& acquisitionError) {
                    handleAcquisitionFailure(frameGate, acquisitionError);
                },
                    &error);
                if (started) {
                    acquisitionFrameGate_ = std::move(frameGate);
                } else {
                    if (camera_.state() == CameraState::Streaming &&
                        acquisitionFrameGate_) {
                        acquisitionFrameGate_->activateFor(
                            sessionGeneration_.load(std::memory_order_acquire));
                    } else {
                        acquisitionFrameGate_.reset();
                    }
                    result = std::move(error);
                }
            } catch (const std::exception& exception) {
                if (camera_.state() == CameraState::Streaming && acquisitionFrameGate_) {
                    acquisitionFrameGate_->activateFor(
                        sessionGeneration_.load(std::memory_order_acquire));
                } else {
                    acquisitionFrameGate_.reset();
                }
                result = CameraError{0, "camera start", exception.what()};
            } catch (...) {
                if (camera_.state() == CameraState::Streaming && acquisitionFrameGate_) {
                    acquisitionFrameGate_->activateFor(
                        sessionGeneration_.load(std::memory_order_acquire));
                } else {
                    acquisitionFrameGate_.reset();
                }
                result = CameraError{0, "camera start",
                                     "Unknown exception while preparing acquisition frame delivery."};
            }
            dispatchLifecycleCallback(
                requestId, std::move(callback), std::move(result));
        },
        FeatureRequestSemantics::LifecycleMutation);
}

QtAmcamCamera::CameraRequestId QtAmcamCamera::requestStop(
    LifecycleCallback callback) {
    if (!callback) return 0;

    return enqueueFeatureRequest(
        [this, callback = std::move(callback)](
            CameraRequestId requestId,
            const FeatureRequestContext& requestContext) mutable {
            LifecycleResult result{std::monostate{}};
            CameraError error;
            try {
                {
                    std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
                    advanceSessionGenerationTo(requestContext.sessionGeneration);
                    if (!camera_.stop(&error)) {
                        result = std::move(error);
                        if (camera_.state() == CameraState::Streaming &&
                            acquisitionFrameGate_) {
                            acquisitionFrameGate_->activateFor(
                                sessionGeneration_.load(std::memory_order_acquire));
                        } else {
                            acquisitionFrameGate_.reset();
                        }
                    } else {
                        acquisitionFrameGate_.reset();
                    }
                }
                clearFrameMailbox();
            } catch (const std::exception& exception) {
                result = CameraError{0, "camera stop", exception.what()};
            } catch (...) {
                result = CameraError{0, "camera stop",
                                     "Unknown exception while stopping acquisition."};
            }
            dispatchLifecycleCallback(
                requestId, std::move(callback), std::move(result));
        },
        FeatureRequestSemantics::LifecycleMutation);
}

CameraState QtAmcamCamera::state() const noexcept { return camera_.state(); }

std::uint64_t QtAmcamCamera::droppedFrames() const noexcept {
    return camera_.droppedFrames();
}

std::uint64_t QtAmcamCamera::framesPulled() const noexcept {
    return camera_.framesPulled();
}

FramePtr QtAmcamCamera::takeLatestFrame() {
    FramePtr frame = std::atomic_exchange_explicit(
        &latestFrame_, FramePtr{}, std::memory_order_acq_rel);
    notificationPending_.store(false, std::memory_order_release);

    // A producer may publish after the exchange but before the notification
    // flag is cleared. Re-arm exactly one wakeup if a newer frame is waiting.
    if (std::atomic_load_explicit(&latestFrame_, std::memory_order_acquire)) {
        bool expected = false;
        if (notificationPending_.compare_exchange_strong(
                expected, true,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            queueFrameNotification();
        }
    }
    return frame;
}


bool QtAmcamCamera::readTriggerConfiguration(
    AmcamTriggerConfiguration& configuration, CameraError* error) {
    if (!requireSynchronousExecutionMode("readTriggerConfiguration", error))
        return false;
    CameraError local;
    bool ok = false;
    {
        std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
        ok = camera_.readTriggerConfiguration(configuration, &local);
    }
    if (!ok) {
        if (error) *error = local;
        emitError(local);
    }
    return ok;
}

bool QtAmcamCamera::configureTrigger(
    const AmcamTriggerConfiguration& configuration, CameraError* error) {
    if (!requireSynchronousExecutionMode("configureTrigger", error))
        return false;
    CameraError local;
    bool ok = false;
    {
        std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
        advanceFeatureStateGeneration();
        ok = camera_.configureTrigger(configuration, &local);
    }
    if (!ok) {
        if (error) *error = local;
        emitError(local);
    }
    return ok;
}

bool QtAmcamCamera::sendSoftwareTrigger(
    std::uint16_t imageCount, CameraError* error) {
    if (!requireSynchronousExecutionMode("sendSoftwareTrigger", error))
        return false;
    CameraError local;
    bool ok = false;
    {
        std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
        advanceFeatureStateGeneration();
        ok = camera_.sendSoftwareTrigger(imageCount, &local);
    }
    if (!ok) {
        if (error) *error = local;
        emitError(local);
    }
    return ok;
}

bool QtAmcamCamera::autoWhiteBalance(CameraError* error) {
    if (!requireSynchronousExecutionMode("autoWhiteBalance", error))
        return false;
    CameraError local;
    bool ok = false;
    {
        std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
        advanceFeatureStateGeneration();
        ok = camera_.autoWhiteBalance(&local);
    }
    if (!ok) {
        if (error) *error = local;
        emitError(local);
    }
    return ok;
}

bool QtAmcamCamera::readFrameRate(
    FrameRateStatistics& stats,
    CameraError* error) {
    if (!requireSynchronousExecutionMode("readFrameRate", error))
        return false;
    CameraError local;
    bool ok = false;
    {
        std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
        ok = camera_.readFrameRate(stats, &local);
    }
    if (!ok) {
        if (error) *error = local;
        emitError(local);
    }
    return ok;
}

bool QtAmcamCamera::captureStill(
    const StillCaptureRequest& request,
    FramePtr& frame,
    CameraError* error) {
    if (!requireSynchronousExecutionMode("captureStill", error))
        return false;
    CameraError local;
    bool ok = false;
    {
        std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
        ok = camera_.captureStill(request, frame, &local);
    }
    if (!ok) {
        if (error) *error = local;
        emitError(local);
    }
    return ok;
}


bool QtAmcamCamera::readLensFocusState(
    LensFocusState& result, CameraError* error) {
    if (!requireSynchronousExecutionMode("readLensFocusState", error))
        return false;
    CameraError local;
    bool ok = false;
    {
        std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
        ok = camera_.readLensFocusState(result, &local);
    }
    if (!ok) {
        if (error) *error = local;
        emitError(local);
    }
    return ok;
}

bool QtAmcamCamera::setManualLensFocusPosition(
    std::int32_t position, CameraError* error) {
    if (!requireSynchronousExecutionMode("setManualLensFocusPosition", error))
        return false;
    CameraError local;
    bool ok = false;
    {
        std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
        advanceFeatureStateGeneration();
        ok = camera_.setManualLensFocusPosition(position, &local);
    }
    if (!ok) {
        if (error) *error = local;
        emitError(local);
    }
    return ok;
}


bool QtAmcamCamera::setLensAperturePosition(
    std::uint32_t positionIndex, CameraError* error) {
    if (!requireSynchronousExecutionMode("setLensAperturePosition", error))
        return false;
    CameraError local;
    bool ok = false;
    {
        std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
        advanceFeatureStateGeneration();
        ok = camera_.setLensAperturePosition(positionIndex, &local);
    }
    if (!ok) {
        if (error) *error = local;
        emitError(local);
    }
    return ok;
}


bool QtAmcamCamera::readIoLines(
    IoLineList& lines,
    CameraError* error) {
    if (!requireSynchronousExecutionMode("readIoLines", error))
        return false;
    CameraError local;
    bool ok = false;
    {
        std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
        ok = camera_.readIoLines(lines, &local);
    }
    if (!ok) {
        if (error) *error = local;
        emitError(local);
    }
    return ok;
}

bool QtAmcamCamera::configureIoLine(
    const AmcamIoLineConfiguration& configuration,
    CameraError* error) {
    if (!requireSynchronousExecutionMode("configureIoLine", error))
        return false;
    CameraError local;
    bool ok = false;
    {
        std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
        advanceFeatureStateGeneration();
        ok = camera_.configureIoLine(configuration, &local);
    }
    if (!ok) {
        if (error) *error = local;
        emitError(local);
    }
    return ok;
}

bool QtAmcamCamera::readStillResolutions(
    StillResolutionList& resolutions,
    CameraError* error) {
    if (!requireSynchronousExecutionMode("readStillResolutions", error))
        return false;
    CameraError local;
    bool ok = false;
    {
        std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
        ok = camera_.readStillResolutions(resolutions, &local);
    }
    if (!ok) {
        if (error) *error = local;
        emitError(local);
    }
    return ok;
}


bool QtAmcamCamera::readEmbeddedLightSource(
    EmbeddedLightSourceState& result,
    CameraError* error) {
    if (!requireSynchronousExecutionMode("readEmbeddedLightSource", error))
        return false;
    CameraError local;
    bool ok = false;
    {
        std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
        ok = camera_.readEmbeddedLightSource(result, &local);
    }
    if (!ok) {
        if (error) *error = local;
        emitError(local);
    }
    return ok;
}

bool QtAmcamCamera::setEmbeddedLightSourceLevel(
    std::uint32_t level,
    CameraError* error) {
    if (!requireSynchronousExecutionMode("setEmbeddedLightSourceLevel", error))
        return false;
    CameraError local;
    bool ok = false;
    {
        std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
        advanceFeatureStateGeneration();
        ok = camera_.setEmbeddedLightSourceLevel(level, &local);
    }
    if (!ok) {
        if (error) *error = local;
        emitError(local);
    }
    return ok;
}

bool QtAmcamCamera::readFilterWheelState(
    FilterWheelState& result,
    CameraError* error) {
    if (!requireSynchronousExecutionMode("readFilterWheelState", error))
        return false;
    CameraError local;
    bool ok = false;
    {
        std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
        ok = camera_.readFilterWheelState(result, &local);
    }
    if (!ok) {
        if (error) *error = local;
        emitError(local);
    }
    return ok;
}

bool QtAmcamCamera::moveFilterWheel(
    std::uint32_t position,
    FilterWheelDirection direction,
    CameraError* error) {
    if (!requireSynchronousExecutionMode("moveFilterWheel", error))
        return false;
    CameraError local;
    bool ok = false;
    {
        std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
        advanceFeatureStateGeneration();
        ok = camera_.moveFilterWheel(position, direction, &local);
    }
    if (!ok) {
        if (error) *error = local;
        emitError(local);
    }
    return ok;
}


bool QtAmcamCamera::st4Guide(
    ST4GuideDirection direction,
    std::uint32_t durationMilliseconds,
    CameraError* error) {
    if (!requireSynchronousExecutionMode("st4Guide", error))
        return false;
    CameraError local;
    bool ok = false;
    {
        std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
        advanceFeatureStateGeneration();
        ok = camera_.st4Guide(direction, durationMilliseconds, &local);
    }
    if (!ok) {
        if (error) *error = local;
        emitError(local);
    }
    return ok;
}

bool QtAmcamCamera::readST4GuideState(
    ST4GuideState& result,
    CameraError* error) {
    if (!requireSynchronousExecutionMode("readST4GuideState", error))
        return false;
    CameraError local;
    bool ok = false;
    {
        std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
        ok = camera_.readST4GuideState(result, &local);
    }
    if (!ok) {
        if (error) *error = local;
        emitError(local);
    }
    return ok;
}


bool QtAmcamCamera::readAstroAutoFocuserState(
    AstroAutoFocuserState& result,
    CameraError* error) {
    if (!requireSynchronousExecutionMode("readAstroAutoFocuserState", error))
        return false;
    CameraError local;
    bool ok = false;
    {
        std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
        ok = camera_.readAstroAutoFocuserState(result, &local);
    }
    if (!ok) {
        if (error) *error = local;
        emitError(local);
    }
    return ok;
}

bool QtAmcamCamera::moveAstroAutoFocuser(
    std::int32_t targetPosition,
    CameraError* error) {
    if (!requireSynchronousExecutionMode("moveAstroAutoFocuser", error))
        return false;
    CameraError local;
    bool ok = false;
    {
        std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
        advanceFeatureStateGeneration();
        ok = camera_.moveAstroAutoFocuser(targetPosition, &local);
    }
    if (!ok) {
        if (error) *error = local;
        emitError(local);
    }
    return ok;
}

bool QtAmcamCamera::haltAstroAutoFocuser(CameraError* error) {
    if (!requireSynchronousExecutionMode("haltAstroAutoFocuser", error))
        return false;
    CameraError local;
    bool ok = false;
    {
        std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
        advanceFeatureStateGeneration();
        ok = camera_.haltAstroAutoFocuser(&local);
    }
    if (!ok) {
        if (error) *error = local;
        emitError(local);
    }
    return ok;
}


QtAmcamCamera::FeatureRequestId QtAmcamCamera::requestTriggerConfiguration(
    TriggerConfigurationCallback callback) {
    if (!callback) return 0;
    return enqueueFeatureRequest(
        [this, callback = std::move(callback)](
            FeatureRequestId requestId,
            const FeatureRequestContext& requestContext) mutable {
            std::optional<TriggerConfigurationResult> result;
            {
                std::lock_guard<std::mutex> operationLock(cameraOperationMutex_);
                if (!isRequestContextCurrent(requestContext)) return;
                AmcamTriggerConfiguration configuration;
                CameraError local;
                bool ok = false;
                try {
                    ok = camera_.readTriggerConfiguration(configuration, &local);
                } catch (const std::exception& exception) {
                    local = {0, "readTriggerConfiguration", exception.what()};
                } catch (...) {
                    local = {0, "readTriggerConfiguration",
                             "Unknown exception while reading trigger configuration."};
                }
                if (ok) result.emplace(std::move(configuration));
                else result.emplace(std::move(local));
            }
            dispatchFeatureCallback(requestContext,
                [callback = std::move(callback), requestId,
                 result = std::move(*result)]() mutable {
                    callback(requestId, std::move(result));
                });
        }, FeatureRequestSemantics::FeatureSnapshot);
}

QtAmcamCamera::FeatureRequestId QtAmcamCamera::requestConfigureTrigger(
    AmcamTriggerConfiguration configuration,
    AccessoryCommandCallback callback) {
    if (!callback) return 0;
    return enqueueFeatureRequest(
        [this, configuration, callback = std::move(callback)](
            FeatureRequestId requestId,
            const FeatureRequestContext& requestContext) mutable {
            std::optional<AccessoryCommandResult> result;
            {
                std::lock_guard<std::mutex> operationLock(cameraOperationMutex_);
                if (!isRequestContextCurrent(requestContext)) return;
                CameraError local;
                bool ok = false;
                try {
                    ok = camera_.configureTrigger(configuration, &local);
                } catch (const std::exception& exception) {
                    local = {0, "configureTrigger", exception.what()};
                } catch (...) {
                    local = {0, "configureTrigger",
                             "Unknown exception while configuring trigger mode."};
                }
                if (ok) result.emplace(std::monostate{});
                else result.emplace(std::move(local));
            }
            dispatchFeatureCallback(requestContext,
                [callback = std::move(callback), requestId,
                 result = std::move(*result)]() mutable {
                    callback(requestId, std::move(result));
                });
        }, FeatureRequestSemantics::StateMutation);
}

QtAmcamCamera::FeatureRequestId QtAmcamCamera::requestSoftwareTrigger(
    std::uint16_t imageCount,
    AccessoryCommandCallback callback) {
    if (!callback) return 0;
    return enqueueFeatureRequest(
        [this, imageCount, callback = std::move(callback)](
            FeatureRequestId requestId,
            const FeatureRequestContext& requestContext) mutable {
            std::optional<AccessoryCommandResult> result;
            {
                std::lock_guard<std::mutex> operationLock(cameraOperationMutex_);
                if (!isRequestContextCurrent(requestContext)) return;
                CameraError local;
                bool ok = false;
                try {
                    ok = camera_.sendSoftwareTrigger(imageCount, &local);
                } catch (const std::exception& exception) {
                    local = {0, "sendSoftwareTrigger", exception.what()};
                } catch (...) {
                    local = {0, "sendSoftwareTrigger",
                             "Unknown exception while submitting software trigger."};
                }
                if (ok) result.emplace(std::monostate{});
                else result.emplace(std::move(local));
            }
            dispatchFeatureCallback(requestContext,
                [callback = std::move(callback), requestId,
                 result = std::move(*result)]() mutable {
                    callback(requestId, std::move(result));
                });
        }, FeatureRequestSemantics::StateMutation);
}

QtAmcamCamera::FeatureRequestId QtAmcamCamera::requestAutoWhiteBalance(
    AutoWhiteBalanceCallback callback) {
    if (!callback) return 0;

    // The command can change white-balance readbacks. The queue lock orders
    // generation advancement with insertion without blocking the GUI caller.
    // Requests submitted afterwards execute behind this command.
    return enqueueFeatureRequest(
        [this, callback = std::move(callback)](
            FeatureRequestId requestId,
            const FeatureRequestContext& requestContext) mutable {
            std::optional<AutoWhiteBalanceResult> result;
            {
                std::lock_guard<std::mutex> operationLock(cameraOperationMutex_);
                if (!isRequestContextCurrent(requestContext)) {
                    return;
                }

                CameraError local;
                bool ok = false;
                try {
                    ok = camera_.autoWhiteBalance(&local);
                } catch (const std::exception& exception) {
                    local = {0, "Amcam_AwbOnce", exception.what()};
                } catch (...) {
                    local = {0, "Amcam_AwbOnce",
                             "Unknown exception while executing auto white balance."};
                }

                if (ok) result.emplace(std::monostate{});
                else result.emplace(std::move(local));
            }

            dispatchFeatureCallback(
                requestContext,
                [callback = std::move(callback),
                 requestId,
                 result = std::move(*result)]() mutable {
                    callback(requestId, std::move(result));
                });
        },
        FeatureRequestSemantics::StateMutation);
}

QtAmcamCamera::FeatureRequestId QtAmcamCamera::requestFrameRate(
    FrameRateCallback callback) {
    if (!callback) return 0;

    return enqueueFeatureRequest(
        [this, callback = std::move(callback)](
            FeatureRequestId requestId,
            const FeatureRequestContext& requestContext) mutable {
            std::optional<FrameRateResult> result;
            {
                std::lock_guard<std::mutex> operationLock(cameraOperationMutex_);
                if (!isRequestContextCurrent(requestContext)) {
                    return;
                }

                FrameRateStatistics statistics;
                CameraError local;
                bool ok = false;
                try {
                    ok = camera_.readFrameRate(statistics, &local);
                } catch (const std::exception& exception) {
                    local = {0, "Amcam_get_FrameRate", exception.what()};
                } catch (...) {
                    local = {0, "Amcam_get_FrameRate",
                             "Unknown exception while reading frame-rate telemetry."};
                }

                if (ok) result.emplace(std::move(statistics));
                else result.emplace(std::move(local));
            }

            dispatchFeatureCallback(
                requestContext,
                [callback = std::move(callback),
                 requestId,
                 result = std::move(*result)]() mutable {
                    callback(requestId, std::move(result));
                });
        });
}

QtAmcamCamera::FeatureRequestId QtAmcamCamera::requestStillResolutions(
    StillResolutionListCallback callback) {
    if (!callback) return 0;

    return enqueueFeatureRequest(
        [this, callback = std::move(callback)](
            FeatureRequestId requestId,
            const FeatureRequestContext& requestContext) mutable {
            std::optional<StillResolutionListResult> result;
            {
                std::lock_guard<std::mutex> operationLock(cameraOperationMutex_);
                if (!isRequestContextCurrent(requestContext)) {
                    return;
                }

                StillResolutionList resolutions;
                CameraError local;
                bool ok = false;
                try {
                    ok = camera_.readStillResolutions(resolutions, &local);
                } catch (const std::exception& exception) {
                    local = {0, "readStillResolutions", exception.what()};
                } catch (...) {
                    local = {0, "readStillResolutions",
                             "Unknown exception while reading still resolutions."};
                }

                if (ok) result.emplace(std::move(resolutions));
                else result.emplace(std::move(local));
            }

            dispatchFeatureCallback(
                requestContext,
                [callback = std::move(callback),
                 requestId,
                 result = std::move(*result)]() mutable {
                    callback(requestId, std::move(result));
                });
        });
}

QtAmcamCamera::FeatureRequestId QtAmcamCamera::requestStillCapture(
    StillCaptureRequest request,
    StillCaptureCallback callback) {
    if (!callback) return 0;

    return enqueueFeatureRequest(
        [this, request,
         callback = std::move(callback)](
            FeatureRequestId requestId,
            const FeatureRequestContext& requestContext) mutable {
            std::optional<StillCaptureResult> result;
            {
                std::lock_guard<std::mutex> operationLock(cameraOperationMutex_);
                if (!isRequestContextCurrent(requestContext)) {
                    return;
                }

                FramePtr frame;
                CameraError local;
                bool ok = false;
                try {
                    ok = camera_.captureStill(request, frame, &local);
                } catch (const std::exception& exception) {
                    local = {0, "captureStill", exception.what()};
                } catch (...) {
                    local = {0, "captureStill",
                             "Unknown exception while capturing a still image."};
                }

                if (ok) result.emplace(std::move(frame));
                else result.emplace(std::move(local));
            }

            dispatchFeatureCallback(
                requestContext,
                [callback = std::move(callback),
                 requestId,
                 result = std::move(*result)]() mutable {
                    callback(requestId, std::move(result));
                });
        });
}


QtAmcamCamera::FeatureRequestId QtAmcamCamera::requestEmbeddedLightSource(
    EmbeddedLightSourceCallback callback) {
    if (!callback) return 0;
    return enqueueFeatureRequest(
        [this, callback = std::move(callback)](
            FeatureRequestId requestId,
            const FeatureRequestContext& requestContext) mutable {
            std::optional<EmbeddedLightSourceResult> result;
            {
                std::lock_guard<std::mutex> operationLock(cameraOperationMutex_);
                if (!isRequestContextCurrent(requestContext)) return;
                EmbeddedLightSourceState state;
                CameraError local;
                bool ok = false;
                try {
                    ok = camera_.readEmbeddedLightSource(state, &local);
                } catch (const std::exception& exception) {
                    local = {0, "readEmbeddedLightSource", exception.what()};
                } catch (...) {
                    local = {0, "readEmbeddedLightSource",
                             "Unknown exception while reading embedded light-source state."};
                }
                if (ok) result.emplace(std::move(state));
                else result.emplace(std::move(local));
            }
            dispatchFeatureCallback(requestContext,
                [callback = std::move(callback), requestId,
                 result = std::move(*result)]() mutable {
                    callback(requestId, std::move(result));
                });
        }, FeatureRequestSemantics::FeatureSnapshot);
}

QtAmcamCamera::FeatureRequestId QtAmcamCamera::requestSetEmbeddedLightSourceLevel(
    std::uint32_t level,
    AccessoryCommandCallback callback) {
    if (!callback) return 0;
    return enqueueFeatureRequest(
        [this, level, callback = std::move(callback)](
            FeatureRequestId requestId,
            const FeatureRequestContext& requestContext) mutable {
            std::optional<AccessoryCommandResult> result;
            {
                std::lock_guard<std::mutex> operationLock(cameraOperationMutex_);
                if (!isRequestContextCurrent(requestContext)) return;
                CameraError local;
                bool ok = false;
                try {
                    ok = camera_.setEmbeddedLightSourceLevel(level, &local);
                } catch (const std::exception& exception) {
                    local = {0, "setEmbeddedLightSourceLevel", exception.what()};
                } catch (...) {
                    local = {0, "setEmbeddedLightSourceLevel",
                             "Unknown exception while setting embedded light-source level."};
                }
                if (ok) result.emplace(std::monostate{});
                else result.emplace(std::move(local));
            }
            dispatchFeatureCallback(requestContext,
                [callback = std::move(callback), requestId,
                 result = std::move(*result)]() mutable {
                    callback(requestId, std::move(result));
                });
        }, FeatureRequestSemantics::StateMutation);
}

QtAmcamCamera::FeatureRequestId QtAmcamCamera::requestFilterWheelState(
    FilterWheelStateCallback callback) {
    if (!callback) return 0;
    return enqueueFeatureRequest(
        [this, callback = std::move(callback)](
            FeatureRequestId requestId,
            const FeatureRequestContext& requestContext) mutable {
            std::optional<FilterWheelStateResult> result;
            {
                std::lock_guard<std::mutex> operationLock(cameraOperationMutex_);
                if (!isRequestContextCurrent(requestContext)) return;
                FilterWheelState state;
                CameraError local;
                bool ok = false;
                try {
                    ok = camera_.readFilterWheelState(state, &local);
                } catch (const std::exception& exception) {
                    local = {0, "readFilterWheelState", exception.what()};
                } catch (...) {
                    local = {0, "readFilterWheelState",
                             "Unknown exception while reading filter-wheel state."};
                }
                if (ok) result.emplace(std::move(state));
                else result.emplace(std::move(local));
            }
            dispatchFeatureCallback(requestContext,
                [callback = std::move(callback), requestId,
                 result = std::move(*result)]() mutable {
                    callback(requestId, std::move(result));
                });
        }, FeatureRequestSemantics::FeatureSnapshot);
}

QtAmcamCamera::FeatureRequestId QtAmcamCamera::requestMoveFilterWheel(
    std::uint32_t position,
    FilterWheelDirection direction,
    AccessoryCommandCallback callback) {
    if (!callback) return 0;
    return enqueueFeatureRequest(
        [this, position, direction, callback = std::move(callback)](
            FeatureRequestId requestId,
            const FeatureRequestContext& requestContext) mutable {
            std::optional<AccessoryCommandResult> result;
            {
                std::lock_guard<std::mutex> operationLock(cameraOperationMutex_);
                if (!isRequestContextCurrent(requestContext)) return;
                CameraError local;
                bool ok = false;
                try {
                    ok = camera_.moveFilterWheel(position, direction, &local);
                } catch (const std::exception& exception) {
                    local = {0, "moveFilterWheel", exception.what()};
                } catch (...) {
                    local = {0, "moveFilterWheel",
                             "Unknown exception while submitting filter-wheel motion."};
                }
                if (ok) result.emplace(std::monostate{});
                else result.emplace(std::move(local));
            }
            dispatchFeatureCallback(requestContext,
                [callback = std::move(callback), requestId,
                 result = std::move(*result)]() mutable {
                    callback(requestId, std::move(result));
                });
        }, FeatureRequestSemantics::StateMutation);
}


QtAmcamCamera::FeatureRequestId QtAmcamCamera::requestST4Guide(
    ST4GuideDirection direction,
    std::uint32_t durationMilliseconds,
    AccessoryCommandCallback callback) {
    if (!callback) return 0;
    return enqueueFeatureRequest(
        [this, direction, durationMilliseconds, callback = std::move(callback)](
            FeatureRequestId requestId,
            const FeatureRequestContext& requestContext) mutable {
            std::optional<AccessoryCommandResult> result;
            {
                std::lock_guard<std::mutex> operationLock(cameraOperationMutex_);
                if (!isRequestContextCurrent(requestContext)) return;
                CameraError local;
                bool ok = false;
                try {
                    ok = camera_.st4Guide(direction, durationMilliseconds, &local);
                } catch (const std::exception& exception) {
                    local = {0, "st4Guide", exception.what()};
                } catch (...) {
                    local = {0, "st4Guide",
                             "Unknown exception while submitting an ST4 guide command."};
                }
                if (ok) result.emplace(std::monostate{});
                else result.emplace(std::move(local));
            }
            dispatchFeatureCallback(requestContext,
                [callback = std::move(callback), requestId,
                 result = std::move(*result)]() mutable {
                    callback(requestId, std::move(result));
                });
        }, FeatureRequestSemantics::StateMutation);
}

QtAmcamCamera::FeatureRequestId QtAmcamCamera::requestST4GuideState(
    ST4GuideStateCallback callback) {
    if (!callback) return 0;
    return enqueueFeatureRequest(
        [this, callback = std::move(callback)](
            FeatureRequestId requestId,
            const FeatureRequestContext& requestContext) mutable {
            std::optional<ST4GuideStateResult> result;
            {
                std::lock_guard<std::mutex> operationLock(cameraOperationMutex_);
                if (!isRequestContextCurrent(requestContext)) return;
                ST4GuideState state;
                CameraError local;
                bool ok = false;
                try {
                    ok = camera_.readST4GuideState(state, &local);
                } catch (const std::exception& exception) {
                    local = {0, "readST4GuideState", exception.what()};
                } catch (...) {
                    local = {0, "readST4GuideState",
                             "Unknown exception while reading ST4 guide state."};
                }
                if (ok) result.emplace(std::move(state));
                else result.emplace(std::move(local));
            }
            dispatchFeatureCallback(requestContext,
                [callback = std::move(callback), requestId,
                 result = std::move(*result)]() mutable {
                    callback(requestId, std::move(result));
                });
        }, FeatureRequestSemantics::FeatureSnapshot);
}


QtAmcamCamera::FeatureRequestId QtAmcamCamera::requestAstroAutoFocuserState(
    AstroAutoFocuserCallback callback) {
    if (!callback) return 0;
    return enqueueFeatureRequest(
        [this, callback = std::move(callback)](
            FeatureRequestId requestId,
            const FeatureRequestContext& requestContext) mutable {
            std::optional<AstroAutoFocuserResult> result;
            {
                std::lock_guard<std::mutex> operationLock(cameraOperationMutex_);
                if (!isRequestContextCurrent(requestContext)) return;
                AstroAutoFocuserState state;
                CameraError local;
                bool ok = false;
                try {
                    ok = camera_.readAstroAutoFocuserState(state, &local);
                } catch (const std::exception& exception) {
                    local = {0, "readAstroAutoFocuserState", exception.what()};
                } catch (...) {
                    local = {0, "readAstroAutoFocuserState",
                             "Unknown exception while reading AAF state."};
                }
                if (ok) result.emplace(std::move(state));
                else result.emplace(std::move(local));
            }
            dispatchFeatureCallback(requestContext,
                [callback = std::move(callback), requestId,
                 result = std::move(*result)]() mutable {
                    callback(requestId, std::move(result));
                });
        }, FeatureRequestSemantics::FeatureSnapshot);
}

QtAmcamCamera::FeatureRequestId QtAmcamCamera::requestMoveAstroAutoFocuser(
    std::int32_t targetPosition,
    AccessoryCommandCallback callback) {
    if (!callback) return 0;
    return enqueueFeatureRequest(
        [this, targetPosition, callback = std::move(callback)](
            FeatureRequestId requestId,
            const FeatureRequestContext& requestContext) mutable {
            std::optional<AccessoryCommandResult> result;
            {
                std::lock_guard<std::mutex> operationLock(cameraOperationMutex_);
                if (!isRequestContextCurrent(requestContext)) return;
                CameraError local;
                bool ok = false;
                try {
                    ok = camera_.moveAstroAutoFocuser(targetPosition, &local);
                } catch (const std::exception& exception) {
                    local = {0, "moveAstroAutoFocuser", exception.what()};
                } catch (...) {
                    local = {0, "moveAstroAutoFocuser",
                             "Unknown exception while submitting an AAF move."};
                }
                if (ok) result.emplace(std::monostate{});
                else result.emplace(std::move(local));
            }
            dispatchFeatureCallback(requestContext,
                [callback = std::move(callback), requestId,
                 result = std::move(*result)]() mutable {
                    callback(requestId, std::move(result));
                });
        }, FeatureRequestSemantics::StateMutation);
}

QtAmcamCamera::FeatureRequestId QtAmcamCamera::requestHaltAstroAutoFocuser(
    AccessoryCommandCallback callback) {
    if (!callback) return 0;
    return enqueueFeatureRequest(
        [this, callback = std::move(callback)](
            FeatureRequestId requestId,
            const FeatureRequestContext& requestContext) mutable {
            std::optional<AccessoryCommandResult> result;
            {
                std::lock_guard<std::mutex> operationLock(cameraOperationMutex_);
                if (!isRequestContextCurrent(requestContext)) return;
                CameraError local;
                bool ok = false;
                try {
                    ok = camera_.haltAstroAutoFocuser(&local);
                } catch (const std::exception& exception) {
                    local = {0, "haltAstroAutoFocuser", exception.what()};
                } catch (...) {
                    local = {0, "haltAstroAutoFocuser",
                             "Unknown exception while halting the AAF."};
                }
                if (ok) result.emplace(std::monostate{});
                else result.emplace(std::move(local));
            }
            dispatchFeatureCallback(requestContext,
                [callback = std::move(callback), requestId,
                 result = std::move(*result)]() mutable {
                    callback(requestId, std::move(result));
                });
        }, FeatureRequestSemantics::StateMutation);
}


QtAmcamCamera::FeatureRequestId QtAmcamCamera::requestLensFocusState(
    LensFocusStateCallback callback) {
    if (!callback) return 0;
    return enqueueFeatureRequest(
        [this, callback = std::move(callback)](
            FeatureRequestId requestId,
            const FeatureRequestContext& requestContext) mutable {
            std::optional<LensFocusStateResult> result;
            {
                std::lock_guard<std::mutex> operationLock(cameraOperationMutex_);
                if (!isRequestContextCurrent(requestContext)) return;
                LensFocusState state;
                CameraError local;
                bool ok = false;
                try {
                    ok = camera_.readLensFocusState(state, &local);
                } catch (const std::exception& exception) {
                    local = {0, "readLensFocusState", exception.what()};
                } catch (...) {
                    local = {0, "readLensFocusState",
                             "Unknown exception while reading lens-focus state."};
                }
                if (ok) result.emplace(std::move(state));
                else result.emplace(std::move(local));
            }
            dispatchFeatureCallback(requestContext,
                [callback = std::move(callback), requestId,
                 result = std::move(*result)]() mutable {
                    callback(requestId, std::move(result));
                });
        }, FeatureRequestSemantics::FeatureSnapshot);
}

QtAmcamCamera::FeatureRequestId QtAmcamCamera::requestSetManualLensFocusPosition(
    std::int32_t position,
    AccessoryCommandCallback callback) {
    if (!callback) return 0;
    return enqueueFeatureRequest(
        [this, position, callback = std::move(callback)](
            FeatureRequestId requestId,
            const FeatureRequestContext& requestContext) mutable {
            std::optional<AccessoryCommandResult> result;
            {
                std::lock_guard<std::mutex> operationLock(cameraOperationMutex_);
                if (!isRequestContextCurrent(requestContext)) return;
                CameraError local;
                bool ok = false;
                try {
                    ok = camera_.setManualLensFocusPosition(position, &local);
                } catch (const std::exception& exception) {
                    local = {0, "setManualLensFocusPosition", exception.what()};
                } catch (...) {
                    local = {0, "setManualLensFocusPosition",
                             "Unknown exception while setting manual lens-focus position."};
                }
                if (ok) result.emplace(std::monostate{});
                else result.emplace(std::move(local));
            }
            dispatchFeatureCallback(requestContext,
                [callback = std::move(callback), requestId,
                 result = std::move(*result)]() mutable {
                    callback(requestId, std::move(result));
                });
        }, FeatureRequestSemantics::StateMutation);
}


QtAmcamCamera::FeatureRequestId QtAmcamCamera::requestSetLensAperturePosition(
    std::uint32_t positionIndex,
    AccessoryCommandCallback callback) {
    if (!callback) return 0;
    return enqueueFeatureRequest(
        [this, positionIndex, callback = std::move(callback)](
            FeatureRequestId requestId,
            const FeatureRequestContext& requestContext) mutable {
            std::optional<AccessoryCommandResult> result;
            {
                std::lock_guard<std::mutex> operationLock(cameraOperationMutex_);
                if (!isRequestContextCurrent(requestContext)) return;
                CameraError local;
                bool ok = false;
                try {
                    ok = camera_.setLensAperturePosition(positionIndex, &local);
                } catch (const std::exception& exception) {
                    local = {0, "setLensAperturePosition", exception.what()};
                } catch (...) {
                    local = {0, "setLensAperturePosition",
                             "Unknown exception while setting lens aperture position."};
                }
                if (ok) result.emplace(std::monostate{});
                else result.emplace(std::move(local));
            }
            dispatchFeatureCallback(requestContext,
                [callback = std::move(callback), requestId,
                 result = std::move(*result)]() mutable {
                    callback(requestId, std::move(result));
                });
        }, FeatureRequestSemantics::StateMutation);
}


QtAmcamCamera::FeatureRequestId QtAmcamCamera::requestIoLines(
    IoLineListCallback callback) {
    if (!callback) return 0;
    return enqueueFeatureRequest(
        [this, callback = std::move(callback)](
            FeatureRequestId requestId,
            const FeatureRequestContext& requestContext) mutable {
            std::optional<IoLineListResult> result;
            {
                std::lock_guard<std::mutex> operationLock(cameraOperationMutex_);
                if (!isRequestContextCurrent(requestContext)) return;
                IoLineList lines;
                CameraError local;
                bool ok = false;
                try {
                    ok = camera_.readIoLines(lines, &local);
                } catch (const std::exception& exception) {
                    local = {0, "readIoLines", exception.what()};
                } catch (...) {
                    local = {0, "readIoLines",
                             "Unknown exception while reading hardware I/O lines."};
                }
                if (ok) result.emplace(std::move(lines));
                else result.emplace(std::move(local));
            }
            dispatchFeatureCallback(
                requestContext,
                [callback = std::move(callback), requestId,
                 result = std::move(*result)]() mutable {
                    callback(requestId, std::move(result));
                });
        }, FeatureRequestSemantics::FeatureSnapshot);
}

QtAmcamCamera::FeatureRequestId QtAmcamCamera::requestConfigureIoLine(
    AmcamIoLineConfiguration configuration,
    AccessoryCommandCallback callback) {
    if (!callback) return 0;
    return enqueueFeatureRequest(
        [this, configuration = std::move(configuration),
         callback = std::move(callback)](
            FeatureRequestId requestId,
            const FeatureRequestContext& requestContext) mutable {
            std::optional<AccessoryCommandResult> result;
            {
                std::lock_guard<std::mutex> operationLock(cameraOperationMutex_);
                if (!isRequestContextCurrent(requestContext)) return;
                CameraError local;
                bool ok = false;
                try {
                    ok = camera_.configureIoLine(configuration, &local);
                } catch (const std::exception& exception) {
                    local = {0, "configureIoLine", exception.what()};
                } catch (...) {
                    local = {0, "configureIoLine",
                             "Unknown exception while configuring a hardware I/O line."};
                }
                if (ok) result.emplace(std::monostate{});
                else result.emplace(std::move(local));
            }
            dispatchFeatureCallback(
                requestContext,
                [callback = std::move(callback), requestId,
                 result = std::move(*result)]() mutable {
                    callback(requestId, std::move(result));
                });
        }, FeatureRequestSemantics::StateMutation);
}

QtAmcamCamera::FeatureRequestId QtAmcamCamera::requestFeatureDescriptors(
    FeatureDescriptorsCallback callback) {
    if (!callback) return 0;

    return enqueueFeatureRequest(
        [this, callback = std::move(callback)](
            FeatureRequestId requestId,
            const FeatureRequestContext& requestContext) mutable {
            std::optional<FeatureDescriptorsResult> result;
            {
                std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
                if (!isRequestContextCurrent(requestContext)) {
                    return;
                }
                try {
                    result.emplace(camera_.featureDescriptors());
                } catch (const std::exception& exception) {
                    result.emplace(featureWorkerException({}, exception));
                } catch (...) {
                    result.emplace(featureWorkerException(
                        {}, "Unknown exception while reading camera feature descriptors."));
                }
            }

            dispatchFeatureCallback(
                requestContext,
                [callback = std::move(callback),
                 requestId,
                 result = std::move(*result)]() mutable {
                    callback(requestId, std::move(result));
                });
        },
        FeatureRequestSemantics::FeatureSnapshot);
}

QtAmcamCamera::FeatureRequestId QtAmcamCamera::requestReadFeature(
    QString name,
    FeatureReadCallback callback) {
    if (!callback) return 0;

    const QByteArray utf8Name = name.toUtf8();
    std::string featureName(
        utf8Name.constData(),
        static_cast<std::size_t>(utf8Name.size()));

    return enqueueFeatureRequest(
        [this,
         featureName = std::move(featureName),
         callback = std::move(callback)](
            FeatureRequestId requestId,
            const FeatureRequestContext& requestContext) mutable {
            std::optional<FeatureReadResult> result;
            {
                std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
                if (!isRequestContextCurrent(requestContext)) {
                    return;
                }
                try {
                    result.emplace(camera_.readFeature(featureName));
                } catch (const std::exception& exception) {
                    result.emplace(featureWorkerException(featureName, exception));
                } catch (...) {
                    result.emplace(featureWorkerException(
                        featureName, "Unknown exception while reading a camera feature."));
                }
            }

            dispatchFeatureCallback(
                requestContext,
                [callback = std::move(callback),
                 requestId,
                 result = std::move(*result)]() mutable {
                    callback(requestId, std::move(result));
                });
        },
        FeatureRequestSemantics::FeatureSnapshot);
}

QtAmcamCamera::FeatureRequestId QtAmcamCamera::requestWriteFeature(
    QString name,
    camera::CameraFeatureValue value,
    FeatureWriteCallback callback) {
    if (!callback) return 0;

    const QByteArray utf8Name = name.toUtf8();
    std::string featureName(
        utf8Name.constData(),
        static_cast<std::size_t>(utf8Name.size()));

    // A queued write supersedes earlier reads/descriptors. The queue lock
    // publishes its new context with insertion, without waiting for an in-flight
    // SDK call on the submitting (potentially GUI) thread.
    return enqueueFeatureRequest(
        [this,
         featureName = std::move(featureName),
         value = std::move(value),
         callback = std::move(callback)](
            FeatureRequestId requestId,
            const FeatureRequestContext& requestContext) mutable {
            std::optional<FeatureWriteResult> result;
            {
                std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
                if (!isRequestContextCurrent(requestContext)) {
                    return;
                }
                try {
                    result.emplace(camera_.writeFeature(featureName, value));
                } catch (const std::exception& exception) {
                    result.emplace(featureWorkerException(featureName, exception));
                } catch (...) {
                    result.emplace(featureWorkerException(
                        featureName, "Unknown exception while writing a camera feature."));
                }
            }

            dispatchFeatureCallback(
                requestContext,
                [callback = std::move(callback),
                 requestId,
                 result = std::move(*result)]() mutable {
                    callback(requestId, std::move(result));
                });
        },
        FeatureRequestSemantics::StateMutation);
}

bool QtAmcamCamera::selectExecutionMode(ExecutionMode mode) noexcept {
    ExecutionMode current = executionMode_.load(std::memory_order_acquire);
    for (;;) {
        if (current == mode)
            return true;
        if (current != ExecutionMode::Undecided)
            return false;
        if (executionMode_.compare_exchange_weak(
                current, mode,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return true;
        }
    }
}

bool QtAmcamCamera::requireSynchronousExecutionMode(
    const char* operation,
    CameraError* error) {
    if (selectExecutionMode(ExecutionMode::Synchronous))
        return true;

    CameraError modeError{
        0,
        operation,
        "This QtAmcamCamera instance is committed to queued request execution."
    };
    if (error) *error = modeError;
    emitError(modeError);
    return false;
}

camera::CameraFeatureAccessError QtAmcamCamera::synchronousModeFeatureError(
    const std::string& feature,
    const char* operation) const {
    return executionModeFeatureError(feature, operation);
}

QtAmcamCamera::FeatureRequestId QtAmcamCamera::enqueueFeatureRequest(
    FeatureRequestTask task,
    FeatureRequestSemantics semantics,
    bool reservedControlRequest) {
    if (!task) return 0;

    std::lock_guard<std::mutex> queueLock(featureQueueMutex_);
    if (stoppingFeatureWorker_ ||
        (!reservedControlRequest &&
         featureQueue_.size() >= kMaximumPendingFeatureRequests)) {
        return 0;
    }

    FeatureRequestId requestId = nextFeatureRequestId_++;
    if (requestId == 0) {
        requestId = nextFeatureRequestId_++;
    }

    // Insert a placeholder while holding the queue lock so the worker cannot
    // observe it until its execution mode and generation are committed.
    // A failed allocation or mode conflict therefore does not latch the instance.
    featureQueue_.push_back({
        requestId,
        {},
        std::move(task)
    });

    if (!reservedControlRequest &&
        !selectExecutionMode(ExecutionMode::Queued)) {
        featureQueue_.pop_back();
        return 0;
    }

    FeatureRequestContext context;
    context.semantics = semantics;
    if (semantics == FeatureRequestSemantics::LifecycleMutation) {
        context.sessionGeneration =
            scheduledSessionGeneration_.fetch_add(1, std::memory_order_acq_rel) + 1;
    } else {
        context.sessionGeneration =
            scheduledSessionGeneration_.load(std::memory_order_acquire);
    }
    if (semantics == FeatureRequestSemantics::StateMutation) {
        context.featureStateGeneration =
            featureStateGeneration_.fetch_add(1, std::memory_order_acq_rel) + 1;
    } else {
        context.featureStateGeneration =
            featureStateGeneration_.load(std::memory_order_acquire);
    }
    featureQueue_.back().context = context;

    featureQueueCondition_.notify_one();
    return requestId;
}

void QtAmcamCamera::runFeatureWorker() {
    for (;;) {
        PendingFeatureRequest request;
        {
            std::unique_lock<std::mutex> queueLock(featureQueueMutex_);
            featureQueueCondition_.wait(queueLock, [this]() {
                return stoppingFeatureWorker_ || !featureQueue_.empty();
            });

            if (stoppingFeatureWorker_) {
                return;
            }

            request = std::move(featureQueue_.front());
            featureQueue_.pop_front();
        }

        if (request.context.semantics != FeatureRequestSemantics::LifecycleMutation &&
            !isRequestContextCurrent(request.context)) {
            continue;
        }

        try {
            request.task(request.id, request.context);
        } catch (const std::exception& exception) {
            qWarning() << "Unhandled camera feature worker exception:"
                       << exception.what();
        } catch (...) {
            qWarning() << "Unknown unhandled camera feature worker exception.";
        }
    }
}

void QtAmcamCamera::dispatchFeatureCallback(
    const FeatureRequestContext& context,
    std::function<void()> callback) {
    dispatchToOwnerThread(
        [this, context, callback = std::move(callback)]() mutable {
            if (!isRequestContextCurrent(context))
                return;
            callback();
        });
}

bool QtAmcamCamera::isRequestContextCurrent(
    const FeatureRequestContext& context) const noexcept {
    if (context.sessionGeneration !=
        sessionGeneration_.load(std::memory_order_acquire)) {
        return false;
    }

    if (context.semantics == FeatureRequestSemantics::FeatureSnapshot &&
        context.featureStateGeneration !=
            featureStateGeneration_.load(std::memory_order_acquire)) {
        return false;
    }

    return true;
}

void QtAmcamCamera::advanceSessionGeneration() noexcept {
    const std::uint64_t generation =
        scheduledSessionGeneration_.fetch_add(1, std::memory_order_acq_rel) + 1;
    advanceSessionGenerationTo(generation);
}

void QtAmcamCamera::advanceSessionGenerationTo(std::uint64_t generation) noexcept {
    std::uint64_t current = sessionGeneration_.load(std::memory_order_acquire);
    while (current < generation &&
           !sessionGeneration_.compare_exchange_weak(
               current, generation,
               std::memory_order_acq_rel,
               std::memory_order_acquire)) {
    }
}

void QtAmcamCamera::advanceFeatureStateGeneration() noexcept {
    featureStateGeneration_.fetch_add(1, std::memory_order_acq_rel);
}

void QtAmcamCamera::handleAcquisitionFailure(
    const std::shared_ptr<detail::AcquisitionFrameGate>& frameGate,
    const CameraError& error) {
    if (!frameGate) return;

    const std::uint64_t acquisitionGeneration = frameGate->generation();
    if (acquisitionGeneration == 0 ||
        !frameGate->invalidateIfCurrent(acquisitionGeneration)) {
        return;
    }

    // Stop frame publication immediately. The gate check and mailbox write are
    // serialized by mailboxMutex_, so no old frame can appear after this clear.
    clearFrameMailbox();

    const CameraError copy = error;
    (void)enqueueFeatureRequest(
        [this, acquisitionGeneration, copy](
            FeatureRequestId,
            const FeatureRequestContext& context) {
            // Fatal failure joins the same FIFO as Stop/Close and feature I/O.
            // Operations accepted before this event run first and may supersede
            // it; requests accepted after it inherit the post-fault generation.
            const bool stillOwnsAcquisition =
                sessionGeneration_.load(std::memory_order_acquire) ==
                    acquisitionGeneration &&
                camera_.state() == CameraState::Faulted;

            advanceSessionGenerationTo(context.sessionGeneration);
            if (!stillOwnsAcquisition)
                return;

            dispatchFeatureCallback(context, [this, copy]() {
                emitError(copy);
                emit stateChanged(camera_.state());
            });
        },
        FeatureRequestSemantics::LifecycleMutation,
        true);
}
void QtAmcamCamera::dispatchLifecycleCallback(
    CameraRequestId requestId,
    LifecycleCallback callback,
    LifecycleResult result) {
    dispatchToOwnerThread(
        [this,
         requestId,
         callback = std::move(callback),
         result = std::move(result)]() mutable {
            if (const auto* error = std::get_if<CameraError>(&result))
                emitError(*error);
            emit stateChanged(camera_.state());
            callback(requestId, std::move(result));
        });
}

void QtAmcamCamera::dispatchToOwnerThread(std::function<void()> callback) {
    if (!callback) return;

    // Synchronize posting with destructor shutdown. Once shutdown marks the
    // executor as stopping, neither the feature worker nor the acquisition
    // callback may enqueue another functor against this QObject.
    std::lock_guard<std::mutex> queueLock(featureQueueMutex_);
    if (stoppingFeatureWorker_) return;

    QMetaObject::invokeMethod(
        this,
        [callback = std::move(callback)]() mutable {
            try {
                callback();
            } catch (const std::exception& exception) {
                qWarning() << "Camera Qt completion callback threw:"
                           << exception.what();
            } catch (...) {
                qWarning() << "Camera Qt completion callback threw an unknown exception.";
            }
        },
        Qt::QueuedConnection);
}

void QtAmcamCamera::publishLatestFrame(
    FramePtr frame,
    const std::shared_ptr<detail::AcquisitionFrameGate>& frameGate) {
    if (!frameGate ||
        !frameGate->accepts(
            sessionGeneration_.load(std::memory_order_acquire))) {
        return;
    }

    // The shared_ptr operation publishes only the lease, not a pixel copy.
    // The UI and the worker own independent references to the same pool frame.
    const FramePtr published = std::move(frame);
    std::atomic_store_explicit(
        &latestFrame_, published, std::memory_order_release);

    // Close the race with Stop/Close invalidation: a frame whose acquisition
    // generation was superseded must not remain visible in the new session.
    if (!frameGate->accepts(
            sessionGeneration_.load(std::memory_order_acquire))) {
        FramePtr expected = published;
        (void)std::atomic_compare_exchange_strong_explicit(
            &latestFrame_, &expected, FramePtr{},
            std::memory_order_acq_rel,
            std::memory_order_acquire);
        return;
    }

    if (!notificationPending_.exchange(true, std::memory_order_acq_rel))
        queueFrameNotification();
}

void QtAmcamCamera::queueFrameNotification() {
    // Avoid featureQueueMutex_: acquisition notification is independent of
    // feature-request scheduling. Close joins acquisition before QObject dies.
    QMetaObject::invokeMethod(this, [this]() {
        emit frameAvailable();
    }, Qt::QueuedConnection);
}

void QtAmcamCamera::clearFrameMailbox() {
    std::atomic_store_explicit(
        &latestFrame_, FramePtr{}, std::memory_order_release);
    notificationPending_.store(false, std::memory_order_release);
}

void QtAmcamCamera::emitError(const CameraError& error) {
    emit errorOccurred(QString::fromStdString(error.message),
                       static_cast<qint32>(error.vendorCode));
}

QtAmcamCamera::FeatureDescriptorsResult QtAmcamCamera::featureDescriptors() {
    if (!selectExecutionMode(ExecutionMode::Synchronous)) {
        return synchronousModeFeatureError("*", "featureDescriptors");
    }

    std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
    return camera_.featureDescriptors();
}

QtAmcamCamera::FeatureReadResult QtAmcamCamera::readFeature(std::string_view name) {
    if (!selectExecutionMode(ExecutionMode::Synchronous)) {
        return synchronousModeFeatureError(std::string(name), "readFeature");
    }

    std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
    return camera_.readFeature(name);
}

QtAmcamCamera::FeatureWriteResult QtAmcamCamera::writeFeature(
    std::string_view name,
    const camera::CameraFeatureValue& value) {
    if (!selectExecutionMode(ExecutionMode::Synchronous)) {
        return synchronousModeFeatureError(std::string(name), "writeFeature");
    }

    std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
    advanceFeatureStateGeneration();
    return camera_.writeFeature(name, value);
}

} // namespace amscope
