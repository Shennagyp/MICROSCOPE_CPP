#pragma once

#include "amscope/AmcamCamera.h"

#include <QMetaType>
#include <QObject>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <variant>

Q_DECLARE_METATYPE(amscope::CameraState)
Q_DECLARE_METATYPE(amscope::FramePtr)

namespace amscope {

// Qt Core bridge for Widgets, Qt Quick, or a dedicated render-thread object.
// Frame notification is coalesced; retrieve the current immutable frame lease
// by calling takeLatestFrame() on the consumer thread.
//
// The synchronous ICameraFeatureAccess methods are available for non-UI
// callers. GUI code should use the request* methods below: feature operations
// execute serially on a dedicated worker and their callbacks are delivered on
// this object's Qt affinity thread. A lifecycle transition, a queued state-changing
// write, or a command may supersede prior requests; superseded callbacks are
// intentionally omitted. The application should also keep its own selected-device
// generation, as in the Swift model, to reject stale UI work across selection changes.
class QtAmcamCamera final : public QObject, public camera::ICameraFeatureAccess {
    Q_OBJECT
public:
    using FeatureRequestId = std::uint64_t;
    using FeatureDescriptors = std::vector<camera::CameraFeatureDescriptor>;
    using FeatureDescriptorsResult = camera::CameraFeatureResult<FeatureDescriptors>;
    using FeatureReadResult = camera::CameraFeatureResult<camera::CameraFeatureValue>;
    using FeatureWriteResult = camera::CameraFeatureResult<std::monostate>;

    using FeatureDescriptorsCallback =
        std::function<void(FeatureRequestId, FeatureDescriptorsResult)>;
    using FeatureReadCallback =
        std::function<void(FeatureRequestId, FeatureReadResult)>;
    using FeatureWriteCallback =
        std::function<void(FeatureRequestId, FeatureWriteResult)>;

    // Commands and telemetry are not camera settings. They share the serialized
    // request worker but retain their own result semantics and public methods.
    template <typename T>
    using CameraOperationResult = std::variant<T, CameraError>;
    using AutoWhiteBalanceResult = CameraOperationResult<std::monostate>;
    using FrameRateResult = CameraOperationResult<FrameRateStatistics>;
    using AutoWhiteBalanceCallback =
        std::function<void(FeatureRequestId, AutoWhiteBalanceResult)>;
    using FrameRateCallback =
        std::function<void(FeatureRequestId, FrameRateResult)>;

    explicit QtAmcamCamera(QObject* parent = nullptr);
    QtAmcamCamera(AmcamCameraProfileRegistry profiles, QObject* parent = nullptr);
    ~QtAmcamCamera() override;

    QtAmcamCamera(const QtAmcamCamera&) = delete;
    QtAmcamCamera& operator=(const QtAmcamCamera&) = delete;

    [[nodiscard]] static std::vector<CameraInfo> enumerate(CameraError* error = nullptr);
    bool open(const QString& deviceId, CameraError* error = nullptr);
    bool close(CameraError* error = nullptr);
    bool start(CameraError* error = nullptr);
    bool stop(CameraError* error = nullptr);

    [[nodiscard]] CameraState state() const noexcept;
    [[nodiscard]] std::uint64_t droppedFrames() const noexcept;
    [[nodiscard]] FramePtr takeLatestFrame();

    // Synchronous command/telemetry entry points are for non-GUI consumers.
    // GUI callers should use the asynchronous request methods below.
    bool autoWhiteBalance(CameraError* error = nullptr);
    bool readFrameRate(FrameRateStatistics& stats, CameraError* error = nullptr);
    FeatureRequestId requestAutoWhiteBalance(AutoWhiteBalanceCallback callback);
    FeatureRequestId requestFrameRate(FrameRateCallback callback);

    // Asynchronous feature access for GUI/application code. A zero request ID
    // means the request was rejected (empty callback or executor shutting down).
    // Completed callbacks run on this QObject's affinity thread. Callbacks from
    // a superseded feature context are intentionally discarded.
    FeatureRequestId requestFeatureDescriptors(FeatureDescriptorsCallback callback);
    FeatureRequestId requestReadFeature(QString name, FeatureReadCallback callback);
    FeatureRequestId requestWriteFeature(
        QString name,
        camera::CameraFeatureValue value,
        FeatureWriteCallback callback);

    // Synchronous backend-neutral contract. These methods must not be called
    // from a GUI thread for potentially blocking feature providers.
    FeatureDescriptorsResult featureDescriptors() override;
    FeatureReadResult readFeature(std::string_view name) override;
    FeatureWriteResult writeFeature(
        std::string_view name,
        const camera::CameraFeatureValue& value) override;

signals:
    void frameAvailable();
    void stateChanged(amscope::CameraState state);
    void errorOccurred(const QString& message, qint32 vendorCode);

private:
    using FeatureRequestTask =
        std::function<void(FeatureRequestId, std::uint64_t)>;

    struct PendingFeatureRequest {
        FeatureRequestId id = 0;
        std::uint64_t contextGeneration = 0;
        FeatureRequestTask task;
    };

    FeatureRequestId enqueueFeatureRequest(
        FeatureRequestTask task,
        bool invalidatePriorRequests = false);
    void runFeatureWorker();
    void dispatchFeatureCallback(std::uint64_t contextGeneration,
                                 std::function<void()> callback);
    void dispatchToOwnerThread(std::function<void()> callback);
    void advanceFeatureContextGeneration() noexcept;

    void publishLatestFrame(FramePtr frame);
    void clearFrameMailbox();
    void emitError(const CameraError& error);

    static constexpr std::size_t kMaximumPendingFeatureRequests = 256;

    AmcamCamera camera_;

    // All calls into camera_ are serialized at this facade boundary. The
    // AmcamCamera also protects its SDK handle internally; this mutex adds an
    // atomic lifecycle-vs-queued-request ordering boundary.
    std::mutex cameraOperationMutex_;
    std::atomic<std::uint64_t> featureContextGeneration_{0};

    std::mutex featureQueueMutex_;
    std::condition_variable featureQueueCondition_;
    std::deque<PendingFeatureRequest> featureQueue_;
    std::thread featureWorker_;
    FeatureRequestId nextFeatureRequestId_ = 1;
    bool stoppingFeatureWorker_ = false;

    std::mutex mailboxMutex_;
    FramePtr latestFrame_;
    bool notificationPending_ = false;
};
} // namespace amscope
