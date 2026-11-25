#pragma once

#include "microscope/core/ids/Ids.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace microscope::core {

// The fact category is independent from how the adapter obtained it.
// CommandDispatched and TimeoutElapsed are Core runtime facts, not adapter
// feedback kinds.
enum class FeedbackKind {
    DeviceAcknowledged,
    DeviceProgress,
    DeviceActionCompleted,
    DeviceStateObserved,
    AdapterFailure
};

enum class ObservationSource {
    HardwareReadback,
    DeviceEvent,
    SdkStatus,
    OperatorConfirmation,
    CalibratedEstimate,
    AdapterReport,
    Unknown
};

enum class DeviceTimestampError {
    ZeroTickRate,
    EmptyClockDomain
};

// A device timestamp is constructible only with an explicit positive tick
// rate. Absence of a timestamp is represented by nullopt.
class DeviceTimestamp final {
public:
    [[nodiscard]] static Result<DeviceTimestamp, DeviceTimestampError> create(
        std::uint64_t ticks,
        std::uint64_t ticksPerSecond,
        std::optional<std::string> clockDomain = std::nullopt) {
        if (ticksPerSecond == 0) {
            return Result<DeviceTimestamp, DeviceTimestampError>::failure(
                DeviceTimestampError::ZeroTickRate);
        }
        if (clockDomain && clockDomain->empty()) {
            return Result<DeviceTimestamp, DeviceTimestampError>::failure(
                DeviceTimestampError::EmptyClockDomain);
        }

        return Result<DeviceTimestamp, DeviceTimestampError>::success(
            DeviceTimestamp(
                ticks,
                ticksPerSecond,
                std::move(clockDomain)));
    }

    [[nodiscard]] std::uint64_t ticks() const noexcept {
        return ticks_;
    }

    [[nodiscard]] std::uint64_t ticksPerSecond() const noexcept {
        return ticksPerSecond_;
    }

    [[nodiscard]] const std::optional<std::string>& clockDomain() const noexcept {
        return clockDomain_;
    }

private:
    DeviceTimestamp(
        std::uint64_t ticks,
        std::uint64_t ticksPerSecond,
        std::optional<std::string> clockDomain)
        : ticks_(ticks),
          ticksPerSecond_(ticksPerSecond),
          clockDomain_(std::move(clockDomain)) {}

    std::uint64_t ticks_;
    std::uint64_t ticksPerSecond_;
    std::optional<std::string> clockDomain_;
};

struct RuntimeDeviceCorrelation final {
    MicroscopeId microscopeId;
    RuntimeId runtimeId;
    RuntimeGeneration runtimeGeneration;
    DeviceSessionGeneration deviceSessionGeneration;
    DeviceId deviceId;
};

struct OperationCorrelation final {
    OperationId operationId;
    StepId stepId;
    std::optional<CommandId> commandId;
};

// Operation correlation is absent for unsolicited device telemetry. When
// present, OperationId and StepId are both required and cannot be half-set.
struct FeedbackCorrelation final {
    RuntimeDeviceCorrelation runtimeDevice;
    std::optional<OperationCorrelation> operation;
};

enum class FeedbackMetadataError {
    InvalidFactKind,
    InvalidObservationSource,
    MissingOperationCorrelation,
    MissingCommandCorrelation,
    InvalidAdapterFailureSource
};

