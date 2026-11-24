# Development Continuation Handoff — AmScope Native Driver

**Last reviewed:** 24 November 2026  
**Repository:** `Shennagyp/MICROSCOPE_CPP`  
**Active branch:** `amscope-native-driver-v0.7`  
**Physical run source revision:** `2da3f5d3503f7bf6494801b8876c96a710f0f240`  
**Phase 1 local-test revision:** `d2e5cb37b6c71ba494ff6805361fd94d83a38521`  
**Phase 2.1 local/CI validation revision:** `03f35bf07d8236e5aab33aea0cde987155cf0425`  
**Phase 2.2 frame-model source snapshot:** `3da2a385508f1b639f98bbd34f5a6dabd9a3406a`  
**Preview migration fix:** `609174c3ad56b50b25465e5688672627ce315a3f`  
**Fixed-8-bit RAW correction:** `a16ae3bd77c56b14384736aa457e859c73a8072f`  
**Common RAW 8/16-bit selector:** `3ef46d3a5b85aa7ae2e22d160d92f61aecace5bb`  
**Latest CI for active branch:** No CI result has been reported for the v0.7 Phase 4 source commit; earlier workflow runs in the historical notes below predate this extraction.  
**Latest owner-reported local validation branch head:** `040e3810a4df83e851a3d17f066f30df6d823512` (after the fast-forward from `89ccf0c`, the Release build compiled the document-centric workspace and linked `amscope_preview`; all 15/15 CTest tests passed). The lifecycle-state switch warning remains resolved.  
**Current CI:** Not reported for the active v0.7 Phase 4 source commit.  
**Latest Phase 4 physical smoke checkout:** `5339c441659b45a3bdb30fdce6971f6dc991f885` (MU503 FrameView preview, Open/Start/Stop/Close; record `docs/testing/hardware/phase-4-mu503-frame-view-smoke-2026-11-24.md`)  
**Current phase:** Phase 4 — extract reusable Qt camera UI  
**Phase status:** Phases 0–3 are accepted by the project owner. Phase 4 FrameView has passed its owner-reported build/test and MU503 preview smoke. The prior compact dock/live-edit workspace and lifecycle-transition mapping built successfully at `89ccf0c`, with 15/15 CTest passing twice. A new document-centric shell is now in source: central Live Preview document tab, separate Camera Explorer and Camera Properties panes on the right, a bottom Output tool window, and an editable Imaging Information strip in the real status bar. The owner subsequently built branch head `040e381` in Release; `amscope_preview` linked and all 15/15 CTest tests passed. The source build/test gate is now PASS. The log does not show the application being launched after this pull, so visual and physical UI verification remains pending. No backend, SDK operation, or camera feature was added.

This is the continuation entrypoint for subsequent development sessions. It is a state snapshot, not a replacement for the architecture papers or completion plan.

## Current continuation state — 24 November 2026

