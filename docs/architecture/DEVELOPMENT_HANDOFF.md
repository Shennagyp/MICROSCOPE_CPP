# Development Continuation Handoff — AmScope Native Driver

**Last reviewed:** 22 November 2026  
**Repository:** `Shennagyp/MICROSCOPE_CPP`  
**Active branch:** `amscope-native-driver`  
**Exact branch head:** `3858c29a8e770901bc6baf414df9410e44d8fd57`  
**Last implementation-bearing commit:** `a3a03fbd1fe652fd4c1dfa8d2082ef86a2972f13`  
**Current phase:** Phase 0 — baseline freeze and verification  
**Phase status:** OPEN — real-camera baseline has not been recorded.

This is the continuation entrypoint for subsequent development sessions. It is a state snapshot, not a replacement for the architecture papers or completion plan.

---

## 1. First actions for the next session

1. Verify that `amscope-native-driver` still points to the exact head recorded above. If it has moved, inspect the new commits and update this handoff before relying on the state below.
2. Read this file, then:
   - [AmScope Native Driver Completion Plan](AMSCOPE_NATIVE_DRIVER_COMPLETION_PLAN.md)
   - [Implementation Inventory](AMSCOPE_IMPLEMENTATION_INVENTORY.md)
   - [Swift-to-C++ Translation Paradigm](SWIFT_TO_CPP_TRANSLATION_PARADIGM.md)
   - [AmScope Driver README](../../Camera/Amscope/README.md)
   - The authoritative Camera Module and UI Libraries architecture paper agreed in the project discussion.
3. Do not begin behavior-changing refactoring until Phase 0's physical-camera baseline has been recorded.
4. The next meaningful implementation task after baseline acceptance is Phase 1: strengthen deterministic tests around the native session/lifecycle failure paths while preserving the current callback-to-pull acquisition mechanism.
5. For every phase, keep the mandatory physical-camera test gate. A phase with code and green CI but no passing physical-camera record remains OPEN.

## 2. Current Git state

### Branch head and recent work

The branch currently points to:

- `3858c29a8e770901bc6baf414df9410e44d8fd57` — `docs(amscope): link completion plan from driver README`
- `5dc4c51285f100c525eeabd7eea3fd0666210ac8` — `docs(amscope): add native driver completion plan`
- `a3a03fbd1fe652fd4c1dfa8d2082ef86a2972f13` — `fix(amscope): remove duplicated trigger enum boundary`

The last two commits before this handoff are documentation-only. No C++ source implementation commit was added after `a3a03fbd`.

### Relationship with `main`

At the time of this review, GitHub reports the branch as 194 commits ahead and 2 behind `main`. The two commits on `main` after the merge base are:

1. `f364b7b458f752f4bc0b340e61127ece32124a9c` — adds an unintended 66-line `Camera/Amscope/include/amscope/CameraTypes.h`.
2. `0edf623c4227b15a3d97a55b59ef4cccead8ba58` — reverts that exact file addition.

The compare from the merge base to `main` shows only this add-then-remove pair and no remaining changed file. There is no net source change from these two commits to import. Do not blindly merge or cherry-pick them. Recheck this comparison if `main` advances later.

Relevant links:

