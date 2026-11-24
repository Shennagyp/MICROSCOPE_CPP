# AmScope Native Camera Module — Revised Implementation and Release Plan

**Revision date:** 24 November 2026  
**Target branch:** `amscope-native-driver-0.7`  
**Revision baseline:** `03417b3a0dbd3b846500bed7fc2417f0a7082057` (accepted v0.6 head from which v0.7 was branched)  
**Language/runtime:** C++17, Qt 6, macOS  
**Status:** Active release plan. This document supersedes the earlier phase sequence in `AMSCOPE_NATIVE_DRIVER_COMPLETION_PLAN.md`.

---

## 1. Release objective and scope

The immediate deliverable is a **reusable, installable AmScope-native Camera Module**, not the complete multi-vendor Camera Module.

The AmScope release includes:

- Native Amcam device discovery, session ownership, acquisition, frame delivery, controls, commands, and recovery.
- Backend-neutral common camera identity, capabilities, feature descriptors/values/errors, and frame contracts where semantics genuinely match.
- A non-Qt programmatic API.
- A Qt Core adapter for queued operations and frame delivery.
- Reusable Qt camera UI components.
- A preview application assembled from those reusable components.
- Exported CMake packages and a clean external-consumer example.
- Documented macOS architecture support and an evidence-based MU503 compatibility record.

### Explicitly outside this release

GenTL/GenApi and other camera-vendor backends are **not included in the AmScope release acceptance gate**. They remain a separate follow-on integration track. The C++ GenTL Consumer choice, vendor CTI Producer, and physical CTI camera are not currently available as a complete integration set. They must not be fabricated, reimplemented casually, or represented as finished.

The repository may therefore deliver an AmScope-native release while the larger multi-backend Camera Module remains open.

Initial platform scope remains macOS. Do not imply Windows or Linux support from the presence of vendor SDK files for those platforms.

## 2. Architectural invariants

These invariants are release-blocking:

1. Exactly one `HAmcam` belongs to one opened AmScope camera session.
2. `AmcamCamera` remains the native session owner. Feature access and acquisition use that same handle.
3. AmScope acquisition remains SDK callback -> bounded event queue -> one application-owned acquisition worker -> `Amcam_PullImageV4`.
4. The vendor callback only enqueues relevant events. It does not pull frames, stop/close the device, call UI code, or release callback-owned resources.
5. SDK calls that require serialization retain one authoritative session-level ordering policy.
6. Stop drains callbacks and joins the acquisition worker before releasing resources. A recoverable Stop failure preserves the active session. Terminal Close remains independent of Stop/cache-flush success.
7. Successful Stop creates a cache-flush obligation; that obligation is cleared only after SDK success and is retried before a later Start when necessary.
8. Session/acquisition generations prevent stale callbacks or frames from becoming visible after a transition.
9. The Qt adapter must not open another `HAmcam` or create an independent acquisition path.
10. `Camera/Common` must remain free of Qt, Amcam SDK, GenTL implementation, and vendor-header dependencies.
11. RAW sensor data must never be represented as RGB without a real decoder.
12. A capability is not advertised merely because a function exists in a header or a UI editor exists.

## 3. Existing evidence at the v0.5 baseline

### Automated/build evidence supplied by the project owner

At commit `f18cd67087e5174fe1181e8537b8034539efb745`:

- Release CMake configuration completed.
- The native driver, Qt adapter, preview application, and both diagnostic probes compiled.
- All **11/11 CTest tests passed**.
- Both probe `--help` paths worked.

The optional Qt Vulkan-header message did not prevent configuration or generation. It is not a release blocker unless a required Qt component later depends on Vulkan.

### Physical MU503 observations supplied by the project owner

Camera reported by the SDK: **MU503 (USB 2.0)**. The direct vendor probe reported **1280 × 960** and a **3,686,400-byte** frame buffer.

| Probe | Actual run | Observed result |
|---|---:|---|
| Native `AmcamCamera` path | 60.722 s; 835 frames | 13.75 FPS overall; zero reported drops |
| Direct vendor `Amcam_WaitImageV3` path | 60.031 s; 1,760 frames | 29.32 FPS; zero timeouts; zero SDK-reported dropped frames; no failure |

During the native run, the user switched on microscope illumination at approximately second 36. Before that change the rate was about 2–3 FPS; after it, the rate was about 29–30 FPS. The vendor-only run was conducted with illumination on. These are **not equivalent lighting conditions**, so the overall averages must not be used to claim that either acquisition API is faster. Under illumination, the native path also reached approximately 29–30 FPS.