- Active branch: `amscope-native-driver-v0.7`.
- Latest integrated source before the current workspace redesign: `89ccf0c53f6a410097af5effaa2ec421f23eee6b`. This version passed the owner's Release build and **15/15 CTest** twice, with no compiler warning. Panel tests include all six backend lifecycle states and verify that `Starting`/`Stopping` disable every control.
- Current workspace source commit: `b99ca65d09e78a615d4bcf44f472e5119bec0600` (preceded by UI composition commits `e2450a2`, `5f56a5e`, `ae2440d`, and `49cb8a7`; `4e9130f` orders File before View; `b99ca65` restores 9 pt base typography and polishes toolbar hover/pressed states). It converts the preview to a document tab, separates Camera Explorer and Camera Properties into stacked right-side docks, adds a bottom Output tool window with bounded timestamped event history, moves the editable ImageInfoBar into the real QStatusBar, adds a camera-state indicator, adds File/View/Workspace commands and layout reset, and saves/restores the new state as version 2. **The owner subsequently built the current branch head `040e381` in Release; `amscope_preview` linked and all 15/15 CTest tests passed.** The prior `89ccf0c` result predates this shell; the new successful build/test result validates compilation and registered automated tests. Actual application rendering and physical-camera operation remain unverified.
- Previously accepted FrameView physical check: owner confirmed the MU503 live preview sequence on checkout `5339c441659b45a3bdb30fdce6971f6dc991f885`; record `docs/testing/hardware/phase-4-mu503-frame-view-smoke-2026-11-24.md`. This validates the earlier FrameView extraction only, not the current dock/edit behavior.
- The main UI is now document-centric: the central tab contains Live Preview; Camera Explorer and Camera Properties are separate stacked right-side tool windows; Output is a docked bottom tool window. Each tool window can move, tabify, float and close, and its toggle action appears in View.
- File → Camera Workspace raises the Camera Explorer and Camera Properties panes. The Workspace toolbar exposes the same workspace action and Reset Window Layout. View exposes each tool-window toggle and Reset Window Layout.
- Camera Explorer owns device discovery, Open/Start/Stop/Close and still-capture controls. Camera Properties owns the existing feature tabs, trigger settings and accessory panels. These are existing widgets and callbacks; no second camera/session owner is introduced.
- Output is a read-only, 2,000-line-bounded application message history with timestamps. Transient application messages continue to populate the editable ImageInfoBar, which is now a permanent widget in the actual QStatusBar. A distinct status indicator reports Closed/Open/Starting/Streaming/Stopping/Faulted; the UI does not overload the imaging metadata with connection state.
- QSettings persists geometry and the new dock arrangement using state version 2. The version bump intentionally rejects old layout snapshots so the previous one-dock workspace cannot corrupt the new pane arrangement.
- The generic ImageInfoBar remains backend-neutral: it renders host-supplied frame metadata and feature values, emits edit intent, and has no camera/descriptor/write dependency of its own.
- Existing AmScope descriptors correctly classify Automatic Exposure, Exposure Time and Analog Gain as Exposure, and Color Matrix/White Balance as Color. During Streaming only readable/writable Exposure and Color items may be edited. Exposure Time requires Automatic Exposure Off. The existing backend validates each write, and display values update after queued write/readback.
- No new camera feature, saturation support, SDK operation, backend, worker, or acquisition path was added. The UI styling/layout changes do not alter frame processing.
- The predecessor software gate passed at `89ccf0c` with 15/15 CTest twice. The owner then fast-forwarded the branch to `040e381`, built `build/amscope-v07` in Release, linked `amscope_preview`, and passed **15/15 CTest**. The current document-centric workspace software gate is PASS.
- **Still pending:** launch the newly built `amscope_preview` and inspect actual rendering, pane proportions, docking/tabification/floating, layout persistence, and MU503 discovery/streaming/live feature editing. Do not infer these from build or offscreen widget tests.
- **Next local gate:** fast-forward to the current branch and run the existing Release build plus full CTest suite. Require `amscope_preview` to link and all 15 tests to pass. Do not use an old CTest binary when the build step fails.
- **Then perform visual and physical UI smoke:** verify document tab, Explorer/Properties split, bottom Output pane, status-bar imaging values, state indicator, dock movement/tabification/floating/closing/restoring, and layout persistence. Open the MU503, confirm changing frames, test supported live Exposure/Color edits and readback, Stop and Close. Check that the Output history records action/result messages and remains bounded.
- No frame-path, driver, SDK, worker, or backend changes are part of this shell redesign. The previous 15/15 result does not validate the new `main.cpp` composition.

