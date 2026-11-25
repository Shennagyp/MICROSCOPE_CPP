#include "microscope/application/runtime/RuntimeHandle.h"

#include <cstdlib>
#include <iostream>
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

} // namespace

int main() {
    using namespace microscope;

    static_assert(!std::is_default_constructible<application::RuntimeHandle>::value,
                  "RuntimeHandle requires a physical microscope/runtime identity");
    static_assert(!std::is_default_constructible<application::OperationHandle>::value,
                  "OperationHandle requires a runtime and operation identity");
    static_assert(!std::is_default_constructible<application::RuntimeSnapshot>::value,
                  "RuntimeSnapshot must be created through its validated factory");
    static_assert(!std::is_default_constructible<application::OperationView>::value,
                  "OperationView must be created through its validated factory");

    const auto microscopeId = core::MicroscopeId::create("microscope-001").value();
    const auto runtimeId = core::RuntimeId::create("runtime-001").value();
    const auto generation1 = core::RuntimeGeneration::create(1).value();
    const auto generation2 = core::RuntimeGeneration::create(2).value();
    const auto operationId = core::OperationId::create("operation-001").value();

    application::RuntimeHandle first(
        microscopeId, runtimeId, generation1);
    application::RuntimeHandle sameRuntime(
        microscopeId, runtimeId, generation1);
    application::RuntimeHandle reopenedRuntime(
        microscopeId, runtimeId, generation2);

    auto activeRuntimeSnapshot = application::RuntimeSnapshot::create(
        first, application::RuntimeLifecycle::Active, 1);
    check(static_cast<bool>(activeRuntimeSnapshot),
          "creates a known runtime lifecycle snapshot with a positive revision");
    if (activeRuntimeSnapshot) {
        check(activeRuntimeSnapshot.value().handle().identifiesSameRuntime(first) &&
              activeRuntimeSnapshot.value().lifecycle() ==
                  application::RuntimeLifecycle::Active &&
              activeRuntimeSnapshot.value().revision() == 1,
              "runtime snapshot preserves immutable runtime identity and lifecycle values");
    }

    auto invalidRuntimeLifecycle = application::RuntimeSnapshot::create(
        first, static_cast<application::RuntimeLifecycle>(255), 1);
    check(!invalidRuntimeLifecycle &&
          invalidRuntimeLifecycle.error() ==
              application::RuntimeSnapshotError::InvalidLifecycle,
          "rejects an unknown runtime lifecycle state");

    auto zeroRuntimeRevision = application::RuntimeSnapshot::create(
        first, application::RuntimeLifecycle::Initializing, 0);
    check(!zeroRuntimeRevision &&
          zeroRuntimeRevision.error() ==
              application::RuntimeSnapshotError::ZeroRevision,
          "rejects a zero runtime snapshot revision");

    check(first.targetsSameMicroscope(reopenedRuntime),
          "runtime generations can refer to the same physical microscope");
    check(first.identifiesSameRuntime(sameRuntime),
          "equal runtime identity and generation identify the same runtime");
    check(!first.identifiesSameRuntime(reopenedRuntime),
          "a later generation is not the previous runtime instance");

    application::OperationHandle operation(first, operationId);
    check(operation.belongsTo(first),
          "operation handle belongs to its accepting runtime");
    check(!operation.belongsTo(reopenedRuntime),
          "old operation handle cannot address a reopened runtime generation");
    check(operation.operationId() == operationId,
          "operation handle preserves the stable operation ID");

    const auto coreSnapshotResult = core::OperationSnapshot::create(
        microscopeId,
        runtimeId,
        generation1,
        operationId,
        core::OperationStatus::Running,
        4);
    check(static_cast<bool>(coreSnapshotResult),
          "creates an immutable Core operation snapshot");
    const auto coreSnapshot = std::move(coreSnapshotResult).value();
    auto applicationViewResult = application::OperationView::create(
        operation, coreSnapshot);
    check(static_cast<bool>(applicationViewResult),
          "creates an application operation view for matching identities");
    if (applicationViewResult) {
        const auto& applicationView = applicationViewResult.value();
        check(applicationView.handle().belongsTo(first),
              "application operation projection retains the runtime-scoped handle");
        check(applicationView.snapshot().operationId() == operationId &&
              applicationView.snapshot().status() == core::OperationStatus::Running &&
              applicationView.snapshot().revision() == 4,
              "application projection carries Core's snapshot without duplicating state fields");
    }

    const auto differentOperationId =
        core::OperationId::create("operation-002").value();
    auto mismatchedSnapshot = core::OperationSnapshot::create(
        microscopeId,
        runtimeId,
        generation1,
        differentOperationId,
        core::OperationStatus::Running,
        5);
    auto mismatchedView = application::OperationView::create(
        operation, std::move(mismatchedSnapshot).value());
    check(!mismatchedView &&
          mismatchedView.error() ==
              application::OperationViewError::OperationIdentityMismatch,
          "rejects a snapshot with a different operation identity");

    const auto staleGenerationSnapshot = core::OperationSnapshot::create(
        microscopeId,
        runtimeId,
        generation2,
        operationId,
        core::OperationStatus::Running,
        6).value();
    auto staleGenerationView = application::OperationView::create(
        operation, staleGenerationSnapshot);
    check(!staleGenerationView &&
          staleGenerationView.error() ==
              application::OperationViewError::RuntimeIdentityMismatch,
          "rejects a snapshot from an earlier runtime generation");

    const auto secondRuntimeId =
        core::RuntimeId::create("runtime-002").value();
    const auto foreignRuntimeSnapshot = core::OperationSnapshot::create(
        microscopeId,
        secondRuntimeId,
        generation1,
        operationId,
        core::OperationStatus::Running,
        6).value();
    auto foreignRuntimeView = application::OperationView::create(
        operation, foreignRuntimeSnapshot);
    check(!foreignRuntimeView &&
          foreignRuntimeView.error() ==
              application::OperationViewError::RuntimeIdentityMismatch,
          "rejects a snapshot from another runtime even when OperationId matches");

    if (failures != 0) {
        std::cerr << failures << " Application contract test(s) failed.\n";
        return EXIT_FAILURE;
    }

    std::cout << "All Application contract tests passed.\n";
    return EXIT_SUCCESS;
}
