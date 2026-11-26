# AmScope Native Camera Module — Subordinate Release Execution Plan

> **Roadmap authority:** this is a narrower AmScope-native release execution
> plan, not the governing Camera Module v2.0 roadmap. Preserve the original
> Phase 0–8 identities in
> [`AMSCOPE_NATIVE_DRIVER_COMPLETION_PLAN.md`](AMSCOPE_NATIVE_DRIVER_COMPLETION_PLAN.md).
> This plan's AmScope release milestones are subordinate and do not renumber or
> remove original Camera Module deliverables.
>
> The optional Qt streaming adapter and reusable Qt camera UI remain required
> by original Phases 3 and 4. A non-Qt core build is an independent consumption
> requirement, not a decision to delete Qt deliverables. GenTL/GenApi remains
> original Phase 5.
>
> Current Camera v2.0 status and continuation instructions are in
> [`CAMERA_V2_DEVELOPMENT_STATUS.md`](CAMERA_V2_DEVELOPMENT_STATUS.md).

**Revision date:** 26 November 2026
**Current implementation branch:** `camera-v.2.0`  
**Target branch:** `amscope-native-driver-0.7`  
**Revision baseline:** `03417b3a0dbd3b846500bed7fc2417f0a7082057` (accepted v0.6 head from which v0.7 was branched)  
**Language/runtime:** C++17, Qt 6, macOS  
**Status:** Subordinate AmScope-native execution plan. Its phase numbering and release scope do not supersede the governing Camera Module Phase 0–8 roadmap.

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

**Status: IN PROGRESS; reusable FrameView, CameraControlPanel, ImageInfoBar, CameraFeatureEditor, CameraCommandPanel and CameraCapabilityPanel exist; amscope_preview builds and 24/24 CTest tests pass on Intel/Apple Silicon, including offscreen composition/dock-state smoke; human visual and current-revision physical MU503 workspace/live-edit smoke remains pending**

Implemented in this Phase 4 slice:

