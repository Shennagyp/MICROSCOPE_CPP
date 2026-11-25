# Camera Module v2.0 — Development Status

**Repository:** `Shennagyp/MICROSCOPE_CPP`  
**Development branch:** `camera-v.2.0`  
**Current branch HEAD:** `11ff79ac115e5f4ecbcbe95eb70f4391c48a21d8`  
**Latest non-Qt implementation/CI checkpoint:** `11ff79ac115e5f4ecbcbe95eb70f4391c48a21d8`  
**CI:** passed on Intel x86_64 and Apple Silicon arm64, run #45654331539  
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

### Optional Qt streaming and reusable UI layers

- `Camera/Qt` provides Qt-specific frame notification/thread-handoff support without changing the native acquisition owner.
- `QtAmcamCamera` currently composes one `AmcamCamera` and adds a serialized Qt request worker, queued completions and frame notifications. This is existing implementation, but Phase 3 still needs to reconcile its combined responsibilities against the separate non-Qt control and Qt streaming boundaries.
- `Camera/QtWidgets` contains reusable `FrameView`, `CameraControlPanel` and `ImageInfoBar` components. These are partial Phase 4 deliverables, not a complete reusable camera UI library.
- `amscope_preview` is a composition/example application. It is not the owner of the driver or the reusable UI library's application-independent contracts.

Qt is optional for consumers that only need the core driver/common contracts. The Qt targets remain in scope as optional Camera Module deliverables and must retain their own build/test coverage. The vendor SDK's original Qt samples are vendor material, not Camera Module targets.

## 4. Governing roadmap and original Phase 0–8 ledger

The original *AmScope Native Driver and Camera Module Completion Plan* on `amscope-native-driver-v.0.2` is the **governing Camera v2.0 roadmap**. Its phase identities and dependency order are preserved. The later `AMSCOPE_NATIVE_RELEASE_PLAN.md` is a narrower AmScope-native execution plan and is subordinate; it does not replace the original phase numbering or remove the reusable Qt streaming/UI deliverables.

| Phase | Original deliverable | Current assessment |
|---:|---|---|
| 0 | Freeze and verify the baseline | **Accepted by owner historically.** MU503 physical observations exist; extended/exact-commit regression evidence remains follow-up, and one-camera observations do not certify all models. |
| 1 | Stabilize and qualify native AmScope driver | **Accepted by owner historically; post-acceptance hardening remains.** Single-session ownership, callback/queue/worker/pull path, frame pool and lifecycle primitives exist. Full injected SDK lifecycle tests and proof of callback quiescence around terminal Close remain open. |
| 2 | Finalize backend-neutral camera models | **Partially implemented.** Slice 2.1 discovery/capability models and 2.2 frame/layout models exist with automated tests and prior cross-architecture CI. The 2.3 semantic ownership audit is documented: common feature-access errors remain common, while AmScope lifecycle/command errors remain provider-specific. Physical common-frame/RAW validation remains open, so Phase 2 is not accepted. |
| 3 | Separate driver, programmatic control and Qt streaming libraries | **In progress.** `AmcamCamera` exposes a non-Qt API; `Camera::Common` has a non-Qt mailbox; `Camera::Qt` and `QtAmcamCamera` provide the existing optional Qt handoff/request path. The public header was isolated under the optional Qt target. The current facade still combines queued control/lifecycle requests and frame notification, so the target/responsibility separation and physical same-session regression are not yet complete. |
| 4 | Extract reusable Qt camera UI library | **Partially implemented.** `Camera/QtWidgets` contains `FrameView`, `CameraControlPanel` and `ImageInfoBar`; the Qt preview composes them. Descriptor-driven editors, commands, telemetry, capability/availability presentation, full consumer independence and phase-wide physical UI regression are not yet accepted. |
| 5 | Integrate existing GenTL/GenApi implementation | **Blocked.** The existing Swift GenTL Consumer has no established C++/C ABI in the reviewed source; a real target CTI Producer and physical CTI camera are required. No reimplementation or synthetic AmScope Producer. |
| 6 | Complete capability, feature, command and model coverage | **Open.** Broad AmScope implementation exists. Reconcile every exposed claim to provider/model support, units, preconditions, readback, asynchronous completion and physical evidence. |
| 7 | Release packaging and external-consumer integration | **Open.** Initial install rules exist and the non-Qt public headers install/compile. An exported CMake package/config/version flow and clean installed external consumers remain; both non-Qt and optional Qt consumer configurations must be considered. |
| 8 | Final hardware qualification and release | **Not started/accepted.** Requires exact-commit hardware records, declared provider/model matrix, installed-consumer tests, release CI and owner acceptance. |

The original phase dependency order remains: 0 → 1 → 2 → 3 → 4; Phase 5 (GenTL/GenApi) can have dependency research in parallel, but adapter acceptance needs the common model, an existing Consumer, a real CTI Producer and physical CTI hardware; Phase 6 follows Phases 2–5; Phase 7 follows Phases 3–6; Phase 8 requires all preceding phases.

## 5. Changes made on camera-v.2.0

