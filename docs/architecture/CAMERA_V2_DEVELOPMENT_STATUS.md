# Camera Module v2.0 — Development Status

**Repository:** `Shennagyp/MICROSCOPE_CPP`  
**Development branch:** `camera-v.2.0`  
**Latest reviewed source checkpoint:** `af19d7e08b24469d7df101baef1d4632019d7937`  
**Latest non-Qt CI pass:** `5cf5bc352e2df926a2cc0317acaccfd3a912602d`  
**Current code validation:** expanded run #45653938133 pending against `af19d7e08b24469d7df101baef1d4632019d7937`  
**Library source branch:** `OPTIKA-B293-DUALSLIDE-APP-v.0.9`  
**Review date:** 25 November 2026

## 1. Branch provenance

The requested name `amscope-b293-dualslide-v.0.9` is not a repository branch. The matching source branch is `OPTIKA-B293-DUALSLIDE-APP-v.0.9`, at `c7bb4b4dc41441989d71ee800c6d33b51dc54abe`.

The existing `camera-v.2.0` branch began at that exact commit and had one additional commit, `591bbeef` (enable the AmScope workflow). A tree comparison found **307 files under `Camera/` with identical paths and identical blob SHAs** across the source and target branches. There was no missing Camera library code to copy. Camera v2.0 is therefore continued on the existing `camera-v.2.0` branch, rather than duplicating the source tree.

The separate `OPTIKA-B293-DUALSLIDE-APP` branch is the application integration line. Its SwiftUI/AppKit GUI, workspace, slides, controller and stage remain outside the Camera library.

## 2. Product boundary

Camera v2.0 is a reusable **non-Qt programmatic camera library**.

It owns provider discovery/identity, native session lifecycle, backend-neutral camera contracts, programmatic settings/commands/telemetry, acquisition, frame delivery, and provider-specific SDK integration.

It does not own GUI frameworks, widgets, windows, application presentation, microscope optical configuration, controller/stage semantics, slide scanning, image processing or GPU rendering.

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

`Camera::Common` provides backend-neutral contracts and a non-Qt latest-frame mailbox. The mailbox owns no camera, session, worker, event loop or UI notification policy. Application adapters own notification scheduling and presentation.

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

### Legacy Qt source

`Camera/Qt`, `Camera/QtWidgets`, `QtAmcamCamera`, the Qt preview and its Qt-based hardware-rate presentation remain in the source tree as legacy/optional code. They are not part of Camera v2.0's public deliverable or acceptance contract and must not be required by the default build, package or external consumer. This status does not claim those files have already been deleted. Qt examples shipped inside the vendor SDK are vendor material, not Camera dependencies.

## 4. Revised roadmap and phase ledger

The original *AmScope Native Driver and Camera Module Completion Plan* on `amscope-native-driver-v.0.2` remains unchanged as the historical Phase 0–8 baseline. The former Qt-preview release plan is superseded for v2.0.

| v2.0 phase | Deliverable | Current assessment |
|---:|---|---|
| 0 | Freeze and verify baseline | **Accepted by owner historically.** MU503 physical observations exist; follow-up extended/exact-commit evidence does not certify all models. |
| 1 | Stabilize native AmScope driver | **Accepted by owner historically; hardening open.** Session/queue/worker/cache-flush/frame-lease primitives exist. Full SDK lifecycle injection and callback-quiescence proof remain open. |
| 2 | Finalize common contracts | **Partially implemented.** Discovery/model and frame/layout slices exist with tests and historical cross-architecture CI. Physical common-frame/RAW qualification is incomplete; Phase 2.3 error/value classification remains. |
| 3 | Non-Qt API and frame delivery | **In progress.** Non-Qt `AmcamCamera` API and common mailbox exist. Default CMake options are OFF for Qt; the install filter excludes `QtAmcamCamera.h`; CI run [#45651928831](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45651928831) passed on arm64/x86_64 with 11/11 non-Qt tests. The newer default-config and install-header workflow is pending validation. Public target/dependency audit and external-consumer validation remain. |
| 4 | GenTL/GenApi integration | **Blocked.** Existing Swift Consumer has no established C++/C ABI in reviewed source; target CTI Producer and physical CTI camera are required. No reimplementation or synthetic AmScope Producer. |
| 5 | Capability/feature/command/model coverage | **Open.** Broad implementation exists; reconcile each claim to provider/model support, semantics, readback and hardware evidence. |
| 6 | Packaging/external consumers | **Open.** Initial install rules exist; exported CMake package and clean installed non-Qt consumer qualification remain. |
| 7 | Hardware qualification/release | **Not started/accepted.** Requires declared provider/model matrix, installed-consumer tests, release CI and exact-commit records. |

Original-to-current crosswalk: Original 0→0; 1→1; 2→2; 3 (driver/control/Qt streaming)→3 (non-Qt API/frame delivery); original 4 (reusable Qt UI) is removed; original 5 (GenTL)→4; original 6 (coverage)→5; original 7 (packaging)→6; original 8 (release)→7.

## 5. Changes made on camera-v.2.0

| Commit | Change | Effect |
|---|---|---|
| `70a7486` | Make Qt integrations opt-in legacy targets | Qt adapter and preview options default OFF. |
| `5cf5bc3` | First non-Qt macOS CI | No Qt install or preview build; driver/probes built and 11/11 non-Qt CTest tests passed on arm64 and x86_64. |
| `69dbc65` | Exclude Qt facade from non-Qt install | Prevented installing the Qt facade header in the default install; superseded by the physical header relocation below. |
| `3e2b328` | Install-surface CI check | Added required-header and no-Qt-header installation assertions. |
| `a6f3409` | Exercise non-Qt CMake defaults | CI now relies on the OFF-by-default Qt options rather than explicitly setting them. |
| `ac90288` | Define Camera v2.0 development status | Added the governing v2.0 phase ledger and continuation plan. |
| `1ac6404`, `e5078d3`, `79d43f0` | Align driver README and ownership description | Document non-Qt build/API and identify legacy preview components. |
| `fbc1053` | Archive old continuation handoff | v0.7 Qt-preview handoff is clearly marked historical. |
| `d250add`, `e187cfd` | Archive stale inventory and release plan | Historical Qt/UI documents no longer appear as current project governance. |
| `85f0a1d`, `affcccf` | Add public non-Qt API contract test | Compile/link/runtime check for API type invariants, initial Closed state, zero counters and idempotent close without hardware enumeration. |
| `287c0e9`, `62115c4`, `8c36506` | Isolate legacy Qt facade | Move Qt header out of Amscope::Driver include root; expose/install it only through the optional Qt target. |
| `af19d7e` | Compile installed non-Qt public headers in CI | After installing to a clean prefix, syntax-compile the public API test source using installed headers. |

Latest implementation/configuration checkpoint: `af19d7e08b24469d7df101baef1d4632019d7937`. The non-Qt workflow run [#45653938133](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45653938133) is pending at this checkpoint. Its intended assertions include the expanded CTest suite, architecture checks, installed public-header presence/absence, and syntax compilation of the installed non-Qt public API. Do not report these latest checks as passing until this exact run completes.

The earlier validated run [#45651928831](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45651928831) passed on Apple Silicon arm64 and Intel x86_64 at `5cf5bc352e2df926a2cc0317acaccfd3a912602d`, with 11/11 non-Qt CTest tests. It does not include the later public API test or installed-header syntax check.

No local build, test or physical-camera run is claimed in this status update.

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
