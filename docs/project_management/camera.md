# Workstream — Camera

**Canonical project:** the reusable camera subsystem in [MICROSCOPE_CPP](https://github.com/Shennagyp/MICROSCOPE_CPP), under `Camera/`  
**Management branch:** `project_manager`  
**Current Camera implementation branch:** `camera-v.2.0`  
**Current Camera branch HEAD:** `2b208d01f70651633a3fc6682566670cf2b3b242`  
**Camera library source branch:** `OPTIKA-B293-DUALSLIDE-APP-v.0.9` at `c7bb4b4dc41441989d71ee800c6d33b51dc54abe`  
**Current Optika application branch:** `OPTIKA-B293-DUALSLIDE-APP` at `4c278aed3af29e51b7485a2902080d84314af73b`  
**Roadmap revision:** Camera Module v2.0 — non-Qt, no camera-UI deliverable

## 1. Scope and ownership

There is **one Camera Module workstream** in this repository.

The Camera Module owns:
- camera-provider discovery and opaque device identity;
- native camera session ownership and lifecycle;
- backend-neutral camera identity, capability, feature, command-result, error and frame contracts;
- programmatic camera control;
- acquisition and backend-neutral frame delivery;
- provider-specific adapters, beginning with the native AmScope SDK backend.

The Camera Module does **not** own:
- application GUI, windows, panels, widgets, docks, view models or presentation state;
- microscope optical configuration or objective/filter coordination;
- controller and stage semantics;
- Sample/Slide/Tile identity or slide-scan orchestration;
- image-processing algorithms or GPU policy;
- application workspace state.

**There is no Qt requirement and no reusable camera-UI deliverable in the revised roadmap.** The camera is a programmatic capability consumed by applications. The Optika application owns its GUI and its application-facing camera presentation/integration.

Do not split this work into separate projects named AmScope, MU503 or GenTL. Provider adapters, device profiles and standards compatibility remain parts of this one Camera Module.

## 2. Current application integration and ownership boundary

The current Optika application branch is `OPTIKA-B293-DUALSLIDE-APP`, inspected at `4c278aed3af29e51b7485a2902080d84314af73b`. Camera library development does not continue on this application branch.

The application GUI is implemented with **SwiftUI/AppKit**, not Qt. Its camera-facing composition is visible in:
- `Application/OptikaB293/Sources/Features/Camera/NativeCameraModel.swift` — application camera state and lifecycle orchestration;
- `Application/OptikaB293/Sources/Features/Camera/NativeCameraFeatureModel.swift` — application feature snapshot, read/write and editor state;
- `Application/OptikaB293/Sources/Adapters/Camera/AmscopeNativeCameraBridge.h/.mm` — Objective-C++ boundary to the C++ camera controller and frame lease;
- `Application/OptikaB293/Sources/Adapters/Camera/NativeCameraController.cpp/.h` — application adapter/controller and serialized command execution;
- `Application/OptikaB293/Sources/Adapters/Camera/NativeCameraFrameFeed.swift` — narrow latest-frame consumption protocol.

The Optika application target links `Amscope::Driver` and the native Apple frameworks (SwiftUI, AppKit, Foundation, CoreGraphics and ImageIO). Its GUI is an application concern; the Camera Module does not supply or own those presentation components.

Ownership remains layered:
1. `AmcamCamera` is the sole owner of the AmScope SDK handle/session and acquisition implementation.
2. The application-side `NativeCameraController` serializes application requests and delegates to the driver; it must not open a competing SDK handle.
3. The Objective-C++ bridge translates the application boundary and retains frame leases as required.
4. SwiftUI/AppKit models and views own presentation state only. They do not own the SDK session or pull frames directly from the vendor SDK.
5. The frame feed is a narrow consumer surface. It does not become a second acquisition engine or a second frame-history queue.

The application GUI changes do not justify moving UI abstractions into `Camera/` or coupling camera contracts to SwiftUI/AppKit.

## 3. Revised governing roadmap

The original *AmScope Native Driver and Camera Module Completion Plan* remains an authoritative **historical baseline** for why the project exists and for the original technical requirements. Its Qt/UI-specific execution structure is superseded by this revision.

Historical source:
- Repository: `Shennagyp/MICROSCOPE_CPP`
- Branch: `amscope-native-driver-v.0.2`
- Path: `docs/architecture/AMSCOPE_NATIVE_DRIVER_COMPLETION_PLAN.md`
- Original baseline date: 22 November 2026
- Original phases: 0–8

The revised roadmap preserves the substantive driver, common-model, multi-backend, capability, packaging and hardware-qualification goals. It removes Qt streaming/UI as deliverables and relocates all presentation responsibilities to the consuming application.

### 3.1 Revised phases

| Revised phase | Name | Objective |
|---:|---|---|
| 0 | Freeze and verify the baseline | Preserve the physical-camera baseline, reconcile source branches, verify SDK behavior and inventory the actual implementation. |
| 1 | Stabilize and qualify the native AmScope driver | Establish safe session ownership, acquisition, lifecycle, callback handling, failure recovery and frame management. |
| 2 | Finalize backend-neutral camera contracts | Complete identity, discovery, capabilities, features, values, commands/results, errors and frame/image-layout contracts without vendor leakage. |
| 3 | Finalize the non-Qt programmatic camera and frame-delivery boundary | Make the driver and common frame-delivery contracts independently consumable without Qt, UI libraries or application presentation dependencies. |
| 4 | Integrate the existing GenTL/GenApi implementation | Add a genuine CTI backend using an existing GenTL Consumer/GenApi implementation alongside the native AmScope backend. |
| 5 | Complete capability, feature, command and model coverage | Reconcile provider SDKs, common semantics, model profiles, operations, telemetry and evidence-backed hardware support. |
| 6 | Release packaging and external-consumer integration | Deliver stable CMake packages, public headers, runtime dependencies and clean non-Qt external consumers. |
| 7 | Final hardware qualification and release | Qualify declared supported camera models/backends and publish compatibility evidence and release artifacts. |

### 3.2 Phase 0 — Freeze and verify the baseline

Preserve the original Phase 0 objective:
- identify the exact implementation branch and commit;
- reconcile historical branches and authoritative architecture records;
- record the physical reference camera and baseline acquisition behavior;
- inventory implemented SDK calls, features, commands, telemetry, frame formats and model claims;
- separate production acquisition from diagnostic probes;
- record known defects and unresolved vendor-SDK assumptions.

**Acceptance:** an exact source baseline, implementation inventory and physical-camera record exist. Existing owner acceptance remains historical evidence; do not silently transfer it to a newer commit.

### 3.3 Phase 1 — Stabilize and qualify the native AmScope driver

The native driver owns the single AmScope SDK session and acquisition path.

Preserve and qualify:
- one `HAmcam` owner per opened camera;
- one acquisition worker performing image pulls;
- notification-only SDK callbacks and bounded callback-to-worker event transfer;
- retained immutable frame leases and safe frame-slot reuse;
- latest-frame/coalesced publication rather than an unbounded preview FIFO;
- lifecycle transitions, failure recovery, Stop/Close semantics and SDK frame-cache flush obligations;
- command serialization and stale-result protection;
- watchdog and acquisition-fault behavior.

**Critical unresolved safety condition:** callback-entry counting can drain callbacks already in flight, but does not independently prove that no callback can begin after `Amcam_Close` returns. Do not release callback-reachable resources until both in-flight drain and no-new-callback guarantees are established by vendor evidence or a defensible synchronization design. A fake SDK can test controlled races; it cannot establish undocumented behavior of the real SDK.

**Acceptance:** deterministic lifecycle/failure tests plus physical AmScope evidence for the tested implementation. Keep recoverable Stop failure, fatal acquisition failure and terminal Close as distinct semantics.

### 3.4 Phase 2 — Finalize backend-neutral camera contracts

Complete the common contracts in three slices, preserving the original roadmap's decomposition:

- **2.1 — Discovery and capability models:** provider-qualified device identity, opaque IDs, model identity, capability knowledge states and resolution/mode descriptions.
- **2.2 — Frame and image-layout model:** dimensions, stride, payload size, pixel format, significant bit depth versus storage container, and optional metadata.
- **2.3 — Errors and remaining shared value types:** typed feature values, ranges/increments, enumerations, availability/readability/writability, operation results and explicit error semantics.

Common contracts describe only semantics shared by providers. AmScope-only codes, accessory operations and SDK-specific details remain in its provider extension. Do not equate advertised capability, runtime availability, readability, writability, command acceptance, readback and observed frame output.

**Acceptance:** common types are provider-neutral, tested, documented and used by the native driver without weakening its existing lifecycle or frame ownership semantics.

### 3.5 Phase 3 — Finalize the non-Qt programmatic camera and frame-delivery boundary

This phase replaces the original Qt-library/UI extraction phase. It is a library/API boundary phase, not a presentation phase.

Required responsibilities:
- `Amscope::Driver`: native AmScope SDK integration and the sole AmScope session owner;
- `Camera::Common`: backend-neutral contracts and the Qt-independent frame-stream/mailbox primitives;
- provider-neutral programmatic API: discovery, open/close, start/stop, feature access, commands, telemetry/results and frame consumption;
- provider-specific extensions: operations that do not have a proven common meaning.

Rules:
1. Keep the public programmatic camera surface independent of Qt and GUI frameworks.
2. Keep frame delivery backend-neutral. The consumer may be SwiftUI/AppKit, another native GUI, a headless process or an external application; none becomes the acquisition owner.
3. Do not add a redundant `camera_amscope_control` wrapper if it only forwards the existing `AmcamCamera` API.
4. Preserve a single SDK-call serialization boundary and one physical session owner.
5. Preserve FIFO operation ordering, session/feature generations, callback shutdown synchronization and frame-lease lifetime.
6. Do not add per-frame copies, conversions, allocations, another acquisition worker, or another notification/operation queue without measured need and explicit semantics.
7. Qt is not a required dependency, supported API surface, package component or acceptance target for this roadmap.
8. Existing `Camera/Qt`, `Camera/QtWidgets`, `QtAmcamCamera` and Qt preview code are legacy source at this revision. Their presence in the repository is not a roadmap requirement. Do not extend them or make them a dependency of the revised deliverable. Remove or retire them only through a deliberate source/build cleanup that verifies no remaining consumers; this roadmap revision alone does not claim they have already been deleted.

**Acceptance:**
- a non-Qt client can enumerate/open/read/write/start/receive frames/stop/close through the public API;
- a headless or non-Qt consumer can use common frame contracts without importing a GUI framework;
- the Optika application consumes the same programmatic driver and frame contracts through its application adapter;
- the build/package graph for the supported Camera Module deliverable does not require Qt;
- one SDK session and one acquisition path remain in force.

### 3.6 Phase 4 — Integrate the existing GenTL/GenApi implementation

This is the second-provider track. It is not permission to reimplement GenTL or GenApi.

Required:
- identify and integrate the existing GenTL Consumer and GenApi runtime selected by the project;
- obtain the actual camera-vendor CTI Producer and verify its version, ABI, license, architecture and device support;
- implement a CTI provider adapter against the common camera contracts;
- preserve Producer/interface/device/stream ownership and provider-qualified device identity;
- keep GenTL acquisition/lifecycle semantics native to that backend rather than forcing them into AmScope's callback/pull model;
- keep AmScope on its native Amcam SDK path. Do not create a synthetic AmScope CTI Producer or fabricate GenApi nodes/registers.

**Hardware gate:** a real CTI Producer and physical CTI camera are required. If they are unavailable, this phase remains blocked by external dependencies; mock-only work does not close it. Run the AmScope regression after integration.

### 3.7 Phase 5 — Complete capability, feature, command and model coverage

Use the implementation inventory as the checklist. Classify each SDK operation as:
- common setting/feature;
- provider-specific setting/feature;
- command/action;
- telemetry/measured state;
- internal lifecycle/acquisition operation;
- unsupported, undocumented or deferred.

Verify descriptors, units, ranges, increments, enum symbols, vector cardinality, availability, read/write semantics, preconditions, asynchronous completion, timeout/failure behavior and native readback. Verify exact model-code/profile behavior; profiles may narrow capabilities but must never invent them.

Keep distinct:
- configured FPS, device/SDK telemetry FPS and host-observed frame rate;
- AmScope pixel-format identifiers and PFNC;
- AmScope gain units and GenICam gain units;
- command acceptance and physical operation completion;
- absent metadata and numeric zero.

Physical tests must use models that actually advertise the capability. One MU503 does not certify other models or accessory families.

### 3.8 Phase 6 — Release packaging and external-consumer integration

Deliver stable namespaced CMake targets and exported package configuration/version files. Install public headers with correct dependency visibility. Verify runtime library loading, install names and RPATHs. Document supported platforms/architectures and vendor SDK redistribution constraints.

The supported Camera Module package must:
- be consumable without repository-relative include/library paths;
- expose the non-Qt programmatic API and common frame contracts;
- avoid leaking Qt or any application GUI framework to consumers;
- avoid exposing vendor SDK headers as common public API;
- keep provider dependencies private unless a consumer explicitly uses that provider;
- build and run from a clean install prefix.

**Acceptance:** a clean non-Qt external consumer builds and exercises the installed driver API with the physical reference camera. Qt consumer packaging is not required.

### 3.9 Phase 7 — Final hardware qualification and release

Freeze exact release candidates. Run automated tests on declared architectures and the physical hardware matrix for every declared supported provider/model combination.

The release matrix must cover, as applicable:
- discovery and opaque identity;
- open/start/stop/restart/close/reopen;
- sustained acquisition and frame geometry/payload correctness;
- feature read/write/readback and restoration;
- still capture and RAW behavior where supported;
- trigger modes, telemetry, GPIO and accessories only on hardware that supports them and with safe physical setup;
- disconnect/reconnect and recovery;
- installed external-consumer operation;
- no stale callback, stale frame, duplicate handle, unsafe frame reuse or unbounded preview queue.

Publish the exact tested revisions, hardware records, compatibility matrix, limitations and release tag.

The AmScope-native release may be qualified independently of the GenTL track. The **complete multi-backend Camera Module** is not complete until Phase 4 also passes its real CTI hardware gate.

## 4. Crosswalk to the original Phases 0–8

The original document is retained unchanged as historical source. This table prevents phase-number drift:

| Original roadmap | Revised roadmap | Change |
|---:|---:|---|
| 0 — Baseline | 0 — Baseline | Retained |
| 1 — Native AmScope driver | 1 — Native AmScope driver | Retained |
| 2 — Backend-neutral models | 2 — Backend-neutral contracts | Retained and clarified |
| 3 — Driver/control/Qt streaming libraries | 3 — Non-Qt programmatic API and frame delivery | Qt-specific target removed; common streaming semantics retained |
| 4 — Reusable Qt camera UI | Removed | No Camera Module UI deliverable; GUI belongs to consuming application |
| 5 — GenTL/GenApi integration | 4 — GenTL/GenApi integration | Retained and reindexed |
| 6 — Capability/feature/command/model coverage | 5 — Coverage | Retained and reindexed |
| 7 — Packaging/external consumers | 6 — Packaging | Retained; non-Qt consumer is the acceptance target |
| 8 — Hardware qualification/release | 7 — Hardware qualification/release | Retained and reindexed |

The later `AMSCOPE_NATIVE_RELEASE_PLAN.md` was written for a Qt-based preview/UI delivery. It is now **historical execution documentation**, not the governing current roadmap. Its useful native-driver, common-contract, feature-inventory, packaging and hardware evidence may be carried forward; its Qt adapter/UI phases and Qt consumer acceptance criteria are superseded.

## 5. Branch and source history

| Branch / source snapshot | Role |
|---|---|
| `amscope-native-driver` and early `amscope-native-driver-0.2` history | Original native AmScope driver foundation |
| `amscope-native-driver-v.0.2` | Historical source of the original complete Camera Module roadmap |
| `amscope-native-driver-0.3` through `0.5` | Native driver, session/acquisition, feature access and physical diagnostic refinement |
| `amscope-native-driver-0.6` | Common camera contracts and frame representation |
| `amscope-native-driver-0.7` | Qt adapter and reusable Qt Widgets implementation; now legacy for the revised roadmap |
| `amscope-native-app-mac.v.0.8` and `amscope-native-app-mac.v.0.8-docksystem` | Native macOS application integration history |
| `OPTIKA-B293-DUALSLIDE-APP-v.0.9` | Source snapshot from which the current `Camera/` tree was brought forward; 307 Camera blobs match `camera-v.2.0` exactly |\n| `camera-v.2.0` | Canonical Camera Module v2.0 development branch |\n| `OPTIKA-B293-DUALSLIDE-APP` | Separate current Optika SwiftUI/AppKit application and camera integration |
| `project_manager` | Workstream governance, status and continuation handoff |

The separate repositories `AMSCOPE_DRIVER` and `AMSCOPE_MU503_DRIVER` contain research/prototype histories. They are not separate active production camera projects in this register.

## 6. Current implementation and evidence ledger

This ledger separates source presence, automated evidence, physical evidence and owner acceptance. It does not claim that this review ran a build, test or hardware session.

| Revised phase | Current assessment | Evidence / remaining work |
|---:|---|---|
| 0 | Historically accepted by owner | Historical baseline and MU503 records exist. Reconcile exact current branch/commit before treating the baseline as current. |
| 1 | Historically accepted by owner; hardening remains | Native single-session driver exists. Callback quiescence and deterministic full SDK lifecycle fault injection remain material safety concerns. |
| 2 | Partially implemented; shared type ownership decision documented | `Camera::Common` contains discovery/model, frame, feature-validation and mailbox contracts. Common feature-access errors stay common; AmScope lifecycle/command errors remain provider-specific. Physical common-frame/RAW verification remains outstanding. |
| 3 | IN PROGRESS; non-Qt build/install CI passed | `Amscope::Driver` exposes a non-Qt C++ API and `Camera::Common` has a non-Qt mailbox. Qt options default OFF; legacy facade header lives under `Camera/Amscope/legacy/qt`. Workflow #45654331539 passed 12/12 CTest on Intel and Apple Silicon, installed the public headers, confirmed the Qt header was absent and compiled the public API test against installed headers. Exported package and clean external-consumer validation remain. |
| 4 | Blocked follow-on | Existing GenTL/GenApi Consumer integration choice, target CTI Producer and physical CTI camera remain prerequisites. |
| 5 | Open | Broad AmScope implementation exists; reconcile every claim to source, model evidence, tests and physical support. |
| 6 | Open | Initial install rules exist; exported package and clean non-Qt external consumer qualification remain. |
| 7 | Not accepted | Full release-candidate hardware matrix and installed-consumer evidence remain outstanding. |

### Physical and automated evidence already recorded

- MU503 RGB24 record: 1280 × 960, 3,840-byte row stride and 3,686,400-byte payload for the recorded run.
- v0.7 FrameView smoke record: discovery/open/start, changing frames, stop and close. This validates the historical Qt FrameView integration only; it is not a current Optika GUI acceptance record.
- Separate MU503 diagnostic runs reported approximately 91 FPS at 640 × 480 with zero reported drops in the recorded short runs. Do not conflate these with the separate 835-frame/60.722-second diagnostic record or claim long-duration qualification from them.
- The existing CMake tree contains `Camera::Common` and `Amscope::Driver` as the primary non-Qt surface. Legacy Qt adapter/widgets targets remain optional and excluded from default build/install; the Qt facade header is physically separated under `Camera/Amscope/legacy/qt`.
- The inspected Optika application target links `Amscope::Driver` and Apple frameworks; its camera models, Objective-C++ bridge and frame-feed adapter are application-owned.
- Source comparison found the 307 files under `Camera/` identical between `OPTIKA-B293-DUALSLIDE-APP-v.0.9` and `camera-v.2.0`; no Camera library copy was missing.\n- Non-Qt CI run [#45651928831](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45651928831) passed on Intel x86_64 and Apple Silicon arm64 at `5cf5bc352e2df926a2cc0317acaccfd3a912602d`, with 11/11 non-Qt CTest tests and driver/diagnostic architecture checks. This is CI evidence, not physical-camera qualification.
- A non-Qt `AmcamPublicApiTests.cpp` target has been added to verify public API type/lifecycle invariants without opening hardware. The Qt facade header has been moved out of the `Amscope::Driver` public include directory. CI run [#45654331539](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45654331539) is queued against source commit `11ff79ac115e5f4ecbcbe95eb70f4391c48a21d8`; it includes installation and syntax compilation of the non-Qt public headers.\n- The exact current Optika branch head inspected for this revision is `4c278aed3af29e51b7485a2902080d84314af73b`. This is an application source inspection checkpoint, not a test result.

### Unresolved risks and gaps

1. Prove callback quiescence and safe callback-reachable resource release around SDK Close.
2. Complete deterministic native SDK lifecycle fault injection, including Open/Start/Stop/Pull/Close failures.
3. Complete production RAW/common-frame hardware qualification.
4. Reconcile all SDK features, commands, telemetry and model profiles against actual hardware evidence.
5. Audit public target/header dependency visibility and the installed non-Qt consumer; decide separately whether legacy Qt source is deleted or archived.
6. Complete exported CMake package and clean non-Qt external-consumer qualification.
7. Resolve the existing GenTL Consumer integration choice and acquire a real CTI Producer/camera.
8. Complete release qualification for every declared supported model/provider combination.
9. Verify vendor SDK redistribution terms.

## 7. Architectural invariants

1. One native SDK session owner per opened camera.
2. One production acquisition worker per AmScope session.
3. SDK callbacks signal work; they do not perform expensive frame processing or application/UI work.
4. Callback-to-worker event transfer is bounded.
5. Preview publication is latest-frame/coalesced, not an unbounded FIFO of obsolete frames.
6. Frame payload ownership is retained through immutable leases; do not add copies without measured necessity and explicit semantics.
7. Feature/command scheduling must not block acquisition.
8. Stop/Close must respect callback and worker lifetimes; failed cleanup cannot silently release a physical-device ownership claim.
9. Camera APIs expose actual readback and explicit unknown/unavailable states, not assumed settings.
10. Vendor-only concepts remain in provider extensions unless another provider demonstrates the same common meaning.
11. Camera acquisition is independent of controller, slide scan and ImageProcessing execution.
12. A camera frame is source evidence; processing and display transformations must not mutate the original acquisition payload.
13. No GUI framework is part of the common camera contract or required public Camera Module dependency.
14. Application GUI models own presentation state; they do not own vendor SDK handles or acquisition workers.

## 8. Continuation handoff

**Governing roadmap:** Camera v2.0 Phases 0–7 and [CAMERA_V2_DEVELOPMENT_STATUS.md](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/camera-v.2.0/docs/architecture/CAMERA_V2_DEVELOPMENT_STATUS.md).  
**Historical authority:** original Phases 0–8 document on `amscope-native-driver-v.0.2`.  
**Active Camera branch:** `camera-v.2.0` at `8ba5fa3791264919bf85586322f095b59a0ec1d5`.  
**Application integration baseline:** `OPTIKA-B293-DUALSLIDE-APP` at `4c278aed3af29e51b7485a2902080d84314af73b`.  
**Management branch:** `project_manager`.

Before changing camera code:
1. Identify the revised phase and exact contract/exit criterion affected.
2. Keep Camera Module work separate from Optika GUI/workspace, controller/stage, slide scanning and ImageProcessing workstreams.
3. Inspect the exact source diff and preserve session, callback, queue and frame-lease invariants.
4. Prefer non-Qt API and test paths. Do not add Qt/UI dependencies to new camera functionality.
5. Add deterministic tests for changed semantics, but do not mistake fake-provider tests for vendor SDK or physical-camera evidence.
6. Record exact source revision, automated result and physical hardware record separately.
7. Update this handoff with phase status, evidence, unresolved risks and the next bounded task.

### Required validation distinctions

- **Contract tests:** typed models, descriptors, ranges, frame layouts, mailbox, queues and session primitives.
- **Native integration:** actual AmScope SDK behavior, lifecycle, feature write/readback and failure handling.
- **Hardware:** exact camera model/connection, frame geometry, metadata, sustained stream, stop/restart/reopen and physical feature readback.
- **Application integration:** Optika SwiftUI/AppKit consumes the driver through the application adapter without owning the SDK session.
- **Multi-backend integration:** actual CTI Producer/GenTL/GenApi operation on a physical CTI camera, while retaining AmScope regression.
- **Packaging:** installed non-Qt external consumer works without repository-relative paths or GUI-framework dependencies.

A compile, mock, UI smoke test or green CI run is not interchangeable with a physical-camera acceptance record.

## 9. Authoritative records

- [Camera v2.0 current development status and continuation plan](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/camera-v.2.0/docs/architecture/CAMERA_V2_DEVELOPMENT_STATUS.md)\n- [Original Camera Module roadmap — historical Phases 0–8](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/amscope-native-driver-v.0.2/docs/architecture/AMSCOPE_NATIVE_DRIVER_COMPLETION_PLAN.md)
- [Later AmScope release plan — historical Qt/UI execution plan](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/OPTIKA-B293-DUALSLIDE-APP/docs/architecture/AMSCOPE_NATIVE_RELEASE_PLAN.md)
- [AmScope implementation inventory](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/OPTIKA-B293-DUALSLIDE-APP/docs/architecture/AMSCOPE_IMPLEMENTATION_INVENTORY.md)
- [Native SDK test-seam and callback-quiescence analysis](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/OPTIKA-B293-DUALSLIDE-APP/docs/architecture/AMSCOPE_NATIVE_DRIVER_TEST_SEAM.md)
- [GenTL/GenApi C++ integration assessment](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/OPTIKA-B293-DUALSLIDE-APP/docs/architecture/AMSCOPE_GENTL_GENAPI_CPP_INTEGRATION_ASSESSMENT.md)
- [Camera common type ownership](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/OPTIKA-B293-DUALSLIDE-APP/docs/architecture/CAMERA_COMMON_TYPE_OWNERSHIP.md)
- [Optika native camera application architecture](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/OPTIKA-B293-DUALSLIDE-APP/docs/architecture/AMSCOPE_NATIVE_MACOS_APP.md)
- [MU503 RGB24 physical frame record](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/OPTIKA-B293-DUALSLIDE-APP/docs/testing/hardware/)
