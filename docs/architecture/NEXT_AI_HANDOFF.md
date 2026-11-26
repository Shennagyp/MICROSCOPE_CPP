# Camera v2.0 — Next AI Handoff

> Read this file and `CAMERA_V2_DEVELOPMENT_STATUS.md` before changing source.
> The status ledger is the phase-history authority; this file is the operational continuation guide.

**Repository:** `Shennagyp/MICROSCOPE_CPP`  
**Branch:** `camera-v.2.0`  
**Latest package implementation commit:** `643319ad6e017748e5973254bf8919e64c115d21`  
**Latest prior implementation CI:** [workflow #45770024248](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45770024248) — GenTL bridge and official GenApi 3.5.1 build/integration tests passed on Intel x86_64 and Apple Silicon arm64.  
**Phase status:** Phase 3 accepted; Phase 4 source/offscreen checks complete with visual acceptance pending and physical MU503 test postponed; Phase 5 complete by owner direction; Phase 6 open but blocked by the AmScope freeze; Phase 7 active by explicit owner authorization on 26 November 2026. Package CI is pending.

---

## 1. Continuation authority

The project owner explicitly corrected the previous continuation attempt:
- Original Camera Module Phases 0–8 remain governing.
- Qt is optional as a dependency, not a removed deliverable. Original Phase 3 (Qt streaming) and Phase 4 (reusable Qt UI) remain in scope.
- Phase 3 is accepted. Phase 4 source/offscreen work is complete; visual/workspace acceptance remains pending and the physical MU503 test is postponed by owner direction. Phase 4 is not marked accepted, but its pending acceptance does not block Phase 6 software work.
- Phase 5 GenTL/GenApi implementation is complete by owner direction; do not restart it.
- Phase 6 remains open. Its provider-neutral GenTL/GenApi work is complete for the current public contract; AmScope provider-level feature/command/model tests remain blocked by the explicit AmScope freeze.
- The owner explicitly authorized Phase 7 packaging and external-consumer work on 26 November 2026, without lifting the AmScope freeze. Phase 7 may modify CMake packaging metadata, package config, consumer fixtures, CI and documentation.
- **AmScope provider source, tests, public contracts and runtime behavior remain frozen. Do not write to `OPTIKA-B293-DUALSLIDE-APP-v.0.9`. Continue only on `camera-v.2.0`.**
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
| 4 — Reusable Qt UI | **SOURCE/OFFSCREEN COMPLETE; OWNER ACCEPTANCE PENDING** | FrameView, CameraControlPanel, ImageInfoBar, CameraFeatureEditor, CameraCommandPanel and CameraCapabilityPanel exist; the AmScope preview uses the feature editor, telemetry strip and model capability panel. CI passes 24/24 tests per architecture, including the application-level offscreen composition/dock-state smoke. Visual/workspace acceptance remains pending; physical MU503 UI test is postponed. Do not mark Phase 4 accepted, but do not block Phase 6 software work on it. |
| 5 — GenTL/GenApi integration | **Complete by owner direction** | Do not restart, duplicate, or replace the integration. External device qualification is separate Phase 8 evidence. |
| 6 — Capability, feature, command and model coverage | **OPEN; BLOCKED AT FROZEN AMSCOPE TESTS** | Provider-neutral GenTL/GenApi work and discovery/vector decisions are complete for the current public contract. AmScope provider-level feature/command/model tests remain blocked. Do not modify AmScope provider source, tests, contracts or behavior; do not mark Phase 6 complete. |
| 7 — Packaging and external consumers | **ACTIVE BY OWNER AUTHORIZATION (26 NOV 2026)** | Commit `643319a` adds the package entry point, exports, component-aware config, relocation consumers and a two-architecture workflow. CI is pending. Keep the AmScope freeze intact. |
| 8 — Hardware qualification and release | Open | Preserve as a separate evidence/release milestone. |

The immediate task is Phase 7 packaging and external-consumer verification on `camera-v.2.0`. Preserve the existing preview and reusable UI. Keep Phase 4 visual acceptance pending and physical MU503 testing postponed. Phase 6 remains open at the frozen AmScope provider-test boundary; do not modify AmScope provider source, tests, contracts or runtime behavior.

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

The separate Optika application owns its own presentation, UI, workspace, microscope stage/controller integration, slide mapping/scanning and image-processing workflows. This is separate from the AmScope Qt preview application in this repository, which is a Phase 4 deliverable and must be preserved.

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

## 5. Phase 4 acceptance and Phase 6 boundary

Phase 4 now includes CameraFeatureEditor in the preview feature tabs,
ImageInfoBar in the status bar, CameraCapabilityPanel in Camera Properties,
and the generic CameraCommandPanel for ICameraCommandAccess commands. The
AmScope preview's parameterized commands remain host-owned. The current Qt CI
checkpoint builds amscope_preview and passes 24/24 CTest tests on both
architectures. An application-level offscreen test also checks the document,
dock, status-bar, Output and capability-panel composition plus dock-state
save/restore. Only human visual inspection and current-revision physical
MU503 UI smoke remain; CI is not a substitute.

Owner direction on 26 November 2026: postpone the physical MU503 test and
continue Phase 6 now. Phase 4 remains pending acceptance, not accepted, and is
not a blocker for software work. Continue from
[CAMERA_V2_PHASE6_CAPABILITY_AUDIT.md](CAMERA_V2_PHASE6_CAPABILITY_AUDIT.md).
The GenApi negative/failure slice is CI-green at `ea356d5`. Do not repeat the
inventory or reopen Phase 5. AmScope source/tests/contracts/behavior are frozen.


Phase 4 includes CameraFeatureEditor in the preview feature tabs,
ImageInfoBar in the status bar, CameraCapabilityPanel in Camera Properties,
and CameraCommandPanel for no-argument ICameraCommandAccess commands. The
AmScope preview's parameterized commands remain host-owned. The current Qt CI
checkpoint builds amscope_preview and passes 24/24 CTest tests on both
architectures, including the application-level offscreen composition and
dock-state restore test. Human visual inspection remains pending; the owner
postponed the physical MU503 UI smoke. Do not mark Phase 4 accepted.

The Phase 6 audit is retained in
[CAMERA_V2_PHASE6_CAPABILITY_AUDIT.md](CAMERA_V2_PHASE6_CAPABILITY_AUDIT.md).
Provider-neutral GenTL/GenApi coverage and the discovery/vector decisions are
complete for the current public contract. AmScope provider-level feature,
command and model tests remain blocked by the explicit AmScope freeze. The
owner authorized Phase 7 on 26 November 2026; this does not waive or complete
Phase 6. Do not repeat the inventory, reopen Phase 5 or modify AmScope
provider source, tests, public contracts or runtime behavior.

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
3. Continue Phase 7 by owner direction. Keep Phase 6 marked open/blocked by the AmScope freeze. Phase 4 acceptance remains pending; the physical MU503 test is postponed. Preserve the existing preview application and reusable widgets.
4. Do not modify unrelated projects, branches, hardware-control code or historical snapshots. Qt Phase 3/4 source is in scope; keep it in its existing targets and preserve the native core's Qt-free boundary.
5. Do not run local builds/tests/debugging in this environment. Use existing CI evidence; do not claim tests ran if they did not. Use/trigger CI only when authorized and appropriate.
6. Never force-push, rewrite history, delete branches or replace whole files without inspecting current contents and preserving unrelated work.
7. Make small isolated commits with descriptive messages. Update this handoff/status only when verified state changes.
8. After each change report exact files, package/target behavior, test/CI evidence, relocation result, remaining risk and next unfinished Phase 7 item. Keep this handoff and the status ledger synchronized.
9. If source contradicts this handoff, stop before destructive changes; inspect history and document the discrepancy.
10. Do not ask the owner to repeat information already recorded here. Continue the next concrete item in the existing plan.

## 9. Immediate next action — Phase 7

Owner authorization is explicit. Do not ask again whether Phase 7 may start.

1. Use `Camera/CMakeLists.txt` as the reusable package entry point. The
   repository root remains the application build.
2. Export `Camera::Common`, `Camera::GenTLConsumerBridge`, optional
   `Camera::GenTLGenApi`, `Amscope::Driver`, optional `Amscope::Qt`,
   and optional `Camera::Qt` / `Camera::QtWidgets`.
3. Keep components and dependencies selective. Common, GenTL and native
   AmScope consumers must not acquire Qt. GenApi exports must depend on the
   official GenApi 3.5.1 CMake package, not absolute workstation paths.
4. Configure external consumers from `Camera/tests/consumers` against an
   installed and relocated prefix. Do not pass source-tree include paths.
5. Verify package build, tests, install, relocation and clean consumer builds
   on Intel x86_64 and Apple Silicon arm64. Do not claim a pass until the
   exact workflow run completes.
6. Keep Phase 6 open/blocked at the frozen AmScope provider-test boundary.
   Do not modify AmScope provider source, tests, public contracts or runtime
   behavior. Phase 7 CMake packaging metadata is the only AmScope-area change
   authorized by this scope.
7. Keep Phase 4 visual/workspace acceptance pending and physical MU503 testing
   postponed. Phase 8 physical/provider qualification and SDK redistribution
   review remain separate.

Package exports and mock consumers do not qualify a physical camera, vendor
CTI Producer or SDK redistribution.
