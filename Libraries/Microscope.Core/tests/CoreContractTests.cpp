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
}

void testDiagnosticsAreSeparateValues() {
    using namespace microscope::core;

    Diagnostic warning{
        DiagnosticSeverity::Warning,
        "configuration.profile.incomplete",
        "The profile does not specify an exposure preset.",
        DiagnosticContext{
            std::string("ConfigurationProfile"),
            std::string("profile-001"),
            std::string("camera.exposure")}};

    check(warning.severity == DiagnosticSeverity::Warning,
          "diagnostic carries severity");
    check(warning.code == "configuration.profile.incomplete",
          "diagnostic carries stable machine-readable code");
    check(warning.context.has_value() &&
          warning.context->fieldPath == std::optional<std::string>("camera.exposure"),
          "diagnostic carries optional field context");
}

} // namespace

int main() {
    testStrongIdentityTypes();
    testRuntimeGeneration();
    testResultAndStatus();
    testDiagnosticsAreSeparateValues();

    if (failures != 0) {
        std::cerr << failures << " Core contract test(s) failed.\n";
        return EXIT_FAILURE;
    }

    std::cout << "All Core contract tests passed.\n";
    return EXIT_SUCCESS;
}
