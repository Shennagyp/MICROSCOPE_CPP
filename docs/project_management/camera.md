# Workstream — Camera

**Canonical project:** the camera subsystem inside [MICROSCOPE_CPP](https://github.com/Shennagyp/MICROSCOPE_CPP), under `Camera/`  
**Management branch:** `project_manager`  
**Current implementation branch:** `OPTIKA-B293-DUALSLIDE-APP-v.0.9`  
**Current source baseline:** `c7bb4b4dc41441989d71ee800c6d33b51dc54abe`  
**Camera-specific driver change in this source history:** `f34e7b050c43ed32a609b4717afc4bdb2ade354e`

## 1. Scope and project identity

There is **one camera project** in this project register.

The canonical camera implementation is the AmScope-backed camera subsystem integrated into MICROSCOPE_CPP. It owns camera discovery/provider identity, native camera session lifecycle, camera feature/command access, acquisition and backend-neutral frame delivery.

It does not own:
- microscope optical configuration or objective/filter coordination;
- controller and stage semantics;
- Sample/Slide/Tile identity;
- slide-scan orchestration;
- image-processing algorithms or GPU policy;
- application workspace state beyond camera-facing reusable components.

Do not split this work into separate project records named AmScope, MU503 or GenTL. Device-specific support, provider adapters, native API research and standards compatibility are components or history of this one camera line. Repositories outside MICROSCOPE_CPP may be linked as supporting historical research, but are not independent active camera projects in this management plan.

## 2. Branch history

The canonical history in MICROSCOPE_CPP developed as follows:

| Branch / source snapshot | Development focus |
|---|---|
| `amscope-native-driver` and early `amscope-native-driver-0.2` history | Original native AmScope driver and Camera Module roadmap foundation. |
| `amscope-native-driver-v.0.2` | Historical source snapshot containing the original complete Camera Module roadmap, `AMSCOPE_NATIVE_DRIVER_COMPLETION_PLAN.md`. This is the governing roadmap source for Camera v2.0. |
| `amscope-native-driver-0.3` | Native AmScope driver and session/acquisition foundation refinements. |
| `amscope-native-driver-0.4` | Driver behavior and feature-access refinement. |
| `amscope-native-driver-0.5` | Native acquisition diagnostics and physical MU503 probe work. Diagnostic probes remained separate from production acquisition. |
| `amscope-native-driver-0.6` | Common camera contracts: `Camera::Common`, typed features/ranges, frame representation and Qt streaming boundary. |
| `amscope-native-driver-0.7` | Reusable Qt Widgets, camera controls, image information and frame view. |
| `amscope-native-app-mac.v.0.8` | Integration into the native macOS application. |
| `amscope-native-app-mac.v.0.8-docksystem` | Application workspace/dock integration; camera API ownership remained separate. |
| `OPTIKA-B293-DUALSLIDE-APP-v.0.9` | Camera remains a reusable capability while microscope/application responsibilities move outside `Camera/`. |
| `project_manager` | Management branch created to maintain independent workstream history and continuation handoffs. |

The separate repositories `AMSCOPE_DRIVER` and `AMSCOPE_MU503_DRIVER` contain research/prototype histories. They are not separate camera workstreams here and must not be used to imply that multiple production camera projects have been integrated into this repository.

## 3. Governing Camera Module roadmap — original Phases 0–8

**Camera v2.0 roadmap authority:** [Original AmScope Native Driver and Camera Module Completion Plan](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/amscope-native-driver-v.0.2/docs/architecture/AMSCOPE_NATIVE_DRIVER_COMPLETION_PLAN.md).

The original document was found in:
- Repository: `Shennagyp/MICROSCOPE_CPP`
- Historical branch: `amscope-native-driver-v.0.2`
- Path: `docs/architecture/AMSCOPE_NATIVE_DRIVER_COMPLETION_PLAN.md`
- Original document title: *AmScope Native Driver and Camera Module Completion Plan*
- Baseline date: 22 November 2026
- Declared target implementation branch in the document: `amscope-native-driver`

Its complete roadmap is retained as the governing Camera Module roadmap. Preserve its phase numbering, names, objectives, dependencies and acceptance rule. Do not replace it with the reindexed phases of a narrower release plan.

| Original phase | Name | Governing objective |
|---:|---|---|
| 0 | Freeze and verify the baseline | Establish the physical-camera baseline, reconcile branches, verify SDK behavior and inventory existing functionality. |
| 1 | Stabilize and qualify the native AmScope driver | Establish reliable session ownership, acquisition, lifecycle, callback handling, failure recovery and frame management. |
| 2 | Finalize backend-neutral camera models | Extract shared identity, capabilities, features, values, errors and frame representations without leaking vendor-specific semantics. |
| 3 | Separate driver, programmatic control and Qt streaming libraries | Establish independent library responsibilities while retaining one physical camera session and native handle. |
| 4 | Extract the reusable Qt camera UI library | Build reusable discovery, lifecycle, feature editors, command controls, telemetry, frame display and capability presentation. |
| 5 | Integrate the existing GenTL/GenApi implementation | Add a genuine CTI-based backend using existing GenTL/GenApi implementations alongside the native AmScope backend. |
| 6 | Complete capability, feature, command and model coverage | Reconcile SDK capabilities, model profiles, controls, commands, telemetry, UI and actual hardware support. |
| 7 | Release packaging and external-consumer integration | Deliver reusable CMake packages, public headers, runtime dependencies and clean external-consumer support. |
| 8 | Final hardware qualification and release | Qualify supported camera models and backends, eliminate release blockers and publish compatibility evidence. |

### 3.1 Original document sections

| Original phase | Authoritative section |
|---:|---|
| 0 | §5 — Freeze and verify the baseline |
| 1 | §6 — Stabilize and qualify the native AmScope driver |
| 2 | §7 — Finalize backend-neutral camera models |
| 3 | §8 — Separate driver, programmatic control, and Qt streaming libraries |
| 4 | §9 — Extract the reusable Qt camera UI library |
| 5 | §10 — Integrate the existing GenTL/GenApi implementation |
| 6 | §11 — Complete capability, feature, command, and model coverage |
| 7 | §12 — Release packaging and external-consumer integration |
| 8 | §13 — Final hardware qualification and release |

Additional governing sections are §1–2 (purpose, architecture authorities and invariants), §3 (mandatory physical-camera verification policy), §14 (dependency order and parallel work), §15 (phase status ledger), and §16 (definition of done).

### 3.2 Required detail within the original phases

**Phase 2 — three delivery slices**

- 2.1 — Common discovery and capability models.
- 2.2 — Common frame and image-layout model.
- 2.3 — Common errors and remaining shared value types.

**Phase 3 — four library responsibilities**

| Library responsibility | Intended ownership |
|---|---|
| `camera_amscope_driver` | Native SDK and sole AmScope session owner. |
| `camera_amscope_control` | Programmatic features, commands and telemetry. |
| `camera_amscope_streaming_qt` | Qt frame delivery and thread handoff. |
| `camera_ui_qt` | Reusable camera UI. |

These are responsibility/ownership boundaries. The original plan allows the final CMake target structure and names to vary. It prohibits multiple competing camera-session owners. Do not create a forwarding-only control library if the existing driver API already provides that responsibility without a meaningful new boundary.

**Phase 5 — existing GenTL/GenApi integration**

This phase requires a genuine CTI-based backend using an existing GenTL/GenApi implementation. It does not authorize writing a replacement Consumer/runtime, creating a synthetic AmScope CTI Producer, or representing AmScope as a GenTL camera. The phase requires an actual target Producer and physical CTI camera for backend hardware acceptance, in addition to the AmScope regression.

### 3.3 Dependency order from original §14

| Original phase | Main deliverable | Depends on |
|---:|---|---|
| 0 | Verified baseline, branch reconciliation and implementation inventory | Physical AmScope camera |
| 1 | Qualified native AmScope session and acquisition implementation | Phase 0 |
| 2 | Backend-neutral common models and contracts | Phase 1 |
| 3 | Separate native driver, control and Qt streaming libraries | Phases 1–2 |
| 4 | Reusable Qt camera UI library and composed preview | Phases 2–3 |
| 5 | Existing GenTL/GenApi integration and CTI adapter | Phase 2 and existing runtime availability; AmScope regression from Phase 1 |
| 6 | Evidence-backed feature, command, telemetry and model coverage | Phases 2–5 |
| 7 | Exported packages and clean external consumers | Phases 3–6 |
| 8 | Final hardware qualification and release | Phases 0–7 |

GenTL/GenApi dependency discovery can proceed in parallel with Phases 1–3. The actual adapter and its hardware acceptance still depend on finalized common models, a target vendor CTI Producer and a real CTI camera.

### 3.4 Mandatory physical-camera acceptance rule

The original §3 establishes a physical-camera gate for **every phase**. Compilation, GitHub Actions, unit tests, mocks and simulated devices are supporting evidence; none substitutes for the required physical-camera test against the implementation produced by that phase.

Phase 5 additionally requires a real CTI Producer/camera. Phase 8 requires the declared supported model/backend hardware matrix. A camera model is not certified by testing a different model.

The original §15 status ledger is a dated snapshot from the original plan. It is historical evidence, not an automatically current status table. Current status must be refreshed from exact source revisions, test results and physical hardware records while retaining the original Phase 0–8 identities and exit criteria.

## 4. Relationship to the later AmScope-native release plan

The later [AmScope Native Camera Module — Revised Implementation and Release Plan](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/OPTIKA-B293-DUALSLIDE-APP-v.0.9/docs/architecture/AMSCOPE_NATIVE_RELEASE_PLAN.md) is a narrower AmScope-native release execution plan. It governs detailed execution and acceptance for that release scope; it does **not** replace the original complete Camera Module roadmap for Camera v2.0.

The later document narrows the immediate release to the AmScope-native path and moves GenTL/GenApi into a separate follow-on track. Its phase numbering is therefore not the governing Camera v2.0 numbering.

| Later AmScope release-plan phase/track | Corresponding original Camera roadmap phase |
|---|---|
| Release Phase 0 — Baseline and scope freeze | Original Phase 0 |
| Release Phase 1 — Native AmScope session and acquisition | Original Phase 1 |
| Release Phase 2 — Close the common camera contract | Original Phase 2 |
| Release Phase 3 — Finalize native control and Qt streaming boundaries | Original Phase 3 |
| Release Phase 4 — Extract reusable Qt camera UI | Original Phase 4 |
| Separate GenTL/GenApi follow-on track | Original Phase 5 |
| Release Phase 5 — Reconcile AmScope feature and model claims | Original Phase 6 |
| Release Phase 6 — CMake package and external consumers | Original Phase 7 |
| Release Phase 7 — Integrated AmScope release qualification | Original Phase 8 |

Use the later plan to execute and qualify the AmScope-native release without reopening accepted work without a reproduced defect or other explicit reason. Do not infer that the complete Camera Module is finished when that narrower release is accepted: original Phase 5 and the multi-backend requirements remain part of Camera v2.0.

## 5. Current implementation

### Native driver and acquisition

The AmScope driver owns the native SDK session and native acquisition boundary. The production path uses:
1. SDK image/event callback;
2. bounded callback-to-worker event queue;
3. one acquisition worker;
4. native image pull;
5. retained immutable frame lease;
6. latest-frame publication/mailbox and coalesced notification.

Lifecycle and ownership facilities include session/generation awareness, callback-drain tracking, retryable frame-cache flush obligations, retained frame-slot lease selection, frame publication gating and watchdog logic.

The Qt adapter composes the driver; it does not create a second native SDK session. The frame stream mailbox is an embedded delivery mechanism, not another camera owner or worker.

### Common contracts

The common camera layer represents backend-neutral device identity, capabilities, frame geometry/layout, optional frame metadata and typed feature values. It preserves distinctions such as:
- significant bit depth versus storage container;
- image width/height versus stride and payload size;
- known/unknown/unsupported capability;
- absent sequence/timestamp/exposure versus numeric zero;
- common camera semantics versus vendor-specific extensions.

Integer feature ranges use integer types rather than floating-point approximations. Feature validation is shared rather than reimplemented independently in each UI.

### Application/UI integrations

Reusable Qt pieces include `FrameView`, `CameraControlPanel` and `ImageInfoBar`. The OPTIKA native macOS application has its own `NativeCameraFrameFeed` integration and routes preview frames to its slide workspace. Presentation remains separate from camera session ownership.

## 6. Hardware and software evidence

### Physical MU503 evidence

The repository contains narrow physical-camera evidence:
- A v0.6 RGB24 record for MU503 at 1280 × 960, 3,840-byte row stride and 3,686,400-byte payload. It validates the recorded RGB24 geometry for that tested run.
- A v0.7 FrameView smoke record confirming discovery/open/start, changing frames in the extracted view, stop and close.
- Separate native/vendor diagnostic comparison evidence exists, but lighting conditions differed. Its aggregate FPS values must not be used to claim one production acquisition API is faster.

These records do not prove:
- production RAW common-frame hardware correctness;
- complete SDK lifecycle fault-injection behavior;
- all camera feature writes/readbacks;
- sustained release qualification across restarts;
- physical acceptance of the current OPTIKA dock/workspace presentation;
- support for a second camera provider.

### Automated evidence

The latest inspected Camera CMake composition registers 16 CTest targets when the normal driver, Qt adapter and Qt Widgets configuration is enabled. They cover common models, features, frames, frame-rate estimation, queues, frame gating, watchdogs, session primitives, Qt requests, mailbox behavior and reusable widgets.

Do not transfer test counts/results to a different branch head without checking the exact commit. The v0.9 platform CI workflow disables AmScope and the native application; its green result does not constitute a Camera build/test result. The AmScope macOS workflow is configured for historical AmScope branches, not the active v0.9 branch. Camera CI coverage on the active implementation branch is an explicit gap.

### Explicitly unresolved

- Production RAW common-frame hardware qualification.
- Full deterministic native SDK lifecycle fault injection, including callback-quiescence assumptions and Open/Start/Stop/Pull/Close failure paths.
- Current OPTIKA native application visual and physical-camera acceptance.
- Complete model/feature/command/telemetry hardware reconciliation.
- Full reusable backend-neutral Qt UI extraction.
- Exported CMake package and clean external-consumer qualification.
- Vendor SDK redistribution terms.
- Original Phase 5 GenTL/GenApi C++ Consumer/adapter decision, target CTI Producer and physical CTI-camera acceptance.

## 7. Architectural invariants

1. One native SDK session owner per opened camera.
2. One acquisition worker for the production stream.
3. SDK callbacks signal work; they do not perform expensive frame processing or UI work.
4. Callback-to-worker event transfer is bounded.
5. Preview publication is latest-frame/coalesced; it is not an unbounded FIFO of obsolete frames.
6. Frame payload ownership is retained through immutable leases; do not insert a per-frame copy without measured necessity and explicit semantics.
7. Frame notification must not contend with feature-command scheduling in a way that blocks acquisition.
8. Stop/Close must respect callback and worker lifetimes; failed cleanup cannot silently release a physical-device ownership claim.
9. Camera APIs expose actual readback and explicit unknown/unavailable states, not assumed settings.
10. Vendor-only concepts remain in the AmScope extension unless another provider demonstrates the exact common meaning.
11. Camera acquisition is independent of controller, slide scan and ImageProcessing execution.
12. A camera frame is source evidence; processing and display transformations must not mutate the original acquisition payload.

## 8. Continuation handoff

**Governing roadmap:** Original Camera Module Phases 0–8, as defined in §3 above.  
**AmScope release execution:** the later `AMSCOPE_NATIVE_RELEASE_PLAN.md`, subordinate to the original roadmap.  
**Active implementation baseline:** `MICROSCOPE_CPP / OPTIKA-B293-DUALSLIDE-APP-v.0.9` at `c7bb4b4dc41441989d71ee800c6d33b51dc54abe`.

Before changing camera code:
1. Identify the original roadmap phase and exact capability/exit criterion affected.
2. Determine whether the current task belongs to the AmScope-native release plan or the separate original Phase 5 GenTL/GenApi track.
3. Inspect the exact source diff and preserve the existing ownership/concurrency invariants.
4. Write or update common and provider-specific tests first.
5. Preserve physical-camera verification as a phase acceptance gate; record exceptions explicitly rather than silently waiving them.
6. Record exact source revision, CI result and physical hardware record.
7. Update this handoff with the original phase status, evidence, unresolved risks and next bounded task.

The current OPTIKA Core Phase 4 operation-runtime work does not, by itself, justify changing Camera APIs. Camera changes should be driven by an actual camera capability or contract requirement, not simply because microscope workflows will eventually issue camera commands.

### Required validation distinctions

- Unit/contract tests: typed common models, descriptors, ranges, frame layouts, queue/gate/mailbox and session primitives.
- Native integration: actual AmScope SDK behavior, lifecycle, feature write/readback and failure handling.
- Hardware: exact camera model/connection, delivered frame geometry, metadata availability, sustained stream, stop/restart/reopen and physical feature readback.
- Application integration: current OPTIKA UI receives frames through the camera adapter without owning the SDK session.
- Multi-backend integration: actual CTI Producer/GenTL/GenApi operation on a physical CTI camera, while retaining AmScope regression coverage.

A compile or fake-provider test does not replace the physical-camera gate.

## 9. Authoritative records

### Governing Camera v2.0 roadmap

- [Original Phases 0–8 roadmap — historical branch v0.2](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/amscope-native-driver-v.0.2/docs/architecture/AMSCOPE_NATIVE_DRIVER_COMPLETION_PLAN.md)

### AmScope-native release execution

- [Revised AmScope-native release plan](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/OPTIKA-B293-DUALSLIDE-APP-v.0.9/docs/architecture/AMSCOPE_NATIVE_RELEASE_PLAN.md)
- [AmScope implementation inventory](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/OPTIKA-B293-DUALSLIDE-APP-v.0.9/docs/architecture/AMSCOPE_IMPLEMENTATION_INVENTORY.md)
- [Native driver test-seam design](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/OPTIKA-B293-DUALSLIDE-APP-v.0.9/docs/architecture/AMSCOPE_NATIVE_DRIVER_TEST_SEAM.md)
- [GenTL/GenApi C++ integration assessment](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/OPTIKA-B293-DUALSLIDE-APP-v.0.9/docs/architecture/AMSCOPE_GENTL_GENAPI_CPP_INTEGRATION_ASSESSMENT.md)
- [Native macOS application architecture](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/OPTIKA-B293-DUALSLIDE-APP-v.0.9/docs/architecture/AMSCOPE_NATIVE_MACOS_APP.md)

### Physical-camera evidence

- [MU503 RGB24 hardware record](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/OPTIKA-B293-DUALSLIDE-APP-v.0.9/docs/testing/hardware/phase-2-mu503-rgb24-2026-11-23.md)
- [MU503 FrameView smoke record](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/OPTIKA-B293-DUALSLIDE-APP-v.0.9/docs/testing/hardware/phase-4-mu503-frame-view-smoke-2026-11-24.md)
