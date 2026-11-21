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
        advanceFeatureContextGeneration();
        CameraError ignored;
        camera_.close(&ignored);
    }
    clearFrameMailbox();
}

std::vector<CameraInfo> QtAmcamCamera::enumerate(CameraError* error) {
    return AmcamCamera::enumerate(error);
}

bool QtAmcamCamera::open(const QString& deviceId, CameraError* error) {
    const QByteArray id = deviceId.toUtf8();
    CameraError local;
    bool ok = false;
    {
        std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
        advanceFeatureContextGeneration();
        ok = camera_.open(
            std::string(id.constData(), static_cast<std::size_t>(id.size())),
            &local);
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
    CameraError local;
    bool ok = false;
    {
        std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
        advanceFeatureContextGeneration();
        ok = camera_.close(&local);
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
    CameraError local;
    bool ok = false;
    {
        std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
        advanceFeatureContextGeneration();
        ok = camera_.start(
            [this](FramePtr frame) {
                publishLatestFrame(std::move(frame));
            },
            [this](const CameraError& acquisitionError) {
                // Marshal notifications to this QObject's affinity thread.
                const CameraError copy = acquisitionError;
                dispatchToOwnerThread([this, copy]() {
                    emitError(copy);
                    emit stateChanged(camera_.state());
                });
            },
            &local);
    }

    if (!ok) {
        if (error) *error = local;
        emitError(local);
    }
    emit stateChanged(camera_.state());
    return ok;
}

bool QtAmcamCamera::stop(CameraError* error) {
    CameraError local;
    bool ok = false;
    {
        std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
        advanceFeatureContextGeneration();
        ok = camera_.stop(&local);
    }
    clearFrameMailbox();
    if (!ok) {
        if (error) *error = local;
        emitError(local);
    }
    emit stateChanged(camera_.state());
    return ok;
}

CameraState QtAmcamCamera::state() const noexcept { return camera_.state(); }

std::uint64_t QtAmcamCamera::droppedFrames() const noexcept {
    return camera_.droppedFrames();
}

FramePtr QtAmcamCamera::takeLatestFrame() {
    std::lock_guard<std::mutex> lock(mailboxMutex_);
    FramePtr frame = std::move(latestFrame_);
    notificationPending_ = false;
    return frame;
}

bool QtAmcamCamera::autoWhiteBalance(CameraError* error) {
    CameraError local;
    bool ok = false;
    {
        std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
        advanceFeatureContextGeneration();
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

QtAmcamCamera::FeatureRequestId QtAmcamCamera::requestAutoWhiteBalance(
    AutoWhiteBalanceCallback callback) {
    if (!callback) return 0;

    // The command can change white-balance readbacks. Invalidate feature
    // requests queued before it, but let requests submitted after it execute
    // against the post-command camera state.
    std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
    return enqueueFeatureRequest(
        [this, callback = std::move(callback)](
            FeatureRequestId requestId,
            std::uint64_t contextGeneration) mutable {
            std::optional<AutoWhiteBalanceResult> result;
            {
                std::lock_guard<std::mutex> operationLock(cameraOperationMutex_);
                if (contextGeneration !=
                    featureContextGeneration_.load(std::memory_order_acquire)) {
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
                contextGeneration,
                [callback = std::move(callback),
                 requestId,
                 result = std::move(*result)]() mutable {
                    callback(requestId, std::move(result));
                });
        },
        true);
}

QtAmcamCamera::FeatureRequestId QtAmcamCamera::requestFrameRate(
    FrameRateCallback callback) {
    if (!callback) return 0;

    return enqueueFeatureRequest(
        [this, callback = std::move(callback)](
            FeatureRequestId requestId,
            std::uint64_t contextGeneration) mutable {
            std::optional<FrameRateResult> result;
            {
                std::lock_guard<std::mutex> operationLock(cameraOperationMutex_);
                if (contextGeneration !=
                    featureContextGeneration_.load(std::memory_order_acquire)) {
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
                contextGeneration,
                [callback = std::move(callback),
                 requestId,
                 result = std::move(*result)]() mutable {
                    callback(requestId, std::move(result));
                });
        });
}

QtAmcamCamera::FeatureRequestId QtAmcamCamera::requestFeatureDescriptors(
    FeatureDescriptorsCallback callback) {
    if (!callback) return 0;

    return enqueueFeatureRequest(
        [this, callback = std::move(callback)](
            FeatureRequestId requestId,
            std::uint64_t contextGeneration) mutable {
            std::optional<FeatureDescriptorsResult> result;
            {
                std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
                if (contextGeneration !=
                    featureContextGeneration_.load(std::memory_order_acquire)) {
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
                contextGeneration,
                [callback = std::move(callback),
                 requestId,
                 result = std::move(*result)]() mutable {
                    callback(requestId, std::move(result));
                });
        });
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
            std::uint64_t contextGeneration) mutable {
            std::optional<FeatureReadResult> result;
            {
                std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
                if (contextGeneration !=
                    featureContextGeneration_.load(std::memory_order_acquire)) {
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
                contextGeneration,
                [callback = std::move(callback),
                 requestId,
                 result = std::move(*result)]() mutable {
                    callback(requestId, std::move(result));
                });
        });
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

    // A queued write supersedes earlier reads/descriptors. Holding the
    // facade operation lock orders it against lifecycle work.
    std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
    return enqueueFeatureRequest(
        [this,
         featureName = std::move(featureName),
         value = std::move(value),
         callback = std::move(callback)](
            FeatureRequestId requestId,
            std::uint64_t contextGeneration) mutable {
            std::optional<FeatureWriteResult> result;
            {
                std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
                if (contextGeneration !=
                    featureContextGeneration_.load(std::memory_order_acquire)) {
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
                contextGeneration,
                [callback = std::move(callback),
                 requestId,
                 result = std::move(*result)]() mutable {
                    callback(requestId, std::move(result));
                });
        },
        true);
}

QtAmcamCamera::FeatureRequestId QtAmcamCamera::enqueueFeatureRequest(
    FeatureRequestTask task,
    bool invalidatePriorRequests) {
    if (!task) return 0;

    std::lock_guard<std::mutex> queueLock(featureQueueMutex_);
    if (stoppingFeatureWorker_ ||
        featureQueue_.size() >= kMaximumPendingFeatureRequests) {
        return 0;
    }

    FeatureRequestId requestId = nextFeatureRequestId_++;
    if (requestId == 0) {
        requestId = nextFeatureRequestId_++;
    }

    auto contextGeneration =
        featureContextGeneration_.load(std::memory_order_acquire);
    if (invalidatePriorRequests)
        ++contextGeneration;

    // Push before publishing the generation. If allocation fails, the old
    // context remains current and the caller can retry without invalidating
    // otherwise valid completions.
    featureQueue_.push_back({
        requestId,
        contextGeneration,
        std::move(task)
    });
    if (invalidatePriorRequests) {
        featureContextGeneration_.store(
            contextGeneration, std::memory_order_release);
    }

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

        if (request.contextGeneration !=
            featureContextGeneration_.load(std::memory_order_acquire)) {
            continue;
        }

        try {
            request.task(request.id, request.contextGeneration);
        } catch (const std::exception& exception) {
            qWarning() << "Unhandled camera feature worker exception:"
                       << exception.what();
        } catch (...) {
            qWarning() << "Unknown unhandled camera feature worker exception.";
        }
    }
}

void QtAmcamCamera::dispatchFeatureCallback(
    std::uint64_t contextGeneration,
    std::function<void()> callback) {
    dispatchToOwnerThread(
        [this,
         contextGeneration,
         callback = std::move(callback)]() mutable {
            if (contextGeneration !=
                featureContextGeneration_.load(std::memory_order_acquire)) {
                return;
            }
            callback();
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

void QtAmcamCamera::advanceFeatureContextGeneration() noexcept {
    featureContextGeneration_.fetch_add(1, std::memory_order_acq_rel);
}

void QtAmcamCamera::publishLatestFrame(FramePtr frame) {
    bool notify = false;
    {
        std::lock_guard<std::mutex> lock(mailboxMutex_);
        latestFrame_ = std::move(frame);
        if (!notificationPending_) {
            notificationPending_ = true;
            notify = true;
        }
    }

    if (notify) {
        // Queue the signal emission onto the QObject's thread. A connected
        // renderer on another thread should use Qt::QueuedConnection too.
        dispatchToOwnerThread([this]() {
            emit frameAvailable();
        });
    }
}

void QtAmcamCamera::clearFrameMailbox() {
    std::lock_guard<std::mutex> lock(mailboxMutex_);
    latestFrame_.reset();
    notificationPending_ = false;
}

void QtAmcamCamera::emitError(const CameraError& error) {
    emit errorOccurred(QString::fromStdString(error.message),
                       static_cast<qint32>(error.vendorCode));
}

QtAmcamCamera::FeatureDescriptorsResult QtAmcamCamera::featureDescriptors() {
    std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
    return camera_.featureDescriptors();
}

QtAmcamCamera::FeatureReadResult QtAmcamCamera::readFeature(std::string_view name) {
    std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
    return camera_.readFeature(name);
}

QtAmcamCamera::FeatureWriteResult QtAmcamCamera::writeFeature(
    std::string_view name,
    const camera::CameraFeatureValue& value) {
    std::lock_guard<std::mutex> cameraLock(cameraOperationMutex_);
    advanceFeatureContextGeneration();
    return camera_.writeFeature(name, value);
}

} // namespace amscope
