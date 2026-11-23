#pragma once

#include "amscope/AmcamCamera.h"
#include "amscope/detail/AcquisitionFrameGate.h"
#include "camera/qt/FrameStreamMailbox.h"

#include <QMetaType>
#include <QObject>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <variant>

Q_DECLARE_METATYPE(amscope::CameraState)
Q_DECLARE_METATYPE(amscope::FramePtr)

namespace amscope {

// AmScope-specific Qt operation facade. AmcamCamera remains the sole native
// session owner; frame mailbox mechanics are shared through Camera::Qt.
//
// The synchronous ICameraFeatureAccess and lifecycle methods are available
// for direct non-GUI consumers. The first accepted operation commits this
// instance for its lifetime to synchronous or queued execution; the opposite
// surface is rejected.
// GUI code should use the request* methods below: operations execute serially
// on one dedicated worker, and completions are delivered on this object's Qt
// affinity thread. Lifecycle transitions invalidate work from the previous
// camera session. State-changing writes/commands invalidate earlier
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
    using TriggerConfigurationResult =
        CameraOperationResult<AmcamTriggerConfiguration>;
    using IoLineList = std::vector<AmcamIoLineState>;
    using IoLineListResult = CameraOperationResult<IoLineList>;
    using StillCaptureResult = CameraOperationResult<FramePtr>;
    using StillResolutionList = std::vector<StillResolution>;
    using StillResolutionListResult = CameraOperationResult<StillResolutionList>;
    using EmbeddedLightSourceResult = CameraOperationResult<EmbeddedLightSourceState>;
    using FilterWheelStateResult = CameraOperationResult<FilterWheelState>;
    using ST4GuideStateResult = CameraOperationResult<ST4GuideState>;
    using AstroAutoFocuserResult = CameraOperationResult<AstroAutoFocuserState>;
    using LensFocusStateResult = CameraOperationResult<LensFocusState>;
    using AccessoryCommandResult = CameraOperationResult<std::monostate>;
    using EmbeddedLightSourceCallback =
        std::function<void(FeatureRequestId, EmbeddedLightSourceResult)>;
    using FilterWheelStateCallback =
        std::function<void(FeatureRequestId, FilterWheelStateResult)>;
    using ST4GuideStateCallback =
        std::function<void(FeatureRequestId, ST4GuideStateResult)>;
    using AstroAutoFocuserCallback =
        std::function<void(FeatureRequestId, AstroAutoFocuserResult)>;
    using LensFocusStateCallback =
        std::function<void(FeatureRequestId, LensFocusStateResult)>;
    using AccessoryCommandCallback =
        std::function<void(FeatureRequestId, AccessoryCommandResult)>;
    using AutoWhiteBalanceCallback =
        std::function<void(FeatureRequestId, AutoWhiteBalanceResult)>;
    using FrameRateCallback =
        std::function<void(FeatureRequestId, FrameRateResult)>;
    using TriggerConfigurationCallback =
        std::function<void(FeatureRequestId, TriggerConfigurationResult)>;
    using IoLineListCallback =
        std::function<void(FeatureRequestId, IoLineListResult)>;
    using StillCaptureCallback =
        std::function<void(FeatureRequestId, StillCaptureResult)>;
    using StillResolutionListCallback =
        std::function<void(FeatureRequestId, StillResolutionListResult)>;
    using LifecycleResult = CameraOperationResult<std::monostate>;
    using LifecycleCallback =
        std::function<void(CameraRequestId, LifecycleResult)>;
    using CameraListResult = CameraOperationResult<std::vector<CameraInfo>>;
    using CameraListCallback =
        std::function<void(CameraRequestId, CameraListResult)>;

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
    CameraRequestId requestEnumerateCameras(CameraListCallback callback);
    CameraRequestId requestOpen(QString deviceId, LifecycleCallback callback);
    CameraRequestId requestClose(LifecycleCallback callback);
    CameraRequestId requestStart(LifecycleCallback callback);
    CameraRequestId requestStop(LifecycleCallback callback);

    [[nodiscard]] CameraState state() const noexcept;
    [[nodiscard]] std::uint64_t droppedFrames() const noexcept;
    [[nodiscard]] std::uint64_t framesPulled() const noexcept;
    [[nodiscard]] std::uint64_t imageEventsReceived() const noexcept;
    [[nodiscard]] FramePtr takeLatestFrame();

