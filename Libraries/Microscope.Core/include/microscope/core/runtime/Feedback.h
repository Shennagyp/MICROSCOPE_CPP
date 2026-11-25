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
    Unknown
};

enum class DeviceTimestampError {
    ZeroTickRate
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

struct FeedbackMetadata final {
    FeedbackCorrelation correlation;
    FeedbackKind kind;
    ObservationSource source = ObservationSource::Unknown;
    // Adapter/device sequence, scoped to DeviceSessionGeneration. Zero may
    // be a valid first sequence; absence means the source does not report one.
    // When supplied, the adapter preserves ordering for that session; it
    // starts a new session generation rather than reusing ambiguous values.
    std::optional<std::uint64_t> sourceSequence;
    std::optional<DeviceTimestamp> deviceTimestamp;
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
    DifferentRuntimeScope
};

// Positive Core-assigned receive sequence scoped to one runtime incarnation.
// Values from different runtime generations are never ordered against each other.
class FeedbackIngressSequence final {
public:
    [[nodiscard]] static Result<
        FeedbackIngressSequence,
        FeedbackIngressSequenceError> create(
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
                    std::move(runtimeId),
                    runtimeGeneration,
                    value));
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
        const RuntimeId& runtimeId,
        RuntimeGeneration runtimeGeneration) const noexcept {
        return runtimeId_ == runtimeId &&
               runtimeGeneration_ == runtimeGeneration;
    }

    [[nodiscard]] Result<bool, FeedbackIngressSequenceError> precedes(
        const FeedbackIngressSequence& other) const {
        if (!belongsTo(other.runtimeId_, other.runtimeGeneration_)) {
            return Result<bool, FeedbackIngressSequenceError>::failure(
                FeedbackIngressSequenceError::DifferentRuntimeScope);
        }

        return Result<bool, FeedbackIngressSequenceError>::success(
            value_ < other.value_);
    }

private:
    FeedbackIngressSequence(
        RuntimeId runtimeId,
        RuntimeGeneration runtimeGeneration,
        std::uint64_t value)
        : runtimeId_(std::move(runtimeId)),
          runtimeGeneration_(runtimeGeneration),
          value_(value) {}

    RuntimeId runtimeId_;
    RuntimeGeneration runtimeGeneration_;
    std::uint64_t value_;
};

// Core-owned, runtime-local receipt metadata. receivedAt is a monotonic
// ordering/deadline value and is not a durable wall-clock timestamp.
struct FeedbackReceiptMetadata final {
    std::chrono::steady_clock::time_point receivedAt;
    FeedbackIngressSequence ingressSequence;
};

enum class FeedbackReceiptError {
    RuntimeScopeMismatch
};

template <typename TPayload>
class FeedbackReceipt final {
public:
    [[nodiscard]] static Result<FeedbackReceipt, FeedbackReceiptError> create(
        FeedbackEnvelope<TPayload> feedback,
        std::chrono::steady_clock::time_point receivedAt,
        FeedbackIngressSequence ingressSequence) {
        const auto& runtime = feedback.metadata.correlation.runtimeDevice;
        if (!ingressSequence.belongsTo(
                runtime.runtimeId,
                runtime.runtimeGeneration)) {
            return Result<FeedbackReceipt, FeedbackReceiptError>::failure(
                FeedbackReceiptError::RuntimeScopeMismatch);
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
// device state before returning; it cannot rely on a later adapter callback
// to report the drop. The runtime cannot verify affected work until it has
// reconciled the lost-feedback condition. Closed identifies a disposed ingress.
template <typename TPayload>
class IFeedbackIngress {
public:
    virtual ~IFeedbackIngress() = default;

    [[nodiscard]] virtual FeedbackSubmitStatus submit(
        FeedbackEnvelope<TPayload> feedback) = 0;
};
} // namespace microscope::core
