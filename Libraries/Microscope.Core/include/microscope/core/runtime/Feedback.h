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

// Core-owned receipt metadata. This uses the Core process monotonic clock,
// not the device clock carried in FeedbackMetadata. ingressSequence starts at
// one and increases within one runtime generation.
struct FeedbackReceiptMetadata final {
    std::chrono::steady_clock::time_point receivedAt;
    std::uint64_t ingressSequence;
};

template <typename TPayload>
struct FeedbackReceipt final {
    FeedbackEnvelope<TPayload> feedback;
    FeedbackReceiptMetadata receipt;
};

// Implementations are thread-safe. submit() transfers ownership of the
// normalized message to the ingress. QueueFull/Closed must be surfaced to
// the runtime; silent loss must never allow an operation to verify.
template <typename TPayload>
class IFeedbackIngress {
public:
    virtual ~IFeedbackIngress() = default;

    [[nodiscard]] virtual FeedbackSubmitStatus submit(
        FeedbackEnvelope<TPayload> feedback) = 0;
};

} // namespace microscope::core
