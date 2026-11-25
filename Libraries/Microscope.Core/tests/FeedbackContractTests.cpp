#include "microscope/core/runtime/Feedback.h"

#include <cstdlib>
#include <iostream>
#include <optional>
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
        return microscope::core::FeedbackSubmitStatus::Accepted;
    }

    std::optional<microscope::core::FeedbackEnvelope<TPayload>> lastFeedback;
};

} // namespace

int main() {
    using namespace microscope::core;

    const auto microscopeId = MicroscopeId::create("microscope-001").value();
    const auto runtimeId = RuntimeId::create("runtime-001").value();
    const auto generation = RuntimeGeneration::create(3).value();
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
                microscopeId,
                runtimeId,
                generation,
                operationId,
                stepId,
                commandId,
                deviceId},
            ObservationSource::HardwareReadback,
            std::uint64_t{91},
            DeviceTimestamp{1200, 1000, std::string("device-clock-A")}},
        SelectorFeedback{true}};

    CapturingIngress<SelectorFeedback> ingress;
    const auto result = ingress.submit(std::move(feedback));

    check(result == FeedbackSubmitStatus::Accepted,
          "ingress reports accepted ownership transfer");
    check(ingress.lastFeedback.has_value(),
          "ingress retains the submitted typed feedback");

    if (ingress.lastFeedback) {
        const auto& captured = *ingress.lastFeedback;
        check(captured.metadata.correlation.operationId == operationId,
              "feedback retains operation correlation");
        check(captured.metadata.correlation.commandId.has_value() &&
              *captured.metadata.correlation.commandId == commandId,
              "feedback retains command correlation");
        check(captured.metadata.correlation.deviceId == deviceId,
              "feedback retains logical device identity");
        check(captured.metadata.source == ObservationSource::HardwareReadback,
              "feedback preserves observation provenance");
        check(captured.metadata.sourceSequence == std::optional<std::uint64_t>(91),
              "feedback preserves adapter sequence");
        check(captured.payload.reachedRequestedPosition,
              "feedback payload remains capability-typed");
    }

    if (failures != 0) {
        std::cerr << failures << " feedback contract test(s) failed.\n";
        return EXIT_FAILURE;
    }

    std::cout << "All feedback contract tests passed.\n";
    return EXIT_SUCCESS;
}
