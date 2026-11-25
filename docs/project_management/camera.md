# Workstream — Camera

**Canonical project:** the camera subsystem inside [MICROSCOPE_CPP](https://github.com/Shennagyp/MICROSCOPE_CPP), under \`Camera/\`  
**Management branch:** \`project_manager\`  
**Current source baseline:** the integrated camera source inherited from \`OPTIKA-B293-DUALSLIDE-APP-v.0.9\` at \`6fdff2ac3907c22a35687606bf05d0cf7b150ce8\`.

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

| Branch | Development focus |
|---|---|
| \`amscope-native-driver\` / \`amscope-native-driver-0.3\` | Initial native AmScope driver and session/acquisition foundation. |
| \`amscope-native-driver-0.4\` | Driver behavior and feature-access refinement. |
| \`amscope-native-driver-0.5\` | Native acquisition diagnostics and physical MU503 probe work. Diagnostic probes remained separate from production acquisition. |
| \`amscope-native-driver-0.6\` | Common camera contracts: \`Camera::Common\`, typed features/ranges, frame representation and Qt streaming boundary. |
| \`amscope-native-driver-0.7\` | Reusable Qt Widgets, camera controls, image information and frame view. |
| \`amscope-native-app-mac.v.0.8\` | Integration into the native macOS application. |
| \`amscope-native-app-mac.v.0.8-docksystem\` | Application workspace/dock integration; camera API ownership remained separate. |
| \`OPTIKA-B293-DUALSLIDE-APP-v.0.9\` | Camera remains a reusable capability while microscope/application responsibilities move outside \`Camera/\`. |
| \`project_manager\` | Management branch created from the current integrated v0.9 source baseline. |

The separate repositories \`AMSCOPE_DRIVER\` and \`AMSCOPE_MU503_DRIVER\` contain research/prototype histories. They are not separate camera workstreams here and must not be used to imply that multiple production camera projects have been integrated into this repository.

## 3. Current implementation

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

Reusable Qt pieces include \`FrameView\`, \`CameraControlPanel\` and \`ImageInfoBar\`. The OPTIKA native macOS application has its own \`NativeCameraFrameFeed\` integration and routes preview frames to its slide workspace. Presentation remains separate from camera session ownership.

## 4. Hardware and software evidence

### Physical MU503 evidence

The repository contains narrow physical-camera evidence:
- A v0.6 RGB24 record for MU503 at 1280 × 960, 3,840-byte row stride and 3,686,400-byte payload. It validates the recorded RGB24 geometry for that tested run.
- A v0.7 FrameView smoke record confirming discovery/open/start, changing frames in the extracted view, stop and close.
- A separate native/vendor diagnostic comparison exists, but lighting conditions differed. Its aggregate FPS values must not be used to claim one production acquisition API is faster.

These records do not prove:
- production RAW common-frame hardware correctness;
- complete SDK lifecycle fault-injection behavior on physical hardware;
- all camera feature writes/readbacks;
- sustained release qualification across restarts;
- physical acceptance of the current OPTIKA dock/workspace presentation;
- support for a second camera provider.

### Automated evidence

The AmScope implementation inventory reports a 15-target CTest suite in the v0.8 workspace/UI line, including common camera models/frames/features, frame gating, session primitives, watchdog, Qt request scheduling, mailbox and reusable widgets. Owner-reported local Release runs passed their exact revisions.

Do not transfer those test counts/results to a different branch head without checking the exact commit. The v0.9 platform CI does not by itself requalify every physical-camera behavior.

### Explicitly unresolved

Production RAW common-frame hardware qualification was explicitly left unverified. The camera inventory also records incomplete generic feature editors, command/telemetry/capability UI, external-consumer CMake package qualification and vendor SDK redistribution terms.

## 5. Architectural invariants

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

## 6. Continuation handoff

**Active status:** established camera capability; no new independent camera project is being created. Maintain and qualify the integrated camera subsystem while the microscope application migrates to Core/Application.

### Next bounded task

Before changing camera code, inspect the current \`project_manager\` source diff and determine whether Phase 4 operation-runtime work actually requires a camera API change. It should not require one merely because camera operations will eventually be initiated by microscope workflows.

When a real camera change is required:
1. state the camera capability and semantic contract affected;
2. write or update common and provider-specific tests first;
3. preserve the single-session/one-worker/frame-lease/mailbox design;
4. keep SDK acceptance distinct from device feedback and verified microscope operation completion;
5. record the exact host CI and, where applicable, exact physical MU503 test record;
6. update this camera handoff only with source and verification evidence that actually exists.

Do not begin a second provider implementation simply to demonstrate architectural extensibility unless the project owner prioritizes it. GenTL/GenApi is not a second camera project and must not be described as a delivered provider in the current camera line.

### Required validation distinctions

- Unit/contract tests: typed common models, descriptors, ranges, frame layouts, queue/gate/mailbox and session primitives.
- Native integration: actual AmScope SDK behavior, lifecycle, feature write/readback and failure handling.
- Hardware: exact camera model/connection, actual delivered frame geometry, metadata availability, sustained stream, stop/restart/reopen and physical feature readback.
- Application integration: current OPTIKA UI receives frames through the camera adapter without owning the SDK session.

A compile or fake-provider test does not replace the physical-camera gate.

## 7. Authoritative records

- [AmScope implementation inventory](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/amscope-native-app-mac.v.0.8-docksystem/docs/architecture/AMSCOPE_IMPLEMENTATION_INVENTORY.md)
- [AmScope native release plan](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/amscope-native-app-mac.v.0.8-docksystem/docs/architecture/AMSCOPE_NATIVE_RELEASE_PLAN.md)
- [MU503 RGB24 hardware record](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/amscope-native-app-mac.v.0.8-docksystem/docs/testing/hardware/phase-2-mu503-rgb24-2026-11-23.md)
- [MU503 FrameView smoke record](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/amscope-native-app-mac.v.0.8-docksystem/docs/testing/hardware/phase-4-mu503-frame-view-smoke-2026-11-24.md)