// Metadata is created through a validator so fact categories cannot omit
// correlation that their semantics require.
class FeedbackMetadata final {
public:
    [[nodiscard]] static Result<FeedbackMetadata, FeedbackMetadataError> create(
        FeedbackCorrelation correlation,
        FeedbackKind kind,
        ObservationSource source = ObservationSource::Unknown,
        std::optional<std::uint64_t> sourceSequence = std::nullopt,
        std::optional<DeviceTimestamp> deviceTimestamp = std::nullopt) {
        switch (kind) {
        case FeedbackKind::DeviceAcknowledged:
        case FeedbackKind::DeviceProgress:
        case FeedbackKind::DeviceActionCompleted:
        case FeedbackKind::DeviceStateObserved:
        case FeedbackKind::AdapterFailure:
            break;
        default:
            return Result<FeedbackMetadata, FeedbackMetadataError>::failure(
                FeedbackMetadataError::InvalidFactKind);
        }

        switch (source) {
        case ObservationSource::HardwareReadback:
        case ObservationSource::DeviceEvent:
        case ObservationSource::SdkStatus:
        case ObservationSource::OperatorConfirmation:
        case ObservationSource::CalibratedEstimate:
        case ObservationSource::AdapterReport:
        case ObservationSource::Unknown:
            break;
        default:
            return Result<FeedbackMetadata, FeedbackMetadataError>::failure(
                FeedbackMetadataError::InvalidObservationSource);
        }

        const bool requiresOperation =
            kind == FeedbackKind::DeviceAcknowledged ||
            kind == FeedbackKind::DeviceActionCompleted;

        if (requiresOperation && !correlation.operation) {
            return Result<FeedbackMetadata, FeedbackMetadataError>::failure(
                FeedbackMetadataError::MissingOperationCorrelation);
        }

        if (kind == FeedbackKind::DeviceAcknowledged &&
            !correlation.operation->commandId) {
            return Result<FeedbackMetadata, FeedbackMetadataError>::failure(
                FeedbackMetadataError::MissingCommandCorrelation);
        }

        const bool acknowledgementSource =
            source == ObservationSource::DeviceEvent ||
            source == ObservationSource::SdkStatus;
        if (kind == FeedbackKind::DeviceAcknowledged && !acknowledgementSource) {
            return Result<FeedbackMetadata, FeedbackMetadataError>::failure(
                FeedbackMetadataError::InvalidObservationSource);
        }

        const bool actionCompletionSource =
            source == ObservationSource::HardwareReadback ||
            source == ObservationSource::DeviceEvent ||
            source == ObservationSource::SdkStatus;
        if (kind == FeedbackKind::DeviceActionCompleted && !actionCompletionSource) {
            return Result<FeedbackMetadata, FeedbackMetadataError>::failure(
                FeedbackMetadataError::InvalidObservationSource);
        }

        if (kind == FeedbackKind::AdapterFailure &&
            source != ObservationSource::AdapterReport) {
            return Result<FeedbackMetadata, FeedbackMetadataError>::failure(
                FeedbackMetadataError::InvalidAdapterFailureSource);
        }

        return Result<FeedbackMetadata, FeedbackMetadataError>::success(
            FeedbackMetadata(
                std::move(correlation),
                kind,
                source,
                sourceSequence,
                std::move(deviceTimestamp)));
    }

    [[nodiscard]] const FeedbackCorrelation& correlation() const noexcept {
        return correlation_;
    }

    [[nodiscard]] FeedbackKind kind() const noexcept {
        return kind_;
    }

    [[nodiscard]] ObservationSource source() const noexcept {
        return source_;
    }

    [[nodiscard]] const std::optional<std::uint64_t>& sourceSequence() const noexcept {
        return sourceSequence_;
    }

    [[nodiscard]] const std::optional<DeviceTimestamp>& deviceTimestamp() const noexcept {
        return deviceTimestamp_;
    }

private:
    FeedbackMetadata(
        FeedbackCorrelation correlation,
        FeedbackKind kind,
        ObservationSource source,
        std::optional<std::uint64_t> sourceSequence,
        std::optional<DeviceTimestamp> deviceTimestamp)
        : correlation_(std::move(correlation)),
          kind_(kind),
          source_(source),
          sourceSequence_(sourceSequence),
          deviceTimestamp_(std::move(deviceTimestamp)) {}

    FeedbackCorrelation correlation_;
    FeedbackKind kind_;
    ObservationSource source_;
    std::optional<std::uint64_t> sourceSequence_;
    std::optional<DeviceTimestamp> deviceTimestamp_;
};

// The payload is a capability-specific Core value. This envelope deliberately
// does not define a universal device-state bag or depend on vendor SDK types.
template <typename TPayload>
struct FeedbackEnvelope final {
    FeedbackMetadata metadata;
    TPayload payload;
};

enum class FeedbackSubmitStatus {
    Enqueued,
    QueueFull,
    Closed
};

enum class FeedbackIngressSequenceError {
    ZeroSequence,
    DifferentScope
};

// Positive Core-assigned sequence scoped to one microscope/runtime incarnation.
// The runtime assigns it atomically with successful FIFO enqueue. Values from
// different scopes are never ordered against each other; values never wrap.
class FeedbackIngressSequence final {
public:
    [[nodiscard]] static Result<
        FeedbackIngressSequence,
        FeedbackIngressSequenceError> create(
            MicroscopeId microscopeId,
            RuntimeId runtimeId,
            RuntimeGeneration runtimeGeneration,
            std::uint64_t value) {
        if (value == 0) {
            return Result<
                FeedbackIngressSequence,
                FeedbackIngressSequenceError>::failure(
                    FeedbackIngressSequenceError::ZeroSequence);
        }

        return Result<
            FeedbackIngressSequence,
            FeedbackIngressSequenceError>::success(
                FeedbackIngressSequence(
                    std::move(microscopeId),
                    std::move(runtimeId),
                    runtimeGeneration,
                    value));
    }