The probes are diagnostic controls, not production acquisition alternatives. The direct `WaitImageV3` path is not to be merged into the production driver.

The reports do not establish a progressing device sequence number: the native probe reported it unavailable and the vendor probe reported zero-to-zero. The vendor frame size is consistent with a 1280 × 960 RGB24 buffer, but that alone does not validate the common `camera::Frame` adapter's stride, pixel-format identity, optional metadata, or RAW semantics.

## 4. Revised phases

Phases 0 and 1 remain accepted by the project owner. The remaining work is reduced to the following release-critical phases.

### Phase 0 — Baseline and scope freeze

**Status: ACCEPTED BY PROJECT OWNER**

The earlier MU503 baseline and native acquisition work remain accepted. The approximately 100 FPS target is a performance objective, not a correctness criterion. Do not reopen this phase solely because the current 1280 × 960 run produced a lower rate.

### Phase 1 — Native AmScope session and acquisition

**Status: ACCEPTED BY PROJECT OWNER; v0.5 diagnostic probes now build and run**

The driver already implements single-session ownership, callback/queue/worker/pull acquisition, frame leases, watchdog behavior, feature/command operations, and lifecycle recovery. The v0.5 probe build and physical run add diagnostic evidence; they do not change production acquisition.

No more work is scheduled for comparing `WaitImageV3` against the production path. Reopen Phase 1 only for a reproduced correctness defect (stale frame, callback lifetime, broken Stop/Close, handle duplication, acquisition failure, or resource leak), not to pursue another FPS experiment.

### Phase 2 — Close the common camera contract

**Status: ACCEPTED TO PROCEED; RGB24 common-frame contract verified; production RAW common-frame hardware qualification explicitly bypassed by project owner and remains unverified**

The common layer already contains discovery identity/capabilities, common frame/payload metadata, typed feature descriptors/values/errors/results, feature validation, and host frame-rate estimation. The public-type ownership decisions are recorded in [CAMERA_COMMON_TYPE_OWNERSHIP.md](CAMERA_COMMON_TYPE_OWNERSHIP.md).

The v0.6 implementation corrects a type mismatch in common feature ranges: integer feature bounds/increments now use exact `int64_t` values, while floating-point features retain `double` ranges. Validation requires the range alternative to match the descriptor kind and avoids signed overflow during integer increment alignment. The Qt editor and Amcam descriptor construction use the corresponding typed range.

Verification completed on v0.6:

- Release build completed for the native driver, Qt adapter, preview, both probes, and test executables.
- All 11/11 CTest tests passed, including exact integer feature range checks.
- A physical MU503 RGB24 frame was recorded at 1280 × 960: stride 3,840 bytes and payload 3,686,400 bytes. The geometry matches packed RGB24 exactly.
- The frame reported `isStill=false`, 24 significant/storage bits per pixel, no native RGB format identifier, and sequence/timestamp/exposure unavailable. Those absent optional fields were preserved as unavailable, not replaced with zero.
- The exact record is [phase-2-mu503-rgb24-2026-11-23.md](../testing/hardware/phase-2-mu503-rgb24-2026-11-23.md).

Remaining work:

1. Review the public contract and exact numeric-range API change before package stability; the v0.6 build/tests currently show no compile or unit-test regression.
2. Keep AmScope SDK errors, camera states, trigger/I/O enums, accessory state, Amcam FourCC, and other vendor-specific concepts in `amscope` unless a second real backend demonstrates an exact shared meaning.
3. Production RAW live common-frame hardware qualification was explicitly bypassed by the project owner when proceeding to Phase 3. The direct vendor-SDK RAW8 probe does not qualify the `AmcamCamera` adapter. Keep production RAW live-frame support marked hardware-unverified; do not claim qualification from the RGB24 test or direct SDK probe.

**Exit criteria**

- `Camera/Common` compiles without Amcam or Qt dependencies.
- Common contract tests pass, including exact integer range checks above (2^{53}) and signed-domain increment validation.
- The physical frame observation agrees with the common frame contract.
- No duplicate or invented common abstraction remains.

### Phase 3 — Finalize native control and Qt streaming boundaries

**Status: ACCEPTED BY OWNER; local Release build and 12/12 CTest passed; physical Qt-to-MU503 streaming smoke check confirmed valid by owner**

The source already has the right ownership foundation:

