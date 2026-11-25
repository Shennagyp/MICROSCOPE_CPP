#pragma once

#include "microscope/core/ids/Ids.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace microscope::core {

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

struct FeedbackCorrelation final {
    MicroscopeId microscopeId;
    RuntimeId runtimeId;
    RuntimeGeneration runtimeGeneration;
    OperationId operationId;
    StepId stepId;
    std::optional<CommandId> commandId;
    DeviceId deviceId;
};

struct FeedbackMetadata final {
    FeedbackCorrelation correlation;
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
