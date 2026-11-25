#include "microscope/core/diagnostics/Diagnostic.h"
#include "microscope/core/ids/Ids.h"
#include "microscope/core/result/Result.h"

#include <cstdlib>
#include <optional>
#include <iostream>
#include <string>
#include <type_traits>
#include <unordered_set>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

void testStrongIdentityTypes() {
    using namespace microscope::core;

    static_assert(!std::is_default_constructible<SampleId>::value,
                  "SampleId must not have an invalid default value");
    static_assert(!std::is_convertible<std::string, SampleId>::value,
                  "raw strings must not implicitly construct SampleId");
    static_assert(!std::is_convertible<SampleId, SlideId>::value,
                  "SampleId must not convert to SlideId");
    static_assert(!std::is_convertible<SlideId, TileId>::value,
                  "SlideId must not convert to TileId");
    static_assert(!std::is_convertible<RuntimeId, MicroscopeId>::value,
                  "RuntimeId must not convert to MicroscopeId");
    static_assert(!std::is_convertible<DeviceId, MicroscopeId>::value,
                  "DeviceId must not convert to MicroscopeId");
    static_assert(!std::is_convertible<AcquisitionPlanId, AcquisitionRunId>::value,
                  "AcquisitionPlanId must not convert to AcquisitionRunId");
    static_assert(!std::is_convertible<AcquisitionRunId, ImageFrameId>::value,
                  "AcquisitionRunId must not convert to ImageFrameId");
    static_assert(!std::is_convertible<ImageFrameId, DerivedImageId>::value,
                  "ImageFrameId must not convert to DerivedImageId");
    static_assert(!std::is_convertible<OperationId, StepId>::value,
                  "OperationId must not convert to StepId");
    static_assert(!std::is_convertible<StepId, CommandId>::value,
                  "StepId must not convert to CommandId");
    static_assert(!std::is_convertible<RuntimeGeneration, DeviceSessionGeneration>::value,
                  "runtime generation must not convert to device-session generation");

    auto sampleResult = SampleId::create("sample-opaque-001");
    auto slideResult = SlideId::create("slide-opaque-001");
    check(static_cast<bool>(sampleResult), "creates a non-empty SampleId");
    check(static_cast<bool>(slideResult), "creates a non-empty SlideId");

    if (sampleResult && slideResult) {
        const SampleId sample = sampleResult.value();
        const SlideId slide = slideResult.value();
        check(sample.value() == "sample-opaque-001",
              "preserves the opaque SampleId representation");
        check(sample != SampleId::create("sample-opaque-002").value(),
              "different opaque values are not equal");
        check(slide.value() == "slide-opaque-001",
              "preserves SlideId independently from SampleId");

        std::unordered_set<SampleId> sampleSet;
        sampleSet.insert(sample);
        check(sampleSet.find(sample) != sampleSet.end(),
              "strong IDs support hash-based lookup");
    }

    auto opaquePathLikeValue = SampleId::create("source/part:alpha");
    check(static_cast<bool>(opaquePathLikeValue),
          "opaque IDs are not restricted to filesystem path syntax");

    auto emptyResult = SampleId::create("");
    check(!emptyResult, "rejects an empty persisted identity");
    if (!emptyResult) {
        check(emptyResult.error().code == IdentityError::Code::EmptyValue,
              "empty identity has a machine-readable error code");
    }
}

void testRuntimeGeneration() {
    using namespace microscope::core;

    auto zero = RuntimeGeneration::create(0);
    check(!zero, "rejects generation zero");

    auto generation = RuntimeGeneration::create(7);
    check(static_cast<bool>(generation), "creates a positive runtime generation");
    if (generation) {
        check(generation.value().value() == 7,
              "preserves runtime generation value");
    }

    auto zeroDeviceSession = DeviceSessionGeneration::create(0);
    check(!zeroDeviceSession, "rejects device session generation zero");
    auto deviceSession = DeviceSessionGeneration::create(5);
    check(static_cast<bool>(deviceSession),
          "creates a positive device session generation");
    if (deviceSession) {
        check(deviceSession.value().value() == 5,
              "preserves device session generation value");
    }
}

void testResultAndStatus() {
    using namespace microscope::core;

    auto success = Result<int, std::string>::success(42);
    check(static_cast<bool>(success), "success result is successful");
    check(success.value() == 42, "success result preserves its value");

    auto failure = Result<int, std::string>::failure("rejected");
    check(!failure, "failure result is not successful");
    check(failure.error() == "rejected", "failure result preserves typed error");

    auto statusSuccess = Status<std::string>::success();
    check(static_cast<bool>(statusSuccess), "successful Status has no error");

    auto statusFailure = Status<std::string>::failure("closed");
    check(!statusFailure, "failed Status reports failure");
    check(statusFailure.error() == "closed", "Status preserves its error");

    auto sameTypeSuccess = Result<std::string, std::string>::success("value");
    auto sameTypeFailure = Result<std::string, std::string>::failure("error");
    check(sameTypeSuccess && sameTypeSuccess.value() == "value",
          "Result distinguishes success when value and error types are identical");
    check(!sameTypeFailure && sameTypeFailure.error() == "error",
          "Result distinguishes failure when value and error types are identical");
}