- `AmcamCamera` is the non-Qt driver/session owner and implements the common feature-access interface.
- `QtAmcamCamera` composes one `AmcamCamera` instance, adds Qt request scheduling and frame notification, and does not create a second native SDK handle.

The revised implementation must preserve and formalize that model. The owner confirmed the physical Qt-to-MU503 preview smoke check as valid on 23 November 2026.

Implemented in this Phase 3 slice:

- Added `Camera::Qt` with the header-only `camera::qt::FrameStreamMailbox`. The mailbox is embedded by value at the original end-of-object position in `QtAmcamCamera`; it adds no runtime QObject, heap allocation, worker, or queue.
- The original `QtAmcamCamera::frameAvailable()` signal and `takeLatestFrame()` API remain intact. Acquisition-generation validation, QObject notification, and the sole `AmcamCamera` session owner remain in the AmScope adapter.
- The extraction preserves the v0.5 frame-publication mechanism: the former tail `FramePtr` slot and atomic notification flag are embedded in `FrameStreamMailbox`, and the same C++17 atomic-`shared_ptr` store/CAS/exchange algorithm remains in use. This is a structural extraction, not a new synchronization strategy. Do not claim those standard-library operations are lock-free.
- The acquisition worker still pulls directly into a pre-sized frame-pool slot, or into its private discard buffer when all leases are retained. The Qt boundary transfers the immutable `FramePtr` lease; it does not copy or convert the pixel payload. Frame notification still bypasses the feature-request FIFO and coalesces wakeups.
- The templated mailbox receives synchronous lifecycle and notification callbacks, allowing optimization/inlining into the existing adapter hot path. `camera_qt_frame_stream_tests` covers producer-thread publication, latest-lease identity, notification coalescing/re-arm, stale publication rejection, clear, and deterministic producer/consumer overlap. The owner built the dedicated target and ran the full Release CTest suite at `8aa3b23`; all 12/12 tests passed, including `camera_qt_frame_stream_tests` (0.49 s).

Work:

1. Keep `AmcamCamera` as the sole owner of the native session and the non-Qt control/command surface.
2. Keep the Qt class as an adapter for QObject affinity, queued operations, coalesced frame notification, and latest-frame delivery.
3. Keep `FrameStreamMailbox` as embedded frame-delivery state; keep the QObject signal and scheduling target on `QtAmcamCamera`. Do not add a second runtime object, operation queue, or camera owner.
4. Preserve independent dependencies: `Amscope::Driver` remains non-Qt; `Camera::Qt` is a header-only mailbox over common frame contracts with no Qt runtime dependency; Qt Core is used by the mailbox test and the `Amscope::Qt` QObject adapter.
5. Do **not** add a redundant `camera_amscope_control` wrapper if it simply forwards the existing `AmcamCamera` API. The existing class already provides the non-Qt programmatic surface.
6. Preserve FIFO operation ordering, session/feature generations, callback shutdown synchronization, and one SDK-call serialization boundary.
7. Treat frame-path performance as an invariant: do not add pixel copies/conversions, per-frame frame-buffer allocations, an explicit frame-path mutex, another worker, or a second notification/operation queue. Preserve the v0.5 publication algorithm unless a measured replacement proves non-regression under equivalent camera settings. The Phase 3 extraction itself is not a physical FPS qualification; controlled rate validation remains in Phase 7.

**Exit criteria**

- A non-Qt client can enumerate/open/read/write/start/receive frames/stop/close through the native API.
- A Qt client receives frames and uses queued requests through the adapter.
- Both paths preserve one `HAmcam` session per opened camera.
- `Camera::Qt` stream tests and AmScope Qt request tests cover their separate responsibilities.
- Tests cover the adapter boundary and existing lifecycle/request semantics.
- Physical Qt-to-MU503 smoke check: open the camera through the preview, start live acquisition, confirm `frameAvailable()` results in changing frames in `FrameView`, then Stop and Close. This is a functional boundary check, not an FPS benchmark; do not change resolution/exposure merely to maximize the observed rate.

### Phase 4 — Extract reusable Qt camera UI

**Status: IN PROGRESS; FrameView build/tests and MU503 smoke passed; File-menu controls window and ImageInfoBar are implemented, but their Release build/CTest and post-change hardware smoke remain pending**

Implemented in this Phase 4 slice:

