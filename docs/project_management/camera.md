# Workstream — Camera

**Canonical project:** the reusable Camera Module in [MICROSCOPE_CPP](https://github.com/Shennagyp/MICROSCOPE_CPP), under `Camera/`  
**Management branch:** `project_manager`  
**Camera development branch:** `camera-v.2.0`  
**Last verified non-Qt code/CI checkpoint:** `11ff79ac115e5f4ecbcbe95eb70f4391c48a21d8`  
**Library source branch:** `OPTIKA-B293-DUALSLIDE-APP-v.0.9` at `c7bb4b4dc41441989d71ee800c6d33b51dc54abe`  
**Optika application branch:** `OPTIKA-B293-DUALSLIDE-APP` at `4c278aed3af29e51b7485a2902080d84314af73b`  
**Roadmap authority:** original Camera Module Phases 0–8, preserved for v2.0.

## 1. Scope and ownership

There is **one Camera Module workstream** in this repository.

The Camera Module owns:
- provider discovery and provider-qualified opaque device identity;
- native camera session ownership and lifecycle;
- backend-neutral camera identity, capabilities, feature values, command/result and frame contracts;
- programmatic settings, commands, telemetry and frame consumption;
- provider-specific implementations and model profiles;
- optional Qt streaming/thread-handoff components (original Phase 3);
- reusable Qt camera UI components (original Phase 4).

The Camera Module does **not** own:
- an application's main window, application workspace, dock arrangement or application-specific view models;
- Optika's SwiftUI/AppKit presentation state;
- microscope optical configuration or objective/filter coordination;
- controller and stage semantics;
- Sample/Slide/Tile identity or slide-scan orchestration;
- image-processing algorithms or application GPU policy.

The core driver and common contracts must be usable without Qt. Qt is an **optional dependency boundary**, not an excluded Camera Module capability. Original Phase 3 still includes Qt frame streaming; original Phase 4 still includes a reusable Qt camera UI library. A consumer that needs only programmatic access must not inherit Qt, while a Qt consumer must be able to opt into the streaming/UI targets.

Do not split this work into separate production projects named AmScope, MU503 or GenTL. Provider adapters, device profiles and standards compatibility remain parts of this one Camera Module.

## 2. Optika application integration and ownership boundary

The current Optika application branch is `OPTIKA-B293-DUALSLIDE-APP`, inspected at `4c278aed3af29e51b7485a2902080d84314af73b`. Camera library development continues on `camera-v.2.0`, not on the application branch.

The Optika GUI is implemented with **SwiftUI/AppKit**, not Qt. Its camera-facing composition is visible in:
- `Application/OptikaB293/Sources/Features/Camera/NativeCameraModel.swift` — application camera state and lifecycle orchestration;
- `Application/OptikaB293/Sources/Features/Camera/NativeCameraFeatureModel.swift` — application feature snapshot, read/write and editor state;
- `Application/OptikaB293/Sources/Adapters/Camera/AmscopeNativeCameraBridge.h/.mm` — Objective-C++ boundary to the C++ camera controller and frame lease;
- `Application/OptikaB293/Sources/Adapters/Camera/NativeCameraController.cpp/.h` — application adapter/controller and serialized command execution;
- `Application/OptikaB293/Sources/Adapters/Camera/NativeCameraFrameFeed.swift` — narrow latest-frame consumption protocol.

The Optika application target links `Amscope::Driver` and native Apple frameworks. Its application shell and presentation are not supplied by the Camera Module. That does not remove the reusable Qt UI deliverable: other Qt consumers may use `Camera::QtWidgets`, and the preview application remains a reference composition.

Ownership remains layered:
1. `AmcamCamera` is the sole owner of the AmScope SDK handle/session and native acquisition.
2. The application-side `NativeCameraController` serializes application requests and delegates to the driver; it must not open a competing SDK handle.
3. The Objective-C++ bridge translates the application boundary and retains frame leases as required.
4. SwiftUI/AppKit models and views own Optika presentation state only. They do not own the SDK session or pull frames directly from the vendor SDK.
5. The frame feed is a narrow consumer surface. It does not become a second acquisition engine or a second frame-history queue.
6. Reusable Camera Qt widgets remain backend-neutral at their common component boundary; an application composes them or uses another UI technology.

## 3. Governing roadmap — original Camera Module Phases 0–8

The original *AmScope Native Driver and Camera Module Completion Plan* is the governing v2.0 roadmap, not merely historical background.

**Authoritative source**
- Repository: `Shennagyp/MICROSCOPE_CPP`
- Branch: `amscope-native-driver-v.0.2`
- Path: `docs/architecture/AMSCOPE_NATIVE_DRIVER_COMPLETION_PLAN.md`
- Original baseline date: 22 November 2026
- Canonical phase identities: 0–8

The later `AMSCOPE_NATIVE_RELEASE_PLAN.md` is a narrower AmScope-native execution plan. It is subordinate to the original roadmap. Its phase numbering must not renumber the Camera Module, and its AmScope release gate must not be confused with completion of the complete multi-backend Camera Module.

### 3.1 Canonical phase objectives

| Phase | Canonical deliverable | Objective |
|---:|---|---|
| 0 | Freeze and verify the baseline | Preserve the physical-camera baseline, reconcile source branches, verify SDK behavior and inventory the actual implementation. |
| 1 | Stabilize and qualify the native AmScope driver | Establish safe session ownership, acquisition, lifecycle, callback handling, failure recovery and frame management. |
| 2 | Finalize backend-neutral camera models | Complete identity, discovery, capabilities, feature/value/error and frame/image-layout contracts without vendor leakage. |
| 3 | Separate driver, programmatic control and Qt streaming libraries | Make non-Qt control independently usable and Qt streaming an optional adapter over the same native session. |
| 4 | Extract the reusable Qt camera UI library | Build reusable discovery, lifecycle, feature, command, telemetry, capability and frame UI components; preview becomes a composition root. |
| 5 | Integrate the existing GenTL/GenApi implementation | Add a genuine CTI backend using the existing Consumer/GenApi implementation alongside native AmScope. |
| 6 | Complete capability, feature, command and model coverage | Reconcile SDKs, model profiles, common semantics, commands, telemetry and evidence-backed hardware support. |
| 7 | Release packaging and external-consumer integration | Export stable CMake packages, install public headers/runtime dependencies and verify clean consumers. |
| 8 | Final hardware qualification and release | Qualify declared supported camera models/backends and publish compatibility evidence and release artifacts. |

### 3.2 Phase 0 — Freeze and verify baseline

Preserve the original Phase 0 objective:
- identify the exact implementation branch and commit;
- reconcile historical branches and authoritative architecture records;
- record the physical reference camera and baseline acquisition behavior;
- inventory SDK calls, features, commands, telemetry, frame formats and model claims;
- distinguish production acquisition from diagnostic probes;
- record known defects and unresolved vendor-SDK assumptions.

**Historical acceptance:** the project owner accepted Phase 0 on 22 November 2026. The recorded MU503 runs and targeted resolution/frame observations remain historical evidence. They do not automatically certify newer commits, every camera model, a 15-minute run or every feature family.

### 3.3 Phase 1 — Stabilize and qualify native AmScope driver

The native driver owns the single AmScope SDK session and acquisition path.

Preserve:
- one `HAmcam` owner per opened camera;
- one acquisition worker performing image pulls;
- notification-only SDK callbacks and bounded callback-to-worker event transfer;
- retained immutable frame leases and safe frame-slot reuse;
- latest-frame/coalesced publication rather than an unbounded preview FIFO;
- distinct recoverable Stop failure, fatal acquisition failure and terminal Close semantics;
- SDK frame-cache flush obligations and retry behavior;
- operation serialization, watchdogs and stale-result protection.

**Highest-priority unresolved safety condition:** callback-entry counting drains callbacks already in flight but does not independently prove that no new callback can begin after `Amcam_Close` returns. Do not release callback-reachable resources until both in-flight drain and no-new-callback guarantees are established. A fake SDK can test controlled races; it cannot establish undocumented real-SDK behavior.

**Status:** Phase 1 was accepted by the owner historically. Post-acceptance deterministic SDK failure injection and callback-quiescence evidence remain hardening work. The native test-seam design has a documented Phase 0 hardware-entry condition; do not bypass it.

### 3.4 Phase 2 — Finalize backend-neutral camera contracts

The original Phase 2 has three delivery slices:

- **2.1 — Discovery/capability models:** opaque provider IDs, optional identity facts, resolution dimensions and Unknown/Unsupported/Supported capability knowledge.
- **2.2 — Frame/image-layout model:** dimensions, stride, payload, pixel layout, opaque format identity, significant source depth versus storage width, and optional metadata with timebases.
- **2.3 — Errors and remaining shared value types:** typed feature values/ranges/increments/enumerations, descriptor semantics and error/value ownership.

Semantic ownership is conservative:
- common feature access uses `CameraFeatureAccessError` and common feature result types;
- AmScope lifecycle/command operations retain `amscope::CameraError` and native SDK operation/code context;
- AmScope states, trigger/accessory types, model flags and native FourCC remain provider-owned;
- do not invent a universal lifecycle state or generic operation-error hierarchy until a second backend demonstrates genuinely common semantics.

**Status:** 2.1/2.2 source and automated test slices exist. The 2.3 ownership decision is documented in `CAMERA_COMMON_TYPE_OWNERSHIP.md`. Physical common-frame/RAW verification and the phase-wide real-camera regression remain necessary; Phase 2 is not accepted merely because its common types compile.

### 3.5 Phase 3 — Separate driver, programmatic control and Qt streaming libraries

Required responsibility boundaries:

| Library | Responsibility | Ownership |
|---|---|---|
| `camera_amscope_driver` / `Amscope::Driver` | Native SDK discovery, session, lifecycle, acquisition/recovery and native frame production | Sole AmScope `HAmcam` owner |
| Non-Qt programmatic control surface | Validated feature reads/writes, commands and telemetry | Uses the existing driver/session; never opens another handle |
| `Camera::Common` | Backend-neutral values, frame contracts and Qt-independent mailbox primitive | No session, SDK or UI ownership |
| `camera_amscope_streaming_qt` / Qt adapter | Qt notification/thread handoff and latest-frame delivery | Observes the same driver session; no second acquisition |
| `camera_ui_qt` | Reusable widgets | Common UI boundary; no SDK calls |

The names and target boundaries may vary, but the ownership semantics are mandatory. Do not add a forwarding-only `camera_amscope_control` wrapper if `AmcamCamera` already provides the non-Qt programmatic API. Do not leave all control/streaming semantics hidden in a single Qt-only public API.

Current source:
- `AmcamCamera` is a non-Qt C++ API and sole session owner.
- `Camera::Common` has a non-Qt latest-frame mailbox.
- `Camera::Qt` and `QtAmcamCamera` provide existing Qt scheduling/stream handoff.
- The current Qt facade combines a serialized request worker with Qt frame notification; Phase 3 must reconcile this against the target responsibility split.
- The Qt facade header is physically isolated under `Camera/Amscope/legacy/qt` and is exposed only by the optional Qt target. This is dependency isolation, not removal of the Qt deliverable.

**Status:** In progress. Non-Qt CI is green. The separate Qt job has been added and must pass. Physical same-session control/streaming regression remains required by the original phase.

### 3.6 Phase 4 — Extract reusable Qt camera UI library

The original reusable UI phase remains in scope. Current reusable source includes:
- `Camera/QtWidgets/FrameView`;
- `Camera/QtWidgets/CameraControlPanel`;
- `Camera/QtWidgets/ImageInfoBar`.

The preview executable is a composition/example, not the reusable library's ownership boundary.

Remaining work includes:
- generic discovery and lifecycle controls;
- descriptor-driven feature editors with current-value loading and unsupported/unavailable states;
- separate command/action controls;
- explicit telemetry presentation and units;
- capability/availability presentation;
- still-capture interaction and truthful RAW presentation;
- backend-neutral common widgets with backend-specific extensions where justified;
- external Qt consumer composition.

**Status:** Partially implemented. Historical v0.7 FrameView physical smoke and owner-reported build/test evidence cover only specific slices. The latest workspace/UI state and all phase-wide criteria have not been reaccepted.

### 3.7 Phase 5 — Integrate existing GenTL/GenApi implementation

This is the second-provider track, not permission to reimplement standards.

Required prerequisites:
- identify the existing Consumer implementation and choose its C++/C ABI integration;
- acquire the actual camera-vendor CTI Producer;
- verify ABI/version/licensing/architecture/GenApi XML and port access;
- qualify on a physical CTI camera.

Keep AmScope on the native Amcam SDK path. Do not fabricate an AmScope CTI Producer or GenApi nodes/registers. Phase 5 remains **BLOCKED** until the real Producer/camera and Consumer boundary exist. Run AmScope regression after integration.

### 3.8 Phase 6 — Complete feature, command, telemetry and model coverage

Use the implementation inventory as the checklist. Classify every SDK operation as common setting, provider-specific setting, command/action, telemetry, internal lifecycle/acquisition, unsupported or deferred.

Verify descriptor kind, units, range/increment, enum symbols, vector cardinality, availability, readability/writability, preconditions, asynchronous completion, failure behavior and native readback. Model profiles may narrow but never invent capability.

Maintain distinctions:
- configured FPS, SDK/device FPS and host arrival FPS;
- AmScope pixel formats and PFNC;
- AmScope gain percent and GenICam gain dB;
- command accepted and physical operation complete;
- unavailable metadata and numeric zero.

Physical checks must use hardware that actually advertises the feature. One MU503 does not certify all models/accessories.

### 3.9 Phase 7 — Release packaging and external consumers

Export stable namespaced CMake targets, package config/version files, public headers and runtime dependencies. Verify RPATH/install names, licensing and redistribution terms.

Required consumer configurations:
- clean non-Qt consumer using only the core driver/common contracts;
- clean Qt consumer using the explicitly exported streaming/UI targets;
- no repository-relative paths;
- no Qt leakage into non-Qt consumers;
- provider SDK dependencies private unless that provider is selected.

**Status:** Initial install rules exist. The non-Qt installed-header and syntax check has passed. Exported package/config/version files and clean external consumer projects remain open.

### 3.10 Phase 8 — Final hardware qualification and release

Freeze exact release candidates and run the complete declared provider/model hardware matrix. Verify lifecycle, sustained acquisition, frame layout/RAW, feature read/write/readback/restoration, still capture, triggers, I/O and accessories where supported, reconnect/recovery, installed consumer behavior and absence of stale callbacks/frames, duplicate handles or unbounded queues.

Publish exact tested revisions, hardware records, compatibility matrix, limitations and release tag. The AmScope-native release may be qualified independently, but the complete multi-backend Camera Module is not complete until the real CTI Phase 5 gate also passes.

## 4. Relationship to later AmScope-native release plan

`AMSCOPE_NATIVE_RELEASE_PLAN.md` is a narrower AmScope-native execution plan. It is subordinate to the original Phase 0–8 roadmap.

Mapping of its phase labels to the original governing roadmap:
- release Phase 0 → original Phase 0;
- release Phase 1 → original Phase 1;
- release Phase 2 → original Phase 2;
- release Phase 3 → original Phase 3;
- release Phase 4 (reusable Qt UI) → original Phase 4;
- separate GenTL/GenApi follow-on → original Phase 5;
- release Phase 5 (feature/model coverage) → original Phase 6;
- release Phase 6 (packaging) → original Phase 7;
- release Phase 7 (AmScope release qualification) → original Phase 8.

An AmScope-native release does not imply the GenTL/GenApi second backend or the complete multi-backend Camera Module is finished.

## 5. Branch and source history

| Branch / source snapshot | Role |
|---|---|
| `amscope-native-driver` and early `amscope-native-driver-0.2` | Native AmScope foundation |
| `amscope-native-driver-v.0.2` | Authoritative original Phase 0–8 roadmap source |
| `amscope-native-driver-0.3` through `0.5` | Native session/acquisition/features and diagnostics |
| `amscope-native-driver-0.6` | Common camera contracts and frame representation |
| `amscope-native-driver-0.7` | Qt streaming adapter and reusable Qt Widgets implementation history |
| `amscope-native-app-mac.v.0.8` and dock-system branch | Native macOS application integration history |
| `OPTIKA-B293-DUALSLIDE-APP-v.0.9` | Camera source snapshot; 307 Camera blobs match the initial `camera-v.2.0` baseline |
| `camera-v.2.0` | Canonical Camera Module development branch |
| `OPTIKA-B293-DUALSLIDE-APP` | Separate Optika SwiftUI/AppKit application |
| `project_manager` | Workstream governance and continuation handoff |

The separate `AMSCOPE_DRIVER` and `AMSCOPE_MU503_DRIVER` repositories contain research/prototype histories. They are not separate active production camera projects in this register.

## 6. Current implementation and evidence ledger

This ledger separates source presence, automated evidence, physical evidence and owner acceptance.

| Phase | Current assessment | Evidence / remaining work |
|---:|---|---|
| 0 | Historically accepted by owner | MU503 records exist; extended/exact-commit evidence does not certify all models. |
| 1 | Historically accepted; hardening remains | Native single-session driver exists. Callback quiescence and full SDK lifecycle fault injection remain material risks. |
| 2 | Partially implemented | Common models/frame contracts and 2.3 ownership decision exist. Physical common-frame/RAW qualification remains. |
| 3 | In progress | Non-Qt API works as a build target; optional Qt adapter exists. Separate responsibilities and same-session physical regression remain. Non-Qt CI passed; Qt CI validation pending. |
| 4 | Partially implemented | FrameView, CameraControlPanel and ImageInfoBar exist. Full reusable UI extraction and phase-wide hardware/UI verification remain. |
| 5 | Blocked follow-on | Existing Consumer integration choice, target CTI Producer and physical CTI camera remain prerequisites. |
| 6 | Open | Broad implementation exists; reconcile every feature and model claim against source, tests and hardware. |
| 7 | Open | Initial install rules and non-Qt header checks exist; exported package and clean non-Qt/Qt consumers remain. |
| 8 | Not accepted | Full release-candidate hardware matrix and exact-commit records remain outstanding. |

### Physical and automated evidence already recorded

- MU503 RGB24: 1280 × 960, 3,840-byte stride and 3,686,400-byte payload for the recorded run.
- Historical v0.7 FrameView physical smoke: discovery/open/start/changing frames/stop/close. It validates that UI slice only, not the current complete Phase 4 UI.
- Separate MU503 short diagnostic runs reported approximately 91 FPS at 640 × 480 with zero reported drops. Do not conflate those with the separate 835-frame/60.722-second run or infer long-duration qualification.
- Source comparison found 307 files under `Camera/` identical between `OPTIKA-B293-DUALSLIDE-APP-v.0.9` and the initial `camera-v.2.0` baseline. No Camera subtree copy was missing.
- Non-Qt CI run [#45654331539](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45654331539) passed on Intel x86_64 and Apple Silicon arm64 at code checkpoint `11ff79ac115e5f4ecbcbe95eb70f4391c48a21d8`: 12/12 CTest, non-Qt header install, Qt facade absence and installed-header syntax compilation. This is software evidence, not physical-camera qualification.
- A separate Qt streaming/UI job has been added to `.github/workflows/amscope-macos.yml`; its first run must be checked against its exact source revision before claiming Qt CI validation.
- Optika's Objective-C++ bridge, NativeCameraController and Swift frame feed are application-owned. They do not create a second AmScope SDK session.

### Unresolved risks and gaps

1. Prove callback quiescence and safe callback-reachable resource release around SDK Close.
2. Complete deterministic native SDK lifecycle fault injection after satisfying its documented physical-baseline entry gate.
3. Complete production RAW/common-frame hardware qualification.
4. Complete Phase 3 separation of non-Qt control and Qt streaming over one session.
5. Complete Phase 4 reusable UI coverage and physical regression.
6. Reconcile all SDK features, commands, telemetry and model profiles to hardware evidence.
7. Resolve GenTL Consumer integration and acquire a real CTI Producer/camera.
8. Complete exported CMake package and clean non-Qt/Qt external-consumer qualification.
9. Complete Phase 8 release hardware matrix.
10. Verify vendor SDK redistribution terms.

## 7. Architectural invariants

1. One native SDK session owner per opened camera.
2. One production AmScope acquisition worker.
3. SDK callbacks signal work; they do not perform expensive frame processing or application/UI work.
4. Callback-to-worker event transfer is bounded.
5. Preview publication is latest-frame/coalesced, not an unbounded FIFO of obsolete frames.
6. Frame payload ownership is retained through immutable leases; do not add copies without measured need and explicit semantics.
7. Feature/command scheduling must not block acquisition.
8. Stop/Close respect callback and worker lifetimes; failed cleanup cannot silently release device ownership.
9. Camera APIs expose actual readback and explicit unknown/unavailable states, not assumed settings.
10. Vendor-only concepts remain in provider extensions unless another provider demonstrates the same common meaning.
11. Camera acquisition is independent of Controller/Stage, Slide Scanning and ImageProcessing execution.
12. A camera frame is source evidence; processing/display transformations must not mutate the original acquisition payload.
13. The core/common library has no Qt dependency.
14. Optional Qt streaming/UI targets consume the same driver/frame contracts; they do not open another SDK handle or create another acquisition engine.
15. Reusable UI components may be supplied by the Camera Module; application shells and workspace/presentation state remain application-owned.

## 8. Continuation handoff

**Governing roadmap:** original Camera Module Phases 0–8 from `amscope-native-driver-v.0.2`.  
**Current Camera development branch:** `camera-v.2.0`.  
**Latest confirmed non-Qt code/CI checkpoint:** `11ff79ac115e5f4ecbcbe95eb70f4391c48a21d8`; workflow [#45654331539](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45654331539).  
**Current application integration baseline:** `OPTIKA-B293-DUALSLIDE-APP` at `4c278aed3af29e51b7485a2902080d84314af73b`.  
**Management branch:** `project_manager`.

Before changing Camera code:
1. Identify the original Phase 0–8 deliverable and exit criterion affected.
2. Verify the exact `camera-v.2.0` HEAD and inspect commits since the last handoff.
3. Keep Camera separate from Optika GUI/workspace, Controller/Stage, Slide Scanning and ImageProcessing.
4. Keep non-Qt core consumers independent of Qt; do not remove the optional Phase 3/4 Qt deliverables.
5. Preserve one session owner, callback/queue/worker/pull semantics and immutable frame leases.
6. Add deterministic tests for changed semantics, but do not mistake mocks/CI for real-camera evidence.
7. Record exact source revision, automated result, physical hardware record and owner acceptance separately.
8. Update this handoff with phase status, evidence, unresolved risks and next bounded task.

## 9. Authoritative records

- [Original governing Camera Module roadmap — Phases 0–8](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/amscope-native-driver-v.0.2/docs/architecture/AMSCOPE_NATIVE_DRIVER_COMPLETION_PLAN.md)
- [Camera v2.0 development status](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/camera-v.2.0/docs/architecture/CAMERA_V2_DEVELOPMENT_STATUS.md)
- [Subordinate AmScope-native release plan](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/camera-v.2.0/docs/architecture/AMSCOPE_NATIVE_RELEASE_PLAN.md)
- [AmScope implementation inventory](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/camera-v.2.0/docs/architecture/AMSCOPE_IMPLEMENTATION_INVENTORY.md)
- [Camera common type ownership](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/camera-v.2.0/docs/architecture/CAMERA_COMMON_TYPE_OWNERSHIP.md)
- [Private native SDK test-seam design](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/camera-v.2.0/docs/architecture/AMSCOPE_NATIVE_DRIVER_TEST_SEAM.md)
- [GenTL/GenApi integration assessment](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/camera-v.2.0/docs/architecture/AMSCOPE_GENTL_GENAPI_CPP_INTEGRATION_ASSESSMENT.md)
- [Optika native camera application architecture](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/OPTIKA-B293-DUALSLIDE-APP/docs/architecture/AMSCOPE_NATIVE_MACOS_APP.md)