- Added the standalone `Camera::QtWidgets` static CMake target, depending on `Camera::Common` and Qt Widgets.
- Extracted `camera::qtwidgets::FrameView`. Its public contract uses only `camera::FramePtr` and Qt Widgets. It wraps retained external frame memory for RGB24, RAW8 and RAW16; it does not make an owned per-frame pixel copy, demosaic RAW data, or add a GPU renderer.
- Extracted `camera::qtwidgets::CameraControlPanel`. It accepts common `CameraDeviceInfo` values and reports Refresh/Open/Start/Stop/Close intent through host callbacks. Its selection-changed callback reports the selected opaque ID; a new discovery snapshot resets selection instead of treating an enumeration ID as stable physical-camera identity. It owns no enumeration, camera session, request scheduler, or backend lifecycle behavior.
- Added `camera::qtwidgets::ImageInfoBar`. It displays host-supplied imaging facts and values, reports double-click/keyboard edit intent, and has no camera access or feature-write behavior of its own.
- Added `camera::qtwidgets::CameraCommandPanel` for no-argument commands described by the common `ICameraCommandAccess` contract. It renders Request pending, Executing, Completed and Failed distinctly; the host owns command issue, completion polling and authoritative state updates. Parameterized AmScope operations remain in the application host and are not flattened into this panel.
- Added `camera::qtwidgets::CameraCapabilityPanel`, which displays all common model capabilities as Unknown, Unsupported or Supported. It is integrated into the AmScope Camera Properties dock and updates from the selected discovery result without inferring missing capability data.
- Added `amscope_preview --self-test-ui` and CTest `amscope_preview_ui_composition_tests`. The test disables startup discovery and user QSettings, then verifies the Live Preview document, dock identities/ownership, Output pane, status-bar widgets, complete Unknown-initialized capability table and QMainWindow dock-state save/restore. It does not replace human visual inspection or physical-camera smoke.
- Added the backend-neutral `camera::qtwidgets::CameraFeatureEditor`. It supports Boolean, exact `int64_t`, integer-vector, floating-point, string and enumeration values; it requires a valid current readback for readable features, preserves write-only/unset state, validates against the common descriptor, and reports typed write intent to the host. The AmScope preview now uses it in feature tabs; a host-supplied enumeration display-name provider preserves friendly Amcam pixel-format labels without changing the canonical submitted symbols.
- The preview shell now uses a document-centric workspace: Live Preview is a document tab in the central area; Camera Explorer and Camera Properties are separate, vertically split right-side tool windows; Output is a docked bottom tool window. The panes support moving, tabification, floating, resizing, closing, View-menu restoration, and persisted layout state.
- Camera Explorer owns the existing common discovery/lifecycle panel and still-capture controls. Camera Properties owns the existing feature tabs, trigger configuration and accessory controls. File → Camera Workspace raises both panes; the Workspace toolbar exposes the workspace action and Reset Window Layout. The View menu exposes each tool-window toggle and layout reset.
- Output is a read-only application/camera message history with timestamps and a 2,000-block maximum. The editable `ImageInfoBar` remains a horizontal information strip but is now a permanent widget in the actual `QStatusBar`; it shows acquired-frame metadata and confirmed existing feature readbacks. A separate status indicator reports camera lifecycle state.
- Dock layout is saved/restored using `QSettings` state version 2. The version bump intentionally rejects old single-dock layout snapshots so an old layout cannot override the new pane model. The old workspace's geometry, pane ownership and callback/session behavior are otherwise retained.
- Existing AmScope feature descriptors now classify `ExposureAuto`, `ExposureTime`, and analog gain in the `Exposure` group, and color-matrix/white-balance controls in the `Color` group. These are classification corrections for existing features; they do not add SDK calls or capabilities.
- Double-clicking an editable feature item opens a typed value editor. When stopped, the existing descriptor rules apply. During Streaming, the bar enables only available/readable/writable features in the live-writable Exposure and Color groups; stopped-only image-processing/format controls remain disabled. The backend rebuilds and validates the live descriptor before every write, and the display changes only after the queued write and readback. Exposure time remains non-writable while automatic exposure is enabled; switch Automatic Exposure to Off first when manual exposure time is required.
- No new camera backend, feature, SDK operation or saturation support was added. The source updates only reuse the existing feature write/readback path and correct the group classification of existing descriptor records.
- Added the offscreen `camera_qt_widgets_image_info_bar_tests` target for item lifecycle, editable double-click routing, and read-only behavior. The current Qt suite includes offscreen tests for FrameView, CameraControlPanel, ImageInfoBar, CameraFeatureEditor, CameraCommandPanel, CameraCapabilityPanel and the application composition smoke. Workflow #45759739032 builds amscope_preview and passes **24/24 CTest tests on each architecture**.
- The owner initially built the earlier composed UI revision at `4d61eca9d0f3c70bc1468628a05640e5339c2693`, linking `amscope_preview` with **15/15** CTest passing. The later compact dock/live-edit workspace and six-state lifecycle mapping were then validated at `89ccf0c`: full Release build including `amscope_preview`, 15/15 CTest twice, and no `-Wswitch` warning. Tests cover all six lifecycle states and the transitional-state lockout.
- The current document-centric shell source includes: `e2450a2` restructures the central document tab and Explorer/Properties/Output panes; `5f56a5e` aligns the QMainWindow save/restore state version; `ae2440d` adds lifecycle-state colors and the Imaging Workbench title; `49cb8a7` adds the explicit QToolBar include; `4e9130f` orders File before View; `b99ca65` restores 9 pt base typography and polishes toolbar states. The owner fast-forwarded to branch head `040e381`, built `build/amscope-v07` in Release, linked `amscope_preview`, and passed all **15/15 CTest** tests. The source build/test gate is PASS; actual application launch/visual inspection and physical-camera UI testing are not shown in the submitted log.
- No backend, SDK, acquisition, feature-write, or worker behavior was changed by the workspace shell.

Previously accepted FrameView physical check: the owner confirmed the MU503 preview sequence on checkout `5339c441659b45a3bdb30fdce6971f6dc991f885`; record `docs/testing/hardware/phase-4-mu503-frame-view-smoke-2026-11-24.md`. That evidence validates FrameView integration only. It does not validate the current document-centric window composition, dock persistence, Output history, or status-bar presentation.

Next acceptance gate:

