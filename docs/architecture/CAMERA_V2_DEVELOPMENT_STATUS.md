# Camera Module v2.0 — Development Status

**Repository:** `Shennagyp/MICROSCOPE_CPP`  
**Development branch:** `camera-v.2.0`  
**Latest provider implementation checkpoint:** `85dd6af1fc71434d16452e4c62ceae6933b2a713` — GenTL discovery routing and GenApi negative-path coverage are recorded. **Latest package implementation checkpoint:** `49cc11e430a8fcf2d62b28f861fb03439992e047` — package options imply required Qt adapters, GenApi exports require official imported SDK targets, and non-Qt consumers disable Qt discovery. **Latest package CI definition:** `c0f50da0916cc3a74fe361ef27ee2583910e1350` — adds installed GenApi consumer, runtime/architecture checks, and verifies the base GenTL component configures with both Qt and GenApi package discovery disabled. Package CI pending.  
**Latest CI-validated checkpoint:** `85dd6af1fc71434d16452e4c62ceae6933b2a713` — workflow [#45770024248](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45770024248) passed the GenTL bridge and official GenApi integration build/tests on Intel x86_64 and Apple Silicon arm64 (10/10 bridge tests and 11/11 official GenApi tests per architecture).  
**Review date:** 26 November 2026.  
**Phase status:** Phase 3 accepted; Phase 4 source/offscreen checks complete with visual and physical MU503 acceptance pending/postponed; Phase 5 complete by owner direction; Phase 6 remains open but blocked at the frozen AmScope provider-test boundary; Phase 7 is active by explicit owner authorization on 26 November 2026.  
**Mandatory continuation guide:** [NEXT_AI_HANDOFF.md](NEXT_AI_HANDOFF.md).

## 0. Immediate continuation instruction

The next AI must read [NEXT_AI_HANDOFF.md](NEXT_AI_HANDOFF.md) and this ledger before touching source. Qt streaming and reusable UI are original Phase 3/4 deliverables, not excluded historical code. Phase 3 is accepted. Phase 4 source/offscreen work is complete, but human visual inspection and physical MU503 UI acceptance remain pending; the owner postponed the physical test. Preserve the existing preview. Phase 5 GenTL/GenApi is complete by owner direction. Phase 6 remains open because AmScope provider-level tests are blocked by the explicit freeze. The owner explicitly authorized Phase 7 packaging and external-consumer work on 26 November 2026. **Do not modify AmScope provider source, tests, public contracts or runtime behavior; do not write to `OPTIKA-B293-DUALSLIDE-APP-v.0.9`.** Phase 7 may change CMake packaging metadata, package configuration, consumer fixtures, CI and documentation only. Continue on `camera-v.2.0`. Do not run local build/test/debug runs. Do not restart Phase 5 or revive retired repositories.

## 1. Branch provenance

The requested name `amscope-b293-dualslide-v.0.9` is not a repository branch. The matching source branch is `OPTIKA-B293-DUALSLIDE-APP-v.0.9`, at `c7bb4b4dc41441989d71ee800c6d33b51dc54abe`.

The existing `camera-v.2.0` branch began at that exact commit and had one additional commit, `591bbeef` (enable the AmScope workflow). A tree comparison found **307 files under `Camera/` with identical paths and identical blob SHAs** across the source and target branches. There was no missing Camera library code to copy. Camera v2.0 is therefore continued on the existing `camera-v.2.0` branch, rather than duplicating the source tree.

The separate `OPTIKA-B293-DUALSLIDE-APP` branch is the application integration line. Its SwiftUI/AppKit GUI, workspace, slides, controller and stage remain outside the Camera library.

## 2. Product boundary

Camera v2.0 is a reusable Camera Module, with a non-Qt native/programmatic core and optional Qt streaming/UI deliverables. Qt is optional at the dependency level: consumers of Camera::Common and Amscope::Driver do not inherit Qt. That does not remove original Phase 3 (Qt streaming) or Phase 4 (reusable Qt UI) from the roadmap.

The AmScope native provider remains the sole owner of its SDK session. The Qt adapter composes that provider; reusable widgets consume common contracts and host-supplied callbacks. The AmScope preview application is a composition root in this repository, not the owner of the reusable widget implementations.

The module does not own an application's unrelated main-window workflows, microscope optical configuration, controller/stage semantics, slide scanning, image-processing algorithms or application GPU policy. Those remain with consuming applications. The AmScope preview shell/workspace is nevertheless an existing Phase 4 deliverable and remains in this repository.

Ownership: the AmScope preview application composes Camera::QtWidgets presentation and the Amscope::Qt QObject request/frame adapter. Amscope::Qt composes one AmcamCamera instance, which remains the sole HAmcam/session owner. The SDK callback feeds the bounded event FIFO, one acquisition worker and Amcam_PullImageV4; immutable FramePtr leases are then delivered through the Qt frame stream to FrameView.

Camera::Common remains free of Qt and vendor SDK dependencies. Camera::Qt and Camera::QtWidgets are separate optional targets. The non-Qt driver and Qt facade must continue to share one AmcamCamera instance and one native handle.

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
### Optional Qt streaming and reusable UI — original Phases 3 and 4

Qt source is active Camera Module deliverable code, with optional target dependencies:

- Camera/Qt: Qt Core frame-stream notification and handoff.
- Camera/QtWidgets: reusable FrameView, CameraControlPanel, ImageInfoBar, and CameraFeatureEditor widgets.
- Camera/Amscope/src/QtAmcamCamera.cpp: QObject request/stream adapter over the single AmcamCamera session.
- Camera/Amscope/app/main.cpp: Qt Widgets preview application, workspace composition, feature/command/accessory controls, still capture and telemetry presentation.

The preview uses the reusable FrameView, CameraControlPanel, and ImageInfoBar; the reusable CameraFeatureEditor is now integrated into the feature tabs. Amcam-specific pixel-format display labels are supplied by the host while canonical enum symbols remain the submitted values. Some AmScope-specific command/accessory panels remain host-owned because their parameters and completion semantics are backend-specific.

The Qt targets remain opt-in. Do not add Qt dependencies to Camera/Common or the non-Qt native driver. The separate Optika application branch remains an application integration/reference line; do not move the Camera library implementation there.

## 4. Governing roadmap and original Phase 0–8 ledger

The original *AmScope Native Driver and Camera Module Completion Plan* on `amscope-native-driver-v.0.2` remains the historical source of phase identities. Original Phases 3 and 4 (Qt streaming and reusable UI) remain in the governing roadmap. Phase 3 is accepted by owner direction. Phase 4 source/offscreen work is complete; visual acceptance remains pending and physical MU503 testing is postponed. Phase 6 is the active software workstream by owner direction. A previous status interpretation that excluded Phases 3 and 4 was incorrect and is superseded by this ledger. The later `AMSCOPE_NATIVE_RELEASE_PLAN.md` is subordinate.

| Phase | Original deliverable | Current assessment |
|---:|---|---|
| 0 | Freeze and verify the baseline | **Accepted by owner historically.** MU503 physical observations exist; extended/exact-commit regression evidence remains follow-up, and one-camera observations do not certify all models. |
| 1 | Stabilize and qualify native AmScope driver | **Accepted by owner historically; post-acceptance hardening remains.** Single-session ownership, callback/queue/worker/pull path, frame pool and lifecycle primitives exist. Full injected SDK lifecycle tests and proof of callback quiescence around terminal Close remain open. |
| 2 | Finalize backend-neutral camera models | **Partially implemented.** Slice 2.1 discovery/capability models and 2.2 frame/layout models exist with automated tests and prior cross-architecture CI. The 2.3 semantic ownership audit is documented: common feature-access errors remain common, while AmScope lifecycle/command errors remain provider-specific. Physical common-frame/RAW validation remains open, so Phase 2 is not accepted. |
| 3 | Separate driver, programmatic control and Qt streaming libraries | **ACCEPTED BY OWNER.** AmcamCamera remains the single native session owner; Amscope::Qt composes it for QObject request scheduling and frame delivery. Camera::Qt provides the extracted frame-stream boundary. Owner-confirmed physical Qt-to-MU503 streaming smoke evidence is recorded in the subordinate release plan. Qt remains optional to non-Qt consumers. |
| 4 | Extract reusable Qt camera UI library | **SOURCE AND OFFSCREEN CHECKS COMPLETE; OWNER ACCEPTANCE PENDING/POSTPONED.** Camera::QtWidgets contains FrameView, CameraControlPanel, ImageInfoBar, CameraFeatureEditor, CameraCommandPanel and CameraCapabilityPanel. The AmScope preview uses the feature editor, telemetry strip and selected-model capability panel. The command panel is for no-argument ICameraCommandAccess commands; parameterized AmScope actions remain host-owned. Workflow #45749181949 built amscope_preview and passed 24/24 Qt CTest tests on both architectures, including an application-level offscreen composition and dock-state restore test. Human visual/workspace inspection and physical MU503 UI smoke remain pending; the owner has postponed the physical test. This pending acceptance does not block the owner-directed Phase 6 software work. |
| 5 | Integrate existing GenTL/GenApi implementation | **COMPLETE — owner-confirmed implementation milestone.** The GenTL Producer/device/stream lifecycle, buffer acquisition, frame metadata projection, remote-port access, XML retrieval and GenApi 3.5.1 feature/command integration are implemented and covered by cross-architecture CI. Do not reopen or reimplement Phase 5. Vendor CTI/device and physical-camera qualification are external integration/release evidence and must be tracked separately; they do not authorize restarting the completed implementation milestone. |
| 6 | Complete capability, feature, command and model coverage | **OPEN; BLOCKED AT THE AMSCOPE PROVIDER-TEST BOUNDARY.** Provider-neutral GenTL/GenApi coverage and discovery/vector decisions are complete for the current public contract. AmScope provider-level feature/command/model tests remain unaddressed because AmScope is frozen by owner instruction. The owner has explicitly authorized Phase 7 to proceed without changing that freeze. Do not mark Phase 6 complete. See [Phase 6 audit](CAMERA_V2_PHASE6_CAPABILITY_AUDIT.md). |
| 7 | Release packaging and external-consumer integration | **ACTIVE BY OWNER DIRECTION (26 November 2026).** Commit `643319a` adds the `Camera/` package entry point, namespaced target exports, relocatable config/version files, optional component dependency loading and clean external-consumer fixtures. The two-architecture package workflow includes build-tree tests, official GenApi package configuration, installation, prefix relocation, clean Common/GenTL/GenApi/AmScope/Qt external consumers, runtime-loading checks and installed-artifact architecture checks. Installed-artifact physical AmScope and GenTL camera smoke tests remain required by the original Phase 7 plan; CI does not replace them. |
| 8 | Final hardware qualification and release | **Open.** Requires exact-commit hardware records, declared provider/model matrix, installed-consumer deployment, vendor SDK redistribution review, release CI and owner acceptance. |

Historical phase order remains 0 → 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8. Current execution is owner-directed: Phase 3 is accepted; Phase 4 source/offscreen work is complete while visual/workspace and physical MU503 acceptance remain pending (physical test postponed); Phase 5 is complete by owner direction; Phase 6 remains open but is blocked by the explicit AmScope freeze; Phase 7 is now active by explicit owner authorization despite that blocked Phase 6 provider-specific test gap. This does not waive or complete Phase 6. Vendor CTI/device qualification remains separate Phase 8 evidence; do not restart Phase 5.

## 5A. Phase 7 — package and external-consumer contract

Owner authorization: 26 November 2026. Phase 7 proceeds while the remaining
AmScope Phase 6 provider tests stay blocked by the existing AmScope freeze.
This is a sequencing authorization, not a declaration that Phase 6 is complete.

The reusable package entry point is `Camera/CMakeLists.txt`, distinct from
the application-oriented repository root. Commits `643319a`, `58f3790`, `49cc11e`, `fc3be40`, `22ee34f` and `c0f50da`
introduce the package entry point, runtime consumer checks, Qt/GenApi
dependency isolation, official GenApi external consumer, installed
architecture checks and namespaced exports
for `Camera::Common`, `Camera::GenTLConsumerBridge`,
optional `Camera::GenTLGenApi`, `Amscope::Driver`, optional
`Amscope::Qt`, and optional `Camera::Qt` / `Camera::QtWidgets`.

Package components load dependencies selectively. Common, GenTL and native
AmScope consumers must not acquire Qt. GenApi exports must resolve the
official GenApi 3.5.1 CMake package rather than embedding machine-specific SDK
paths. The AmScope vendor runtime is installed beside its driver; SDK
redistribution approval remains a Phase 8 gate. GenTL packages do not bundle
a CTI Producer or the separate C ABI bridge runtime.

Acceptance requires clean consumer projects for Common, GenTL, AmScope and
optional Qt to run `find_package(CameraModule CONFIG)` against an installed
and relocated prefix, then compile, link and run without source-tree include
paths. The package workflow tests Intel x86_64 and Apple Silicon arm64, and
inspects installed dylib dependencies. CI is software/package evidence only.
The original Phase 7 plan additionally requires an installed non-Qt AmScope
consumer to enumerate/open/read a setting/start/receive frames/stop/close, an
installed Qt consumer to show physical frames and descriptor-driven controls,
and an installed GenTL consumer tested with a physical CTI camera. These
hardware checks remain pending. Phase 7 also requires the vendor SDK
redistribution requirements to be documented before publication.

Usage and component options are documented in [Camera/README.md](../../Camera/README.md).

## 5. Changes made on camera-v.2.0

| Commit | Change | Effect |
|---|---|---|
| `70a7486` | Make Qt integrations opt-in | Correctly separated Qt targets from the native core; the later interpretation that this removed Phase 3/4 deliverables was incorrect. Qt remains an optional, active deliverable. |
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

| `a1be35d` | Preserve RAW bit-depth enumeration errors | Distinguishes vendor `E_NOTIMPL` (feature unavailable) from other SDK failures (backend failure with native HRESULT); rejects invalid enumeration counts; adds a private classifier and CTest coverage. |
| `b8148ee` | Apply model profile to RAW bit-depth selection | `RawOutputBitDepth` now obeys the same `PixelFormatSelection` profile policy as `AmcamPixelFormat`; adds profile-policy assertion and documents the mapping. |
| `5302a06` | Restore optional Qt CI matrix | Restores the two-architecture Qt streaming/reusable-widget/preview build, CTest and artifact checks; adds Qt source paths to push filters. |
| `17d3ea1` | Integrate reusable feature editor | Replaces duplicated AmScope preview feature-tab widget construction, readback application and value parsing with `CameraFeatureEditor`; host retains request scheduling, generation handling and readback authority. |
| `a445bfa` | Preserve host enumeration labels | Adds a generic enumeration display-name provider so Amcam pixel-format labels remain readable while canonical enum symbols remain submitted values; adds a widget regression test. |
| `8d04a06` | Add reusable command presentation | Adds CameraCommandPanel for no-argument common commands, with request-pending, executing, completed and failed states; host owns issue/completion. |
| `3d207c3` | Test command state semantics | Covers availability/executability, retries, disabled states and independent completion reporting. |
| `0f6465c` | Add model capability presentation | Adds CameraCapabilityPanel and integrates it into the AmScope Camera Properties dock using the selected discovery result. |
| `28a9710`, `9c3362e` | Report discovery selection safely | Adds a selection-changed callback and resets selection on a new discovery snapshot; opaque enumeration IDs are not persisted as stable camera identity. |
| `587ee5e` | Add preview composition smoke | Adds `--self-test-ui` to the existing preview executable; verifies document/dock composition, status bar, Output pane, tri-state capability table and dock save/restore without discovery or user settings. |
| `37a9b54` | Verify nested widget ownership | Uses QWidget ancestry checks for nested dock contents; the offscreen smoke passes in cross-architecture CI. |
| `a4fc39b` | Explain RAW bit-depth profile denial | The descriptor factory now supplies an actionable disabled reason when model policy blocks the feature; a regression test checks the exact reason. |
| `8b9be09`, `977d53b` | Test GenApi port failure recovery | Adds mock read/write fault injection and verifies feature read failure recovery, failed-write no-side-effect behavior, and command execution failure/recovery. The read fault is injected before first feature access to avoid GenApi cache bypass. |
| `9bb1c4c` | Test malformed GenApi XML handling | Injects malformed XML through the mock remote port; verifies node-map construction rejects it and a later valid node map recovers. |
| `a07c8a2` | Initial unsupported-node fixture | Added a generic `Node` case; superseded by the explicit `IRegister` test below. |
| `ea356d5` | Test GenApi command completion-query failure recovery | Injects a remote-port read failure while `ICommand::IsDone()` polls completion; verifies `BackendFailure` mapping, then verifies polling resumes. Workflow #45767832186 passed official GenApi 3.5.1 integration on both architectures. |
| `a455979` | Reject stale GenTL interface descriptors | Verifies `Producer::openDevice` rejects a descriptor whose interface ID is no longer retained, complementing the existing foreign-Producer-path rejection. Workflow #45768851121 passed both architectures. |
| `5080256` | Keep Producer test failures distinct | Assigns a unique return code to the stale-interface descriptor assertion so it cannot be confused with the later closed-stream assertion; no production behavior changes. Workflow #45769387254 passed both architectures. |
| `85dd6af` | Validate GenTL device identifier routing | The mock `IFOpenDevice` rejects unknown device IDs; `ProducerTests` verifies `openDevice` propagates the failure for a descriptor with a missing device ID. Workflow #45770024248 passed both architectures. |
| `643319a` | Add relocatable CameraModule CMake package | Adds the separate `Camera/` package entry point, component-aware namespaced target exports, config/version files, and Common/GenTL/AmScope/Qt external-consumer fixtures. |
| `58f3790` | Exercise installed package consumers | External consumers instantiate the AmScope, GenTL and Qt APIs; package CI runs them after installation and prefix relocation, and inspects installed runtime link paths. |
| `49cc11e` | Enforce optional dependency isolation | Preview configuration implies its Qt adapter; GenApi package exports require imported official SDK targets; Common/GenTL/native consumers configure with Qt package discovery disabled. |

### Current Qt and non-Qt CI — checkpoint a4fc39b

Workflow [#45759739032](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45759739032)
passed at exact checkpoint a4fc39b3d876ba301a2f1c8a916216c94186cd3b:

- Non-Qt Intel x86_64 and Apple Silicon arm64 builds, CTest, install-surface checks, public-header syntax checks and architecture checks passed.
- Qt Intel x86_64 and Apple Silicon arm64 builds compiled the Qt adapter, reusable widgets, amscope_preview and the hardware-rate test executable.
- 24/24 CTest tests passed on each Qt architecture, including `amscope_feature_access_validation_tests` and the application-level `amscope_preview_ui_composition_tests` which verifies dock/document composition, status-bar widgets, the model capability table and dock-state save/restore.
- Artifact and architecture checks passed for the driver, Qt adapter, vendor SDK, preview application and hardware-rate test executable.

The offscreen test suppresses discovery and user QSettings; it does not perform human visual inspection, exercise dock persistence interactively, or verify a physical camera.

### CI evidence

Workflow [#45667125968](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45667125968) passed at exact source checkpoint `e12d765f408c2b0be7100c64b2eb245de5a2aca4` on both Intel x86_64 and Apple Silicon arm64:

- **Non-Qt Intel x86_64:** 12/12 CTest tests passed; non-Qt public headers installed; Qt facade absent from the non-Qt install; installed-header syntax check and architecture/probe checks passed.
- **Non-Qt Apple Silicon arm64:** 12/12 CTest tests passed with the same install-surface, syntax and architecture/probe checks.
- **Qt Apple Silicon arm64:** 18/18 CTest tests passed, including `camera_qt_frame_stream_tests`, `camera_qt_stream_handoff_tests`, the AmScope Qt request tests and all reusable widget tests; streaming library, widgets and preview built and artifact/architecture checks passed.
- **Qt Intel x86_64:** 18/18 CTest tests passed with the same Qt target/test and artifact/architecture checks.

Qt streaming and reusable UI tests are original Phase 3/4 deliverables. Their CI results prove the configured software build and widget tests, not physical camera behavior, callback quiescence or RAW sensor correctness.

### GenTL C++ bridge CI

Workflow [#45682828203](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45682828203) passed at exact code checkpoint `549433466098c5daeb63d72609776d958a35c2fe`:

- **Apple Silicon arm64:** 2/2 CTest tests passed; static library architecture check passed.
- **Intel x86_64:** 2/2 CTest tests passed; static library architecture check passed.

The mock bridge tests cover dynamic-library symbol/version checks, CTI/TL/interface/device discovery, device-session lifetime after the Producer wrapper is released, stream-ID enumeration, remote-port read/write, `LOCAL` XML retrieval, stream open/start, buffer allocation/announcement/queueing, `EVENT_NEW_BUFFER` delivery, payload copy, frame ID/timestamp/PFNC format and namespace, buffer requeue, stop/unregister/flush/revoke/close, timeout unit conversion, a stream retaining its device after the `DeviceSession` wrapper closes, and process-lifetime bridge retention after an injected `DSClose` failure.

These tests use a mock C ABI bridge. The GenApi integration suite parses a synthetic XML feature map and exercises feature/command access against the official GenApi implementation. The tests do **not** validate the actual Swift C ABI dylib, a vendor CTI Producer, actual camera acquisition or physical hardware.

### GenApi official SDK and runtime CI

Workflow [#45693246924](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45693246924) passed on Intel x86_64 and Apple Silicon arm64 at commit `31278e255ea3fc7c7a23e3fd3e2e1c734be6e896`. CI downloaded and hash-verified the official EMVA GenICam 2026.07 package, extracted the GenApi 3.5.1 SDK and runtime, built `Camera::GenTLGenApi`, ran the XML-backed integration suite and verified architecture. The fixture covers Root-category-only feature/command enumeration and direct lookup, internal-register rejection, dynamic `pIsAvailable`, Boolean/Integer/Enumeration reads and writes, command issue/completion polling, and GenTL READONLY mapping to GenApi RO. This validates the GenApi adapter against the official GenApi implementation and a synthetic port—not a vendor CTI Producer or physical camera.

The Swift C ABI source and tests are on private repository branch `Shennagyp/AMSCOPE_DRIVER:gentl-c-api-v.1.0`, latest commit `92b7f28d3aab94d5d295a89a6436fe99df601c87`. Tests cover typed pointer/buffer marshalling, event registration/read/unregister, buffer-info forwarding and invalid invocation-shape rejection; the bridge bounds argument vectors to the maximum GenTL operation arity. Workflow [#45692472539](https://github.com/Shennagyp/AMSCOPE_DRIVER/actions/runs/45692472539) was cancelled before any job steps. The Swift GenTL packages declare macOS 13.0 as their minimum deployment target. The real C ABI dylib, operation mapping and universal packaging remain unvalidated. Do not treat the C++ mock bridge as proof that the Swift dylib loads or behaves correctly.

Historical Core/Qt workflow [#45681878032](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45681878032) passed all four jobs after the Common CMake target was moved to the root project. Later Qt CI removal was incorrect and is reversed by 5302a06. The earlier GenTL-only run [#45678105743](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45678105743) is superseded by the later stream-acquisition matrix.

No local build or physical-camera test is claimed in this status update. The GenTL/GenApi workflow [#45700046670](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45700046670) passed both architectures with the pending-wait cancellation test. The newer `EventKill` fallback change passed Intel x86_64 in [#45700375523](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45700375523); Apple Silicon validation is queued. Vendor CTI and physical-camera validation remain outstanding.


## 6. Phase 6 audit checkpoint — 26 November 2026

The source-to-test audit is recorded in
[CAMERA_V2_PHASE6_CAPABILITY_AUDIT.md](CAMERA_V2_PHASE6_CAPABILITY_AUDIT.md).
It covers Common contracts, AmScope discovery/features/commands/model policy,
and GenTL/GenApi discovery/features/commands. The audit is not a Phase 6
completion claim. Owner direction on 26 November 2026 explicitly postponed
the physical MU503 test and instructed continued Phase 6 development. Phase 4
remains pending owner acceptance, but it is not a blocker for Phase 6 software
work. AmScope implementation is frozen: do not modify its source, tests,
contracts or behavior.

The first three confirmed AmScope gaps were corrected:
- `a1be35d`: preserve `E_NOTIMPL` as model-level unavailability and map
  other failed `Amcam_get_PixelFormatSupport` calls to `BackendFailure`
  with the native HRESULT. Add deterministic classification tests.
- `b8148ee`: gate `RawOutputBitDepth` with the existing
  `PixelFormatSelection` model-profile policy because both settings mutate
  `AMCAM_OPTION_PIXEL_FORMAT`.
- `a4fc39b`: provide an explicit descriptor `disabledReason` when that profile
  policy blocks RAW bit-depth selection; the regression test verifies the
  exact user-facing reason rather than accepting an empty tooltip.

CI evidence:
- [#45742832263](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45742832263)
  passed Intel x86_64 and Apple Silicon arm64 for `a1be35d`.
- [#45742968336](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45742968336)
  passed Intel x86_64 and Apple Silicon arm64 for `b8148ee`.
- [#45759739032](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45759739032)
  passed all four non-Qt/Qt jobs on Intel x86_64 and Apple Silicon arm64 for
  `a4fc39b`; the feature-validation test passed on both architectures.

No local build/test/debug run or new physical-camera test was performed.
AmScope source and tests were not modified. The latest GenTL/GenApi-only
increment is commit `85dd6af1fc71434d16452e4c62ceae6933b2a713`; workflow
[#45770024248](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45770024248)
passed the GenTL suite and official GenApi 3.5.1 build/integration tests on
Intel x86_64 and Apple Silicon arm64.

That increment covers:
- malformed GenApi XML is rejected and a subsequent valid node map recovers;
- a public GenApi `Register`/`IRegister` node reachable from `Root` is omitted
  from scalar feature descriptors and direct read/write return `Unsupported`;
- injected remote-port failures map feature read, feature write, command
  execution and command-completion polling to `BackendFailure`, with subsequent
  recovery / no-side-effect assertions.

Phase 6 disposition:
1. GenTL discovery-to-Common is resolved: retain provider-specific
   `DeviceDescriptor`; the Producer tests now reject foreign Producer paths,
   stale interface IDs and unknown device IDs. Do not add a lossy projection
   without a real multi-provider consumer requirement.
2. Vector semantics are resolved for current consumers: `CameraFeatureEditor`
   uses `elementCount` for cardinality guidance and Common validation rejects
   mismatched vector lengths. Per-element labels/ranges are not currently
   required; do not expand `CameraFeatureDescriptor` without a new consumer.
3. GenTL/GenApi positive and negative adapter-boundary coverage is complete
   for the current public contract, including malformed XML, unsupported
   IRegister, feature read/write failures, command execution/status failures,
   and recovery.
4. The remaining high-priority Phase 6 gaps are AmScope provider-level feature,
   command and model-mapping tests. They are blocked by the explicit AmScope
   freeze. Do not modify AmScope source, tests, contracts or behavior.
5. No additional provider-neutral Phase 6 source gap is currently identified.
   Do not invent work or reopen completed GenTL integration. Keep physical
   MU503, vendor CTI and real-device GenApi evidence pending for Phase 8.

## 7. Highest-priority unresolved risks

### Callback quiescence

The current callback-entry counter can drain callbacks already entered; it does not independently prove that a new callback cannot begin after `Amcam_Close` returns. Do not release callback-reachable resources based only on a zero counter. Prove both in-flight drain and no-new-callback after terminal close, using authoritative vendor evidence or a defensible synchronization design.

The private SDK dispatch-seam design can deterministically exercise injected failures and races, but a fake cannot prove undocumented behavior of the real SDK.

### Native lifecycle failure injection

The intended seam remains private to implementation/test support. It must preserve public `AmcamCamera` ABI/vtable, exact SDK function pointer types, one SDK-call serialization boundary, one session owner and the callback → bounded queue → one pull worker model. Cover Open, Start setup/callback registration, worker creation, recoverable Stop, successful Stop, flush failure/retry, fatal acquisition, terminal Close and still-capture quarantine. Do not add a public generic SDK abstraction.

### Hardware evidence

MU503 RGB24 and short-rate observations are narrow evidence. They do not prove production RAW layout, all features/accessories, other models, long-duration stability, reconnect recovery or CTI operation. Keep source presence, automated tests, physical tests and owner acceptance as separate evidence categories.

## 8. Next development sequence

1. Phase 6 remains open at the AmScope provider-test boundary. Its
   provider-neutral GenTL/GenApi coverage and discovery/vector decisions are
   complete for the current public contract. The remaining AmScope
   feature/command/model tests are blocked by the explicit freeze; do not
   modify AmScope source, tests, contracts or runtime behavior.
2. The owner explicitly authorized Phase 7 on 26 November 2026. Continue
   package/export work on `camera-v.2.0`; do not wait for the frozen Phase 6
   tests and do not mark Phase 6 complete.
3. Phase 7 software acceptance requires the dedicated package workflow to
   pass on Intel x86_64 and Apple Silicon arm64: package build/tests, install,
   relocation, component-aware `find_package`, clean external consumers,
   non-Qt dependency isolation and installed runtime-loading checks.
4. Phase 7 hardware acceptance remains separate and pending: use installed
   artifacts for non-Qt AmScope enumerate/open/read-setting/start/frame/
   stop/close; verify Qt preview and descriptor-driven controls against the
   physical AmScope camera; and test the installed GenTL consumer with a
   physical CTI camera. Document vendor SDK redistribution requirements
   before publication.
5. Keep Phase 4 visual/workspace acceptance pending and the physical MU503 UI
   test postponed. Do not mark Phase 4 accepted.
6. Phase 8 covers exact-commit qualification across the declared provider/
   model matrix, long-duration and recovery evidence, deployment validation,
   vendor SDK redistribution review and release acceptance.

## 9. Continuation rules

- Continue Camera source work on `camera-v.2.0`; do not develop the library on the Optika application branch.
- Use Optika as an application integration/reference line only. Its camera preview already exists; do not restart preview development or merge its GUI/workspace changes into Camera.
- Keep Controller/Stage, Slide Scanning and ImageProcessing as separate workstreams.
- Keep Qt optional to the native/core targets, but preserve original Phase 3/4 deliverables in Camera/Qt, Camera/QtWidgets and the AmScope preview composition. Do not add Qt dependencies to Camera/Common or Amscope::Driver.
- Verify branch HEAD and commits at each continuation.
- Update this ledger after each meaningful increment with exact source commit, changed files, tests actually run, physical hardware evidence and the next bounded task.
- Never mark phases complete from source presence or CI alone.

## 10. References

- [Governing original Camera Module Phase 0–8 roadmap](AMSCOPE_NATIVE_DRIVER_COMPLETION_PLAN.md)
- [AmScope implementation inventory](AMSCOPE_IMPLEMENTATION_INVENTORY.md)
- [Common/provider-specific type ownership](CAMERA_COMMON_TYPE_OWNERSHIP.md)
- [Private SDK test-seam design](AMSCOPE_NATIVE_DRIVER_TEST_SEAM.md)
- [GenTL/GenApi assessment](AMSCOPE_GENTL_GENAPI_CPP_INTEGRATION_ASSESSMENT.md)
- [Native Optika application camera integration](AMSCOPE_NATIVE_MACOS_APP.md)
- [Driver README](../../Camera/Amscope/README.md)
