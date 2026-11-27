#include "microscope/core/configuration/OperationMode.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using namespace microscope::core;
int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

template <typename T, typename E>
T take(Result<T, E> result) {
    if (!result) {
        throw std::runtime_error("OperationMode fixture rejected valid input");
    }
    return std::move(result).value();
}

template <typename TId>
TId id(const char* value) { return take(TId::create(value)); }

void testOperationModeIsContrastIntentWithoutHardwareSettings() {
    const auto microscopeId = id<MicroscopeId>("optika-microscope");
    const auto modeId = id<OperationModeId>("mode-phase-contrast");
    const auto mode = take(OperationMode::create(
        microscopeId, modeId, 3, "Phase contrast", ContrastMethod::PhaseContrast));

    check(mode.microscopeId() == microscopeId && mode.id() == modeId &&
          mode.revision() == 3,
          "retains microscope-scoped mode identity and revision");
    check(mode.name() == "Phase contrast",
          "retains the mode display name");
    check(mode.contrastMethod() == ContrastMethod::PhaseContrast,
          "defines the selected contrast method");
}

void testOperationModeRejectsUnknownContrastAndInvalidRevision() {
    const auto microscopeId = id<MicroscopeId>("optika-microscope");
    const auto modeId = id<OperationModeId>("mode-invalid");
    const auto unknown = OperationMode::create(
        microscopeId, modeId, 1, "Unknown", ContrastMethod::Unknown);
    check(!unknown && unknown.error() == OperationModeError::InvalidContrastMethod,
          "does not accept Unknown as an operating contrast method");

    const auto zeroRevision = OperationMode::create(
        microscopeId, modeId, 0, "Zero revision", ContrastMethod::Brightfield);
    check(!zeroRevision && zeroRevision.error() == OperationModeError::ZeroRevision,
          "rejects zero mode revision");
}

void testOperationModeRejectsInvalidNames() {
    const auto microscopeId = id<MicroscopeId>("optika-microscope");
    const auto modeId = id<OperationModeId>("mode-invalid-name");
    const auto empty = OperationMode::create(
        microscopeId, modeId, 1, "   ", ContrastMethod::Brightfield);
    check(!empty && empty.error() == OperationModeError::EmptyName,
          "rejects whitespace-only mode names");

    const auto embeddedNull = OperationMode::create(
        microscopeId, modeId, 1, std::string("bad\0name", 8),
        ContrastMethod::Brightfield);
    check(!embeddedNull && embeddedNull.error() == OperationModeError::InvalidName,
          "rejects embedded null bytes in mode names");
}

} // namespace

int main() {
    testOperationModeIsContrastIntentWithoutHardwareSettings();
    testOperationModeRejectsUnknownContrastAndInvalidRevision();
    testOperationModeRejectsInvalidNames();

    if (failures != 0) {
        std::cerr << failures << " OperationMode contract test(s) failed.\n";
        return 1;
    }
    std::cout << "All OperationMode contract tests passed.\n";
}
