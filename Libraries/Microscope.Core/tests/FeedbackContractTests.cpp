#include "microscope/core/runtime/Feedback.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

template <typename TPayload>
class CapturingIngress final : public microscope::core::IFeedbackIngress<TPayload> {
public:
    microscope::core::FeedbackSubmitStatus submit(
        microscope::core::FeedbackEnvelope<TPayload> feedback) override {
        lastFeedback = std::move(feedback);
        return microscope::core::FeedbackSubmitStatus::Enqueued;
    }

    std::optional<microscope::core::FeedbackEnvelope<TPayload>> lastFeedback;
};

} // namespace

int main() {
    using namespace microscope::core;

    static_assert(!std::is_convertible<
        FeedbackIngressSequence, std::uint64_t>::value,
        "Core ingress sequence must not convert to a device source sequence");

    const auto microscopeId = MicroscopeId::create("microscope-001").value();
    const auto runtimeId = RuntimeId::create("runtime-001").value();
    const auto generation = RuntimeGeneration::create(3).value();
    const auto deviceSessionGeneration = DeviceSessionGeneration::create(8).value();
    const auto operationId = OperationId::create("operation-001").value();
    const auto stepId = StepId::create("step-001").value();
    const auto commandId = CommandId::create("command-001").value();
    const auto deviceId = DeviceId::create("objective-selector").value();

    struct SelectorFeedback final {
        bool reachedRequestedPosition;
    };

    FeedbackEnvelope<SelectorFeedback> feedback{
        FeedbackMetadata{
            FeedbackCorrelation{
                RuntimeDeviceCorrelation{
                    microscopeId,
                    runtimeId,
                    generation,
                    deviceSessionGeneration,
                    deviceId},
                OperationCorrelation{
                    operationId,
                    stepId,
                    commandId}},
            FeedbackKind::DeviceStateObserved,
            ObservationSource::HardwareReadback,
            std::uint64_t{91},
            DeviceTimestamp::create(1200, 1000, std::string("device-clock-A")).value()},
        SelectorFeedback{true}};

    CapturingIngress<SelectorFeedback> ingress;
    const auto result = ingress.submit(std::move(feedback));

    check(result == FeedbackSubmitStatus::Enqueued,
          "ingress reports that feedback was enqueued");
    check(ingress.lastFeedback.has_value(),
          "ingress retains the submitted typed feedback");

    if (ingress.lastFeedback) {
        const auto& captured = *ingress.lastFeedback;
        check(captured.metadata.correlation.operation.has_value(),
              "operation feedback has explicit operation correlation");
        if (captured.metadata.correlation.operation) {
            check(captured.metadata.correlation.operation->operationId == operationId,
                  "feedback retains operation correlation");
            check(captured.metadata.correlation.operation->stepId == stepId,
                  "feedback retains step correlation");
            check(captured.metadata.correlation.operation->commandId.has_value() &&
                  *captured.metadata.correlation.operation->commandId == commandId,
                  "feedback retains command correlation");
        }
        check(captured.metadata.correlation.runtimeDevice.deviceId == deviceId,
              "feedback retains logical device identity");
        check(captured.metadata.correlation.runtimeDevice.microscopeId == microscopeId &&
              captured.metadata.correlation.runtimeDevice.runtimeId == runtimeId,
              "feedback retains runtime and microscope scope");
        check(captured.metadata.correlation.runtimeDevice.deviceSessionGeneration ==
                  deviceSessionGeneration,
              "feedback retains the current device session generation");
        check(captured.metadata.kind == FeedbackKind::DeviceStateObserved,
              "feedback preserves normalized fact kind");
        check(captured.metadata.source == ObservationSource::HardwareReadback,
              "feedback preserves observation provenance");
        check(captured.metadata.sourceSequence == std::optional<std::uint64_t>(91),
              "feedback preserves adapter sequence");
        check(captured.payload.reachedRequestedPosition,
              "feedback payload remains capability-typed");
    }

    FeedbackEnvelope<SelectorFeedback> telemetry{
        FeedbackMetadata{
            FeedbackCorrelation{
                RuntimeDeviceCorrelation{
                    microscopeId,
                    runtimeId,
                    generation,
                    deviceSessionGeneration,
                    deviceId},
                std::nullopt},
            FeedbackKind::DeviceProgress,
            ObservationSource::SdkStatus,
            std::nullopt,
            std::nullopt},
        SelectorFeedback{false}};
    CapturingIngress<SelectorFeedback> telemetryIngress;
    const auto telemetryResult = telemetryIngress.submit(std::move(telemetry));
    check(telemetryResult == FeedbackSubmitStatus::Enqueued,
          "ingress enqueues unsolicited device telemetry without operation IDs");
    check(telemetryIngress.lastFeedback.has_value() &&
          !telemetryIngress.lastFeedback->metadata.correlation.operation.has_value(),
          "telemetry does not fabricate operation or step identities");
    check(telemetryIngress.lastFeedback.has_value() &&
          telemetryIngress.lastFeedback->metadata.kind == FeedbackKind::DeviceProgress,
          "telemetry preserves its normalized fact category");

    const auto invalidDeviceTimestamp = DeviceTimestamp::create(1500, 0);
    check(!invalidDeviceTimestamp,
          "rejects a device timestamp without a valid ticks-per-second value");
    const auto validDeviceTimestamp = DeviceTimestamp::create(1500, 1000);
    check(static_cast<bool>(validDeviceTimestamp),
          "accepts a device timestamp with an explicit positive tick rate");

    const auto zeroIngressSequence =
        FeedbackIngressSequence::create(microscopeId, runtimeId, generation, 0);
    check(!zeroIngressSequence,
          "rejects a zero Core feedback ingress sequence");
    const auto validIngressSequence =
        FeedbackIngressSequence::create(microscopeId, runtimeId, generation, 17);
    const auto laterIngressSequence =
        FeedbackIngressSequence::create(microscopeId, runtimeId, generation, 18);
    check(static_cast<bool>(validIngressSequence),
          "accepts a positive Core feedback ingress sequence");
    check(validIngressSequence.value().precedes(laterIngressSequence.value()).value(),
          "Core ingress sequences preserve receive order within one runtime generation");
    check(validIngressSequence.value().belongsTo(microscopeId, runtimeId, generation),
          "Core ingress sequence retains its runtime scope");
    const auto otherMicroscopeId =
        MicroscopeId::create("microscope-other").value();
    const auto otherMicroscopeSequence =
        FeedbackIngressSequence::create(
            otherMicroscopeId, runtimeId, generation, 19).value();
    const auto crossMicroscopeOrder =
        validIngressSequence.value().precedes(otherMicroscopeSequence);
    check(!crossMicroscopeOrder &&
          crossMicroscopeOrder.error() ==
              FeedbackIngressSequenceError::DifferentRuntimeScope,
          "rejects ordering ingress sequences from different microscopes");

    const auto otherRuntimeId =
        RuntimeId::create("runtime-other").value();
    const auto otherRuntimeSequence =
        FeedbackIngressSequence::create(microscopeId, otherRuntimeId, generation, 19).value();
    const auto crossRuntimeOrder =
        validIngressSequence.value().precedes(otherRuntimeSequence);
    check(!crossRuntimeOrder &&
          crossRuntimeOrder.error() ==
              FeedbackIngressSequenceError::DifferentRuntimeScope,
          "rejects ordering ingress sequences from different runtimes");

    const auto receiptTime = std::chrono::steady_clock::time_point{
        std::chrono::steady_clock::duration{1234}};
    auto receivedResult = FeedbackReceipt<SelectorFeedback>::create(
        FeedbackEnvelope<SelectorFeedback>{
            FeedbackMetadata{
                FeedbackCorrelation{
                    RuntimeDeviceCorrelation{
                        microscopeId,
                        runtimeId,
                        generation,
                        deviceSessionGeneration,
                        deviceId},
                    OperationCorrelation{
                        operationId,
                        stepId,
                        commandId}},
                FeedbackKind::DeviceAcknowledged,
                ObservationSource::DeviceEvent,
                std::uint64_t{92},
                DeviceTimestamp::create(1500, 1000, std::string("device-clock-A")).value()},
            SelectorFeedback{true}},
        receiptTime,
        FeedbackIngressSequence::create(microscopeId, runtimeId, generation, 17).value());
    check(static_cast<bool>(receivedResult),
          "creates a feedback receipt when ingress sequence and feedback runtime match");
    if (receivedResult) {
        const auto& received = receivedResult.value();
        check(received.receipt().receivedAt == receiptTime,
              "Core receipt time uses a separate monotonic-clock field");
        check(received.feedback().metadata.deviceTimestamp.has_value() &&
              received.feedback().metadata.deviceTimestamp->ticks() == 1500,
              "device timestamp remains separate from Core receipt time");
        check(received.receipt().ingressSequence.value() == 17,
              "Core ingress sequence is independent of adapter source sequence");
    }

    auto mismatchedReceipt = FeedbackReceipt<SelectorFeedback>::create(
        FeedbackEnvelope<SelectorFeedback>{
            FeedbackMetadata{
                FeedbackCorrelation{
                    RuntimeDeviceCorrelation{
                        microscopeId,
                        runtimeId,
                        generation,
                        deviceSessionGeneration,
                        deviceId},
                    std::nullopt},
                FeedbackKind::DeviceProgress,
                ObservationSource::SdkStatus,
                std::nullopt,
                std::nullopt},
            SelectorFeedback{false}},
        receiptTime,
        FeedbackIngressSequence::create(microscopeId, otherRuntimeId, generation, 20).value());
    check(!mismatchedReceipt &&
          mismatchedReceipt.error() == FeedbackReceiptError::RuntimeScopeMismatch,
          "rejects a receipt whose ingress sequence belongs to another runtime");

    if (failures != 0) {
        std::cerr << failures << " feedback contract test(s) failed.\n";
        return EXIT_FAILURE;
    }

    std::cout << "All feedback contract tests passed.\n";
    return EXIT_SUCCESS;
}
