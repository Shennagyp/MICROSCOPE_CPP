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
// The synchronous ICameraFeatureAccess and lifecycle methods are available
// for direct non-GUI consumers. A caller should use either the synchronous
// surface or queued requests for one instance, not interleave both concurrently.
// GUI code should use the request* methods below: operations execute serially on
// this object's Qt affinity thread. Lifecycle transitions invalidate work from
// the previous camera session. State-changing writes/commands invalidate earlier
// descriptor/read snapshots, but accepted mutations still execute in FIFO order
// and report their own results. The application should also keep its own selected-
// device generation, as in the Swift model, to reject stale UI work across selection changes.
class QtAmcamCamera final : public QObject, public camera::ICameraFeatureAccess {
    Q_OBJECT
public:
    using CameraRequestId = std::uint64_t;
    using FeatureRequestId = CameraRequestId;
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
    using LifecycleResult = CameraOperationResult<std::monostate>;
    using LifecycleCallback =
        std::function<void(CameraRequestId, LifecycleResult)>;

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

    // GUI lifecycle operations share the same bounded FIFO as feature I/O.
    // Accepted lifecycle requests execute in order and report their own result;
    // the application applies its own selection/request generation to ignore
    // obsolete UI completions.
    CameraRequestId requestOpen(QString deviceId, LifecycleCallback callback);
    CameraRequestId requestClose(LifecycleCallback callback);
    CameraRequestId requestStart(LifecycleCallback callback);
    CameraRequestId requestStop(LifecycleCallback callback);

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
    // Callbacks run on this QObject's affinity thread. Lifecycle changes cancel
    // requests from old sessions; feature-state changes cancel only older reads
    // and descriptor snapshots. Accepted writes/commands remain FIFO and report
    // their result unless a lifecycle transition invalidates their session.
    FeatureRequestId requestFeatureDescriptors(FeatureDescriptorsCallback callback);
    FeatureRequestId requestReadFeature(QString name, FeatureReadCallback callback);
    FeatureRequestId requestWriteFeature(
        QString name,
        camera::CameraFeatureValue value,
        FeatureWriteCallback callback);

    // Synchronous backend-neutral methods are for a direct, non-GUI consumer.
    // Use either this synchronous surface or the queued request surface for one
    // instance; do not interleave them concurrently.
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
    enum class FeatureRequestSemantics : std::uint8_t {
        SessionBound,
        FeatureSnapshot,
        StateMutation,
        LifecycleMutation
    };

    struct FeatureRequestContext {
        std::uint64_t sessionGeneration = 0;
        std::uint64_t featureStateGeneration = 0;
        FeatureRequestSemantics semantics = FeatureRequestSemantics::SessionBound;
    };

    using FeatureRequestTask =
        std::function<void(FeatureRequestId, const FeatureRequestContext&)>;

    struct PendingFeatureRequest {
        FeatureRequestId id = 0;
        FeatureRequestContext context;
        FeatureRequestTask task;
    };

    CameraRequestId enqueueFeatureRequest(
        FeatureRequestTask task,
        FeatureRequestSemantics semantics = FeatureRequestSemantics::SessionBound);
    void runFeatureWorker();
    void dispatchFeatureCallback(const FeatureRequestContext& context,
                                 std::function<void()> callback);
    void dispatchLifecycleCallback(CameraRequestId requestId,
                                   LifecycleCallback callback,
                                   LifecycleResult result);
    void dispatchToOwnerThread(std::function<void()> callback);
    [[nodiscard]] bool isRequestContextCurrent(
        const FeatureRequestContext& context) const noexcept;
    void advanceSessionGeneration() noexcept;
    void advanceSessionGenerationTo(std::uint64_t generation) noexcept;
    void advanceFeatureStateGeneration() noexcept;

    void publishLatestFrame(FramePtr frame);
    void clearFrameMailbox();
    void emitError(const CameraError& error);

    static constexpr std::size_t kMaximumPendingFeatureRequests = 256;

    AmcamCamera camera_;

    // All calls into camera_ are serialized at this facade boundary. The
    // AmcamCamera also protects its SDK handle internally; this mutex adds an
    // atomic lifecycle-vs-queued-request ordering boundary.
    std::mutex cameraOperationMutex_;
    // Scheduled generation advances when a lifecycle request is accepted;
    // active generation advances only when that request reaches the worker head.
    std::atomic<std::uint64_t> scheduledSessionGeneration_{0};
    std::atomic<std::uint64_t> sessionGeneration_{0};
    std::atomic<std::uint64_t> featureStateGeneration_{0};

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
