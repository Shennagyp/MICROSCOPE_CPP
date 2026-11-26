# Camera v2.0 — Next AI Handoff

> Read this file and `CAMERA_V2_DEVELOPMENT_STATUS.md` before changing source.
> The status ledger is the phase-history authority; this file is the operational continuation guide.

**Repository:** `Shennagyp/MICROSCOPE_CPP`  
**Branch:** `camera-v.2.0`  
**Latest reviewed implementation checkpoint:** `a445bfa7a9d01ade09782dd7ad91e1c6c8775f50`  
**Latest CI:** [workflow #45744057767](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45744057767) — non-Qt and Qt builds passed on Intel x86_64 and Apple Silicon arm64; Qt preview built and 21/21 CTest passed per architecture.  
**Phase status:** Phase 3 accepted; Phase 4 in progress; Phase 5 complete by owner direction; Phase 6 follows Phase 4. Recheck the live branch tip before each session.

---

## 1. Continuation authority

The project owner explicitly corrected the previous continuation attempt:
- Original Camera Module Phases 0–8 remain governing.
- Qt is optional as a dependency, not a removed deliverable. Original Phase 3 (Qt streaming) and Phase 4 (reusable Qt UI) remain in scope.
- Phase 3 is accepted; Phase 4 is the current critical-path phase and remains open for visual/workspace and physical MU503 UI verification.
- Phase 5 GenTL/GenApi implementation is complete by owner direction; do not restart it.
- Phase 6 capability/feature/command/model coverage follows Phase 4. Two preliminary AmScope corrections and an initial audit are already recorded.
- The AmScope preview application exists in Camera/Amscope/app/main.cpp; do not treat it as obsolete or exclude it from the project.
- The retired AMSCOPE_DRIVER project/repository is not a current dependency or source of truth. Do not inspect, revive, clone, build against, or modify it.

If older documents conflict with this owner direction or the current status ledger, do not silently follow the older instruction. Record the conflict.

## 2. Phase status

| Phase | Status | Instruction |
|---|---|---|
| 0 — Baseline | Historically accepted | Do not reopen without a concrete regression. |
| 1 — Native AmScope driver | Historically accepted; hardening evidence remains | Do not rewrite. Reopen only for a reproduced defect or a specific Phase 6 gap. |
| 2 — Common camera contracts | Implemented substantially; residual hardware evidence remains | Reuse existing contracts. Change only for a demonstrated semantic defect. |
| 3 — Qt streaming separation | **Accepted by owner** | Preserve the one-session adapter and backend-neutral frame-stream boundary. |
| 4 — Reusable Qt UI | **IN PROGRESS** | Reusable widgets and preview app exist. CameraFeatureEditor is now integrated into feature tabs. Complete visual/workspace and physical MU503 UI smoke before accepting Phase 4. |
| 5 — GenTL/GenApi integration | **Complete by owner direction** | Do not restart, duplicate, or replace the integration. External device qualification is separate Phase 8 evidence. |
| 6 — Capability, feature, command and model coverage | **OPEN; preliminary work exists** | Two AmScope corrections and the initial audit are recorded. Continue after Phase 4 acceptance from CAMERA_V2_PHASE6_CAPABILITY_AUDIT.md. |
| 7 — Packaging and external consumers | Open | Do not jump ahead of Phase 6. |
| 8 — Hardware qualification and release | Open | Preserve as a separate evidence/release milestone. |

The immediate task is not “add more GenTL” and not “remove Qt.” Preserve the existing preview and reusable UI. Finish Phase 4 acceptance, then determine whether the public API accurately describes what each provider and supported camera model can actually do.

## 3. Repository boundaries

- `Camera/Common`: backend-neutral identity, capabilities, frame/layout types, feature/command contracts, validation and common frame-delivery primitives.
- `Camera/Amscope`: native AmScope C++ provider. `AmcamCamera` owns the AmScope SDK session and acquisition lifecycle.
- `Camera/GenTL`: GenTL 1.6 Consumer-side implementation, Producer/device/data-stream lifecycle, frame projection, remote-port/XML access and optional GenApi 3.5.1 adapters.
- Camera/Qt: optional Qt Core frame-stream library.
- Camera/QtWidgets: optional backend-neutral reusable widgets, including CameraFeatureEditor.
- Camera/Amscope/src/QtAmcamCamera.cpp and Camera/Amscope/app/main.cpp: AmScope Qt adapter and preview composition. The adapter composes one native session owner; the application does not open a second camera handle.
- Root CMake and CI: configure/build/test/install boundaries and cross-architecture validation.
- `docs/architecture/CAMERA_V2_DEVELOPMENT_STATUS.md`: current phase ledger and evidence history.
- `docs/architecture/AMSCOPE_IMPLEMENTATION_INVENTORY.md`: historical v0.7 inventory, not current phase authority. Do not trust its old UI/test counts without re-verification.
- `Camera/GenTL/README.md`: API usage and implementation limitations; compare with current source before relying on individual details.

The separate Optika application owns presentation, UI, workspace, microscope stage/controller integration, slide mapping/scanning and image-processing workflows. These are not Camera Module v2.0 Phase 6 responsibilities.

## 4. Invariants to preserve

1. Common abstractions must represent genuinely shared semantics. Keep provider-specific errors, state enums, commands, feature names, units and identifiers provider-specific unless another backend proves the same meaning.
2. Capabilities are evidence, not optimism. An SDK function, GenApi node or UI editor does not prove support on a specific device.
3. Model policy may narrow provider support; it must never invent SDK functionality. Combine provider capability, model policy and runtime availability explicitly.
4. Features and commands are different contracts. Settings have typed value/read/write semantics; commands have issue/completion semantics.
5. Availability, readability and writability are distinct. A successful read does not prove writability.
6. Preserve exact units and numeric domains. Integer ranges/increments remain exact `int64_t`; do not round through `double`.
7. Readback is authoritative where promised. Do not report a requested value as applied before provider acceptance and required readback.
8. Asynchronous command issue and completion polling remain distinct. Do not silently claim completion.
9. Unknown is not true or false. Preserve unknown, unsupported and not-available distinctions; do not fabricate metadata.
10. Preserve provider ownership and lifecycle. Do not create duplicate camera sessions, GenApi node maps or serialization boundaries.
11. Preserve immutable frame ownership, payload lifetime, layout validation, overflow checks and opaque handling of unsupported pixel formats.
12. Keep Qt optional to Camera/Common and Amscope::Driver, but preserve Phase 3/4 Qt targets and the AmScope preview application. Do not move Qt dependencies into the native core.

## 5. Phase 4 current work and Phase 6 follow-on

Phase 4 source now includes the reusable CameraFeatureEditor, integrated
into the AmScope preview feature tabs. The current Qt CI checkpoint builds
amscope_preview and passes 21/21 CTest tests on both architectures. The
remaining Phase 4 gate is application visual/workspace inspection and the
current-revision physical MU503 UI smoke; CI is not a substitute.

After Phase 4 acceptance, continue Phase 6 from
[CAMERA_V2_PHASE6_CAPABILITY_AUDIT.md](CAMERA_V2_PHASE6_CAPABILITY_AUDIT.md).
The initial audit and two AmScope provider corrections are already recorded;
do not repeat the inventory or reopen Phase 5.


### 5.1 Create a source-grounded coverage matrix

Inspect current public APIs and implementations for AmScope and GenTL. For every exposed item record:
- provider and API item;
- feature, command, telemetry or discovery category;
- applicable model/provider restrictions;
- value type, unit, range, increment and enum-symbol rules;
- availability/readability/writability semantics;
- preconditions and lifecycle state;
- synchronous/asynchronous behavior;
- readback/completion semantics and error mapping;
- implementation location and automated tests;
- hardware-evidence status.

Do not guess missing cells. Use explicit `unknown`, `not implemented`, `not applicable` or `hardware unverified`.

### 5.2 Audit capability resolution

Trace provider discovery through model policy to the public API. Check that provider support and per-model restrictions combine consistently; model profiles only narrow support; runtime availability is not confused with static support; unknown SDK flags are not treated as positive; and duplicate layers do not expose conflicting meanings.

### 5.3 Audit feature semantics

Check descriptor kind/value alternative consistency; units; exact numeric bounds/increments; non-finite floating-point handling; current enum symbols; availability/access modes; validation before provider calls; write/readback behavior; and correct preservation of provider-specific errors.

### 5.4 Audit command semantics

Check that commands use the command contract, not writable feature values; issue and completion polling are separate; availability/preconditions are enforced; timeout/cancellation/failure/completion remain distinct; and command issue is not misreported as physical effect.

### 5.5 Audit model coverage

Reconcile model profiles and provider discovery with actual model identifiers and SDK/GenApi evidence. Do not broaden support from family resemblance or marketing descriptions. Unknown models remain conservative.

### 5.6 Add only targeted tests

After the audit finds a concrete gap, add the smallest deterministic test at the correct layer. Test semantic boundaries and failure behavior. Mocks/XML fixtures prove deterministic software behavior, not physical-device behavior. Avoid broad refactors and unrelated cleanup.

### 5.7 Keep the evidence ledger current

For each completed slice record exact commit, source files, public behavior, tests, exact CI run/architectures, hardware evidence separately, limitations and follow-up. Never label a feature hardware-verified from mock CI.

## 6. GenTL implementation note — not permission to reopen Phase 5

At reviewed code baseline `808779f8`, both of these exist:
- `ConsumerBridge`, which is used by `Producer::open(...)`;
- `GenTLProducerLibrary`, a direct CTI symbol loader with its own CMake target/tests.

`Producer` still uses `ConsumerBridge`; the direct loader is not wired into the Producer/device/stream lifecycle. This is a known isolated source/documentation inconsistency. It does **not** authorize restarting Phase 5.

Do not automatically replace the bridge, add a second Producer, remove existing code, change the public API, or claim the direct loader is the active runtime path. If a later explicit task requires resolving this, first compare GenTL 1.6 function coverage, ABI correctness, initialization/shutdown, ownership, error handling, library lifetime and test evidence. Do not depend on the retired `AMSCOPE_DRIVER` repository.

Workflow #45733875474 proves the tested code at `808779f8` passes the configured cross-architecture mock/XML suite. It does not prove operation with a vendor CTI Producer or physical GenTL camera.

## 7. Evidence labels and qualification

Use these labels consistently:
- **Source implemented:** present in inspected source.
- **Automated test present:** a test exists; inspect its assertions and limits.
- **CI passed:** the exact commit and relevant job passed.
- **Hardware verified:** a physical-device record exists for the exact relevant implementation/configuration.
- **Owner accepted:** explicitly accepted by the project owner.
- **Deferred / unknown:** not established or postponed.

Phase 8 remains responsible for exact-commit hardware records, supported provider/model matrix, installed-consumer deployment and release acceptance. Keep Phase 8 open until its criteria are met; do not use it to reopen Phase 5 implementation.

## 8. Safe working protocol

1. Confirm current branch and HEAD before every session; the branch may have advanced.
2. Read this handoff, the status ledger, relevant source and tests before proposing a change.
3. Continue from the governing phase sequence. Phase 4 is current; use the existing preview application and reusable widgets rather than replacing or omitting them. Phase 6 audit work follows Phase 4 acceptance.
4. Do not modify unrelated projects, branches, hardware-control code or historical snapshots. Qt Phase 3/4 source is in scope; keep it in its existing targets and preserve the native core's Qt-free boundary.
5. Do not run local builds/tests/debugging in this environment. Use existing CI evidence; do not claim tests ran if they did not. Use/trigger CI only when authorized and appropriate.
6. Never force-push, rewrite history, delete branches or replace whole files without inspecting current contents and preserving unrelated work.
7. Make small isolated commits with descriptive messages. Update this handoff/status only when verified state changes.
8. After each change report exact files, behavior, test/CI evidence, remaining risk and next unfinished Phase 6 item.
9. If source contradicts this handoff, stop before destructive changes; inspect history and document the discrepancy.
10. Do not ask the owner to repeat information already recorded here. Continue the next concrete item in the existing plan.

## 9. Immediate next action

Complete the Phase 4 acceptance gate for the existing amscope_preview:
inspect the document-centric workspace, dock move/tabify/float/close/restore,
layout persistence, Output pane and status-bar information strip; then run the
physical MU503 discover/open/preview/live-feature-readback/stop/close smoke.
Do not mark Phase 4 complete from CI alone. After that, continue Phase 6 from
the recorded audit. Do not add a public SDK interface, change session ownership,
or modify the completed GenTL integration.