- [Active branch](https://github.com/Shennagyp/MICROSCOPE_CPP/tree/amscope-native-driver)
- [Branch head](https://github.com/Shennagyp/MICROSCOPE_CPP/commit/3858c29a8e770901bc6baf414df9410e44d8fd57)
- [Comparison with main](https://github.com/Shennagyp/MICROSCOPE_CPP/compare/main...amscope-native-driver)

## 3. CI and test evidence

The latest workflow run is [Amcam macOS build #45028702181](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45028702181), associated with the current documentation head `3858c29a`.

Both jobs passed:

- Intel x86_64 macOS.
- Apple Silicon arm64 macOS.

Each job configured and compiled the AmScope native driver, Qt adapter, and preview application; ran CTest; checked expected artifacts and architectures; syntax-checked the launcher; and ran the launcher smoke check.

The CTest suite has eight tests:

1. `amscope_camera_types_tests`
2. `amscope_acquisition_frame_gate_tests`
3. `amscope_profile_tests`
4. `amscope_event_queue_tests`
5. `amscope_frame_stall_watchdog_tests`
6. `amscope_qt_request_tests`
7. `camera_common_feature_tests`
8. `camera_common_frame_rate_tests`

All eight passed in the successful run.

**Important limitation:** the launcher reported `0 camera(s) found` in the hosted CI environment. The CI run proves compilation, the current automated tests, artifact architecture checks, and a no-device launcher path. It does not prove real-camera enumeration, acquisition, controls, RAW format, still capture, Stop/Close behavior, accessory operation, or hardware recovery.

No local C++ build, test, or hardware run was performed during the documentation/handoff work.

## 4. Actual implementation state at the last source revision

The implementation-bearing source revision is `a3a03fbd`. The current branch head adds the completion plan and its README link, but does not alter that source.

### Native driver

Primary files:

- `Camera/Amscope/include/amscope/AmcamCamera.h`
- `Camera/Amscope/src/AmcamCamera.cpp` (about 5,105 lines)
- `Camera/Amscope/src/AmcamEventQueue.h`
- `Camera/Amscope/include/amscope/detail/FrameStallWatchdog.h`
- `Camera/Amscope/include/amscope/detail/AcquisitionFrameGate.h`
- `Camera/Amscope/include/amscope/AmcamCameraProfiles.h`
- `Camera/Amscope/src/AmcamCameraProfiles.cpp`

The native driver already contains enumeration/open/close/start/stop, the callback-to-pull acquisition loop, a bounded event FIFO, watchdog logic for video and triggered acquisition, frame-pool/lease handling, RAW frame metadata, still capture, and broad feature/command APIs.

### Common feature contract

Primary files:

- `Camera/Common/include/camera/CameraFeatureAccess.h`
- `Camera/Common/include/camera/CameraFeatureValidation.h`
- `Camera/Common/include/camera/FrameRateEstimator.h`

The common feature layer supports typed Boolean, integer, integer vector, floating-point, string, and enumeration values; descriptors; typed results/errors; validation; and frame-rate estimation.

It does **not** yet contain all backend-neutral camera models. For example, `CameraInfo`, `Frame`, `Resolution`, `CameraState`, and `CameraError` are currently declared in the AmScope namespace in `CameraTypes.h`.

### Qt facade and preview

Primary files:

- `Camera/Amscope/include/amscope/QtAmcamCamera.h`
- `Camera/Amscope/src/QtAmcamCamera.cpp` (about 2,046 lines)
- `Camera/Amscope/app/main.cpp` (about 2,447 lines)

`QtAmcamCamera` currently combines Qt frame handoff with lifecycle operations, feature access, asynchronous feature requests, commands, telemetry, still capture, triggers, I/O, and accessory request wrappers. It has a bounded FIFO request worker, session and feature-state generations, and a one-frame latest mailbox.

The preview still has one `PreviewWindow` implementation. It already includes generic descriptor-driven controls, trigger UI, lens aperture/focus UI, and accessory UI, but is not yet a reusable backend-neutral UI library. The latest README also says that hardware-I/O operations have native/Qt APIs while several I/O capabilities remain outside the preview UI.

### CMake and backend scope

- `Camera/Amscope/CMakeLists.txt` explicitly rejects non-Apple builds.
- It builds `camera_amscope_driver`, `camera_amscope_qt`, and `amscope_preview`.
- It installs headers, libraries, the Amcam vendor library, and the application, but does not yet define a complete exported CMake package/config/version flow for clean external consumers.
- The branch has no GenTL/GenApi C++ headers, linked libraries, imported CMake targets, Producer `.cti` files, or CTI adapter.
- GenTL/GenApi must be integrated from the already-existing implementation. They must not be reimplemented.
- The bundled vendor SDK tree is large (about 223 MiB in the reviewed tree); its redistribution terms must be established before redistributing the full bundle.

## 5. Target architecture to preserve

The target is not two unrelated camera applications. It is one Camera Module with common application-facing identity, capability, feature, error, and frame models, backed by backend-specific implementations.

Expected library responsibilities:

| Library | Responsibility |
|---|---|
| `Camera.Common` / common camera core | Backend-neutral camera identity, capabilities, feature model, values/errors, common frame semantics |
| `camera_amscope_driver` | Native Amcam SDK, single AmScope session/handle, AmScope-specific lifecycle and acquisition |
| `camera_amscope_control` | Non-Qt programmatic feature operations, commands, telemetry on the existing session |
| `camera_amscope_streaming_qt` | Qt frame delivery and thread handoff from that same session |
| `camera_ui_qt` | Reusable backend-neutral discovery, controls, command, telemetry, and frame widgets |
| GenTL backend integration | Existing GenTL/GenApi runtime and Producer device/acquisition path |
| Preview application | Composition of common UI, backend control, and frame streaming |

The names/targets may be adjusted to the final CMake dependency graph, but the ownership model is mandatory.

**Never create a second `HAmcam` handle for the control and streaming libraries.** Both must use the same native session and one serialized SDK-call boundary. Do not move acquisition into Qt or force CTI lifecycle into AmScope's callback/pull model.

## 6. Critical behavioral invariants

- One opened AmScope session owns one `HAmcam` handle.
- Feature operations and acquisition use that same handle.
- AmScope live acquisition remains SDK callback -> bounded event FIFO -> one C++ acquisition worker -> `Amcam_PullImageV4`.
- The vendor callback only enqueues relevant events. It does not pull images, call UI, Stop/Close, or release callback resources.
- SDK calls are serialized according to the existing session locking policy. Do not introduce independent control/streaming queues that race.
- Successful Stop drains callbacks and joins the worker before releasing resources.
- A recoverable Stop failure while Streaming retains callback context, worker, buffers, and event path.
- Terminal Close remains independent of Stop/cache-flush success.
- Every successful Stop creates a both-cache flush obligation using `AMCAM_OPTION_FLUSH = 3`; clear only after SDK success; retry before the next Start.
- Session and acquisition generations prevent old callbacks/frames from leaking into new sessions.
- The Qt latest-frame mailbox is freshness-first, not a recording queue.
- Four immutable `FramePtr` leases are used; if all are retained, acquisition drains into a private discard buffer and counts dropped frames rather than blocking.
- Processed live frames are packed RGB24. RAW mode retains SDK FourCC, significant sensor depth, and storage-container width separately.
- Still capture uses the separate still event waiter and pull path; it must not be merged into the live event FIFO.
- Feature settings, commands, and telemetry are separate. Auto-white-balance-once is a command; device-measured FPS is telemetry.
- Amcam gain percent is not GenICam gain dB; configured FPS, speed level, and measured FPS are separate; Amcam pixel-format codes are not PFNC unless a mapping is proven.
- Feature availability depends on model facts, non-mutating SDK probes, exact-model allow/deny profiles, and implemented backend coverage.
- Profiles may narrow capabilities but must never fabricate hardware support.
- Triggered idle uses hardware heartbeat health; an absence of frames while awaiting a trigger is not automatically a stall.
- GenTL candidates and opened sessions stay bound to the Producer/backend context that discovered/opened them.

## 7. Phase status snapshot

The [completion plan](AMSCOPE_NATIVE_DRIVER_COMPLETION_PLAN.md) is the governing phase plan. Current status:

| Phase | Current status |
|---|---|
| 0 — Baseline freeze and verification | OPEN. Source/docs/branch review has progressed. No physical-camera baseline record exists. |
| 1 — Native driver stabilization | OPEN. Native implementation and eight tests exist; more deterministic failure-path tests and physical qualification are required. |
| 2 — Backend-neutral models | OPEN. Feature types are common; other important camera models still reside in AmScope. |
| 3 — Driver/control/Qt streaming separation | OPEN. Current Qt facade combines responsibilities. |
| 4 — Reusable Qt UI library | OPEN. Preview exists; reusable UI library is not delivered. |
| 5 — GenTL/GenApi integration | BLOCKED. Existing C++ dependency/runtime and a real CTI camera are not established in this branch. |
| 6 — Capability/feature/command coverage | OPEN. Broad support exists; full inventory and physical model matrix required. |
| 7 — Packaging | OPEN. Install rules exist; exported package and clean consumer proof are missing. |
| 8 — Release qualification | OPEN. Complete hardware matrix and release acceptance are not established. |

### Phase 0 work now recorded

- Current branch and exact head verified.
- Latest documentation-triggered CI verified green on Intel and Apple Silicon.
- The two commits missing from `main` were inspected; they add and revert the same unintended file, with no net change to import.
- Source tree and main architecture gaps reviewed.
- The completion plan, implementation inventory, and hardware test-record template are now present in the repository.

### Phase 0 work still required

- Identify the physical reference AmScope camera actually connected to the development environment; do not assume a model based on a previous shopping comparison or SDK availability.
- Run the physical-camera baseline in the completion plan.
- Save the completed record under `docs/testing/hardware/`.
- Confirm original camera settings are restored after safe reversible tests.
- Keep Phase 0 OPEN until that record passes.

## 8. Immediate next actions

### Required before Phase 1 refactoring

1. Obtain/confirm the actual physical AmScope reference camera and its connection to the development machine.
2. Run the baseline procedure in [the completion plan](AMSCOPE_NATIVE_DRIVER_COMPLETION_PLAN.md), using [the hardware record template](../testing/hardware/PHASE_HARDWARE_TEST_RECORD_TEMPLATE.md).
3. Record the exact model code, SDK version, OS/architecture, results, duration, and observations.
4. If the baseline fails, fix the baseline defect before architectural extraction.

### Work that can proceed after/beside the physical baseline without altering behavior

- Complete the source-to-test mapping in [the implementation inventory](AMSCOPE_IMPLEMENTATION_INVENTORY.md).
- Review the C++ source against the Swift behavior and identify untested native SDK call paths.
- Design the internal SDK-call seam for deterministic fault injection, without changing the public camera abstraction.
- Design the common model extraction as a type migration with explicit AmScope adapters.
- Identify the external GenTL/GenApi C++ runtime and its supply/version/licensing constraints. Do not create a replacement implementation.

### First implementation task after Phase 0 acceptance

Start Phase 1 with a focused change set:

1. Add private/test-only injection around the native calls that must fail deterministically.
2. Cover Stop failure, flush failure/retry, successful Stop callback drain, terminal Close, event worker creation failure, still-capture timeout/quarantine, and session/frame-generation races.
3. Preserve one `HAmcam`, the existing callback-to-pull mechanism, and existing FIFO operation ordering.
4. Run the automated tests.
5. Run the physical-camera regression plus the Phase 1 hardware test.
6. Update the hardware record and phase ledger before moving on.

## 9. Hardware record policy

The directory `docs/testing/hardware/` is for actual completed hardware records, not plans or placeholders.

Use [PHASE_HARDWARE_TEST_RECORD_TEMPLATE.md](../testing/hardware/PHASE_HARDWARE_TEST_RECORD_TEMPLATE.md) to create a new record for each phase/camera/date.

A hardware test record must refer to the exact tested commit. If code changes after the recorded test, do not treat the old record as proof for the new source unless the changed area is demonstrably outside the tested behavior and the record explicitly says so.

Mocks, fault injection, and CI are additional evidence. They do not replace the real-camera test required by every phase.

## 10. Handoff maintenance rule

Update this file during development, not only at milestones.

At the beginning of each continuation session:

- Verify current branch, HEAD SHA, and last implementation-bearing SHA.
- Inspect commits since the last handoff.
- Check current CI run and identify exactly which commit it tested.
- Read the latest phase ledger and hardware records.
- Reclassify work as implemented, tested automatically, real-camera tested, blocked, or unverified.

At the end of each meaningful development increment, update:

- Exact head SHA and commit link.
- Changed files and semantic effect.
- Tests actually run and their exact result.
- Physical camera/model and hardware test record, or state explicitly that no hardware run occurred.
- Current phase and exit criteria remaining.
- Next concrete implementation task and blockers.

Never write that a phase is complete from source inspection or CI alone. Never invent camera model identity, hardware results, test duration, or SDK behavior.
