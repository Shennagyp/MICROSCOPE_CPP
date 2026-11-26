# Camera Module v2.0 — Development Status

**Repository:** `Shennagyp/MICROSCOPE_CPP`  
**Development branch:** `camera-v.2.0`  
**Current branch HEAD:** `808779f8f67d81ed99e66a2aff0b5b9f1cde1321` — `fix(gentl): use global GenTL bool8_t ABI type`.  
**Latest CI-validated checkpoint:** `808779f8f67d81ed99e66a2aff0b5b9f1cde1321` — GenTL/GenApi workflow [#45733875474](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45733875474) passed Intel x86_64 and Apple Silicon arm64, including the C++ GenTL suite and official GenApi 3.5.1 build/tests.  
**Review date:** 26 November 2026.  
**Next implementation milestone:** Phase 6 — capability, feature, command and model coverage. Phase 5 is owner-confirmed complete; do not restart it.  
**Mandatory continuation guide:** [NEXT_AI_HANDOFF.md](NEXT_AI_HANDOFF.md).

## 0. Immediate continuation instruction

The next AI must read [NEXT_AI_HANDOFF.md](NEXT_AI_HANDOFF.md) before touching source. The project owner has confirmed Phase 5 as complete and identified Phase 6 as the next implementation milestone. Do not restart GenTL/GenApi integration, replace the Consumer architecture, or revive retired repositories as an assumed dependency. Start Phase 6 with a read-only source-to-test capability audit; implement only concrete, evidenced gaps.

## 1. Branch provenance

The requested name `amscope-b293-dualslide-v.0.9` is not a repository branch. The matching source branch is `OPTIKA-B293-DUALSLIDE-APP-v.0.9`, at `c7bb4b4dc41441989d71ee800c6d33b51dc54abe`.

The existing `camera-v.2.0` branch began at that exact commit and had one additional commit, `591bbeef` (enable the AmScope workflow). A tree comparison found **307 files under `Camera/` with identical paths and identical blob SHAs** across the source and target branches. There was no missing Camera library code to copy. Camera v2.0 is therefore continued on the existing `camera-v.2.0` branch, rather than duplicating the source tree.

The separate `OPTIKA-B293-DUALSLIDE-APP` branch is the application integration line. Its SwiftUI/AppKit GUI, workspace, slides, controller and stage remain outside the Camera library.

## 2. Product boundary

Camera v2.0 is a reusable **native/programmatic Camera Module**. The active scope is the non-UI camera library: provider discovery, device/session ownership, control, commands, telemetry, acquisition and backend-neutral frames. Qt and UI development have been removed from the active roadmap by the project owner.

It owns provider discovery/identity, native session lifecycle, backend-neutral camera contracts, programmatic settings/commands/telemetry, acquisition, frame delivery and provider-specific SDK integration.

It does not own an application's main window, workspace, application-specific view models or presentation state; microscope optical configuration; controller/stage semantics; slide scanning; image-processing algorithms; or application GPU policy. Consuming applications own their presentation and may build native UI independently.

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

`Camera::Common` provides backend-neutral contracts and a non-Qt latest-frame mailbox. The mailbox owns no camera, session, worker, event loop or UI notification policy. The active Camera Module has no Qt streaming or reusable UI deliverable. Application adapters own composition, window/workspace state and presentation policy.

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
- Data streams open through the retained device session, register `EVENT_NEW_BUFFER`, allocate/announce and queue a bounded buffer set, start acquisition, wait for completed-buffer events, copy payload and metadata, and requeue buffers. Stop unregisters the event before waiting for active reads to drain; GenTL specifies that unregistration aborts pending `EventGetData` calls. If unregistration fails, the implementation attempts `EventKill` for the single permitted waiter and quarantines the native context because normal teardown did not complete.
- `DataStreamFrame` preserves copied payload bytes, frame ID, nanosecond timestamp, pixel-format value and namespace (including PFNC namespace), pixel endianness, dimensions, image-present/chunk/incomplete state, image offset and X/Y padding. `GenTLFrameAdapter` maps only complete, image-only, zero-offset/zero-padding buffers with exact payload-size and stride checks: PFNC Mono8, unpacked Mono10/12/14/16 and RGB8 are supported. Multi-byte layouts require known byte order; packed, Bayer, planar, compressed and unknown layouts retain their opaque identifier/payload without fabricated stride. All size calculations are overflow-checked. The adapter accepts the frame by value and moves its owned payload into `camera::Frame`; callers can pass an rvalue to avoid another full-frame copy.
- Discovery and frame-wait APIs accept milliseconds and convert to GenTL nanosecond timeouts. Only one frame waiter is allowed per stream. Deterministic tests now hold `EventGetData` pending while closing the stream and verify that event unregistration cancels the wait; a separate fault-injection test verifies the `EventKill` fallback when unregistration fails.
- Device and stream sessions retain the required parent state. A stream can outlive its `DeviceSession` wrapper; the device handle closes only after the last stream/session reference is released.
- Failed native cleanup quarantines and process-retains the Consumer context and dynamically loaded C ABI bridge. The bridge is not unloaded while CTI handles may remain.
- GenApi XML retrieval uses `GCGetNumPortURLs` / `GCGetPortURLInfo`; `LOCAL` register-map XML is read in bounded chunks and `FILE` XML is read from the advertised file. HTTP and custom URL schemes are not implemented.
- `Camera::GenTLGenApi` is optional and requires the official GenApi 3.5.1 Mac64_Universal SDK. `GenApiFeatureAccess` maps Boolean, Integer, Float, String and Enumeration nodes to `ICameraFeatureAccess`. `GenApiCommandAccess` maps `ICommand` nodes to a separate `ICameraCommandAccess`, separating command issue from completion polling. `GenApiDeviceAccess` owns one DeviceSession and shares one GenApi node map/serialization boundary across both adapters. Descriptor enumeration and direct feature/command lookup recursively traverse categories reachable from the GenApi `Root`, deduplicating nodes while keeping internal `IntReg` and `Port` nodes private. The build option is off by default; official GenApi 3.5.1 SDK compilation and nested-category XML-backed integration tests pass on both architectures.
- See [Camera/GenTL README](../../Camera/GenTL/README.md) for the current API and explicit limitations.
### Excluded presentation layer — Qt and UI

The project owner has removed Qt, camera UI and preview development from the active Camera v2.0 scope. Existing `Camera/Qt`, `Camera/QtWidgets`, `QtAmcamCamera` and `amscope_preview` source remains historical/optional code in the repository; do not extend, refactor or treat it as a release dependency. Do not add Qt as a dependency of the active native library. The Optika application owns its SwiftUI/AppKit presentation and preview.

## 4. Governing roadmap and original Phase 0–8 ledger

The original *AmScope Native Driver and Camera Module Completion Plan* on `amscope-native-driver-v.0.2` remains the historical source of phase identities. The active roadmap is revised by the project owner: original Phases 3 and 4 (Qt streaming and reusable UI) are excluded from current scope. Their numbering remains in the ledger for traceability; they are not active blockers. The later `AMSCOPE_NATIVE_RELEASE_PLAN.md` is subordinate.

| Phase | Original deliverable | Current assessment |
|---:|---|---|
| 0 | Freeze and verify the baseline | **Accepted by owner historically.** MU503 physical observations exist; extended/exact-commit regression evidence remains follow-up, and one-camera observations do not certify all models. |
| 1 | Stabilize and qualify native AmScope driver | **Accepted by owner historically; post-acceptance hardening remains.** Single-session ownership, callback/queue/worker/pull path, frame pool and lifecycle primitives exist. Full injected SDK lifecycle tests and proof of callback quiescence around terminal Close remain open. |
| 2 | Finalize backend-neutral camera models | **Partially implemented.** Slice 2.1 discovery/capability models and 2.2 frame/layout models exist with automated tests and prior cross-architecture CI. The 2.3 semantic ownership audit is documented: common feature-access errors remain common, while AmScope lifecycle/command errors remain provider-specific. Physical common-frame/RAW validation remains open, so Phase 2 is not accepted. |
| 3 | Separate driver, programmatic control and Qt streaming libraries | **Excluded from active scope by owner decision.** The non-Qt `AmcamCamera` control/session API remains the active boundary. Existing Qt code is retained as legacy source but is not to be extended or qualified for this roadmap. |
| 4 | Extract reusable Qt camera UI library | **Excluded from active scope by owner decision.** No reusable Qt UI, camera preview or UI-consumer work is required. Optika presentation remains application-owned. |
| 5 | Integrate existing GenTL/GenApi implementation | **COMPLETE — owner-confirmed implementation milestone.** The GenTL Producer/device/stream lifecycle, buffer acquisition, frame metadata projection, remote-port access, XML retrieval and GenApi 3.5.1 feature/command integration are implemented and covered by cross-architecture CI. Do not reopen or reimplement Phase 5. Vendor CTI/device and physical-camera qualification are external integration/release evidence and must be tracked separately; they do not authorize restarting the completed implementation milestone. |
| 6 | Complete capability, feature, command and model coverage | **Open.** Broad AmScope implementation exists. Reconcile every exposed claim to provider/model support, units, preconditions, readback, asynchronous completion and physical evidence. |
| 7 | Release packaging and external-consumer integration | **Open.** Initial install rules exist and native public headers install/compile. An exported CMake package/config/version flow and clean native/GenTL external consumers remain; Qt/UI consumers are excluded. |
| 8 | Final hardware qualification and release | **Not started/accepted.** Requires exact-commit hardware records, declared provider/model matrix, installed-consumer tests, release CI and owner acceptance. |

Active continuation order: 0 → 1 → 2 → 5 → **6 (next implementation milestone)** → 7 → 8. Original Phases 3 and 4 are excluded and do not gate active delivery. Phase 5 is complete by owner direction; do not treat outstanding vendor-CTI/physical-camera qualification as permission to restart Phase 5. Track such evidence under Phase 8 and preserve the distinction between mock CI and real hardware qualification.

## 5. Changes made on camera-v.2.0

| Commit | Change | Effect |
|---|---|---|
| `70a7486` | Make Qt integrations opt-in | Historical build separation only. Qt streaming/UI targets are now excluded from active scope by owner decision. |
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
| `f049f91` | Compile official GenApi 3.5.1 SDK | CI downloads and hash-verifies the EMVA GenICam 2026.07 package, extracts SDK/runtime and compiles the GenApi target on Intel x86_64 and Apple Silicon arm64. |
| `78b3e43`–`31278e2` | Add XML-backed GenApi runtime coverage | Official GenApi parses a synthetic XML map and tests Root-category-only access, internal-register rejection, dynamic availability, Boolean/Integer/Enumeration read/write, command issue/completion and GenTL read-only access on both architectures. |
| `2d851fc` | Preserve pixel endianness and map verified PFNC layouts | Added GenTL `BUFFER_INFO_PIXEL_ENDIANNESS` capture and safe Mono8, unpacked Mono10/12/14/16 and RGB8 projections with exact payload-size/stride checks. |
| `5fa61e7`–`6721248` | Verify stream-close cancellation | Added a deterministic pending `EventGetData` test and expanded mock buffer capacity; the full GenTL/GenApi workflow passed both architectures in [#45700046670](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45700046670). |
| `caa0e8a` | Add event-unregistration failure fallback | On failed `GCUnregisterEvent`, attempt `EventKill` for the single allowed waiter and quarantine the context. This is validated on both architectures by the later workflow #45701059261. |
| `9043f82` | Traverse nested GenApi categories | Recursively enumerate features and commands reachable through Root category descendants, deduplicate node references, and test nested feature reads plus nested command execution. |
| `09f14fd`–`31ac9f6` | Transfer owned frame payload | `makeCommonFrame` accepts a `DataStreamFrame` by value and moves its payload into `camera::Frame`; exact payload-size checks run against the transferred storage. Both architectures passed [#45701918457](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45701918457). |
| `b9945dc` | Handle shutdown/unregister race | `EventGetData` returns `nullopt` for `GC_ERR_INVALID_HANDLE` only when shutdown is already in progress; both architectures passed [#45702691214](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45702691214). |
| `b1d0a61` | Complete scalar GenApi write coverage | Added Float/String write-readback and Float increment validation tests; both architectures passed [#45703819447](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45703819447). |

### CI evidence

Workflow [#45667125968](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45667125968) passed at exact source checkpoint `e12d765f408c2b0be7100c64b2eb245de5a2aca4` on both Intel x86_64 and Apple Silicon arm64:

- **Non-Qt Intel x86_64:** 12/12 CTest tests passed; non-Qt public headers installed; Qt facade absent from the non-Qt install; installed-header syntax check and architecture/probe checks passed.
- **Non-Qt Apple Silicon arm64:** 12/12 CTest tests passed with the same install-surface, syntax and architecture/probe checks.
- **Qt Apple Silicon arm64:** 18/18 CTest tests passed, including `camera_qt_frame_stream_tests`, `camera_qt_stream_handoff_tests`, the AmScope Qt request tests and all reusable widget tests; streaming library, widgets and preview built and artifact/architecture checks passed.
- **Qt Intel x86_64:** 18/18 CTest tests passed with the same Qt target/test and artifact/architecture checks.

Historical Qt tests cover the frame-stream publication/handoff implementation that remains in the repository. They are not active Camera v2.0 deliverables. Those CI results do not prove physical camera behavior, callback quiescence or RAW sensor correctness.

### GenTL C++ bridge CI

Workflow [#45682828203](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45682828203) passed at exact code checkpoint `549433466098c5daeb63d72609776d958a35c2fe`:

- **Apple Silicon arm64:** 2/2 CTest tests passed; static library architecture check passed.
- **Intel x86_64:** 2/2 CTest tests passed; static library architecture check passed.

The mock bridge tests cover dynamic-library symbol/version checks, CTI/TL/interface/device discovery, device-session lifetime after the Producer wrapper is released, stream-ID enumeration, remote-port read/write, `LOCAL` XML retrieval, stream open/start, buffer allocation/announcement/queueing, `EVENT_NEW_BUFFER` delivery, payload copy, frame ID/timestamp/PFNC format and namespace, buffer requeue, stop/unregister/flush/revoke/close, timeout unit conversion, a stream retaining its device after the `DeviceSession` wrapper closes, and process-lifetime bridge retention after an injected `DSClose` failure.

These tests use a mock C ABI bridge. The GenApi integration suite parses a synthetic XML feature map and exercises feature/command access against the official GenApi implementation. The tests do **not** validate the actual Swift C ABI dylib, a vendor CTI Producer, actual camera acquisition or physical hardware.

### GenApi official SDK and runtime CI

Workflow [#45693246924](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45693246924) passed on Intel x86_64 and Apple Silicon arm64 at commit `31278e255ea3fc7c7a23e3fd3e2e1c734be6e896`. CI downloaded and hash-verified the official EMVA GenICam 2026.07 package, extracted the GenApi 3.5.1 SDK and runtime, built `Camera::GenTLGenApi`, ran the XML-backed integration suite and verified architecture. The fixture covers Root-category-only feature/command enumeration and direct lookup, internal-register rejection, dynamic `pIsAvailable`, Boolean/Integer/Enumeration reads and writes, command issue/completion polling, and GenTL READONLY mapping to GenApi RO. This validates the GenApi adapter against the official GenApi implementation and a synthetic port—not a vendor CTI Producer or physical camera.

The Swift C ABI source and tests are on private repository branch `Shennagyp/AMSCOPE_DRIVER:gentl-c-api-v.1.0`, latest commit `92b7f28d3aab94d5d295a89a6436fe99df601c87`. Tests cover typed pointer/buffer marshalling, event registration/read/unregister, buffer-info forwarding and invalid invocation-shape rejection; the bridge bounds argument vectors to the maximum GenTL operation arity. Workflow [#45692472539](https://github.com/Shennagyp/AMSCOPE_DRIVER/actions/runs/45692472539) was cancelled before any job steps. The Swift GenTL packages declare macOS 13.0 as their minimum deployment target. The real C ABI dylib, operation mapping and universal packaging remain unvalidated. Do not treat the C++ mock bridge as proof that the Swift dylib loads or behaves correctly.

Historical Core/Qt workflow [#45681878032](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45681878032) passed all four jobs after the Common CMake target was moved to the root project; Qt/UI is now excluded from active scope. The earlier GenTL-only run [#45678105743](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45678105743) is superseded by the later stream-acquisition matrix.

No local build or physical-camera test is claimed in this status update. The GenTL/GenApi workflow [#45700046670](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45700046670) passed both architectures with the pending-wait cancellation test. The newer `EventKill` fallback change passed Intel x86_64 in [#45700375523](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45700375523); Apple Silicon validation is queued. Vendor CTI and physical-camera validation remain outstanding.

## 6. Highest-priority unresolved risks

### Callback quiescence

The current callback-entry counter can drain callbacks already entered; it does not independently prove that a new callback cannot begin after `Amcam_Close` returns. Do not release callback-reachable resources based only on a zero counter. Prove both in-flight drain and no-new-callback after terminal close, using authoritative vendor evidence or a defensible synchronization design.

The private SDK dispatch-seam design can deterministically exercise injected failures and races, but a fake cannot prove undocumented behavior of the real SDK.

### Native lifecycle failure injection

The intended seam remains private to implementation/test support. It must preserve public `AmcamCamera` ABI/vtable, exact SDK function pointer types, one SDK-call serialization boundary, one session owner and the callback → bounded queue → one pull worker model. Cover Open, Start setup/callback registration, worker creation, recoverable Stop, successful Stop, flush failure/retry, fatal acquisition, terminal Close and still-capture quarantine. Do not add a public generic SDK abstraction.

### Hardware evidence

MU503 RGB24 and short-rate observations are narrow evidence. They do not prove production RAW layout, all features/accessories, other models, long-duration stability, reconnect recovery or CTI operation. Keep source presence, automated tests, physical tests and owner acceptance as separate evidence categories.

## 7. Next development sequence

1. Validate the real Swift `libGenTLConsumerCAPI.dylib`: pointer marshalling, complete operation mapping, dynamic loading and Swift runtime packaging. The current C++ workflow validates only a mock bridge.
2. Expand GenApi runtime coverage for malformed XML, unsupported/unknown node types, command completion transitions and failure/recovery paths; keep public access restricted to Root-category features.
3. Extend the verified PFNC projection only when component order, packing, row stride and byte order are explicit; keep unsupported layouts opaque.
4. Qualify discovery, device access, stream lifecycle, frame payload/metadata, PFNC layouts and GenApi behavior against a vendor CTI Producer and physical CTI camera.
5. Complete Phase 2 physical common-frame/RAW evidence.
6. Complete Phase 1 callback-quiescence and failure-injection hardening only after satisfying its documented hardware entry condition; test callback drain and establish the SDK no-new-callback guarantee.
7. Reconcile AmScope feature/model coverage against physical evidence.
8. Export native and GenTL CMake packages and verify clean external consumers under Phase 7.
9. Complete Phase 8 physical qualification for every declared provider/model combination.
10. Verify vendor SDK redistribution terms.

## 8. Continuation rules

- Continue Camera source work on `camera-v.2.0`; do not develop the library on the Optika application branch.
- Use Optika as an application integration/reference line only. Its camera preview already exists; do not restart preview development or merge its GUI/workspace changes into Camera.
- Keep Controller/Stage, Slide Scanning and ImageProcessing as separate workstreams.
- Keep the active Camera Module and its consumers free of Qt/UI dependencies. Existing Qt/UI source is out of active scope; do not extend it unless the owner reopens that scope.
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