- Added the standalone `Camera::QtWidgets` static CMake target, depending on `Camera::Common` and Qt Widgets.
- Extracted `camera::qtwidgets::FrameView`. Its public contract uses only `camera::FramePtr` and Qt Widgets. It wraps retained external frame memory for RGB24, RAW8 and RAW16; it does not make an owned per-frame pixel copy, demosaic RAW data, or add a GPU renderer.
- Extracted `camera::qtwidgets::CameraControlPanel`. It accepts common `CameraDeviceInfo` values, preserves opaque device IDs, and reports Refresh/Open/Start/Stop/Close intent through host callbacks. It owns no enumeration, camera session, request scheduler, or backend lifecycle behavior.
- Added `camera::qtwidgets::ImageInfoBar`. It displays host-supplied imaging facts and values, reports double-click/keyboard edit intent, and has no camera access or feature-write behavior of its own.
- The main preview is now image-first: the main window contains the live `FrameView` and the bottom `ImageInfoBar`. Camera discovery, Open/Start/Stop/Close, still capture, feature controls, trigger configuration and accessories are hosted in a separate modeless **File → Camera Controls…** window. Discovery is initiated when that window is opened; there is no camera-control toolbar in the main preview.
- The bottom information bar combines acquired-frame metadata (dimensions, pixel layout/depth, payload size, sequence, frame exposure, event/pull/paint rates, and dropped-frame count) with existing readable image-related camera-feature values. It displays only features actually present and available in the current descriptor inventory.
- Double-clicking an editable feature item opens a typed value editor. Editing is offered only when the descriptor is available, readable and writable and a complete feature readback is present. The host validates with the existing common feature validator, submits through the existing queued `requestWriteFeature` path, and refreshes descriptors/current values after the backend write. The bar displays the newly confirmed readback, not an optimistic requested value.
- This increment does **not** add a camera backend, SDK operation, feature descriptor, or saturation support. Saturation was an illustrative example only; the UI does not synthesize unsupported settings. No `AmcamCamera` or vendor SDK source was changed for this UI request.
- Added the offscreen `camera_qt_widgets_image_info_bar_tests` target for item lifecycle, editable double-click routing, and read-only behavior. Together with `camera_qt_widgets_camera_control_panel_tests` and `camera_qt_widgets_frame_view_tests`, the expected CTest suite is now **15 tests**.
- Source implementation commits: ImageInfoBar component `aca62b2`; CMake/test registration `1a7749c`; File-menu controls window and info-bar composition `f60029d`; readback/edit gating `922c841`; lazy discovery on opening the controls window `8ff51be`; vector/string editing `a7cc6d5`; info-bar interaction/readback hardening `614213b`; image-property display filtering `775f2b3`. The latest source commit is `775f2b341e7d8fd61c2071e808c13058721c9d76`.
- This new UI increment has **not** yet been built or tested locally. The prior 13/13 build and prior MU503 smoke record apply to the earlier FrameView increment only. Do not extrapolate that result to this UI reorganization.

Previously accepted FrameView physical check: the owner confirmed the MU503 preview sequence on checkout `5339c441659b45a3bdb30fdce6971f6dc991f885`; record `docs/testing/hardware/phase-4-mu503-frame-view-smoke-2026-11-24.md`. That evidence verifies the FrameView integration before the current File-menu/info-bar change. The new UI still requires a separate smoke check.

Next acceptance gate:

1. Release build of the full `amscope_preview` target and all **15/15** CTest tests.
2. Launch the app and verify the main window contains only the image preview and bottom information bar; open **File → Camera Controls…** and verify all camera operations/settings are in that window.
3. With the MU503, discover/select, Open, Start, verify changing frames and bottom metadata, Stop, and Close.
4. Double-click one image-related feature that the current device descriptors report as available/readable/writable; verify the edit goes through the existing write/readback flow and the bar changes only after readback. Do not use saturation as an acceptance target unless the camera already advertises it in its descriptors.
5. No FPS benchmark or new feature-capability qualification is implied by this UI work.

Phase 4 remains open after these checks until the remaining reusable feature-editor, command, telemetry, and capability presentation slices are completed and the phase-wide exit criteria are met.

Extract only the components needed to turn the preview into a reusable library:

1. Camera discovery and lifecycle controls — `CameraControlPanel`, hosted from the File menu.
2. Bottom image information bar — `ImageInfoBar`, displaying host-supplied frame metadata and confirmed feature values.
3. Descriptor-driven feature editors, including current-value loading and disabled/unavailable states — existing application editor remains in place; generic extraction is a later slice.
4. Command controls kept separate from setting writes.
5. Telemetry presentation with explicit units.
6. Live frame view and still-capture interaction.
7. Capability/availability presentation.