void testOperationSnapshotValidation() {
    using namespace microscope::core;

    static_assert(!std::is_default_constructible<OperationSnapshot>::value,
                  "OperationSnapshot must be created through Core validation");

    const auto microscopeId = MicroscopeId::create("snapshot-microscope").value();
    const auto runtimeId = RuntimeId::create("snapshot-runtime").value();
    const auto generation = RuntimeGeneration::create(1).value();
    const auto operationId = OperationId::create("snapshot-operation").value();

    const auto validSnapshot = OperationSnapshot::create(
        microscopeId,
        runtimeId,
        generation,
        operationId,
        OperationStatus::Accepted,
        1);
    check(static_cast<bool>(validSnapshot),
          "creates a known operation status with a positive revision");

    const auto invalidStatus = OperationSnapshot::create(
        microscopeId,
        runtimeId,
        generation,
        operationId,
        static_cast<OperationStatus>(255),
        1);
    check(!invalidStatus &&
          invalidStatus.error() == OperationSnapshotError::InvalidStatus,
          "rejects an unknown operation status");

    const auto zeroRevision = OperationSnapshot::create(
        microscopeId,
        runtimeId,
        generation,
        operationId,
        OperationStatus::Accepted,
        0);
    check(!zeroRevision &&
          zeroRevision.error() == OperationSnapshotError::ZeroRevision,
          "rejects a zero operation snapshot revision");
}

void testDiagnosticsAreSeparateValues() {
    using namespace microscope::core;

    static_assert(!std::is_default_constructible<Diagnostic>::value,
                  "Diagnostic must be created through its validator");

    auto warningResult = Diagnostic::create(
        DiagnosticSeverity::Warning,
        "configuration.profile.incomplete",
        "The profile does not specify an exposure preset.",
        DiagnosticContext{
            std::string("ConfigurationProfile"),
            std::string("profile-001"),
            std::string("camera.exposure")});
    check(static_cast<bool>(warningResult),
          "creates a diagnostic with valid severity/code/message");
    if (warningResult) {
        const auto& warning = warningResult.value();
        check(warning.severity() == DiagnosticSeverity::Warning,
              "diagnostic carries severity");
        check(warning.code() == "configuration.profile.incomplete",
              "diagnostic carries stable machine-readable code");
        check(warning.context().has_value() &&
              warning.context()->fieldPath ==
                  std::optional<std::string>("camera.exposure"),
              "diagnostic carries optional field context");
    }

    auto emptyCode = Diagnostic::create(
        DiagnosticSeverity::Warning,
        "",
        "A message is present.");
    check(!emptyCode && emptyCode.error() == DiagnosticError::EmptyCode,
          "rejects a diagnostic without a machine-readable code");

    auto emptyMessage = Diagnostic::create(
        DiagnosticSeverity::Warning,
        "configuration.profile.incomplete",
        "");
    check(!emptyMessage &&
          emptyMessage.error() == DiagnosticError::EmptyMessage,
          "rejects a diagnostic without a message");

    auto whitespaceCode = Diagnostic::create(
        DiagnosticSeverity::Warning,
        "  \t",
        "A message is present.");
    check(!whitespaceCode &&
          whitespaceCode.error() == DiagnosticError::InvalidCode,
          "rejects whitespace in a diagnostic code");

    auto whitespaceMessage = Diagnostic::create(
        DiagnosticSeverity::Warning,
        "configuration.profile.incomplete",
        " \n ");
    check(!whitespaceMessage &&
          whitespaceMessage.error() == DiagnosticError::EmptyMessage,
          "rejects a whitespace-only diagnostic message");

    auto emptyContextValue = Diagnostic::create(
        DiagnosticSeverity::Warning,
        "configuration.profile.incomplete",
        "A message is present.",
        DiagnosticContext{std::string(""), std::nullopt, std::nullopt});
    check(!emptyContextValue &&
          emptyContextValue.error() == DiagnosticError::EmptyContextValue,
          "uses absent context values instead of empty-string sentinels");

    auto invalidSeverity = Diagnostic::create(
        static_cast<DiagnosticSeverity>(255),
        "configuration.profile.incomplete",
        "The severity value is invalid.");
    check(!invalidSeverity &&
          invalidSeverity.error() == DiagnosticError::InvalidSeverity,
          "rejects an unknown diagnostic severity");
}

} // namespace

int main() {
    testStrongIdentityTypes();
    testRuntimeGeneration();
    testResultAndStatus();
    testOperationSnapshotValidation();
    testDiagnosticsAreSeparateValues();

    if (failures != 0) {
        std::cerr << failures << " Core contract test(s) failed.\n";
        return EXIT_FAILURE;
    }

    std::cout << "All Core contract tests passed.\n";
    return EXIT_SUCCESS;
}