    [[nodiscard]] const MicroscopeId& microscopeId() const noexcept {
        return microscopeId_;
    }

    [[nodiscard]] const RuntimeId& runtimeId() const noexcept {
        return runtimeId_;
    }

    [[nodiscard]] RuntimeGeneration runtimeGeneration() const noexcept {
        return runtimeGeneration_;
    }

    [[nodiscard]] std::uint64_t value() const noexcept {
        return value_;
    }

    [[nodiscard]] bool belongsTo(
        const MicroscopeId& microscopeId,
        const RuntimeId& runtimeId,
        RuntimeGeneration runtimeGeneration) const noexcept {
        return microscopeId_ == microscopeId &&
               runtimeId_ == runtimeId &&
               runtimeGeneration_ == runtimeGeneration;
    }

    [[nodiscard]] Result<bool, FeedbackIngressSequenceError> precedes(
        const FeedbackIngressSequence& other) const {
        if (!belongsTo(
                other.microscopeId_,
                other.runtimeId_,
                other.runtimeGeneration_)) {
            return Result<bool, FeedbackIngressSequenceError>::failure(
                FeedbackIngressSequenceError::DifferentScope);
        }

        return Result<bool, FeedbackIngressSequenceError>::success(
            value_ < other.value_);
    }

private:
    FeedbackIngressSequence(
        MicroscopeId microscopeId,
        RuntimeId runtimeId,
        RuntimeGeneration runtimeGeneration,
        std::uint64_t value)
        : microscopeId_(std::move(microscopeId)),
          runtimeId_(std::move(runtimeId)),
          runtimeGeneration_(runtimeGeneration),
          value_(value) {}

    MicroscopeId microscopeId_;
    RuntimeId runtimeId_;
    RuntimeGeneration runtimeGeneration_;
    std::uint64_t value_;
};

// Core-owned, runtime-local receipt metadata for a successfully enqueued
// feedback item. receivedAt is monotonic and not a durable wall-clock value.
struct FeedbackReceiptMetadata final {
    std::chrono::steady_clock::time_point receivedAt;
    FeedbackIngressSequence ingressSequence;
};

enum class FeedbackReceiptError {
    ScopeMismatch
};

template <typename TPayload>
class FeedbackReceipt final {
public:
    [[nodiscard]] static Result<FeedbackReceipt, FeedbackReceiptError> create(
        FeedbackEnvelope<TPayload> feedback,
        std::chrono::steady_clock::time_point receivedAt,
        FeedbackIngressSequence ingressSequence) {
        const auto& runtime = feedback.metadata.correlation().runtimeDevice;
        if (!ingressSequence.belongsTo(
                runtime.microscopeId,
                runtime.runtimeId,
                runtime.runtimeGeneration)) {
            return Result<FeedbackReceipt, FeedbackReceiptError>::failure(
                FeedbackReceiptError::ScopeMismatch);
        }

        return Result<FeedbackReceipt, FeedbackReceiptError>::success(
            FeedbackReceipt(
                std::move(feedback),
                FeedbackReceiptMetadata{
                    receivedAt,
                    std::move(ingressSequence)}));
    }

    [[nodiscard]] const FeedbackEnvelope<TPayload>& feedback() const noexcept {
        return feedback_;
    }

    [[nodiscard]] const FeedbackReceiptMetadata& receipt() const noexcept {
        return receipt_;
    }

private:
    FeedbackReceipt(
        FeedbackEnvelope<TPayload> feedback,
        FeedbackReceiptMetadata receipt)
        : feedback_(std::move(feedback)),
          receipt_(std::move(receipt)) {}

    FeedbackEnvelope<TPayload> feedback_;
    FeedbackReceiptMetadata receipt_;
};

// Implementations are thread-safe. Enqueued means the ingress owns the
// message. On QueueFull, the ingress itself must latch feedback loss/unknown
// device state and wake the runtime through a control path independent of the
// saturated queue before returning. It cannot rely on a later adapter callback
// to report the drop or on another queue slot becoming available. The runtime
// cannot verify affected work until it has reconciled the lost-feedback
// condition. Closed identifies a disposed ingress.
template <typename TPayload>
class IFeedbackIngress {
public:
    virtual ~IFeedbackIngress() = default;

    [[nodiscard]] virtual FeedbackSubmitStatus submit(
        FeedbackEnvelope<TPayload> feedback) = 0;
};
} // namespace microscope::core