1. **Software build and automated tests: PASS** at owner-reported branch head `040e381`: Release build compiled and linked `amscope_preview`; all **15/15 CTest** tests passed.
2. **Application visual smoke remains pending:** launch the newly built app; inspect document-tab appearance, default pane proportions, dock title bars, toolbar/menu styling, status-bar metadata, and Output pane. Exercise move, tabify, float, close/restore and Reset Window Layout. Restart and verify persisted layout.
3. **Physical MU503 smoke remains pending:** discover/select, Open and Start; verify changing frames and metadata updates. Test live Automatic Exposure, Analog Gain and White Balance when their descriptors permit; set Automatic Exposure Off before Exposure Time. Confirm values update only after backend readback, then Stop and Close.
5. While Streaming, double-click Automatic Exposure, Analog Gain, or White Balance values when their descriptors are available/readable/writable. Set Automatic Exposure to Off before expecting Exposure Time to be editable. Confirm the backend write/readback succeeds and the displayed value changes only after readback.
6. Confirm stopped-only image-processing/format settings remain non-editable while Streaming and become available only after Stop when their descriptors permit it. Stop and Close the camera cleanly.
7. No FPS benchmark or new feature-capability qualification is implied by this UI work.

Phase 4 remains open after these checks until human visual/workspace inspection and current-revision physical MU503 UI smoke are completed and the phase-wide exit criteria are met. The offscreen test verifies composition and dock-state restoration only. The reusable command/capability panels, generic feature editor and telemetry strip are implemented; the preview's status-bar edit dialog remains host-owned.

Extract only the components needed to turn the preview into a reusable library:

1. Camera discovery and lifecycle controls — `CameraControlPanel`, hosted in Camera Explorer and raised through File → Camera Workspace.
2. Imaging-information strip — `ImageInfoBar`, displaying host-supplied frame metadata and confirmed feature values in the real status bar; application/camera event history is separate in the Output tool window.
3. Descriptor-driven feature editors, including current-value loading and disabled/unavailable states — CameraFeatureEditor is extracted and integrated into the AmScope preview feature tabs. The host still owns request scheduling, generations, readback, Amcam-specific command semantics and the status-bar edit dialog.
4. Command controls kept separate from setting writes. CameraCommandPanel only renders no-argument ICameraCommandAccess commands; parameterized AmScope actions remain in the application host.
5. Telemetry presentation with explicit units.
6. Live frame view and still-capture interaction.
7. Capability/availability presentation.

The reusable Qt Widgets target must depend on common camera contracts and Qt, not Amcam headers, `HAmcam`, or backend implementation classes. The AmScope application retains enumeration, request scheduling, validation/readback policy, session ownership, and backend execution. Backend-specific accessory panels may remain in a separate AmScope UI extension.

Do not redesign this as a visual graph editor or add a node editor.

**Exit criteria**

- The essential camera UI is built from reusable components.
- The main preview is the central document tab, with Camera Explorer and Camera Properties in separate docked tool windows and Output in the bottom tool area.
- The editable imaging-information strip is a permanent widget in the actual status bar; application messages are recorded separately in Output.
- Camera controls are reachable through **File → Camera Workspace…** and can be shown/hidden through View.
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

**Status: ACTIVE BY OWNER AUTHORIZATION (26 NOV 2026).** The package entry
point, namespaced exports, component-aware config/version files, clean
Common/GenTL/GenApi/AmScope/Qt consumer fixtures and vendor-runtime opt-in
are committed in `camera-v.2.0`. Package CI #45778844769 passed on both
architectures (28/28 tests per architecture, relocated external consumers,
runtime/architecture checks); the default-off vendor-runtime contract also
passed on Apple Silicon. See
[Camera/README.md](../../Camera/README.md) and the canonical
[Camera v2.0 status ledger](CAMERA_V2_DEVELOPMENT_STATUS.md).

1. Define stable namespaced targets: `Camera::Common`,
   `Camera::GenTLConsumerBridge`, optional `Camera::GenTLGenApi`,
   `Amscope::Driver`, optional `Amscope::Qt`, `Camera::Qt` and
   `Camera::QtWidgets`.
2. Export targets and generate relocatable `CameraModuleConfig.cmake` and
   version files, with component-aware dependency loading.
3. Install public headers and preserve correct PUBLIC/PRIVATE dependency
   visibility. GenApi package exports require imported targets from the
   official GenApi 3.5.1 CMake package.
4. Keep Qt optional for Common, GenTL and native AmScope consumers. CI
   explicitly disables Qt discovery in those clean consumer configurations.
5. Verify runtime library loading, install names, RPATHs and architecture of
   the installed artifacts. The package workflow relocates the prefix, builds
   and runs clean consumers, and inspects dylib dependencies.
6. Document required Qt components, bundled Amcam runtime selection,
   supported macOS architectures and build options.
