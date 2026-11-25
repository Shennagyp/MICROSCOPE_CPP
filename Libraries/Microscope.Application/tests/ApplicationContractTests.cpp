#include "microscope/application/runtime/RuntimeHandle.h"

#include <cstdlib>
#include <iostream>

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

    const auto coreSnapshot = core::OperationSnapshot{
        microscopeId,
        runtimeId,
        generation1,
        operationId,
        core::OperationStatus::Running,
        4};
    const auto applicationView = application::OperationView{
        operation,
        coreSnapshot};
    check(applicationView.handle.belongsTo(first),
          "application operation projection retains the runtime-scoped handle");
    check(applicationView.snapshot.operationId == operationId &&
          applicationView.snapshot.status == core::OperationStatus::Running &&
          applicationView.snapshot.revision == 4,
          "application projection carries Core's snapshot without duplicating state fields");
    check(applicationView.isConsistent(),
          "application projection confirms handle and Core snapshot identity match");

    const auto differentOperationId =
        core::OperationId::create("operation-002").value();
    const auto mismatchedView = application::OperationView{
        operation,
        core::OperationSnapshot{
            microscopeId,
            runtimeId,
            generation1,
            differentOperationId,
            core::OperationStatus::Running,
            5}};
    check(!mismatchedView.isConsistent(),
          "application projection detects a mismatched Core snapshot identity");

    const auto staleGenerationSnapshot = core::OperationSnapshot{
        microscopeId,
        runtimeId,
        generation2,
        operationId,
        core::OperationStatus::Running,
        6};
    const auto staleGenerationView = application::OperationView{
        operation,
        staleGenerationSnapshot};
    check(!staleGenerationView.isConsistent(),
          "application projection rejects a snapshot from an earlier runtime generation");

    const auto secondRuntimeId =
        core::RuntimeId::create("runtime-002").value();
    const auto foreignRuntimeSnapshot = core::OperationSnapshot{
        microscopeId,
        secondRuntimeId,
        generation1,
        operationId,
        core::OperationStatus::Running,
        6};
    const auto foreignRuntimeView = application::OperationView{
        operation,
        foreignRuntimeSnapshot};
    check(!foreignRuntimeView.isConsistent(),
          "application projection rejects a snapshot from another runtime even when OperationId matches");

    if (failures != 0) {
        std::cerr << failures << " Application contract test(s) failed.\n";
        return EXIT_FAILURE;
    }

    std::cout << "All Application contract tests passed.\n";
    return EXIT_SUCCESS;
}