- After the software gate, physical smoke-check the new dock workspace: move/float/close both docks and restore them through View; restart and verify the saved dock layout is restored. Confirm the central preview remains unobstructed and only one Imaging Information surface exists. Use File → Camera Controls to raise the right dock, discover/select the MU503, Open and Start. Verify compact text/control sizing is legible and the info dock is not clipped. Test Automatic Exposure, Analog Gain and White Balance edits while streaming when descriptors permit. Set Automatic Exposure to Off before testing Exposure Time. Verify readback updates, Stop, then check stopped-only settings and Close.
- No FPS benchmark, new camera capability, or backend qualification is implied. Keep the acquisition/frame path unchanged.
- After this UI increment passes, the next reusable-UI slice is the descriptor-driven feature editor. Keep request ordering, generations, feature validation, readback policy, camera ownership and backend execution in the host/application layer unless a separate generic UI contract is justified.
- Do not mark Phase 4 complete until the remaining reusable UI components and its phase-wide exit criteria are met.

> The sections below retain the earlier v0.6 handoff snapshot and historical evidence. Where their branch, phase, or next-action statements conflict with the current continuation state above, this current section and the revised release plan take precedence. The acquisition, session-ownership, frame-lease and performance invariants remain applicable unless the current source and governing plan explicitly supersede them.

---

## 1. First actions for the next session

1. Verify that `amscope-native-driver` still points to `9daf193c7a107ae93984f5bf00f56d0dfbd66b3b` or a reviewed descendant. Inspect any newer commits before relying on this state.
2. Read this file, then:
   - [AmScope Native Driver Completion Plan](AMSCOPE_NATIVE_DRIVER_COMPLETION_PLAN.md)
   - [Implementation Inventory](AMSCOPE_IMPLEMENTATION_INVENTORY.md)
   - [Swift-to-C++ Translation Paradigm](SWIFT_TO_CPP_TRANSLATION_PARADIGM.md)
   - [AmScope Driver README](../../Camera/Amscope/README.md)
   - The authoritative Camera Module and UI Libraries architecture paper agreed in the project discussion.
3. Phases 0 and 1 are accepted by the project owner. Do not reopen them solely because the requested 100 FPS target was not reached; that target is a performance objective, not a lifecycle correctness threshold.
4. Phase 2 is in progress. Slices 2.1 and 2.2 have passed local clean builds/CTest and cross-architecture CI. Slice 2.2 still requires physical-camera frame/metadata/RAW regression; after that, slice 2.3 classifies remaining common errors and value types.
5. Keep the remaining 15-minute hardware run, repeated lifecycle cycles, still-capture test where supported, and full SDK fault injection as explicit follow-up coverage. Formal `Hardware verified` status still requires an exact-commit record under `docs/testing/hardware/`.

## 2. Current Git state

### Branch head and recent work

The physical-run source was `2da3f5d`; the latest diagnostic source is `a54c56c`. The hardware test preserves the native callback -> bounded event queue -> single `Amcam_PullImageV4` worker. It uses host-delivery revisions rather than SDK sequence metadata for preview frame identity. The latest diagnostic revision reports `ExposureAuto` and `ExposureTime` before Start, after Start, and at timed measurement start/end. It also reports preview timer polls, host-frame submissions, Qt paint events, and painted frames separately. The exposure-derived FPS ceiling is populated only when manual exposure is stable during the timed interval. No native acquisition mechanism was changed.

### Relationship with `main`

The current GitHub comparison reports `amscope-native-driver` as 202 commits ahead and 2 commits behind `main`, with merge base `a11bec3a1fe6e7ec481ec00de775e69a98f8e1aa`. The two commits on `main` after that merge base are:

1. `f364b7b458f752f4bc0b340e61127ece32124a9c` — adds an unintended 66-line `Camera/Amscope/include/amscope/CameraTypes.h`.
2. `0edf623c4227b15a3d97a55b59ef4cccead8ba58` — reverts that exact file addition.

Those two commits have no net file change to import. The comparison also shows the branch's Camera/Amscope, Camera/Common, workflow, and documentation deliverables as additions relative to the merge base; this is the branch's substantive work, not an unexplained source delta. Do not blindly merge or cherry-pick the two main-only commits. Recheck the comparison if `main` advances later.

Relevant links:

