#pragma once
#include "microscope/core/equipment/StageSpecifications.h"
#include "microscope/core/runtime/Feedback.h"
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace microscope::core {
enum class MotionValueError { NonFiniteValue, UnsupportedCoordinateUnit };

class AxisPosition final {
public:
    static Result<AxisPosition, MotionValueError> create(StageAxisId, double, AxisCoordinateUnit);
    const StageAxisId& axisId() const noexcept;
    double value() const noexcept;
    AxisCoordinateUnit unit() const noexcept;
private:
    AxisPosition(StageAxisId, double, AxisCoordinateUnit);
    StageAxisId axisId_; double value_; AxisCoordinateUnit unit_;
};
enum class StagePositionEvidence {
    Unknown,
    CommandDerivedEstimate,
    ControllerReported,
    EncoderMeasured,
    ExternalMeasured
};

enum class AxisPositionObservationError { InvalidUncertainty, InvalidEvidence };
class AxisPositionObservation final {
public:
    static Result<AxisPositionObservation,AxisPositionObservationError> create(
        AxisPosition position,double uncertaintyInAxisUnit,StagePositionEvidence evidence);
    const AxisPosition& position() const noexcept;
    double uncertaintyInAxisUnit() const noexcept;
    StagePositionEvidence evidence() const noexcept;
private:
    AxisPositionObservation(AxisPosition,double,StagePositionEvidence);
    AxisPosition position_;
    double uncertaintyInAxisUnit_;
    StagePositionEvidence evidence_;
};

class AxisDisplacement final {
public:
    static Result<AxisDisplacement, MotionValueError> create(StageAxisId, double, AxisCoordinateUnit);
    const StageAxisId& axisId() const noexcept;
    double value() const noexcept;
    AxisCoordinateUnit unit() const noexcept;
private:
    AxisDisplacement(StageAxisId, double, AxisCoordinateUnit);
    StageAxisId axisId_; double value_; AxisCoordinateUnit unit_;
};

enum class StageAxisSetError { Empty, DuplicateAxisId };
class StageAxisSet final {
public:
    static Result<StageAxisSet, StageAxisSetError> create(std::vector<StageAxisId>);
    const std::vector<StageAxisId>& axes() const noexcept;
private:
    explicit StageAxisSet(std::vector<StageAxisId>);
    std::vector<StageAxisId> axes_;
};

enum class StageMotionCommandError { EmptyAxisValues, DuplicateAxisId };
class MoveAxesTo final {
public:
    static Result<MoveAxesTo, StageMotionCommandError> create(std::vector<AxisPosition>);
    const std::vector<AxisPosition>& targets() const noexcept;
private:
    explicit MoveAxesTo(std::vector<AxisPosition>);
    std::vector<AxisPosition> targets_;
};
class MoveAxesBy final {
public:
    static Result<MoveAxesBy, StageMotionCommandError> create(std::vector<AxisDisplacement>);
    const std::vector<AxisDisplacement>& displacements() const noexcept;
private:
    explicit MoveAxesBy(std::vector<AxisDisplacement>);
    std::vector<AxisDisplacement> displacements_;
};
class HomeAxes final {
public:
    static HomeAxes create(StageAxisSet);
    const StageAxisSet& axes() const noexcept;
private:
    explicit HomeAxes(StageAxisSet);
    StageAxisSet axes_;
};
class StopAxes final {
public:
    static StopAxes create(StageAxisSet);
    const StageAxisSet& axes() const noexcept;
private:
    explicit StopAxes(StageAxisSet);
    StageAxisSet axes_;
};

enum class StageMotionCommandContextError { MissingCommandId };
class StageMotionCommandContext final {
public:
    static Result<StageMotionCommandContext, StageMotionCommandContextError> create(
        RuntimeDeviceCorrelation, OperationCorrelation);
    const RuntimeDeviceCorrelation& runtimeDevice() const noexcept;
    const OperationCorrelation& operation() const noexcept;
private:
    StageMotionCommandContext(RuntimeDeviceCorrelation, OperationCorrelation);
    RuntimeDeviceCorrelation runtimeDevice_;
    OperationCorrelation operation_;
};

enum class StageMotionSubmissionStatus { Accepted, Rejected };
enum class StageMotionSubmissionRejection {
    DeviceNotReady, ResourceBusy, UnsupportedOperation, InvalidTarget,
    DeviceFaulted, TransportUnavailable, Other
};
class StageMotionSubmissionResult final {
public:
    static StageMotionSubmissionResult accepted() noexcept;
    static StageMotionSubmissionResult rejected(StageMotionSubmissionRejection) noexcept;
    StageMotionSubmissionStatus status() const noexcept;
    const std::optional<StageMotionSubmissionRejection>& rejection() const noexcept;
private:
    StageMotionSubmissionResult(StageMotionSubmissionStatus,
        std::optional<StageMotionSubmissionRejection>);
    StageMotionSubmissionStatus status_;
    std::optional<StageMotionSubmissionRejection> rejection_;
};

enum class StageAxisMotionState { Idle, Moving, Homing, Stopping, Faulted, Unknown };
enum class AxisLimitEnd { Negative, Positive };
enum class StageMotionObservationError { InvalidMotionState, InvalidLimitEnd };
class AxisMotionStateObservation final {
public:
    static Result<AxisMotionStateObservation, StageMotionObservationError> create(
        StageAxisId, StageAxisMotionState);
    const StageAxisId& axisId() const noexcept;
    StageAxisMotionState state() const noexcept;
private:
    AxisMotionStateObservation(StageAxisId, StageAxisMotionState);
    StageAxisId axisId_; StageAxisMotionState state_;
};
class AxisLimitObservation final {
public:
    static Result<AxisLimitObservation, StageMotionObservationError> create(
        StageAxisId, AxisLimitEnd, bool);
    const StageAxisId& axisId() const noexcept;
    AxisLimitEnd end() const noexcept;
    bool active() const noexcept;
private:
    AxisLimitObservation(StageAxisId, AxisLimitEnd, bool);
    StageAxisId axisId_; AxisLimitEnd end_; bool active_;
};

enum class StageMotionAction { MoveAxesTo, MoveAxesBy, HomeAxes, StopAxes };
enum class StageMotionAcknowledgementError { InvalidAction };
class StageMotionAcknowledgement final {
public:
    static Result<StageMotionAcknowledgement,StageMotionAcknowledgementError> create(
        StageMotionAction,StageAxisSet);
    StageMotionAction action() const noexcept;
    const StageAxisSet& axes() const noexcept;
private:
    StageMotionAcknowledgement(StageMotionAction,StageAxisSet);
    StageMotionAction action_;
    StageAxisSet axes_;
};

enum class StageMotionActionCompletionError { InvalidAction };
class StageMotionActionCompletion final {
public:
    static Result<StageMotionActionCompletion, StageMotionActionCompletionError> create(
        StageMotionAction, StageAxisSet);
    StageMotionAction action() const noexcept;
    const StageAxisSet& axes() const noexcept;
private:
    StageMotionActionCompletion(StageMotionAction, StageAxisSet);
    StageMotionAction action_; StageAxisSet axes_;
};

enum class StageHomeReferenceObservationError {
    EmptyPositions,
    DuplicateAxisId
};

// A normalized fact emitted only after the adapter's configured reference
// procedure has established the machine reference for every listed axis.
// The fact carries coordinates at the established references but prescribes
// no switch, hard-stop, encoder-index or controller-specific homing method.
class StageHomeReferenceObservation final {
public:
    static Result<StageHomeReferenceObservation,StageHomeReferenceObservationError> create(
        std::vector<AxisPositionObservation> positions) {
        if (positions.empty()) {
            return Result<StageHomeReferenceObservation,StageHomeReferenceObservationError>::failure(
                StageHomeReferenceObservationError::EmptyPositions);
        }
        for (std::size_t i = 0; i < positions.size(); ++i) {
            for (std::size_t j = i + 1; j < positions.size(); ++j) {
                if (positions[i].position().axisId() == positions[j].position().axisId()) {
                    return Result<StageHomeReferenceObservation,StageHomeReferenceObservationError>::failure(
                        StageHomeReferenceObservationError::DuplicateAxisId);
                }
            }
        }
        return Result<StageHomeReferenceObservation,StageHomeReferenceObservationError>::success(
            StageHomeReferenceObservation(std::move(positions)));
    }

    const std::vector<AxisPositionObservation>& positions() const noexcept {
        return positions_;
    }

    // Host verifies that the complete evidence set matches its HomeAxes request.
    bool matchesAxes(const StageAxisSet& expected) const noexcept {
        if (positions_.size() != expected.axes().size()) {
            return false;
        }
        for (const auto& position : positions_) {
            bool found = false;
            for (const auto& axis : expected.axes()) {
                if (position.position().axisId() == axis) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return false;
            }
        }
        return true;
    }

private:
    explicit StageHomeReferenceObservation(
        std::vector<AxisPositionObservation> positions)
        : positions_(std::move(positions)) {}

    std::vector<AxisPositionObservation> positions_;
};

enum class StageMotionFaultKind {
    LimitReached, FollowingError, ControllerFault, CommunicationLost, EmergencyStop, Unknown
};
enum class StageMotionFaultError { InvalidFaultKind };
class StageMotionFaultObservation final {
public:
    static Result<StageMotionFaultObservation, StageMotionFaultError> create(
        StageMotionFaultKind, std::optional<StageAxisId> = std::nullopt);
    StageMotionFaultKind kind() const noexcept;
    const std::optional<StageAxisId>& axisId() const noexcept;
private:
    StageMotionFaultObservation(StageMotionFaultKind, std::optional<StageAxisId>);
    StageMotionFaultKind kind_;
    std::optional<StageAxisId> axisId_;
};

using StageMotionFeedbackPayload = std::variant<
    AxisPositionObservation, AxisMotionStateObservation, AxisLimitObservation,
    StageMotionAcknowledgement, StageMotionActionCompletion,
    StageHomeReferenceObservation, StageMotionFaultObservation>;

// Driver libraries implement this port. STEP/DIR drivers own calibrated
// physical-unit-to-step conversion and pulse generation. Intelligent-controller
// drivers send engineering coordinates and delegate axis scaling/trajectory
// execution to the controller.
class IStageMotionPort {
public:
    virtual ~IStageMotionPort() = default;
    virtual StageMotionSubmissionResult moveAxesTo(
        const StageMotionCommandContext&, const MoveAxesTo&,
        IFeedbackIngress<StageMotionFeedbackPayload>&) = 0;
    virtual StageMotionSubmissionResult moveAxesBy(
        const StageMotionCommandContext&, const MoveAxesBy&,
        IFeedbackIngress<StageMotionFeedbackPayload>&) = 0;
    virtual StageMotionSubmissionResult homeAxes(
        const StageMotionCommandContext&, const HomeAxes&,
        IFeedbackIngress<StageMotionFeedbackPayload>&) = 0;
    // Controlled stop only; not a safety-rated emergency-stop path.
    virtual StageMotionSubmissionResult stopAxes(
        const StageMotionCommandContext&, const StopAxes&,
        IFeedbackIngress<StageMotionFeedbackPayload>&) = 0;
};
} // namespace microscope::core