    // Synchronous command/telemetry entry points are for non-GUI consumers.
    // GUI callers should use the asynchronous request methods below.
    bool autoWhiteBalance(CameraError* error = nullptr);
    bool readFrameRate(FrameRateStatistics& stats, CameraError* error = nullptr);
    bool readTriggerConfiguration(
        AmcamTriggerConfiguration& configuration, CameraError* error = nullptr);
    bool readIoLines(IoLineList& lines, CameraError* error = nullptr);
    bool configureIoLine(
        const AmcamIoLineConfiguration& configuration,
        CameraError* error = nullptr);
    bool configureTrigger(
        const AmcamTriggerConfiguration& configuration,
        CameraError* error = nullptr);
    bool sendSoftwareTrigger(std::uint16_t imageCount = 1,
                             CameraError* error = nullptr);
    bool captureStill(const StillCaptureRequest& request,
                      FramePtr& frame,
                      CameraError* error = nullptr);
    bool readStillResolutions(StillResolutionList& resolutions,
                              CameraError* error = nullptr);
    bool readEmbeddedLightSource(
        EmbeddedLightSourceState& state, CameraError* error = nullptr);
    bool setEmbeddedLightSourceLevel(
        std::uint32_t level, CameraError* error = nullptr);
    bool readFilterWheelState(
        FilterWheelState& state, CameraError* error = nullptr);
    bool moveFilterWheel(
        std::uint32_t position,
        FilterWheelDirection direction = FilterWheelDirection::Automatic,
        CameraError* error = nullptr);
    bool st4Guide(
        ST4GuideDirection direction, std::uint32_t durationMilliseconds,
        CameraError* error = nullptr);
    bool readST4GuideState(ST4GuideState& state, CameraError* error = nullptr);
    bool readAstroAutoFocuserState(
        AstroAutoFocuserState& state, CameraError* error = nullptr);
    bool moveAstroAutoFocuser(
        std::int32_t targetPosition, CameraError* error = nullptr);
    bool haltAstroAutoFocuser(CameraError* error = nullptr);
    bool readLensFocusState(LensFocusState& state, CameraError* error = nullptr);
    bool setManualLensFocusPosition(
        std::int32_t position, CameraError* error = nullptr);
    bool setLensAperturePosition(
        std::uint32_t positionIndex, CameraError* error = nullptr);
    FeatureRequestId requestLensFocusState(LensFocusStateCallback callback);
    FeatureRequestId requestSetManualLensFocusPosition(
        std::int32_t position, AccessoryCommandCallback callback);
    FeatureRequestId requestSetLensAperturePosition(
        std::uint32_t positionIndex, AccessoryCommandCallback callback);
    FeatureRequestId requestAstroAutoFocuserState(
        AstroAutoFocuserCallback callback);
    FeatureRequestId requestMoveAstroAutoFocuser(
        std::int32_t targetPosition, AccessoryCommandCallback callback);
    FeatureRequestId requestHaltAstroAutoFocuser(
        AccessoryCommandCallback callback);
    FeatureRequestId requestST4Guide(
        ST4GuideDirection direction, std::uint32_t durationMilliseconds,
        AccessoryCommandCallback callback);
    FeatureRequestId requestST4GuideState(ST4GuideStateCallback callback);
    FeatureRequestId requestEmbeddedLightSource(
        EmbeddedLightSourceCallback callback);
    FeatureRequestId requestSetEmbeddedLightSourceLevel(
        std::uint32_t level, AccessoryCommandCallback callback);
    FeatureRequestId requestFilterWheelState(FilterWheelStateCallback callback);
    FeatureRequestId requestMoveFilterWheel(
        std::uint32_t position,
        FilterWheelDirection direction,
        AccessoryCommandCallback callback);
    FeatureRequestId requestStillResolutions(StillResolutionListCallback callback);
    FeatureRequestId requestAutoWhiteBalance(AutoWhiteBalanceCallback callback);
    FeatureRequestId requestFrameRate(FrameRateCallback callback);
    FeatureRequestId requestTriggerConfiguration(
        TriggerConfigurationCallback callback);
    FeatureRequestId requestConfigureTrigger(
        AmcamTriggerConfiguration configuration,
        AccessoryCommandCallback callback);
    FeatureRequestId requestSoftwareTrigger(
        std::uint16_t imageCount,
        AccessoryCommandCallback callback);
    FeatureRequestId requestIoLines(IoLineListCallback callback);
    FeatureRequestId requestConfigureIoLine(
        AmcamIoLineConfiguration configuration,
        AccessoryCommandCallback callback);
    FeatureRequestId requestStillCapture(
        StillCaptureRequest request,
        StillCaptureCallback callback);

    // Asynchronous camera operations for GUI/application code. A zero request
    // ID means rejection (empty callback, full queue, shutdown, or this instance
    // already committed to synchronous execution).
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
    enum class ExecutionMode : std::uint8_t {
        Undecided,
        Synchronous,
        Queued
    };

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
        FeatureRequestSemantics semantics = FeatureRequestSemantics::SessionBound,
        bool reservedControlRequest = false);
    [[nodiscard]] bool selectExecutionMode(ExecutionMode mode) noexcept;
    bool requireSynchronousExecutionMode(const char* operation, CameraError* error);
    [[nodiscard]] camera::CameraFeatureAccessError synchronousModeFeatureError(
        const std::string& feature,
        const char* operation) const;
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
    void handleAcquisitionFailure(
        const std::shared_ptr<detail::AcquisitionFrameGate>& frameGate,
        const CameraError& error);

    void publishLatestFrame(
        FramePtr frame,
        const std::shared_ptr<detail::AcquisitionFrameGate>& frameGate);
    void queueFrameNotification();
    void clearFrameMailbox();
    void emitError(const CameraError& error);

    static constexpr std::size_t kMaximumPendingFeatureRequests = 256;

    AmcamCamera camera_;
    std::shared_ptr<detail::AcquisitionFrameGate> acquisitionFrameGate_;

    // All calls into camera_ are serialized at this facade boundary. The
    // AmcamCamera also protects its SDK handle internally; this mutex adds an
    // atomic lifecycle-vs-queued-request ordering boundary.
    std::mutex cameraOperationMutex_;
    std::atomic<ExecutionMode> executionMode_{ExecutionMode::Undecided};
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

    // Preserve the original mailbox-at-tail placement in this facade.
    camera::qt::FrameStreamMailbox frameStreamMailbox_;

};
} // namespace amscope