The reusable Qt Widgets target must depend on common camera contracts and Qt, not Amcam headers, `HAmcam`, or backend implementation classes. The AmScope application retains enumeration, request scheduling, validation/readback policy, session ownership, and backend execution. Backend-specific accessory panels may remain in a separate AmScope UI extension.

Do not redesign this as a visual graph editor or add a node editor.

**Exit criteria**

- The essential camera UI is built from reusable components.
- The main preview contains the live image and bottom information bar, not camera-control toolbars or feature panels.
- All camera controls are reachable through **File → Camera Controls…** and are hosted in that separate window.
- The information bar shows only actual frame metadata and feature values confirmed by readback; editable items require current available/readable/writable descriptors.
- Generic widgets have no AmScope SDK dependency.
- Unsupported controls are not presented as usable.
- The physical MU503 can be discovered, opened, previewed, controlled through an existing supported feature, stopped, and closed through the composed UI.

### Phase 5 — Reconcile AmScope feature and model claims

**Status: BROAD SOURCE IMPLEMENTATION EXISTS; INVENTORY/HARDWARE CLAIMS NEED CLOSURE**

Use `AMSCOPE_IMPLEMENTATION_INVENTORY.md` and `Camera/Amscope/README.md` as the source-to-test-to-UI checklist.

1. Reconcile each public SDK-facing operation as a feature/setting, command/action, telemetry/state, internal lifecycle operation, unsupported, or deferred.
2. Verify descriptor kind, unit, range, increment, enum symbols, vector size, read/write state, and native readback where the MU503 supports the operation.
3. Verify the exact model code used by profile policy; profiles may narrow capabilities but never invent support.
4. Keep configured FPS, SDK/device frame-rate telemetry, and host-observed frame rate distinct.
5. Keep Amcam pixel-format identifiers separate from PFNC and Amcam gain percent separate from GenICam gain dB.
6. Do not attempt unsafe GPIO, thermal, trigger-output, motion, or accessory tests without the required hardware and safe setup.
7. Mark other AmScope models and accessory families as source-implemented, SDK-probed, unverified, unsupported, or tested as appropriate. One MU503 does not certify another model.

**Exit criteria**

- README, inventory, UI, and source agree.
- Public capability claims are evidence-based.
- Unsupported and unverified operations are clearly identified.
- No broad hardware matrix is claimed from the single MU503 test.

### Phase 6 — CMake package and external consumers

**Status: INITIAL INSTALL RULES EXIST; EXPORTED PACKAGE NOT COMPLETE**

1. Define stable namespaced public CMake targets.
2. Export targets and generate package config/version files.
3. Install public headers and preserve correct PUBLIC/PRIVATE dependency visibility.
4. Keep Qt optional for non-Qt consumers.
5. Verify runtime library loading, install names, and RPATHs.
6. Document required Qt components, bundled Amcam SDK selection, supported macOS architectures, and build options.
7. Establish the AmScope SDK redistribution decision and preserve vendor notices. Do not redistribute a large SDK/sample bundle without checking its terms.
8. Build a clean non-Qt consumer using `find_package`, with no repository-relative include/library paths.
9. Build a clean Qt consumer using only the exported Qt target and its declared dependencies.

**Exit criteria**

- Both external consumer configurations build from an install prefix.
- Runtime dependencies resolve outside the repository build tree.
- Non-Qt consumers do not inherit Qt dependencies.
- Vendor redistribution and platform scope are documented.

### Phase 7 — Integrated AmScope release qualification

**Status: NOT STARTED**

Run this once against the release-candidate commit, rather than repeating the same full regression after every documentation or internal refactor.

Automated gate:

- Clean Release configure/build.
- All CTest tests pass.
- CI builds on the declared macOS architectures (Intel x86_64 and Apple Silicon arm64, if both remain in release scope).
- Installed non-Qt and Qt consumer examples build.

Physical MU503 gate:

1. Enumerate and open by the opaque ID returned from discovery.
2. Read a supported feature; change one safe, reversible value; verify native readback; restore it.
3. Start RGB preview and inspect the common frame layout/payload metadata.
4. Run one continuous stability interval of **5 minutes**. This replaces the old blanket 15-minute repeat at every phase; the shorter release gate is an explicit schedule-driven reduction, not a claim of 15-minute qualification.
5. Stop and start again; verify fresh frames.
6. Close and reopen; verify acquisition works again.
7. Test processed still capture when supported. Test RAW only when the physical device reports a supported RAW format.
8. Record drops, timeouts, state transitions, and any absent metadata without inventing defaults.
9. Save the exact-commit record under `docs/testing/hardware/`.