- [Active branch](https://github.com/Shennagyp/MICROSCOPE_CPP/tree/amscope-native-driver)
- [Branch head before this handoff refresh](https://github.com/Shennagyp/MICROSCOPE_CPP/commit/c6833e8023b23c0e351740ee5cf4bb367752ca9c)
- [Comparison with main](https://github.com/Shennagyp/MICROSCOPE_CPP/compare/main...amscope-native-driver)

## 3. CI and test evidence

The user locally built the Phase 2.1 revision `03f35bf` with a clean build; the driver, Qt adapter, preview, hardware-rate executable, and tests all compiled, and all 10/10 CTest targets passed. The Qt request test also passed separately with verbose output. This is user-reported local evidence. GitHub Actions run [#45067873506](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45067873506) passed on both Intel x86_64 and Apple Silicon arm64 for the same revision. This validates Phase 2.1's discovery models, not Phase 2.2.

Phase 2.2 adds `CameraFrames.h`, the Amcam frame adapter, optional-metadata mapping using the SDK's validity flags, hardware-rate report adaptation, `camera_common_frame_tests`, and preview-side migration of RAW export, frame diagnostics, and metadata sidecar serialization. The user's clean Release build at branch head `9daf193c7a107ae93984f5bf00f56d0dfbd66b3b` compiled the native driver, Qt adapter, hardware-rate executable, preview application, and all test targets. All 11/11 CTest tests passed locally. GitHub Actions run [#45070941692](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45070941692) passed for the same branch head on Intel x86_64 and Apple Silicon arm64. The earlier `c13101ce` preview compile failure was caused by stale `Frame` field references and is closed by the preview migration in `609174c` plus explicit includes in `0952952`. Physical-camera regression against the extracted common frame model has not yet been performed.

The successful workflow run executes configuration and compilation of the native driver, Qt adapter, and preview application; CTest; SDK/library/application artifact and architecture checks; launcher syntax; and launcher smoke test.

The earlier eight-target suite passed on both architectures in [#45059333867](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45059333867). The ninth target, `amscope_session_primitives_tests`, was then locally built and passed as part of the user's 9/9 run at `d2e5cb3`. The Phase 2.1 discovery-model slice added `camera_common_models_tests` and passed local and cross-architecture CI at `03f35bf`. Phase 2.2 subsequently added `camera_common_frame_tests`, bringing the suite to eleven; current-head local and cross-architecture CI evidence is recorded above.

The eight targets in the earlier completed suite were:

1. `amscope_camera_types_tests`
2. `amscope_acquisition_frame_gate_tests`
3. `amscope_profile_tests`
4. `amscope_event_queue_tests`
5. `amscope_frame_stall_watchdog_tests`
6. `amscope_qt_request_tests`
7. `camera_common_feature_tests`
8. `camera_common_frame_rate_tests`

The launcher reported `0 camera(s) found` in the hosted CI environment. This proves the no-device enumeration path only. It does not prove physical camera enumeration, acquisition, control readback, RAW/still capture, Stop/Close timing, accessory operation, or recovery.

The first physical run recorded 91.253 event/pull/callback FPS at 640×480; the second run recorded 91.183 FPS for all three rates over 14.498 s, with 1,322 frames and zero drops. The second run's paint rate was 14.140 FPS (205 frames). Both runs used the SDK device string `MU503(USB2.0)`; both restored the original 1280×960 mode and successfully stopped/closed. On the second run, model speed level 6/6 was applied. The second run read 70,000 µs from `ExposureTime` before Start, but did not record `ExposureAuto` or active exposure during measurement; its derived 14.286 FPS value is not an established active exposure limit. SDK sequence remained zero, timestamps were unavailable, and per-frame exposure samples were zero. The low paint rate is not yet localized because that test did not expose timer-poll, submission, and paint-event counts. New source instrumentation is intended to resolve these ambiguities.

## 4. Actual implementation state at the last source revision

The original native acquisition and session-lifecycle implementation remains based on `a3a03fbd`; performance changes preserve its callback-to-pull acquisition mechanism. The current implementation adds:

- Direct zero-copy preview: `FrameView` wraps the immutable frame-pool bytes in a non-owning `QImage` and paints with `QPainter`. It no longer performs a per-frame `QImage::copy()`, `QPixmap` conversion, or pixmap allocation. The view retains the `FramePtr` lease until replacement.
- A C++17 atomic-shared-pointer latest-frame mailbox replaces the mailbox mutex and does not accumulate stale frames. Frame notifications bypass the feature-request queue mutex.
- Feature descriptor discovery is cached per camera state. Individual feature reads reuse the inventory instead of repeating every SDK feature probe under the SDK mutex. The preview does not rebuild the inventory when entering Streaming; setting editors are disabled until Stop.
- The driver counts `AMCAM_EVENT_IMAGE` callbacks and successful `Amcam_PullImageV4` calls with independent atomics. The UI separately measures newly submitted frames when they are actually painted; it does not use SDK sequence as a frame-identity token. The one-second monitor does not call the SDK or acquire the SDK-call mutex. It displays event FPS, pull FPS, paint FPS, exposure time, dimensions/sequence, and pool drops.
- Every Start clears the generic SDK FPS limit with `AMCAM_OPTION_FRAMERATE=0`. After `Amcam_StartPullModeWithCallback` succeeds, it sets the model-specific maximum: maximum precise FPS plus 100% bandwidth for precise-rate models, or maximum model `maxspeed` through `Amcam_put_Speed` otherwise. Low-noise mode is disabled when the model advertises it. This follows ToupTek's documented post-Start procedure; no arbitrary fixed FPS target is set.
- Status-label formatting was removed from the per-frame path and throttled to the one-second diagnostics update.

### Hardware performance test executable

`Camera/Amscope/app/hardware_rate_test.cpp` builds as `amscope_hardware_rate_test`. It is a physical-camera-only utility and is intentionally not registered with CTest. It selects and verifies a requested live-resolution feature, then runs the existing native acquisition path while a small visible Qt widget renders the latest RGB24 frame. It reports image-event/pull/callback FPS, host-revision frame submissions, preview poll ticks, Qt paint events and painted-frame FPS, actual dimensions, SDK sequence/timestamp values, exposure-auto state and exposure readbacks at multiple lifecycle points, per-frame exposure metadata, applied model speed level, drops, and Stop/resolution-restore/Close outcomes. It refuses to start when the camera reports RAW output enabled because the test renderer measures the RGB24 display path. The original preview resolution is restored after Stop where the camera state permits it.

The launcher mode is:

```sh
AMSCOPE_BUILD_TESTING=ON ./scripts/run-amscope.sh \\
  --hardware-rate-test --resolution 640x480 --warmup 3 --duration 15 --target-fps 100
```

The preview window must remain visible. A `PASS` result means the operational checks passed and pull FPS met the target; `BELOW_TARGET` means the run completed but pull FPS was lower; `FAIL` means an operational check failed. Exit codes are 0, 10, and 8 respectively; setup failures use other non-zero codes. A short performance measurement is not a substitute for the Phase 0 15-minute baseline regression.

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

### Common camera models and feature contract

Primary files:

- `Camera/Common/include/camera/CameraModels.h` — identity, device discovery, preview resolution, and tri-state capabilities.
- `Camera/Common/include/camera/CameraFrames.h` — common frame, pixel-buffer layout, opaque pixel-format identity, sequence, timestamp, exposure, payload, and immutable frame pointer.
- `Camera/Common/include/camera/CameraFeatureAccess.h`
- `Camera/Common/include/camera/CameraFeatureValidation.h`
- `Camera/Common/include/camera/FrameRateEstimator.h`

The common feature layer supports typed Boolean, integer, integer vector, floating-point, string, and enumeration values; descriptors; typed results/errors; validation; and frame-rate estimation. `CameraInfo` derives from the common `CameraDeviceInfo`, while `Resolution`, `Frame`, and `FramePtr` are aliases to common types.

The `AmcamFrame` extension retains only Amcam-specific SDK flags, exposure gain percent, and black level. The common `Frame` carries optional sequence/timestamp/exposure values; Amcam sets them only when the matching `AMCAM_FRAMEINFO_FLAG_*` bit is present. Timestamps and exposure durations use a 1,000,000 ticks-per-second timebase; the timestamp clock origin is unknown. The raw FourCC is represented by a `PixelFormatIdentifier` with scheme `amcam.fourcc`, never assumed to be PFNC. `CameraState` and `CameraError` remain AmScope-specific at this slice.

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
| 0 — Baseline freeze and verification | ACCEPTED BY OWNER. Two MU503 640×480 physical runs measured 91.253 and 91.183 event/pull/callback FPS, zero drops, successful Stop/restore/Close. A 15-minute exact-commit hardware record remains follow-up coverage. |
| 1 — Native driver stabilization | ACCEPTED BY OWNER. The single-session native acquisition path is accepted; deterministic callback/cache/lease tests are added as post-acceptance hardening. Full injected SDK failure coverage remains future work. |
| 2 — Backend-neutral models | IN PROGRESS. Slices 2.1 and 2.2 passed clean local builds/CTest (10/10 and 11/11 respectively) and cross-architecture CI at their recorded revisions. The fixed-8-bit RAW storage-mode correction `a16ae3b` passed a clean local Release build and 11/11 CTest at `7b81b5e`; Apple Silicon CI passed and Intel CI was still running at this refresh. Physical-camera frame-layout/payload/metadata and RAW-capable-camera regression remains open. Slice 2.3 common errors/value models has not started. |
| 3 — Driver/control/Qt streaming separation | OPEN. Current Qt facade combines responsibilities. |
| 4 — Reusable Qt UI library | OPEN. Preview exists; reusable UI library is not delivered. |
| 5 — GenTL/GenApi integration | BLOCKED. Existing C++ dependency/runtime and a real CTI camera are not established in this branch. |
| 6 — Capability/feature/command coverage | OPEN. Broad support exists; full inventory and physical model matrix required. |
| 7 — Packaging | OPEN. Install rules exist; exported package and clean consumer proof are missing. |
| 8 — Release qualification | OPEN. Complete hardware matrix and release acceptance are not established. |

### Phase 0 work now recorded

- Repository/source/branch review and the implementation inventory are documented.
- The implementation inventory was refreshed against the latest implementation-bearing source (`41d7471`) and now records the user-reported resolution smoke result separately from formal hardware qualification.
- The native SDK test-seam design was rechecked against the current `Start` implementation. It now includes `Amcam_get_Speed` / `Amcam_put_Speed` so the non-precise-FPS model path and its readback/failure cleanup can be tested alongside the precise-FPS option path. This is design-only; the seam has not been implemented.
- The user reports that the physical camera opens and produces live preview frames.
- After removing the stale generated `build/amscope` tree and rebuilding with `AMSCOPE_BUILD_TESTING=ON ./scripts/run-amscope.sh`, the user reports that the build and application launch succeeded and the live `Resolution` feature successfully changed resolution. The earlier `Operation not permitted` failure while writing `AmcamCamera.cpp.o` is no longer blocking the clean build.
- The user did not provide exact selected/reported dimensions or still-capture dimensions; do not infer that the 640×480 performance mode or toolbar still-capture path has been verified.
- A dedicated physical rate test now automates the measurement. It is launched through `scripts/run-amscope.sh --hardware-rate-test`; it records event, pull, callback and paint rates, actual frame dimensions, exposure, sequence and pool drops. It restores the saved live resolution after Stop when the camera state permits. It is intentionally excluded from CTest because the physical camera is required.
- The initial live preview was substantially below the approximately 100 FPS previously seen at 640p. No exact model, dimensions, numerical rate, or complete baseline test record has yet been provided.
- The source has since been optimized for the reported objective: direct frame-pool rendering with a retained immutable frame lease; atomic latest-frame mailbox; no feature-queue lock on frame notifications; cached feature descriptor inventory; no full feature inventory refresh during Streaming; and no per-frame status-label formatting.
- Maximum supported rate is now selected per model after `Amcam_StartPullModeWithCallback`, following the vendor SDK procedure. The generic SDK frame-rate cap is set to zero; no arbitrary FPS target is used.
- The UI now displays event FPS, pull FPS, paint FPS, frame dimensions/sequence, exposure time, and pool drops. These counters do not call the SDK or acquire the SDK-call mutex.
- The current source changes preserve the callback -> bounded event queue -> single `Amcam_PullImageV4` worker. The earlier SDK frame-rate polling path was removed because it acquired the SDK-call mutex during acquisition.
- The completion plan and README describe the new implementation and hardware acceptance criteria.

### Phase 0 residual follow-up coverage

- Phase 1's nine-target suite passed locally at `d2e5cb3`. Phase 2.1 passed a clean full build and 10/10 CTest locally at `03f35bf`, plus CI on Intel x86_64 and Apple Silicon arm64. Phase 2.2 passed the user's clean full Release build and 11/11 CTest suite at `9daf193c7a107ae93984f5bf00f56d0dfbd66b3b`; GitHub Actions run [#45070941692](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45070941692) passed on Intel x86_64 and Apple Silicon arm64 for the same head. Real-camera frame/metadata and RAW-format checks remain outstanding.
- The user has built and launched the branch locally after removing the stale generated build directory, and has confirmed that the live `Resolution` feature changes resolution. Record the exact resulting live frame dimensions before treating 640×480 as the measured mode.
- Rebuild and rerun the newest diagnostic revision. Review `event_fps`, `pull_fps`, `callback_fps`, `paint_fps`, `frame_poll_ticks`, `preview_frame_submissions`, `paint_events`, all four exposure readback points and `exposure_fps_limit_basis`, `applied_speed_level`, timestamps, actual dimensions and dropped frames.
- Check the negotiated macOS USB connection speed for the MU503 using `system_profiler SPUSBDataType` and record whether the connected device is actually running at USB 2.0 (up to 480 Mb/s) or USB 3.x (typically 5 Gb/s or higher). The SDK's `MU503(USB2.0)` device string is a transport lead, not by itself proof of the physical negotiated speed.
- Do not change the acquisition implementation until the exposure and negotiated transport speed are known.
- Interpret the three rates: low event FPS is upstream of the pull worker; event-to-pull loss points to callback/queue/pull throughput; pull-to-paint loss points to UI rendering.
- Record actual camera manufacturer/model, SDK model code, firmware, SDK version, OS, and architecture.
- Complete Section 3.2, including safe setting readback/restoration, Stop/Start, Close/reopen, still capture where supported, and the required 15-minute continuous acquisition interval.
- Save the complete hardware record under `docs/testing/hardware/`.
- These are recommended evidence improvements, not blockers to the owner's explicit Phase 0 acceptance.

## 8. Immediate next actions



### RAW bit-depth option correction

The Amcam SDK manual describes `AMCAM_OPTION_BITDEPTH` as model-specific: 0 selects 8-bit output and 1 selects the camera's maximum bit depth. `Amcam_get_MaxBitDepth` returns that maximum depth. A fixed 8-bit sensor therefore has no selectable 8/16-bit mode and may return `E_NOTIMPL` (documented HRESULT `0x80004001`) from `Amcam_get_Option(AMCAM_OPTION_BITDEPTH)`.

The initial RAW still implementation treated that option as mandatory for every RAW-capable model and aborted before snap/pull when it was unavailable. Commit `a16ae3b` adds a shared storage-mode resolver: maximum depth 8 selects the fixed 8-bit container without querying the optional option; models above 8 bits still query the option and preserve its 8-bit-versus-16-bit mode. It is used by both RAW still capture and RAW live startup. This source fix passed the user's clean Release build and 11/11 CTest suite at `7b81b5e`; Apple Silicon CI passed and Intel x86_64 CI was still in progress at this handoff update. It does not change the still-event waiter/pull path or live acquisition callback/queue/worker mechanism.

### Physical-camera validation for Phase 2.2

The original frame-model software validation passed at `9daf193c7a107ae93984f5bf00f56d0dfbd66b3b`. The fixed-8-bit RAW correction at `a16ae3b` passed the user's clean Release build and 11/11 CTest suite at `7b81b5e`. The common 8/16-bit selector at `3ef46d3` is a newer unvalidated source increment; CI run [#45075496341](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45075496341) was in progress at this refresh.

The remaining required Phase 2.2 regression must use a physical AmScope camera:

1. Open the camera and verify normal live RGB24 preview, including correct dimensions and stable image rendering.
2. Capture a processed still. Verify actual width, height, strideBytes, payload byte count, and that the saved PNG is readable. The current preview application writes a JSON sidecar only for RAW stills; the processed PNG path does not currently emit one. If the camera supports RAW still capture, select RAW still, save the payload and adjacent JSON sidecar, then inspect format name, significant/storage bit depths, format identifier fields, optional sequence, timestamp/timebase/clock domain, exposure/timebase, and Amcam extension fields.
3. Verify optional metadata semantics against the device using the RAW sidecar when RAW still capture is supported: a missing SDK validity flag must remain unavailable/null; a valid zero value must remain present as zero. Do not infer availability from a nonzero payload. The live hardware-rate diagnostic can additionally report optional per-frame sequence/timestamp/exposure values for the RGB24 acquisition path.
4. Attempt RAW still capture only if the physical camera/SDK reports it as supported. For a returned RAW frame, verify the opaque amcam.fourcc identifier, significant source depth, storage-container width, stride, and payload size independently. Do not assume the identifier is PFNC or that the sensor's significant depth equals its storage width.
5. Verify still capture leaves the live acquisition lifecycle intact, then exercise Stop, restart, and Close/reopen where practical. The still event waiter/pull path must remain separate from the live callback/event-queue/pull-worker path.

The broader 15-minute continuous run, repeated lifecycle cycles, negotiated USB-speed inspection, and full SDK fault injection remain follow-up coverage. They are not reasons to reopen the accepted Phase 0/1 gates. Do not infer an exposure limit from the earlier pre-Start 70,000 µs value.

### Next-phase and parallel work

- Keep the source-to-test mapping current in [the implementation inventory](AMSCOPE_IMPLEMENTATION_INVENTORY.md); the current inventory and targeted smoke observation are refreshed.
- Continue the C++/Swift behavior comparison and identify untested native SDK call paths without changing runtime behavior before Phase 0 acceptance.
- The Phase 1 internal SDK-call seam is documented in [the native-driver test seam design](AMSCOPE_NATIVE_DRIVER_TEST_SEAM.md), including both post-Start maximum-rate model branches. Implement it only after Phase 0 hardware acceptance; it must remain private to implementation/testing and must not change the public camera abstraction.
- After the Phase 2.2 physical-camera frame/metadata and RAW-capable-camera checks pass, proceed to Phase 2.3: classify remaining common errors and value types. Preserve native error codes and operation context; move only semantics genuinely shared by the backends.
- Identify the external GenTL/GenApi C++ runtime and its supply/version/licensing constraints. Do not create a replacement implementation.

### Phase 1 post-acceptance hardening

The first post-acceptance test increment is now implemented in `AmcamSessionPrimitives.h` and `AmcamSessionPrimitivesTests.cpp`. It covers callback lease drain behavior, cache-flush failure/retry and newer-obligation preservation, and frame-slot reuse only after a lease is released.

The broader private SDK dispatch seam remains a possible next hardening increment for deterministic native-session tests covering:

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

Those additional fault-injection tests are follow-up coverage. The user has accepted Phase 1; the follow-up must preserve the single `HAmcam` owner and the callback → bounded queue → single pull worker mechanism.

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
