# Camera Module v2.0 — Development Status

**Repository:** `Shennagyp/MICROSCOPE_CPP`  
**Development branch:** `camera-v.2.0`  
**Latest implementation checkpoint:** `549433466098c5daeb63d72609776d958a35c2fe`  
**Latest CI-validated code checkpoint:** `549433466098c5daeb63d72609776d958a35c2fe`  
**CI:** GenTL C++ workflow [#45682828203](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45682828203) passed 2/2 tests on Intel x86_64 and Apple Silicon arm64. Core/Qt workflow [#45681878032](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45681878032) passed all four jobs at the CMake common-target checkpoint.  
**Library source branch:** `OPTIKA-B293-DUALSLIDE-APP-v.0.9`  
**Review date:** 25 November 2026

## 1. Branch provenance

The requested name `amscope-b293-dualslide-v.0.9` is not a repository branch. The matching source branch is `OPTIKA-B293-DUALSLIDE-APP-v.0.9`, at `c7bb4b4dc41441989d71ee800c6d33b51dc54abe`.

The existing `camera-v.2.0` branch began at that exact commit and had one additional commit, `591bbeef` (enable the AmScope workflow). A tree comparison found **307 files under `Camera/` with identical paths and identical blob SHAs** across the source and target branches. There was no missing Camera library code to copy. Camera v2.0 is therefore continued on the existing `camera-v.2.0` branch, rather than duplicating the source tree.

The separate `OPTIKA-B293-DUALSLIDE-APP` branch is the application integration line. Its SwiftUI/AppKit GUI, workspace, slides, controller and stage remain outside the Camera library.

## 2. Product boundary

Camera v2.0 is a reusable **multi-layer Camera Module with a non-Qt core and optional Qt libraries**.

It owns provider discovery/identity, native session lifecycle, backend-neutral camera contracts, programmatic settings/commands/telemetry, acquisition, frame delivery, provider-specific SDK integration, the optional Qt streaming adapter, and reusable Qt camera UI components.

It does not own an application's main window, workspace, application-specific view models or presentation state; microscope optical configuration; controller/stage semantics; slide scanning; image-processing algorithms; or application GPU policy. Consuming applications compose the reusable camera UI components or build their own native presentation.

The current ownership model is:

```text
Optika (or another consuming application)
  application camera model / command coordination
    application adapter
      AmScope::Driver
        AmcamCamera — sole HAmcam/session owner
          AmScope SDK callback
            bounded event FIFO
              one acquisition worker
                Amcam_PullImageV4
                  immutable FramePtr lease
                    application frame consumer
```

`Camera::Common` provides backend-neutral contracts and a non-Qt latest-frame mailbox. The mailbox owns no camera, session, worker, event loop or UI notification policy. The optional Qt streaming layer provides Qt-thread notification/handoff, and reusable widgets live in `Camera/QtWidgets`. Application adapters own composition, window/workspace state and presentation policy.

Core invariants: one SDK session owner per camera; one AmScope acquisition worker; notification-only SDK callbacks; bounded event transfer; latest-frame/coalesced preview; immutable frame leases; preserved SDK-call serialization and generations; no fabricated metadata/capabilities; settings, commands and telemetry remain distinct.

## 3. Current implementation map

### Common contracts — `Camera/Common`

- `CameraModels.h`: backend-neutral identity, resolution and tri-state capabilities.
- `CameraFrames.h`: frame dimensions, stride, payload layout, opaque format identity and optional sequence/timestamp/exposure.
- `CameraFeatureAccess.h` and `CameraFeatureValidation.h`: typed feature values and shared validation.
- `FrameRateEstimator.h`: host arrival cadence, distinct from SDK/device rate telemetry.
- `FrameStreamMailbox.h`: non-Qt, single-slot latest-frame mailbox.

The common model separates significant source bit depth from storage width, absent optional metadata from present zero values, and vendor format identifiers from standard identifiers.

### AmScope provider — `Camera/Amscope`

`AmcamCamera` is a non-Qt C++ API and sole owner of one `HAmcam` handle. It exposes discovery, open/close, start/stop, frame/error callbacks, feature access, still capture, trigger operations, telemetry, I/O and accessory commands.

Live acquisition remains SDK callback → bounded event FIFO → one acquisition worker → `Amcam_PullImageV4`. Four immutable frame-pool leases are retained. When all are retained, acquisition drains into a discard buffer and counts drops instead of overwriting retained memory or blocking.

Still capture has a separate event waiter and pull path. Settings, one-shot commands, telemetry and accessory/motion operations remain separate categories. Model profiles may narrow probed capabilities but must not invent support.

### GenTL integration — `Camera/GenTL`

- `ConsumerBridge` loads a separately built `libGenTLConsumerCAPI.dylib` and validates bridge ABI version 1 plus GenTL header version 1.6. The C++ target does not implement a second CTI loader or GenTL function table.
- `Producer` initializes the Consumer and TL module, discovers interfaces/devices, retains interface handles, and opens a device session. `DeviceSession` owns the device handle and remote port access; producer state remains alive while device sessions retain it.
- Data streams now open through the retained device session, register `EVENT_NEW_BUFFER`, allocate/announce and queue a bounded buffer set, start acquisition, wait for completed-buffer events, copy payload and metadata, and requeue buffers. Stop unregisters the event and drains active reads before queue flush, buffer revocation and stream close.
- `DataStreamFrame` preserves copied payload bytes, frame ID, nanosecond timestamp, pixel-format value and namespace (including PFNC namespace), dimensions, image-present and incomplete-buffer state. Mapping all GenTL pixel layouts and stride semantics into the backend-neutral `camera::Frame` remains open.
- Discovery and frame-wait APIs accept milliseconds and convert to GenTL nanosecond timeouts. Only one frame waiter is allowed per stream.
- Device and stream sessions retain the required parent state. A stream can outlive its `DeviceSession` wrapper; the device handle closes only after the last stream/session reference is released.
- Failed native cleanup quarantines and process-retains the Consumer context and dynamically loaded C ABI bridge. The bridge is not unloaded while CTI handles may remain.
- GenApi XML retrieval uses `GCGetNumPortURLs` / `GCGetPortURLInfo`; `LOCAL` register-map XML is read in bounded chunks and `FILE` XML is read from the advertised file. HTTP and custom URL schemes are not implemented.
- `Camera::GenTLGenApi` is optional and requires the official GenApi 3.5.1 Mac64_Universal SDK. Its `GenApiPort` / `GenApiNodeMap` source is present but has not been compiled against that SDK. The build option is off by default. Typed GenApi-to-Common feature mapping is not implemented yet.
- See [Camera/GenTL README](../../Camera/GenTL/README.md) for the current API and explicit limitations.
### Optional Qt streaming and reusable UI layers

- `Camera/Qt` provides Qt-specific frame notification/thread-handoff support without changing the native acquisition owner.
- `QtAmcamCamera` currently composes one `AmcamCamera` and adds a serialized Qt request worker, queued completions and frame notifications. This is existing implementation, but Phase 3 still needs to reconcile its combined responsibilities against the separate non-Qt control and Qt streaming boundaries.
- `Camera/QtWidgets` contains reusable `FrameView`, `CameraControlPanel` and `ImageInfoBar` components. These are partial Phase 4 deliverables, not a complete reusable camera UI library.
- `amscope_preview` is a composition/example application. It is not the owner of the driver or the reusable UI library's application-independent contracts.
- The Optika application already has its camera preview on the Optika application branch. Do not restart preview implementation as a Camera v2.0 priority; Phase 4 concerns reusable UI components and their consumer boundary.

Qt is optional for consumers that only need the core driver/common contracts. The Qt targets remain in scope as optional Camera Module deliverables and must retain their own build/test coverage. The vendor SDK's original Qt samples are vendor material, not Camera Module targets.

## 4. Governing roadmap and original Phase 0–8 ledger

The original *AmScope Native Driver and Camera Module Completion Plan* on `amscope-native-driver-v.0.2` is the **governing Camera v2.0 roadmap**. Its phase identities and dependency order are preserved. The later `AMSCOPE_NATIVE_RELEASE_PLAN.md` is a narrower AmScope-native execution plan and is subordinate; it does not replace the original phase numbering or remove the reusable Qt streaming/UI deliverables.

| Phase | Original deliverable | Current assessment |
|---:|---|---|
| 0 | Freeze and verify the baseline | **Accepted by owner historically.** MU503 physical observations exist; extended/exact-commit regression evidence remains follow-up, and one-camera observations do not certify all models. |
| 1 | Stabilize and qualify native AmScope driver | **Accepted by owner historically; post-acceptance hardening remains.** Single-session ownership, callback/queue/worker/pull path, frame pool and lifecycle primitives exist. Full injected SDK lifecycle tests and proof of callback quiescence around terminal Close remain open. |
| 2 | Finalize backend-neutral camera models | **Partially implemented.** Slice 2.1 discovery/capability models and 2.2 frame/layout models exist with automated tests and prior cross-architecture CI. The 2.3 semantic ownership audit is documented: common feature-access errors remain common, while AmScope lifecycle/command errors remain provider-specific. Physical common-frame/RAW validation remains open, so Phase 2 is not accepted. |
| 3 | Separate driver, programmatic control and Qt streaming libraries | **In progress.** `AmcamCamera` is the non-Qt programmatic control/session API and sole native handle owner. `Camera::Qt` is now a concrete Qt Core library with backend-neutral `camera::qt::FrameStream`, wrapping the Common latest-frame mailbox and owning QObject-thread notification. `QtAmcamCamera` composes one driver session and one FrameStream; its provider-specific serialized operation/lifecycle queue and generation semantics remain. CI passes on both architectures; same-session physical regression and final boundary acceptance remain open. |
| 4 | Extract reusable Qt camera UI library | **Partially implemented; not the current priority.** `Camera/QtWidgets` contains reusable components. The Optika application already has its camera preview; do not rebuild that preview in this workstream. Remaining Phase 4 scope is reusable feature/command/telemetry/capability presentation and consumer validation. |
| 5 | Integrate existing GenTL/GenApi implementation | **In progress.** C++ bridge, CTI/TL/interface/device discovery, retained device/stream lifetimes, GenTL event/buffer acquisition, payload copying/requeue, basic frame metadata, remote-port access and `LOCAL`/`FILE` XML retrieval are implemented. Mock CI passes on both architectures. The real Swift C ABI dylib remains unvalidated; optional GenApi 3.5.1 source is not compiled and typed feature mapping is outstanding. No real CTI Producer or physical CTI camera has been qualified. |
| 6 | Complete capability, feature, command and model coverage | **Open.** Broad AmScope implementation exists. Reconcile every exposed claim to provider/model support, units, preconditions, readback, asynchronous completion and physical evidence. |
| 7 | Release packaging and external-consumer integration | **Open.** Initial install rules exist and the non-Qt public headers install/compile. An exported CMake package/config/version flow and clean installed external consumers remain; both non-Qt and optional Qt consumer configurations must be considered. |
| 8 | Final hardware qualification and release | **Not started/accepted.** Requires exact-commit hardware records, declared provider/model matrix, installed-consumer tests, release CI and owner acceptance. |

The original phase dependency order remains: 0 → 1 → 2 → 3 → 4; Phase 5 (GenTL/GenApi) can have dependency research in parallel, but adapter acceptance needs the common model, an existing Consumer, a real CTI Producer and physical CTI hardware; Phase 6 follows Phases 2–5; Phase 7 follows Phases 3–6; Phase 8 requires all preceding phases.

## 5. Changes made on camera-v.2.0

| Commit | Change | Effect |
|---|---|---|
| `70a7486` | Make Qt integrations opt-in | Qt adapter/preview options default OFF so core-only consumers do not inherit Qt. Optional Qt targets remain Phase 3/4 deliverables. |
| `5cf5bc3` | First non-Qt macOS CI | Driver/probes built; 11/11 non-Qt tests passed on arm64 and x86_64. |
| `69dbc65`, `8c36506` | Isolate Qt facade header | Move the QObject facade header out of the core driver include root; optional Qt target owns the extra include path and installation. |
| `3e2b328`, `af19d7e` | Verify non-Qt install surface | CI checks required common/native headers, absence of Qt facade from non-Qt install, and syntax compilation against installed headers. |
| `a6f3409` | Exercise non-Qt CMake defaults | Core-only CI relies on default OFF Qt options. |
| `ac90288` | Initial v2.0 status ledger | Established branch provenance and initial phase map; its removal/reindexing of Qt Phase 4 was incorrect and has been corrected to original Phases 0–8. |
| `1ac6404`, `e5078d3`, `79d43f0`, `5ccd990` | Align driver README | Explain non-Qt core plus optional Qt streaming/reusable UI obligations. |
| `fbc1053`, `7ae8f65` | Archive v0.7 handoff snapshot | Preserve prior native/Qt implementation evidence without treating its old next actions as current. |
| `d250add`, `b5e0da0`, `a8e0547` | Correct historical inventory/release plan notices | Qt streaming/UI remain within Phases 3/4; later AmScope release plan is subordinate to original roadmap. |
| `85f0a1d`, `affcccf` | Add non-Qt public API contract test | Checks API type/ownership, initial Closed state, zero counters and idempotent close without hardware enumeration. |
| `61e1523` | Add separate Qt CI job | Builds/tests optional Qt streaming, reusable widgets and preview composition on Intel and Apple Silicon. |
| `64ca8da` | Restore original Phase 0–8 ledger | Re-established original canonical numbering, Qt Phase 3/4 and GenTL Phase 5. |
| `e12d765` | Extract backend-neutral Qt frame streaming | Added concrete `Camera::Qt` Core library and `camera::qt::FrameStream`; moved latest-frame Qt notification/handoff out of `QtAmcamCamera`; added standalone stream tests; corrected Qt workflow path filters and optional-target CMake description. |
| `ef58bf6`–`3212b4b` | Begin Phase 5 GenTL integration | Pinned the GenTL 1.6 header; added a version-checked dynamic C ABI bridge adapter, CTI/TL/interface/device discovery, retained device sessions, register-port access, GenApi XML retrieval and an optional GenApi 3.5.1 target. The Swift C ABI package is on the separate private `AMSCOPE_DRIVER` branch `gentl-c-api-v.1.0`. |
| `a058a4d`–`5494334` | Implement GenTL stream acquisition and cleanup safety | Added retained device state, stream open/start/wait/stop, event registration, bounded announced buffers, frame payload/metadata copying and requeue, nanosecond timeout conversion, PFNC namespace preservation, cleanup-failure quarantine and process-lifetime C ABI bridge retention. Mock tests include injected `DSClose` failure and verify the bridge remains retained. |
| `8a18f81`–`c629f53` | Decouple Common CMake target from AmScope | Root CMake now defines `Camera::Common` independently of the AmScope target, allowing a GenTL-only configuration. The AmScope CMake project reuses the root target or creates it when configured standalone. |

### CI evidence

Workflow [#45667125968](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45667125968) passed at exact source checkpoint `e12d765f408c2b0be7100c64b2eb245de5a2aca4` on both Intel x86_64 and Apple Silicon arm64:

- **Non-Qt Intel x86_64:** 12/12 CTest tests passed; non-Qt public headers installed; Qt facade absent from the non-Qt install; installed-header syntax check and architecture/probe checks passed.
- **Non-Qt Apple Silicon arm64:** 12/12 CTest tests passed with the same install-surface, syntax and architecture/probe checks.
- **Qt Apple Silicon arm64:** 18/18 CTest tests passed, including `camera_qt_frame_stream_tests`, `camera_qt_stream_handoff_tests`, the AmScope Qt request tests and all reusable widget tests; streaming library, widgets and preview built and artifact/architecture checks passed.
- **Qt Intel x86_64:** 18/18 CTest tests passed with the same Qt target/test and artifact/architecture checks.

The two new Qt tests cover the generic frame-stream publication/handoff path: latest-frame retention/coalescing, notification re-arming, QObject affinity, and clearing. The successful CI run proves build/test/install-surface behavior on the two hosted architectures. It does not prove physical camera behavior, Phase 3 same-session operation, callback quiescence, RAW sensor correctness, or the Phase 4 physical UI regression.

### GenTL C++ bridge CI

Workflow [#45682828203](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45682828203) passed at exact code checkpoint `549433466098c5daeb63d72609776d958a35c2fe`:

- **Apple Silicon arm64:** 2/2 CTest tests passed; static library architecture check passed.
- **Intel x86_64:** 2/2 CTest tests passed; static library architecture check passed.

The mock bridge tests cover dynamic-library symbol/version checks, CTI/TL/interface/device discovery, device-session lifetime after the Producer wrapper is released, stream-ID enumeration, remote-port read/write, `LOCAL` XML retrieval, stream open/start, buffer allocation/announcement/queueing, `EVENT_NEW_BUFFER` delivery, payload copy, frame ID/timestamp/PFNC format and namespace, buffer requeue, stop/unregister/flush/revoke/close, timeout unit conversion, a stream retaining its device after the `DeviceSession` wrapper closes, and process-lifetime bridge retention after an injected `DSClose` failure.

These tests use a mock C ABI bridge. They do **not** validate the actual Swift C ABI dylib, a vendor CTI Producer, actual camera acquisition, GenApi XML parsing, GenApi feature mapping or physical hardware.

The Swift C ABI source and tests are on private repository branch `Shennagyp/AMSCOPE_DRIVER:gentl-c-api-v.1.0` at `2f8f82d1e4b645a1191bf68021f9803ac6843121`. The tests now cover typed pointer/buffer marshalling, event registration/read/unregister and buffer-info forwarding. The workflow now attempts separate x86_64/arm64 builds and universal dylib assembly, but workflow [#45684107190](https://github.com/Shennagyp/AMSCOPE_DRIVER/actions/runs/45684107190) was cancelled before any build/test steps. The Swift GenTL packages currently declare macOS 13.0 as their minimum deployment target. Therefore the real C ABI dylib, its operation mapping and universal packaging remain unvalidated. Do not treat the C++ mock bridge as proof that the Swift dylib loads or behaves correctly.

Core/Qt workflow [#45681878032](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45681878032) passed all four non-Qt/Qt Intel and Apple Silicon jobs after the Common CMake target was moved to the root project. The earlier GenTL-only run [#45678105743](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45678105743) is superseded by the later stream-acquisition matrix.

No local build or physical-camera test is claimed in this status update. The GenApi 3.5.1 target is source-only until the official Mac64_Universal SDK is installed and used in a CI build.

## 6. Highest-priority unresolved risks

### Callback quiescence

The current callback-entry counter can drain callbacks already entered; it does not independently prove that a new callback cannot begin after `Amcam_Close` returns. Do not release callback-reachable resources based only on a zero counter. Prove both in-flight drain and no-new-callback after terminal close, using authoritative vendor evidence or a defensible synchronization design.

The private SDK dispatch-seam design can deterministically exercise injected failures and races, but a fake cannot prove undocumented behavior of the real SDK.

### Native lifecycle failure injection

The intended seam remains private to implementation/test support. It must preserve public `AmcamCamera` ABI/vtable, exact SDK function pointer types, one SDK-call serialization boundary, one session owner and the callback → bounded queue → one pull worker model. Cover Open, Start setup/callback registration, worker creation, recoverable Stop, successful Stop, flush failure/retry, fatal acquisition, terminal Close and still-capture quarantine. Do not add a public generic SDK abstraction.

### Hardware evidence

MU503 RGB24 and short-rate observations are narrow evidence. They do not prove production RAW layout, all features/accessories, other models, long-duration stability, reconnect recovery or CTI operation. Keep source presence, automated tests, physical tests and owner acceptance as separate evidence categories.

## 7. Next development sequence

1. Get the real Swift `libGenTLConsumerCAPI.dylib` building and tested; validate its pointer marshalling, full operation mapping, dynamic loading and Swift runtime packaging. The current C++ workflow validates only a mock bridge.
2. Install the official GenApi 3.5.1 Mac64_Universal SDK and compile `Camera::GenTLGenApi` on both supported architectures. Then load a real device XML and implement typed GenApi feature descriptors/read/write through `Camera::Common`; keep commands separate from features.
3. Complete the GenTL-to-Common frame adapter: derive verified pixel layout/stride from buffer metadata and PFNC namespace, preserve unknown layouts without guessing, and return the common immutable frame contract.
4. Qualify discovery, device access, stream lifecycle, frame payload/metadata, XML and GenApi feature behavior against an actual vendor CTI Producer and physical CTI camera.
5. Complete Phase 3 same-session AmScope control/stream physical regression and Phase 2 physical common-frame/RAW evidence.
6. Satisfy the original Phase 1 test-seam hardware entry condition before behavior-changing lifecycle injection; then test callback drain and investigate the SDK no-new-callback guarantee.
7. Reconcile AmScope feature/model coverage against physical evidence.
8. Complete remaining Phase 4 reusable UI component work; the Optika preview already exists and is not to be reimplemented.
9. Export non-Qt and optional Qt/GenTL CMake packages and verify clean external consumers under Phase 7.
10. Complete Phase 8 physical qualification for every declared provider/model combination.

## 8. Continuation rules

- Continue Camera source work on `camera-v.2.0`; do not develop the library on the Optika application branch.
- Use Optika as an application integration/reference line only. Its camera preview already exists; do not restart preview development or merge its GUI/workspace changes into Camera.
- Keep Controller/Stage, Slide Scanning and ImageProcessing as separate workstreams.
- Keep Qt optional to the core driver/common targets; preserve optional Phase 3 streaming and Phase 4 reusable UI targets and their tests.
- Verify branch HEAD and commits at each continuation.
- Update this ledger after each meaningful increment with exact source commit, changed files, tests actually run, physical hardware evidence and the next bounded task.
- Never mark phases complete from source presence or CI alone.

## 9. References

- [Governing original Camera Module Phase 0–8 roadmap](AMSCOPE_NATIVE_DRIVER_COMPLETION_PLAN.md)
- [AmScope implementation inventory](AMSCOPE_IMPLEMENTATION_INVENTORY.md)
- [Common/provider-specific type ownership](CAMERA_COMMON_TYPE_OWNERSHIP.md)
- [Private SDK test-seam design](AMSCOPE_NATIVE_DRIVER_TEST_SEAM.md)
- [GenTL/GenApi assessment](AMSCOPE_GENTL_GENAPI_CPP_INTEGRATION_ASSESSMENT.md)
- [Native Optika application camera integration](AMSCOPE_NATIVE_MACOS_APP.md)
- [Driver README](../../Camera/Amscope/README.md)