7. The checked-in vendor SDK tree contains no license/notice/EULA. The
   reusable package defaults `CAMERA_MODULE_INSTALL_AMSCOPE_VENDOR_RUNTIME=OFF`;
   CI opts in only for ephemeral internal validation. Obtain and review the
   actual SDK terms or written vendor permission before redistributing the
   linked driver binary or vendor runtime. Preserve vendor notices.
8. Build clean Common, GenTL, GenApi, AmScope and Qt consumers using
   `find_package`, with no repository-relative include/library paths.
9. Verify that GenTL base consumers do not require GenApi and non-Qt
   consumers do not require Qt.

**Exit criteria**

- Common, GenTL, GenApi, native AmScope and optional Qt external consumers
  configure, compile, link and run from an installed, relocated prefix.
- Runtime dependencies resolve outside the repository build tree.
- Common/GenTL/native AmScope consumers do not inherit Qt; GenTL base does not
  inherit GenApi.
- Installed artifacts pass architecture and runtime-link checks on Intel
  x86_64 and Apple Silicon arm64.
- Vendor redistribution remains unresolved: the checked-in SDK tree has no
  license/notice/EULA, so public distribution stays gated pending the actual
  SDK terms or written permission. The package option defaults OFF.
- Platform scope is documented.
- Original Camera Module Phase 7 hardware acceptance also passes: installed
  AmScope non-Qt/Qt physical smoke and installed GenTL CTI-camera smoke.

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

## 5. Separate GenTL/GenApi track — implementation complete, hardware pending

The pre-integration dependency assessment in this subordinate AmScope plan is
superseded by the implementation on `camera-v.2.0`. The canonical Camera
Module Phase 5 GenTL/GenApi integration is complete by owner direction:
the GenTL 1.6 C ABI bridge, Producer/device/session layer and optional official
GenApi 3.5.1 adapter are implemented. The current GenTL/GenApi build and
integration workflow passed both Intel x86_64 and Apple Silicon arm64 at
`49cc11e` (workflow #45777372439).

Do not restart the integration or invent a CTI Producer. A physical CTI camera
is still required for installed-package hardware acceptance in original
Camera Module Phase 7 and broader provider qualification in Phase 8. This
hardware gate is separate from the AmScope-native release path.

## 6. Revised status ledger

| Phase | Deliverable | Status after local Phase 4 verification on v0.7 |
|---|---|---|
| 0 | Baseline and scope freeze | ACCEPTED BY OWNER |
| 1 | Native AmScope session/acquisition | ACCEPTED BY OWNER; v0.5 probes built and physically run |
| 2 | Common camera contract closure | ACCEPTED TO PROCEED; RGB24 common-frame contract verified; RAW live common-frame hardware qualification explicitly bypassed by owner |
| 3 | Native control / Qt streaming boundaries | ACCEPTED BY OWNER; preserve the current implementation and existing preview |
| 4 | Reusable Qt camera UI | SOURCE/OFFSCREEN WORK COMPLETE; human visual/workspace acceptance and physical MU503 UI smoke remain pending; generic feature editor, capability panel and command panel are implemented |
| 5 | AmScope feature/model claim reconciliation | OPEN; provider-level tests remain blocked by the explicit AmScope freeze |
| 6 | Exported CMake package and external consumers | ACTIVE BY OWNER AUTHORIZATION (26 NOV 2026); package implementation committed, two-architecture installed-consumer CI pending |
| 7 | Integrated AmScope release qualification | OPEN; requires installed-artifact physical AmScope smoke and full release regression |
| GT | GenTL/GenApi second backend | IMPLEMENTATION COMPLETE BY OWNER DIRECTION; physical CTI installed-consumer qualification pending |

## 7. Dependency order and work discipline

The subordinate AmScope-native sequence remains:

`Phase 2 -> Phase 3 -> Phase 4 -> Phase 5 -> Phase 6 -> Phase 7`

The owner explicitly authorized subordinate Phase 6 package work to proceed
while Phase 5 AmScope provider-test reconciliation remains open and frozen.
This is a sequencing exception, not a declaration that Phase 5 is complete.
Keep the AmScope source/tests/contracts/runtime freeze intact.

The canonical Camera Module roadmap remains authoritative: original Phase 7
includes installed-package physical smoke for AmScope and GenTL; original
Phase 8 expands to the declared provider/model qualification matrix and
release acceptance. Avoid unrelated diagnostics and broad refactors. Run
the integrated AmScope release regression once at subordinate Phase 7;
phase-specific software verification should remain focused.

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
