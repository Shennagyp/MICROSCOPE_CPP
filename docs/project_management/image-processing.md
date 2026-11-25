# Workstream — Image Processing

**Implementation repository:** [Shennagyp/ImageProcessing](https://github.com/Shennagyp/ImageProcessing)  
**Active branch:** \`wp1-target-inventory\`  
**Latest observed head:** \`2cbd4662dabf4e7cab26cf824bb3204fdd85b3fc\` (25 November 2026, 15:00:04 UTC)  
**Management status:** active independent library; not yet integrated into the Station's Python pipeline or OPTIKA application.

## 1. Product definition

ImageProcessing is a cross-platform C++20 image-processing library built around:
- image documents and typed layers;
- stable operation semantics and parameter contracts;
- independently registered operation implementations;
- heterogeneous execution providers;
- qualification and candidate selection;
- explicit resource ownership, routing, submission and completion;
- functional composition through a sequential ImageFunction API.

The architecture is functional-core/imperative-shell. Operations define image meaning; implementations provide algorithms; providers provide compute and resource-execution technology. These responsibilities must not collapse into one backend class.

The public processing model is typed function composition. It is not a visual node editor or graph-editor frontend.

## 2. Development history

### Swift v0.1 foundation

The \`version-0.1\` / \`stateful-operation-session-boundary\` source establishes the semantic architecture:
- GeneralExecutionProtocol;
- ImageProcessingProtocol;
- ImageProcessingRuntime;
- immutable image snapshots and revision-scoped storage bindings;
- typed operations/implementations;
- provider/implementation registries;
- qualification policy and candidate ranking;
- execution coordinator and sequential ImageFunction composition;
- CPU reference operation.

### C++20 migration

The \`version-0.2-cpp\` branch ports and refines the v0.1 architecture into C++20/CMake. It is not a restart from empty contracts.

### Registration and provider work

- \`wp0-registration-integrity\`: provider and operation registration identity/conformance and registry-integrity work.
- \`wp1-target-inventory\`: current active line, expanding target/environment inventory, provider behavior, execution lifecycle, resource routing and native provider cleanup semantics.

The current WP1 history preserves the source and later commits after the user reference milestone \`dc95ae3\`; do not reset or move back to that point.

## 3. Current implementation

The C++ repository contains three principal libraries:

1. **GeneralExecutionProtocol** — generic execution/provider/resource contracts that do not depend on image semantics.
2. **ImageProcessingProtocol** — image/document/layer/pixel/operation semantics and typed function composition.
3. **ImageProcessingRuntime** — provider/implementation registries, environment snapshots, qualification, candidate binding, route planning, execution coordination and lifecycle.

The branch also contains separate CPU reference provider and operation packs, a real CPU reference operation, CUDA and Metal provider source, tests and a consumer example.

Current architecture/source evidence includes:
- typed image/document and pixel storage descriptors;
- stable image identity and revision-checked atomic document/storage-binding publication;
- operation and implementation specifications with typed parameter and output contracts;
- common qualification that separates hard eligibility from preference ranking;
- candidate-specific provider/device/context and resource-access routes;
- input access routes for direct, mapped, copy, managed-migration and peer-transfer profiles;
- sequential ImageFunction execution that qualifies the actual preceding output and captures a fresh environment for each step;
- provider-owned work/resource lifetimes retained until terminal completion;
- explicit cancellation and quiescence semantics;
- CUDA fatal-context poisoning and cleanup ownership logic;
- Metal command-buffer/resource ownership implementation.

### Simulation versus native evidence

The CPU direct and CPU copy paths contain real CPU execution. Several cross-provider or device routes are represented by test/simulated device registrations. They do not demonstrate PCIe/NVLink transfers, DMA, CUDA managed migration or physical GPU peer transfer.

CUDA and Metal source implementations exist. Hosted compile/fake-runtime or provider-build CI is not equivalent to physical NVIDIA or Apple GPU hardware acceptance.

## 4. Current change and verification state

The latest observed branch tip \`2cbd466\` adds tests for fatal native CUDA enqueue errors from payloads. The preceding commits preserve native enqueue error codes in payload results and poison the CUDA context on fatal payload-enqueue status.

At the time this record was prepared:
- exact-head CI for \`2cbd466\` was queued;
- some hardware workflows were skipped on hosted runners without the required hardware;
- earlier runs passed all 14 CTest entries on Ubuntu, macOS and Windows for earlier source revisions;
- no local build/test/debug run is claimed.

Earlier cross-platform examples include:
- [run 45170416341](https://github.com/Shennagyp/ImageProcessing/actions/runs/45170416341)
- [run 45170641022](https://github.com/Shennagyp/ImageProcessing/actions/runs/45170641022)
- [run 45158782733](https://github.com/Shennagyp/ImageProcessing/actions/runs/45158782733)
- [run 45159781407](https://github.com/Shennagyp/ImageProcessing/actions/runs/45159781407)

These successes belong to their exact earlier commits. They are not a pass for current head \`2cbd466\`.

### Current review priority

The current work is still within native execution-provider lifecycle correctness, particularly CUDA fatal errors and ownership/cleanup behavior. Metal submission completion, cancellation and resource retention also remain review areas. The broader Phase I provider/backend and operation/format conformance matrix is not closed.

## 5. Integration boundary

The Station milestone documents Python-scripted processing pipelines and the Workflow Designer as S9 requirements; those pipelines are not implemented in the Station source baseline reviewed for this handoff.

ImageProcessing is a separate C++ library and is not currently wired into:
- Station's Python script catalogue/worker;
- Station's image-processing dock or workflow designer;
- OPTIKA B293's C++ Application runtime;
- microscope acquisition or camera control.

Do not claim integration because both projects contain image-processing-related types or requirements. Define a separate adapter/integration phase only after the library API, ownership, payload format, execution environment and Station workflow contracts are reconciled.

## 6. Continuation handoff

**Active branch:** \`wp1-target-inventory\`.  
**Active focus:** native CUDA execution/lifecycle correctness and provider conformance.  
**Next bounded task:** refresh the exact-head CI status for \`2cbd466\`, then continue the CUDA fatal enqueue/poison/cleanup review using the current \`docs/project_management/Continuation-Handoff.md\` as source authority.

### Required procedure

1. Fetch current \`wp1-target-inventory\` HEAD; the SHA above is a snapshot, not a permanent pointer.
2. Read \`docs/project_management/Continuation-Handoff.md\`, \`Current-Execution-Status.md\`, \`Phase-I-Static-Audit.md\`, \`CUDA-Cleanup-Implementation-Design.md\` and the C++ architecture decision register.
3. Reconcile the exact latest source commits with the handoff. Older entries are historical when a later entry explicitly supersedes them.
4. Poll CI for the exact current head and record every platform/workflow result. Distinguish queued, running, failed, skipped and successful results.
5. Review the fatal native enqueue status path and cleanup authority without weakening the distinction between submission terminality, context health and physical-device loss.
6. Keep resource lifetime valid through terminal provider completion; cancellation request is not terminal cancellation.
7. Continue the static audit for races, identity stability and teardown ordering. Do not perform local builds/tests/debugging in this management workflow; use actual owner/CI evidence.
8. Update the upstream continuation handoff and this summary after a substantive source change.

### Acceptance evidence required

- Exact revision builds and passes portable C++ tests on supported CI platforms.
- CUDA fake-runtime tests cover fatal enqueue, fatal discovery, context poisoning, admission closure, in-flight operation drain and cleanup dispositions.
- CUDA hardware acceptance on an actual supported NVIDIA device.
- Metal build/provider tests and a separate Apple hardware lane for native execution.
- Installed-package consumer tests for exported CMake packages.
- Provider route/backend conformance across the declared operation/format profiles.

A skipped hardware job is not hardware acceptance. A passing fake runtime is not a physical GPU result.

## 7. Authoritative records

- [ImageProcessing README](https://github.com/Shennagyp/ImageProcessing/tree/wp1-target-inventory)
- [Continuation handoff](https://github.com/Shennagyp/ImageProcessing/blob/wp1-target-inventory/docs/project_management/Continuation-Handoff.md)
- [Current execution status](https://github.com/Shennagyp/ImageProcessing/blob/wp1-target-inventory/docs/project_management/Current-Execution-Status.md)
- [Implementation status](https://github.com/Shennagyp/ImageProcessing/blob/wp1-target-inventory/docs/project_management/Implementation-Status.md)
- [C++ implementation specification](https://github.com/Shennagyp/ImageProcessing/blob/wp1-target-inventory/docs/project_management/Cpp-Implementation-Specification.md)
- [C++ architecture](https://github.com/Shennagyp/ImageProcessing/blob/wp1-target-inventory/docs/research/architecture/ImageProcessing-Architecture.tex)