Do not repeat the standalone FPS comparison probes. They are diagnostic tools and are not release acceptance tests.

**Exit criteria**

- Automated and installed-consumer gates pass.
- The physical MU503 release regression passes.
- The exact tested revision and results are recorded.
- Known limitations and unverified model claims are published.
- A release tag identifies the tested commit.

## 5. Separate follow-on track — GenTL/GenApi

This is not a phase blocking the AmScope-native release.

Before implementation can begin, the project needs:

1. A target CTI camera and its vendor Producer package, version, license, and macOS architecture support.
2. A decision on the existing GenTL Consumer integration: a supported C ABI over the existing Swift Consumer, an existing external C++ Consumer, or an explicitly approved C++ port. Do not silently choose a reimplementation.
3. The official GenTL ABI header and GenApi runtime package with verified dependencies, architectures, and redistribution conditions.
4. Evidence that the CTI camera/Producer exposes GenApi XML and usable port access.

Only then implement the CTI backend against the common contracts. Preserve Producer/interface/device/stream ownership and backend identity. Keep AmScope on its native SDK path; do not create a synthetic AmScope CTI Producer.

The GenTL track requires a real CTI camera for physical acceptance. Until then its status is **BLOCKED BY EXTERNAL DEPENDENCIES**, not silently omitted or marked complete.

## 6. Revised status ledger

| Phase | Deliverable | Status after local Phase 4 verification on v0.7 |
|---|---|---|
| 0 | Baseline and scope freeze | ACCEPTED BY OWNER |
| 1 | Native AmScope session/acquisition | ACCEPTED BY OWNER; v0.5 probes built and physically run |
| 2 | Common camera contract closure | ACCEPTED TO PROCEED; RGB24 common-frame contract verified; RAW live common-frame hardware qualification explicitly bypassed by owner |
| 3 | Native control / Qt streaming boundaries | ACCEPTED BY OWNER; local Release build and 12/12 CTest passed, including concurrent mailbox test; physical Qt-to-MU503 frame-delivery smoke check confirmed valid |
| 4 | Reusable Qt camera UI | IN PROGRESS; FrameView is accepted; File-menu controls window and ImageInfoBar source are implemented at `775f2b3`; Release build, 15/15 CTest and new MU503 UI/edit smoke pending; generic feature-editor extraction remains |
| 5 | AmScope feature/model claim reconciliation | OPEN |
| 6 | Exported CMake package and external consumers | OPEN |
| 7 | Integrated AmScope release qualification | OPEN |
| GT | GenTL/GenApi second backend | BLOCKED FOLLOW-ON TRACK; not an AmScope release blocker |

## 7. Dependency order and work discipline

The critical path is:

`Phase 2 -> Phase 3 -> Phase 4 -> Phase 5 -> Phase 6 -> Phase 7`

Phase 5 inventory reconciliation may run alongside Phase 3/4 after the public interfaces are stable. Avoid unrelated diagnostics and broad refactors.

Work one phase at a time. Each phase has one explicit implementation result and one focused verification gate. Do not proceed to the next phase while the current phase has a known regression. Run the full integrated hardware regression once at Phase 7; phase-specific hardware checks should be limited to behavior changed by that phase.

A phase is complete only when its stated exit criteria pass. If a required physical device, Producer, or accessory is unavailable, record that precise limitation rather than simulating success.

## 8. Definition of done

### AmScope-native release

The AmScope-native release is complete when:

1. Phases 0–7 are accepted.
2. The native AmScope session/acquisition invariants remain intact.
3. Common contracts accurately describe only shared semantics.
4. Non-Qt control and Qt streaming are independently consumable over one session.
5. The essential UI is reusable and the preview is a composition root.
6. The feature inventory and compatibility claims match the implementation and evidence.
7. Installed external consumers work.
8. The integrated MU503 hardware record and supported-architecture CI are green.
9. The release package, notices, limitations, and exact revision are published.

### Complete multi-backend Camera Module

The broader multi-backend project is complete only after the separate GenTL track integrates an existing Consumer/Producer path and passes physical CTI-camera qualification in addition to the AmScope release criteria.

The AmScope release must not be described as the completed multi-backend Camera Module.