| Commit | Change | Effect |
|---|---|---|
| `70a7486` | Make Qt integrations opt-in legacy targets | Qt adapter and preview options default OFF; consumers of only the core need no Qt. This does not remove Qt Phase 3/4 deliverables. |
| `5cf5bc3` | First non-Qt macOS CI | Driver/probes built; 11/11 non-Qt tests passed on arm64 and x86_64. |
| `69dbc65`, `8c36506` | Isolate Qt facade header | Move the QObject facade header out of the core driver include root; the optional Qt target owns the extra include path and install. |
| `3e2b328`, `af19d7e` | Verify non-Qt install surface | CI checks common/native headers, absence of the Qt facade from the non-Qt install, and syntax-compiles a consumer source against installed headers. |
| `a6f3409` | Exercise non-Qt CMake defaults | The core-only CI path relies on default OFF Qt options. |
| `ac90288` | Add Camera v2.0 status ledger | Establish branch provenance, architecture, phase status and continuation risks. Its initial phase/UI crosswalk was incorrect and is corrected in this revision. |
| `1ac6404`, `e5078d3`, `79d43f0`, `5ccd990` | Align provider README | Explain non-Qt core and optional Qt streaming/reusable UI obligations. |
| `fbc1053` | Archive v0.7 continuation snapshot | Preserve prior UI evidence as history; current roadmap remains the original Phase 0–8 plan. |
| `d250add`, `e187cfd` | Mark prior inventory/release plan snapshots | Their prior execution/status statements are historical/subordinate, not replacements for the governing roadmap. |
| `85f0a1d`, `affcccf` | Add non-Qt public API contract test | Checks API type/ownership invariants, initial Closed state, zero counters and idempotent close without enumerating hardware. |
| `61e1523` | Restore Qt streaming/UI CI coverage | Adds a separate Qt build/test job for optional streaming, reusable widgets and preview composition on Intel and Apple Silicon. |

### CI evidence

Non-Qt workflow [#45654331539](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45654331539) passed at exact code checkpoint `11ff79ac115e5f4ecbcbe95eb70f4391c48a21d8` on Apple Silicon arm64 and Intel x86_64. Each job passed **12/12 non-Qt CTest tests**, installed the non-Qt public headers, verified that `QtAmcamCamera.h` was absent from the non-Qt install, syntax-compiled the public API test against installed headers, and verified driver/vendor/diagnostic architecture slices.

The workflow now also contains a separate Qt job for the original Phase 3/4 optional targets. That new job has not yet been validated. No Qt test pass or physical-camera run is claimed until its exact run completes. The existing non-Qt pass does not prove the Qt targets compile.

No local build or physical-camera run is claimed in this status update. CI evidence remains separate from physical-camera evidence and phase acceptance.

## 6. Highest-priority unresolved risks

### Callback quiescence

The current callback-entry counter can drain callbacks already entered; it does not independently prove that a new callback cannot begin after `Amcam_Close` returns. Do not release callback-reachable resources based only on a zero counter. Prove both in-flight drain and no-new-callback after terminal close, using authoritative vendor evidence or a defensible synchronization design.

The private SDK dispatch-seam design can deterministically exercise injected failures and races, but a fake cannot prove undocumented behavior of the real SDK.

### Native lifecycle failure injection

The intended seam remains private to implementation/test support. It must preserve public `AmcamCamera` ABI/vtable, exact SDK function pointer types, one SDK-call serialization boundary, one session owner and the callback → bounded queue → one pull worker model. Cover Open, Start setup/callback registration, worker creation, recoverable Stop, successful Stop, flush failure/retry, fatal acquisition, terminal Close and still-capture quarantine. Do not add a public generic SDK abstraction.

### Hardware evidence

MU503 RGB24 and short-rate observations are narrow evidence. They do not prove production RAW layout, all features/accessories, other models, long-duration stability, reconnect recovery or CTI operation. Keep source presence, automated tests, physical tests and owner acceptance as separate evidence categories.

## 7. Next development sequence

1. Validate the new non-Qt CI workflow on the exact current branch head.
2. Audit public headers and CMake target dependencies to ensure `Amscope::Driver` and `Camera::Common` have no hidden Qt or application dependencies.
3. Complete Phase 2.3 common error/value classification only for semantics actually shared by providers.
4. Implement the private native SDK dispatch seam, retaining ABI, session, lock and worker ownership.
5. Add deterministic lifecycle fault tests and investigate the actual SDK no-new-callback guarantee.
6. Reconcile the AmScope inventory against implementation, model-specific SDK evidence and physical tests.
7. Continue GenTL only after choosing the existing Consumer interface and obtaining a real CTI Producer/camera.
8. Export the non-Qt CMake package and test it from a clean external consumer.
9. Complete physical qualification for every declared provider/model combination.

## 8. Continuation rules

- Continue Camera source work on `camera-v.2.0`; do not develop the library on the Optika application branch.
- Use Optika as an application integration/reference line only. Do not merge its GUI/workspace changes into Camera.
- Keep Controller/Stage, Slide Scanning and ImageProcessing as separate workstreams.
- No Qt dependency or reusable camera UI is to be added to Camera v2.0.
- Verify branch HEAD and commits at each continuation.
- Update this ledger after each meaningful increment with exact source commit, changed files, tests actually run, physical hardware evidence and the next bounded task.
- Never mark phases complete from source presence or CI alone.

## 9. References

- [Original Phase 0–8 roadmap](AMSCOPE_NATIVE_DRIVER_COMPLETION_PLAN.md)
- [AmScope implementation inventory](AMSCOPE_IMPLEMENTATION_INVENTORY.md)
- [Common/provider-specific type ownership](CAMERA_COMMON_TYPE_OWNERSHIP.md)
- [Private SDK test-seam design](AMSCOPE_NATIVE_DRIVER_TEST_SEAM.md)
- [GenTL/GenApi assessment](AMSCOPE_GENTL_GENAPI_CPP_INTEGRATION_ASSESSMENT.md)
- [Native Optika application camera integration](AMSCOPE_NATIVE_MACOS_APP.md)
- [Driver README](../../Camera/Amscope/README.md)
