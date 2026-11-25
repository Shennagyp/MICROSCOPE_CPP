# Workstream — Camera

**Canonical project:** the reusable Camera Module in [MICROSCOPE_CPP](https://github.com/Shennagyp/MICROSCOPE_CPP), under `Camera/`  
**Management branch:** `project_manager`  
**Camera development branch:** `camera-v.2.0`  
**Latest Camera status checkpoint:** pending update from `camera-v.2.0` commit `682724d04e9abaea4f91e73d7e3de57136421045`  
**Latest CI-validated GenApi checkpoint:** `f049f91d6afe1c9f785b60ce344d216b6d251a59` (official GenApi 3.5.1 SDK build passed Intel x86_64 + Apple Silicon arm64, run [#45688350553](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45688350553)). **Latest frame-adapter fix:** `682724d04e9abaea4f91e73d7e3de57136421045`, workflow [#45690108087](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45690108087) pending.  
**CI:** GenTL stream baseline [#45682828203](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45682828203) passed both architectures; official GenApi SDK compile [#45688350553](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45688350553) passed both architectures. The latest empty-payload regression workflow [#45690108087](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45690108087) is pending.  
**Library source branch:** `OPTIKA-B293-DUALSLIDE-APP-v.0.9` at `c7bb4b4dc41441989d71ee800c6d33b51dc54abe`  
**Optika application branch:** `OPTIKA-B293-DUALSLIDE-APP` at `4c278aed3af29e51b7485a2902080d84314af73b`  
**Roadmap authority:** original Camera Module Phases 0–8, preserved for v2.0.

## 1. Scope and ownership

There is **one Camera Module workstream** in this repository.

The Camera Module owns:
- provider discovery and provider-qualified opaque device identity;
- native camera session ownership and lifecycle;
- backend-neutral camera identity, capabilities, feature values, command/result and frame contracts;
- programmatic settings, commands, telemetry, acquisition and frame consumption;
- provider-specific implementations and model profiles.

The active Camera v2.0 scope is a non-UI native/programmatic library. The project owner has removed Qt, preview and UI work from the roadmap. Existing Qt/UI source remains in the repository as legacy code but must not be extended, refactored or treated as a release dependency.

The Camera Module does **not** own:
- an application's main window, application workspace, dock arrangement or application-specific view models;
- Optika's SwiftUI/AppKit presentation state;
- microscope optical configuration or objective/filter coordination;
- controller and stage semantics;
- Sample/Slide/Tile identity or slide-scan orchestration;
- image-processing algorithms or application GPU policy.

The core driver and common contracts are the complete active product boundary. Qt is not an optional deliverable in the revised roadmap. Do not add Qt dependencies, streaming adapters, widgets, previews or UI consumer packages.

Do not split this work into separate production projects named AmScope, MU503 or GenTL. Provider adapters, device profiles and standards compatibility remain parts of this one Camera Module.

## 2. Optika application integration and ownership boundary

The current Optika application branch is `OPTIKA-B293-DUALSLIDE-APP`, inspected at `4c278aed3af29e51b7485a2902080d84314af73b`. Camera library development continues on `camera-v.2.0`, not on the application branch.

The Optika GUI is implemented with **SwiftUI/AppKit**, not Qt. Its camera-facing composition is visible in:
- `Application/OptikaB293/Sources/Features/Camera/NativeCameraModel.swift` — application camera state and lifecycle orchestration;
- `Application/OptikaB293/Sources/Features/Camera/NativeCameraFeatureModel.swift` — application feature snapshot, read/write and editor state;
- `Application/OptikaB293/Sources/Adapters/Camera/AmscopeNativeCameraBridge.h/.mm` — Objective-C++ boundary to the C++ camera controller and frame lease;
- `Application/OptikaB293/Sources/Adapters/Camera/NativeCameraController.cpp/.h` — application adapter/controller and serialized command execution;
- `Application/OptikaB293/Sources/Adapters/Camera/NativeCameraFrameFeed.swift` — narrow latest-frame consumption protocol.

The Optika application target links `Amscope::Driver` and native Apple frameworks. Its application shell and presentation are not supplied by the Camera Module. The Optika application owns its presentation. No reusable Qt UI or preview deliverable remains in the active Camera Module scope.

Ownership remains layered:
1. `AmcamCamera` is the sole owner of the AmScope SDK handle/session and native acquisition.
2. The application-side `NativeCameraController` serializes application requests and delegates to the driver; it must not open a competing SDK handle.
3. The Objective-C++ bridge translates the application boundary and retains frame leases as required.
4. SwiftUI/AppKit models and views own Optika presentation state only. They do not own the SDK session or pull frames directly from the vendor SDK.
5. The frame feed is a narrow consumer surface. It does not become a second acquisition engine or a second frame-history queue.
6. Camera remains presentation-agnostic; UI and preview are application responsibilities.

## 3. Governing roadmap — original Camera Module Phases 0–8

The original *AmScope Native Driver and Camera Module Completion Plan* remains the historical source of phase identities. The owner-revised active roadmap excludes original Phases 3 and 4 (Qt streaming and reusable UI); those phases are retained in this handoff only for historical traceability.

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
| 3 | Separate driver, programmatic control and Qt streaming libraries | **Excluded from active scope by owner decision.** Keep the existing non-Qt programmatic API; no Qt streaming work is required. |
| 4 | Extract the reusable Qt camera UI library | **Excluded from active scope by owner decision.** No Qt UI or preview deliverable is required. |
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

### 3.5 Phase 3 — Qt streaming (excluded from active scope)

The original Phase 3 proposed a Qt streaming adapter. The project owner has removed Qt from active Camera v2.0 scope. Keep the existing non-Qt `AmcamCamera` programmatic control/session API as the active boundary. Existing `Camera/Qt` and `QtAmcamCamera` source is retained as legacy code; do not extend, refactor, qualify or make it a release dependency.

**Status:** Excluded by owner decision. It is not a current deliverable or acceptance gate.

### 3.6 Phase 4 — Reusable Qt camera UI (excluded from active scope)

The original Phase 4 proposed reusable Qt widgets and preview composition. The owner has removed UI/preview work from active scope. The Optika application already owns its SwiftUI/AppKit preview. Existing `Camera/QtWidgets` and `amscope_preview` code is retained as legacy source only.

**Status:** Excluded by owner decision. Do not implement UI widgets, preview applications or UI consumer integration.

`Camera/Qt`, `Camera/QtWidgets`, `QtAmcamCamera` and `amscope_preview` must not be treated as required release components. Their historical CI records remain evidence for the code that existed at those commits, not an obligation to continue those workstreams.

### 3.7 Phase 5 — Integrate existing GenTL/GenApi implementation

This is the second-provider track, not permission to reimplement standards.

Current implementation on `camera-v.2.0` under `Camera/GenTL`:
- version-checked C++ dynamic bridge adapter for the Swift C ABI Consumer (bridge ABI 1, GenTL header 1.6);
- CTI initialization, TL/interface/device discovery, retained interface/device ownership and device opening;
- data-stream opening, `EVENT_NEW_BUFFER` registration, bounded buffer allocation/announcement/queueing, acquisition start/stop, event wait, payload/metadata copy, buffer requeue and ordered cleanup;
- discovery/frame timeout conversion from public milliseconds to GenTL nanoseconds;
- frame ID, timestamp, dimensions, pixel-format value and PFNC namespace preservation;
- device and stream shared ownership, including stream lifetime beyond the public DeviceSession wrapper;
- process-lifetime quarantine retention of the Consumer context and dynamic C ABI bridge after native cleanup failure;
- remote-port read/write and GenApi XML retrieval from `LOCAL` register-map and `FILE` URL schemes;
- optional `Camera::GenTLGenApi` target using official GenApi 3.5.1. The official EMVA GenICam 2026.07 SDK/runtime are downloaded, hash-verified and the target compiles on Intel x86_64 and Apple Silicon arm64 in CI [#45688350553](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45688350553).

C++ mock CI [#45682828203](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45682828203) passed on Intel x86_64 and Apple Silicon arm64. The official GenApi SDK build [#45688350553](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45688350553) also passed both architectures. The test includes an injected `DSClose` failure and verifies that the Consumer bridge remains retained after public wrappers are released.

The Swift C ABI source/tests/workflow are on private `AMSCOPE_DRIVER:gentl-c-api-v.1.0` at `2f8f82d1e4b645a1191bf68021f9803ac6843121`. Tests cover typed pointer/buffer marshalling, event registration/read/unregister and buffer-info forwarding. The workflow now attempts separate x86_64/arm64 builds and universal dylib assembly, but [#45684107190](https://github.com/Shennagyp/AMSCOPE_DRIVER/actions/runs/45684107190) was cancelled before build/test steps. The real dylib and operation mapping remain unvalidated. The Swift GenTL packages currently declare macOS 13.0 as their minimum deployment target.

Still required:
- validate the actual Swift C ABI dylib and its runtime packaging;
- add GenApi runtime tests using valid XML and a mock GenTL port; validate typed GenApi nodes against the Common feature contract and keep commands separate;
- map verified GenTL pixel layouts/stride into the Common immutable `camera::Frame` contract without guessing unsupported layouts;
- acquire the actual camera-vendor CTI Producer and qualify on physical CTI hardware.

Keep AmScope on the native Amcam SDK path. Do not fabricate an AmScope CTI Producer or GenApi nodes/registers. Phase 5 is **IN PROGRESS, not accepted**. See [Camera/GenTL README](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/camera-v.2.0/Camera/GenTL/README.md).

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

**Status:** Initial install rules exist. The non-Qt installed-header and syntax check has passed. Exported package/config/version files and clean native/GenTL external consumer projects remain open; Qt consumers are excluded from the revised scope.

### 3.10 Phase 8 — Final hardware qualification and release

Freeze exact release candidates and run the complete declared provider/model hardware matrix. Verify lifecycle, sustained acquisition, frame layout/RAW, feature read/write/readback/restoration, still capture, triggers, I/O and accessories where supported, reconnect/recovery, installed consumer behavior and absence of stale callbacks/frames, duplicate handles or unbounded queues.

Publish exact tested revisions, hardware records, compatibility matrix, limitations and release tag. The AmScope-native release may be qualified independently, but the complete multi-backend Camera Module is not complete until the real CTI Phase 5 gate also passes.

## 4. Relationship to later AmScope-native release plan

`AMSCOPE_NATIVE_RELEASE_PLAN.md` is a narrower AmScope-native execution plan. It is subordinate to the original Phase 0–8 roadmap.

Mapping of its phase labels to the original governing roadmap:
- release Phase 0 → original Phase 0;
- release Phase 1 → original Phase 1;
- release Phase 2 → original Phase 2;
- release Phases 3 and 4 (Qt streaming/UI) → excluded from active scope by owner decision;
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
| 3 | Excluded | Owner removed Qt streaming from active scope. `AmcamCamera` remains the active non-Qt control/session API. |
| 4 | Excluded | Owner removed UI/preview work from active scope. Existing widgets/preview code is legacy and not an acceptance target. |
| 5 | In progress | C++ bridge, producer/device/stream lifecycle, buffer requeue, and mock-tested frame delivery exist. `GenTLFrameAdapter` maps only explicitly safe PFNC Mono8 image buffers into Common frames; other layouts remain opaque. `GenApiFeatureAccess` maps scalar/enumerated features; `GenApiCommandAccess` and the separate `ICameraCommandAccess` contract now model command issue and completion separately. Real Swift C ABI validation, official GenApi 3.5.1 SDK compilation, vendor CTI/physical-camera qualification and broader PFNC layout mapping remain.
| 6 | Open | Broad implementation exists; reconcile every feature and model claim against source, tests and hardware. |
| 7 | Open | Initial install rules and non-Qt header checks exist; exported package and clean non-Qt/Qt consumers remain. |
| 8 | Not accepted | Full release-candidate hardware matrix and exact-commit records remain outstanding. |

### Physical and automated evidence already recorded

- MU503 RGB24: 1280 × 960, 3,840-byte stride and 3,686,400-byte payload for the recorded run.
- Historical v0.7 FrameView physical smoke: discovery/open/start/changing frames/stop/close. It validates that UI slice only, not the current complete Phase 4 UI.
- Separate MU503 short diagnostic runs reported approximately 91 FPS at 640 × 480 with zero reported drops. Do not conflate those with the separate 835-frame/60.722-second run or infer long-duration qualification.
- Source comparison found 307 files under `Camera/` identical between `OPTIKA-B293-DUALSLIDE-APP-v.0.9` and the initial `camera-v.2.0` baseline. No Camera subtree copy was missing.
- Workflow [#45667125968](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45667125968) passed at `e12d765f408c2b0be7100c64b2eb245de5a2aca4`: non-Qt 12/12 CTest tests on Intel and Apple Silicon; Qt 18/18 CTest tests on Intel and Apple Silicon. The Qt build includes the concrete `Camera::Qt` stream library, its handoff tests, reusable widgets, AmScope Qt request facade and preview composition. Installed non-Qt header checks and architecture/artifact verification also passed.
- GenTL C++ workflow [#45682828203](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45682828203) passed at `549433466098c5daeb63d72609776d958a35c2fe`: 2/2 CTest tests on Apple Silicon arm64 and 2/2 on Intel x86_64, with architecture checks. Coverage includes stream open/start/event wait, buffer announcement/queue/requeue, copied payload and frame metadata, PFNC namespace, timeout conversion, stream/device lifetime and cleanup-failure quarantine retention.
- The GenTL workflow uses a mock C ABI bridge. It does not validate the Swift C ABI dylib, a vendor CTI Producer, actual camera acquisition, GenApi parsing/feature mapping or physical hardware.
- Swift C ABI source/tests/workflow are at `AMSCOPE_DRIVER:gentl-c-api-v.1.0`, commit `2f8f82d1e4b645a1191bf68021f9803ac6843121`; latest workflow [#45684107190](https://github.com/Shennagyp/AMSCOPE_DRIVER/actions/runs/45684107190) was cancelled before build/test steps. Its universal dylib and C ABI tests remain unvalidated. The package's declared minimum macOS version is 13.0.
- Core/Qt workflow [#45681878032](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45681878032) passed all four non-Qt/Qt Intel and Apple Silicon jobs after the Common CMake target was made independent of AmScope.

- The optional `Camera::GenTLGenApi` target is gated by `CAMERA_GENTL_ENABLE_GENAPI=ON`; the official GenApi 3.5.1 Mac64_Universal SDK/runtime build now passes on both architectures in workflow [#45688350553](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45688350553).
- No real CTI Producer or physical CTI camera has been qualified.
- This is software evidence only. It does not constitute physical-camera regression, same-session control/stream verification, or phase acceptance.
- Optika's Objective-C++ bridge, NativeCameraController and Swift frame feed are application-owned. They do not create a second AmScope SDK session.

### Unresolved risks and gaps

1. Prove callback quiescence and safe callback-reachable resource release around SDK Close.
2. Complete deterministic native SDK lifecycle fault injection after satisfying its documented physical-baseline entry gate.
3. Complete production RAW/common-frame hardware qualification.
4. Complete Phase 3 same-session physical regression for non-Qt control and Qt streaming; source-level Qt frame handoff is separated and CI-validated at `e12d765`.
5. Complete Phase 4 reusable UI coverage and physical regression.
6. Reconcile all SDK features, commands, telemetry and model profiles to hardware evidence.
7. Validate the real Swift C ABI bridge, implement GenTL stream acquisition, install GenApi 3.5.1 and qualify against a real CTI Producer/camera.
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
13. The active Camera Module has no Qt or UI dependency.
14. Camera remains a programmatic library; application shells and presentation state belong to consuming applications.

## 8. Continuation handoff

**Governing roadmap:** original Camera Module Phases 0–8 from `amscope-native-driver-v.0.2`.  
**Current Camera development branch:** `camera-v.2.0`.  
**Latest Camera status checkpoint:** to be updated from `camera-v.2.0`. **Latest implementation checkpoint:** `682724d04e9abaea4f91e73d7e3de57136421045`. The official GenApi 3.5.1 SDK build passed Intel and Apple Silicon in [#45688350553](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45688350553). The empty-payload frame-adapter regression run [#45690108087](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45690108087) is pending Apple Silicon completion. The earlier bridge/stream baseline `549433466098c5daeb63d72609776d958a35c2fe` passed both architectures in [#45682828203](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45682828203). Historical Core/Qt workflow [#45681878032](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45681878032) passed all four jobs; Qt/UI is now excluded from active scope.  
**Current application integration baseline:** `OPTIKA-B293-DUALSLIDE-APP` at `4c278aed3af29e51b7485a2902080d84314af73b`.  
**Management branch:** `project_manager`.

Before changing Camera code:
1. Identify the original Phase 0–8 deliverable and exit criterion affected.
2. Verify the exact `camera-v.2.0` HEAD and inspect commits since the last handoff.
3. Keep Camera separate from Optika GUI/workspace, Controller/Stage, Slide Scanning and ImageProcessing.
4. Keep the active Camera Module free of Qt/UI dependencies; do not extend the legacy Qt/UI code unless the owner reopens that scope.
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
