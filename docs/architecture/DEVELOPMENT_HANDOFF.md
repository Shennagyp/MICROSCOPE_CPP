# Development Continuation Handoff — AmScope Native Driver

**Last reviewed:** 22 November 2026  
**Repository:** `Shennagyp/MICROSCOPE_CPP`  
**Active branch:** `amscope-native-driver`  
**Last verified branch head before this handoff refresh:** `b5fdd7c1e1b261e9f9c5008d525a76745353dd4a`  
**Last implementation-bearing commit:** `41d74718fa9a0be37060889b5d2bfcdbd6892e0f`  
**Current phase:** Phase 0 — baseline freeze and verification  
**Phase status:** OPEN — the initial camera open/live preview succeeded, but the maximum-rate/zero-copy changes have not yet been built and retested on physical hardware; the complete baseline record is outstanding.

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
3. Phase 0 remains OPEN until the latest performance changes pass CI and the user repeats the 640×480 physical-camera test, followed by the complete baseline regression.
4. Do not begin Phase 1 native-driver lifecycle refactoring until Phase 0 is accepted. The required implementation work for the FPS regression is already recorded in the completion plan; the next intervention gate is the user's physical-camera retest.
5. For every phase, keep the mandatory physical-camera test gate. A phase with code and green CI but no passing physical-camera record remains OPEN.

## 2. Current Git state

### Branch head and recent work

The last verified branch head before this handoff refresh is `b5fdd7c1e1b261e9f9c5008d525a76745353dd4a`. The last implementation-bearing commit is `41d74718fa9a0be37060889b5d2bfcdbd6892e0f`; the current performance work changes the preview presentation/telemetry path, frame handoff, descriptor read caching, and Amcam Start-time maximum-rate configuration. It preserves the native callback -> bounded event queue -> single `Amcam_PullImageV4` worker. The latest README and completion-plan entries record the implementation and outstanding verification. This handoff refresh is documentation-only.

### Relationship with `main`

The current GitHub comparison reports `amscope-native-driver` as 202 commits ahead and 2 commits behind `main`, with merge base `a11bec3a1fe6e7ec481ec00de775e69a98f8e1aa`. The two commits on `main` after that merge base are:

1. `f364b7b458f752f4bc0b340e61127ece32124a9c` — adds an unintended 66-line `Camera/Amscope/include/amscope/CameraTypes.h`.
2. `0edf623c4227b15a3d97a55b59ef4cccead8ba58` — reverts that exact file addition.

Those two commits have no net file change to import. The comparison also shows the branch's Camera/Amscope, Camera/Common, workflow, and documentation deliverables as additions relative to the merge base; this is the branch's substantive work, not an unexplained source delta. Do not blindly merge or cherry-pick the two main-only commits. Recheck the comparison if `main` advances later.

Relevant links:

