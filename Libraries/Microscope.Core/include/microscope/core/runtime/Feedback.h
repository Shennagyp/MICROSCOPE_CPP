#pragma once

#include "microscope/core/ids/Ids.h"

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

struct DeviceTimestamp final {
    std::uint64_t ticks = 0;
    std::uint64_t ticksPerSecond = 0;
    std::optional<std::string> clockDomain;
};

struct RuntimeDeviceCorrelation final {
    MicroscopeId microscopeId;
    RuntimeId runtimeId;
    RuntimeGeneration runtimeGeneration;
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
    FeedbackKind kind = FeedbackKind::DeviceStateObserved;
    ObservationSource source = ObservationSource::Unknown;
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
    Accepted,
    QueueFull,
    Closed
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