- [Active branch](https://github.com/Shennagyp/MICROSCOPE_CPP/tree/amscope-native-driver)
- [Branch head before this handoff refresh](https://github.com/Shennagyp/MICROSCOPE_CPP/commit/b5fdd7c1e1b261e9f9c5008d525a76745353dd4a)
- [Comparison with main](https://github.com/Shennagyp/MICROSCOPE_CPP/compare/main...amscope-native-driver)

## 3. CI and test evidence

The performance implementation has triggered several path-filtered CI runs; older runs were cancelled by the workflow's `cancel-in-progress` concurrency rule as later commits were pushed. The latest source-containing run at the time of this handoff is [#45058730726](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45058730726), associated with `ee700a1016e4bf108255daab67cedb93b96bee83`; both Apple Silicon arm64 and Intel x86_64 were still in progress at the last status check. The follow-up SDK error-code correction and documentation commits trigger newer runs. Check the run associated with the latest branch head before requesting the user's build or claiming CI success. No local build or CTest run has been performed during this work, and no physical-camera retest has yet been reported.

The successful workflow run executes configuration and compilation of the native driver, Qt adapter, and preview application; CTest; SDK/library/application artifact and architecture checks; launcher syntax; and launcher smoke test.

All eight CTest tests passed on both architectures in completed run #45051829571:

1. `amscope_camera_types_tests`
2. `amscope_acquisition_frame_gate_tests`
3. `amscope_profile_tests`
4. `amscope_event_queue_tests`
5. `amscope_frame_stall_watchdog_tests`
6. `amscope_qt_request_tests`
7. `camera_common_feature_tests`
8. `camera_common_frame_rate_tests`

The launcher reported `0 camera(s) found` in the hosted CI environment. This proves the no-device enumeration path only. It does not prove physical camera enumeration, acquisition, control readback, RAW/still capture, Stop/Close timing, accessory operation, or recovery.

No local C++ build/test run was performed during this continuation review. The user reports a successful physical camera open and live preview, but this is only an initial smoke observation and is not a complete hardware qualification record.

## 4. Actual implementation state at the last source revision

The original native acquisition and session-lifecycle implementation remains based on `a3a03fbd`; performance changes preserve its callback-to-pull acquisition mechanism. The current implementation adds:

- Direct zero-copy preview: `FrameView` wraps the immutable frame-pool bytes in a non-owning `QImage` and paints with `QPainter`. It no longer performs a per-frame `QImage::copy()`, `QPixmap` conversion, or pixmap allocation. The view retains the `FramePtr` lease until replacement.
- A C++17 atomic-shared-pointer latest-frame mailbox replaces the mailbox mutex and does not accumulate stale frames. Frame notifications bypass the feature-request queue mutex.
- Feature descriptor discovery is cached per camera state. Individual feature reads reuse the inventory instead of repeating every SDK feature probe under the SDK mutex. The preview does not rebuild the inventory when entering Streaming; setting editors are disabled until Stop.
- The driver counts `AMCAM_EVENT_IMAGE` callbacks and successful `Amcam_PullImageV4` calls with independent atomics. The UI separately measures actual painted frame sequences. The one-second monitor does not call the SDK or acquire the SDK-call mutex. It displays event FPS, pull FPS, paint FPS, exposure time, dimensions/sequence, and pool drops.
- Every Start clears the generic SDK FPS limit with `AMCAM_OPTION_FRAMERATE=0`. After `Amcam_StartPullModeWithCallback` succeeds, it sets the model-specific maximum: maximum precise FPS plus 100% bandwidth for precise-rate models, or maximum model `maxspeed` through `Amcam_put_Speed` otherwise. Low-noise mode is disabled when the model advertises it. This follows ToupTek's documented post-Start procedure; no arbitrary fixed FPS target is set.
- Status-label formatting was removed from the per-frame path and throttled to the one-second diagnostics update.

### Native driver

Primary files:

- `Camera/Amscope/include/amscope/AmcamCamera.h`
- `Camera/Amscope/src/AmcamCamera.cpp` (about 5,105 lines)
- `Camera/Amscope/src/AmcamEventQueue.h`
- `Camera/Amscope/include/amscope/detail/FrameStallWatchdog.h`
- `Camera/Amscope/include/amscope/detail/AcquisitionFrameGate.h`
- `Camera/Amscope/include/amscope/AmcamCameraProfiles.h`
- `Camera/Amscope/src/AmcamCameraProfiles.cpp`

The native driver contains enumeration/open/close/start/stop, the callback-to-pull acquisition loop, a bounded event FIFO, watchdog logic for video and triggered acquisition, frame-pool/lease handling, RAW frame metadata, still capture, and broad feature/command APIs. It now also has lock-free counter reads for image-event and successfully-pulled-frame totals. The maximum-rate configuration is performed after callback acquisition starts and before the pull worker is launched, following the vendor's documented order.

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

The preview still has one `PreviewWindow` implementation and is not yet a reusable backend-neutral UI library. Its custom `FrameView` reads the retained pool buffer directly. The UI reports event/pull/paint FPS and exposure without calling the SDK from the FPS timer; frame metadata/status text is formatted once per second.

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

- Repository/source/branch review and the implementation inventory are documented.
- The user reports that the physical camera opens and produces live preview frames.
- The initial live preview was substantially below the approximately 100 FPS previously seen at 640p. No exact model, dimensions, numerical rate, or complete baseline test record has yet been provided.
- The source has since been optimized for the reported objective: direct frame-pool rendering with a retained immutable frame lease; atomic latest-frame mailbox; no feature-queue lock on frame notifications; cached feature descriptor inventory; no full feature inventory refresh during Streaming; and no per-frame status-label formatting.
- Maximum supported rate is now selected per model after `Amcam_StartPullModeWithCallback`, following the vendor SDK procedure. The generic SDK frame-rate cap is set to zero; no arbitrary FPS target is used.
- The UI now displays event FPS, pull FPS, paint FPS, frame dimensions/sequence, exposure time, and pool drops. These counters do not call the SDK or acquire the SDK-call mutex.
- The current source changes preserve the callback -> bounded event queue -> single `Amcam_PullImageV4` worker. The earlier SDK frame-rate polling path was removed because it acquired the SDK-call mutex during acquisition.
- The completion plan and README describe the new implementation and hardware acceptance criteria.

### Phase 0 work still required

- Wait for a successful CI run for the latest branch head; no local build or test has been run for these changes.
- Pull and run the latest physical-camera build. Verify that the actual live mode is 640×480 before comparing against the 100 FPS target.
- Record event FPS, pull FPS, paint FPS, exposure microseconds, frame dimensions, sequence progression, and dropped count over a fixed interval.
- Interpret the three rates: low event FPS is upstream of the pull worker; event-to-pull loss points to callback/queue/pull throughput; pull-to-paint loss points to UI rendering.
- Record actual camera manufacturer/model, SDK model code, firmware, SDK version, OS, and architecture.
- Complete Section 3.2, including safe setting readback/restoration, Stop/Start, Close/reopen, still capture where supported, and the required 15-minute continuous acquisition interval.
- Save the complete hardware record under `docs/testing/hardware/`.
- Keep Phase 0 OPEN until the performance retest and complete hardware record pass.

## 8. Immediate next actions

### Required before Phase 1 refactoring

1. Check the latest GitHub Actions run for the current branch head. Do not run the physical test with an unbuilt or older commit.
2. Pull and build the latest branch, then open the camera. Set its preview resolution to 640×480 while stopped using the `AmcamResolutionIndex` feature.
3. Run the preview for 15 seconds and record event FPS, pull FPS, paint FPS, exposure microseconds, frame dimensions, sequence progression, and dropped count. The top-toolbar resolution selector is still-capture-only.
4. Compare the stage rates before changing any additional code. If event FPS is below target, examine exposure and camera/transport limits; if event FPS is high but pull is low, examine callback/queue/pull; if pull is high but paint is low, examine UI rendering.
5. After the rate target is achieved, complete the full Section 3.2 regression and save the hardware record. Keep Phase 0 open until it passes.

### Work that can proceed after/beside the physical baseline without altering behavior

- Complete the source-to-test mapping in [the implementation inventory](AMSCOPE_IMPLEMENTATION_INVENTORY.md).
- Review the C++ source against the Swift behavior and identify untested native SDK call paths.
- The Phase 1 internal SDK-call seam has been statically scoped and documented in [the native-driver test seam design](AMSCOPE_NATIVE_DRIVER_TEST_SEAM.md). Implement it only after Phase 0 hardware acceptance; it must remain private to implementation/testing and must not change the public camera abstraction.
- Design the common model extraction as a type migration with explicit AmScope adapters.
- Identify the external GenTL/GenApi C++ runtime and its supply/version/licensing constraints. Do not create a replacement implementation.

### First Phase 1 source change after Phase 0 acceptance

Implement the private SDK dispatch seam in [AMSCOPE_NATIVE_DRIVER_TEST_SEAM.md](AMSCOPE_NATIVE_DRIVER_TEST_SEAM.md), then add deterministic native-session tests for:

1. Open failure and idempotent terminal Close.
2. Start setup failure and StartPull callback-start failure.
3. Worker creation failure with both successful and failed native Stop.
4. Recoverable Stop failure while Streaming; prove the same worker, callback context, queue, and frame pool remain live.
5. Successful Stop, callback drain, worker join, and frame-resource release.
6. Cache flush failure, retained obligation, and retry-before-Start.
7. Terminal Close after failed Stop, independent of Stop/flush success.
8. Fatal SDK event versus user Stop transition ownership.
9. Still-capture timeout/failure quarantine and the successful Stop/Start reset boundary.

Keep the test seam private to implementation and test support. Do not add a second public camera abstraction, second `HAmcam`, second SDK-call lock, or a different acquisition mechanism.

The physical AmScope regression remains mandatory after those code changes. The Phase 1 record must identify the exact camera/model and tested commit, include the baseline procedure, and measure the required 15-minute acquisition interval.

## 8. GenTL/GenApi dependency assessment

The dependency investigation is recorded in [AMSCOPE_GENTL_GENAPI_CPP_INTEGRATION_ASSESSMENT.md](AMSCOPE_GENTL_GENAPI_CPP_INTEGRATION_ASSESSMENT.md).

Findings:

- EMVA's current package is GenICam 2026.07: GenApi 3.5.1 and GenTL 1.6, among other modules.
- GenApi 3.5.1 has macOS Runtime/SDK packages; its release notes say a CMake package configuration file was added. Binary slices and CMake target metadata still need direct inspection.
- The EMVA GenTL deliverable is the standard and C ABI header. A camera/transport vendor supplies the actual .cti Producer. The standard package is not a generic Consumer library.
- AMSCOPE_DRIVER/macos-version-0.4 contains an existing tested Swift GenTL Consumer (GenTLNative plus API/router and Amcam adapter), but it does not expose a C++ API or C ABI facade.
- The Amcam adapter does not fabricate an AmScope CTI Producer or GenApi XML/register interface. Keep the AmScope native SDK backend independent.
- Phase 5 requires a deliberate Consumer integration choice: bridge the Swift Consumer through C ABI, approve a C++ port of its Consumer logic, or identify another existing C++ Consumer. No choice is made in the assessment.

The current phase remains Phase 0. The GenTL investigation proceeds in parallel and does not waive the physical AmScope baseline gate.

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
