# AmScope Implementation Inventory

**Source snapshot:** `afe2c31a189c68e8502220287c08ad3b42652cc2` (v0.6 common integer-range typing, exact validation, first-frame common metadata reporting, and ownership audit)  
**Branch head reviewed for this inventory refresh:** `6e4f6bb584719aec6323936350a65e0112dc0066` (project-owner supplied Release build, 11/11 CTest pass, and MU503 RGB24 common-frame hardware record; CI for this exact revision not yet reported)  
**Purpose:** source-to-test-to-UI inventory. This document records source claims, automated coverage, and separately classified user-reported physical-camera observations. Owner acceptance of Phases 0 and 1 is recorded in the completion plan; acceptance does not retroactively promote unrecorded hardware checks to Hardware verified.

---

## 1. Evidence classification

Use the following labels consistently:

- **Source implemented:** implementation is present in the inspected C++ source.
- **Automated test present:** a test target exists for at least part of the behavior. Read the test before assuming full coverage.
- **CI passed:** the current CI revision compiled/ran the relevant test.
- **Hardware verified:** a completed physical-camera record exists for the exact relevant source revision and camera model.
- **Not present / deferred:** current source/README explicitly says the feature is not delivered.
- **Unknown:** not established by the inspected source or evidence.

No feature in this inventory is labeled **Hardware verified** because there is no completed, exact-commit hardware record under `docs/testing/hardware/`. The latest CI run found zero cameras and is not hardware evidence. A later user-reported targeted resolution smoke check is recorded separately in Section 10.1 and must not be promoted to full hardware qualification.

## 2. Source layout and responsibilities

| Area | Main files | Current responsibility |
|---|---|---|
| Native AmScope API | `AmcamCamera.h`, `AmcamCamera.cpp` | Owns native SDK session, handle, lifecycle, feature access, commands, telemetry, still capture, and native acquisition |
| Common camera models | `Camera/Common/include/camera/CameraModels.h`, `CameraFrames.h` | Backend-neutral discovery identity, image resolution, tri-state model capabilities, and frame/image metadata |
| AmScope discovery extension | `AmcamCamera.h`, `CameraTypes.h` | Adds native model code/flags, speed/fan limits, and I/O line count without polluting common models |
| Event queue | `AmcamEventQueue.h` | Bounded callback-to-worker event transfer |
| Session primitives | `AmcamSessionPrimitives.h` | Production callback lease tracker, generation-based frame-cache flush obligation, and retained frame-slot selection |
| Watchdog | `detail/FrameStallWatchdog.h` | Video frame-stall and trigger heartbeat timing logic |
| Frame gate | `detail/AcquisitionFrameGate.h` | Session/generation-aware publication gate |
| Model policy | `AmcamCameraProfiles.h/.cpp` | Exact model allow/deny feature policy |
| AmScope types | `CameraTypes.h` | AmScope CameraInfo extension, camera error/state, `AmcamFrame` native metadata extension, SDK enums, trigger/I/O/accessory models; common Frame/FramePtr and ImageResolution are aliases to Camera.Common |
| Common feature contract | `Camera/Common/include/camera/CameraFeatureAccess.h` | Feature kinds, typed values, descriptors, typed errors/results, feature interface; integer and floating-point ranges use distinct types |
| Common validation | `CameraFeatureValidation.h` | Shared type, exact int64 range/increment validation, floating range validation, enum, vector, availability and writability validation |
| Common FPS estimator | `FrameRateEstimator.h` | Host frame-rate estimation |
| Qt facade | `QtAmcamCamera.h/.cpp` | Qt QObject facade, lifecycle/request worker, queued feature/control operations, frame mailbox |
| Preview | `app/main.cpp` | Current single-window Qt Widgets application |\n| Physical-camera rate test | `app/hardware_rate_test.cpp` | Dedicated Qt preview/test executable measuring event, pull, callback-delivery and paint rates on a connected camera |
| Build/CI | `CMakeLists.txt`, `.github/workflows/amscope-macos.yml` | macOS driver/Qt/preview build, CTest, architecture and launcher checks |

## 3. Session lifecycle and live acquisition

| Behavior | Source state | Automated test evidence | Real-camera evidence |
|---|---|---|---|
| Device enumeration and opaque ID open | Source implemented | No driver-level hardware enumeration test in the previous CTest suite | Not recorded |
| Single `HAmcam` per opened session | Source/documented invariant | No direct one-handle hardware test identified | Not recorded |
| Open/Close/Start/Stop | Source implemented | No dedicated native session lifecycle integration test target | Not recorded |
| SDK callback -> bounded FIFO -> one pull worker -> `Amcam_PullImageV4` | Source implemented | Event queue unit test covers queue behavior, not the complete physical path | Not recorded |
| `E_PENDING` transient handling | Source implemented/documented | Full SDK interaction not established by unit tests | Not recorded |
| Fatal disconnect/error/no-frame/no-packet/queue overflow handling | Source implemented/documented | Queue and watchdog helpers tested; full SDK failure path not fully covered | Not recorded |
| Exposure-aware video watchdog | Source implemented | Frame-stall watchdog unit test | Not recorded |
| Trigger heartbeat watchdog | Source implemented | Frame-stall watchdog unit test covers timing helpers | Not recorded |
| Stop success callback drain and worker join | Source implemented/documented | No dedicated native SDK callback-drain integration test identified | Not recorded |
| Recoverable Stop failure while Streaming | Source implemented/documented | Frame gate tests cover reactivation helper semantics, not injected SDK Stop behavior | Not recorded |
| Cache flush obligation and retry | Source implemented/documented | No explicit cache-flush fault/retry test target identified | Not recorded |
| Terminal Close independent of Stop/flush success | Source implemented/documented | No dedicated native failure-injection test identified | Not recorded |
| Old-session frame rejection and atomic latest-frame mailbox | Source implemented | Frame gate test and Qt request tests cover portions; concurrent mailbox behavior has no dedicated stress test | Not recorded |
| Frame notifications bypass feature-request queue lock | Source implemented | No dedicated contention test | Not recorded |
| SDK image-event, successful-pull, and actual paint counters | Source implemented | Counter wiring is compile/CI pending; no unit-level hardware-rate test | Not recorded |
| Maximum supported frame rate selected on Start by model flags | Source implemented | No deterministic SDK-setting test yet | Not recorded |
| Generic SDK FPS cap removed (`AMCAM_OPTION_FRAMERATE=0`) | Source implemented | No deterministic SDK-setting test yet | Not recorded |\n| Physical-camera rate measurement | Test executable implemented; manual run required | `amscope_hardware_rate_test` reports event/pull/callback rates, preview poll/submission/paint counters, dimensions, SDK metadata, exposure-auto state/readbacks, and drops | Two MU503 runs report about 91.2 event/pull/callback FPS at 640×480 with zero drops; second run reports 14.14 painted FPS and a pre-Start 70 ms exposure readback. Active exposure and GUI-rate causes remain under diagnosis |
| Frame-pool exhaustion and dropped-frame accounting | Source implemented/documented | No dedicated frame-pool behavior test identified | Not recorded |

## 4. Frame and image payload behavior

| Behavior | Source state | Automated test evidence | Real-camera evidence |
|---|---|---|---|
| Packed RGB24 live frames | Source implemented/documented | Common frame model tests; v0.6 native probe reports first common frame | **Hardware verified on MU503:** 1280×960, RGB24, stride 3,840, payload 3,686,400 bytes; exact geometry match. See `docs/testing/hardware/phase-2-mu503-rgb24-2026-11-23.md` |
| RAW8 and RAW16 live frame representation | Source implemented/documented | Camera type tests verify representation fields, not sensor payload bytes | Project owner bypassed production RAW common-frame hardware qualification in Phase 2; direct SDK-only probe is not adapter evidence |
| Sensor FourCC and significant depth separate from storage container width | Source implemented | Camera type tests cover representation distinction | Not recorded |
| Optional sequence/timestamp/exposure metadata | Source implemented; populated only when the corresponding Amcam `AMCAM_FRAMEINFO_FLAG_*` validity bit is set | Common frame tests cover optional-vs-zero semantics and timebases | MU503 RGB24 sample: all three reported unavailable; physical presence/zero-value behavior in other modes not established |
| Amcam SDK flags, exposure gain percent, and black level | Source implemented in `amscope::AmcamFrame` extension, not in common frame | AmScope type test checks extension and retained values | Not recorded |
| Opaque pixel-format identity | Source implemented; Amcam RAW uses `amcam.fourcc`, with no automatic PFNC translation | Common and AmScope type tests cover scheme/value retention | RGB24 sample correctly has no invented native identifier; physical RAW FourCC verification remains open if RAW is supported/claimed |
| Significant source depth vs payload storage width | Source implemented as independent `significantBitsPerPixel` and `storageBitsPerPixel` | Common and AmScope type tests cover independent values | Not recorded |
| Four immutable `FramePtr` leases | Source implemented/documented | No dedicated pool/retention stress test identified | Not recorded |
| Private discard buffer when every lease is retained | Source implemented/documented | No direct pool exhaustion test identified | Not recorded |
| Direct `QImage` wrapper over retained `FramePtr` and `QPainter` paint (no per-frame pixel copy or `QPixmap`) | Source implemented | No dedicated FrameView rendering test; branch-head CI passed on Intel x86_64 and Apple Silicon arm64 | Not recorded |
| RAW sensor display as RGB | Explicitly refused by current UI | Not applicable | Not recorded |
| RAW/Bayer demosaic for visible preview | Not present/deferred | No test target | Not recorded |
| GPU rendering | Not present/deferred | No test target | Not recorded |

## 5. Common feature interface and settings

The common contract currently supports Boolean, integer, integer vector, floating-point, string, and enumeration values. Enumeration symbols are distinct from ordinary strings. Descriptors carry availability, readability, writability, unit, scalar range/increment, vector cardinality, enumeration symbols, and group. Integer limits use `CameraIntegerRange` with exact `int64_t` bounds/increments; floating-point limits use `CameraFloatingPointRange`. The validator rejects a range alternative that does not match the feature kind, checks integer increments without signed overflow, and covers values above `2^53`. Errors distinguish unsupported, not found, unavailable, not readable, not writable, type mismatch, invalid value, and backend failure.

The Amcam implementation caches its full descriptor inventory per camera state; individual `readFeature()` operations reuse that inventory instead of repeating the complete set of SDK probes under the SDK mutex. Successful feature writes invalidate the cache. The preview stops automatic descriptor refresh when Streaming so the full probe set does not contend with `Amcam_PullImageV4`.

| Feature family | Current source coverage | Key semantics/constraints | Test/UI coverage | Hardware evidence |
|---|---|---|---|---|
| Exposure auto | `ExposureAuto` | Off/Once/Continuous maps native values 0/2/1 as declared; Once is native one-shot, not emulated | Feature validator and type tests; descriptor-driven UI | Not recorded |
| Integer numeric range semantics | `CameraIntegerRange` | Exact int64 bounds and increments; no conversion through double | Common feature tests include adjacent values above `2^53`, mismatched range kind, invalid increments, and full int64-domain arithmetic | Software verification pending on v0.6 |
| Exposure time | `ExposureTime` | Integer microseconds; writable only when current exposure mode permits | Feature validator; descriptor UI | Not recorded |
| Analog gain | `AmcamAnalogGainPercent` | Percentage, not GenICam dB | Descriptor UI | Not recorded |
| Color matrix | `AmcamColorMatrixEnabled` | SDK/model-probed setting | Descriptor UI | Not recorded |
| Pixel format | `AmcamPixelFormat` | Native SDK format codes; not automatically PFNC | Descriptor UI | Not recorded |
| White balance temperature/tint | `AmcamWhiteBalanceTemperature`, `AmcamWhiteBalanceTint` | Temperature/tint are coupled where SDK setter takes pair; preserve unchanged component | Descriptor UI | Not recorded |
| RGB white-balance gain | `AmcamWhiteBalanceRGBGain` | Three-element integer vector | Common vector validation; descriptor UI | Not recorded |
| Fixed FPS | `AmcamFixedFrameRate` | Configured fixed FPS in 0.1 FPS increments when model/SDK proves support | Common numeric validation; UI | Not recorded |
| Frame speed | `AmcamFrameSpeedLevel` | Discrete speed level, not FPS | Descriptor UI | Not recorded |
| Resolution index | `AmcamResolutionIndex` | Enumerated index/width/height; stopped-state write | Descriptor UI | Not recorded |
| RAW output | `AmcamRawOutputEnabled`, common `RawOutputBitDepth` | Stopped-state-only; choices are 8 bits and the SDK-reported maximum significant depth; Amcam maps them to `AMCAM_OPTION_BITDEPTH` 0/1; accepted setter value may be retained when SDK getter is unavailable | Generic descriptor UI and common value validation; physical-camera verification of actual significant depth and unchanged payload remains outstanding | Not recorded |
| ISP/image processing | Linear tone mapping, curve, video/still demosaic, denoise, flat/dark-field, defect-pixel correction, sharpening, orientation, test pattern, auto-exposure threshold | Runtime/model probed; stream-affecting writes stopped-state-only | Some generic descriptor editor coverage; feature-by-feature automated tests not established | Not recorded |
| Sensor modes | Black level, conversion gain, low-noise, high-full-well | Model/SDK capability-gated | Generic descriptor UI | Not recorded |
| Thermal | Sensor temperature, TEC enable/target/voltage, fan, anti-fog heater | Temperature/voltage are telemetry; conservative stopped-state setting policy; ranges from SDK/model data | Generic descriptor UI; no physical behavior test | Not recorded |
| Hardware ROI | Typed `[x,y,width,height]` integer vector | Even coordinates and preview-mode bounds validated; stopped-state-only | Common vector validation; descriptor UI | Not recorded |
| Binning/skipping | Enumeration from SDK method/value pairs | Stopped-state-only; next Start obtains final frame dimensions | Descriptor UI | Not recorded |
| Model capability facts | Sensor class, monochrome/global shutter, buffers, trigger, focus, ST4, wheel, lights, transport | Facts are read-only evidence; not proof that corresponding commands are implemented | Profile registry tests cover allow/deny policy | Not recorded |

## 6. Commands and telemetry

| Operation | Category | Current source state | Notes / limitations | Hardware evidence |
|---|---|---|---|---|
| Auto white balance once | Command | Implemented | Requires appropriate acquisition state; not a Boolean feature | Not recorded |
| Device frame-rate statistics | Telemetry | Implemented | Measured SDK FPS must remain distinct from configured fixed FPS and host preview FPS | Not recorded |
| Still resolution table | Query | Implemented | Uses SDK still table | Not recorded |
| Processed still capture | Command | Implemented | `Amcam_SnapN`, dedicated still event waiter, `PullImageV4(bStill=1)` | Not recorded |
| RAW still capture | Command | Implemented | `Amcam_SnapR`; significant depth separate from container width | Not recorded |
| Trigger configuration | Command/configuration | Implemented | Video, software/simulated, external, mixed external+software; stopped-state configuration | Not recorded |
| Software trigger | Command | Implemented for finite bursts | Zero and continuous sentinel intentionally excluded; continuous trigger and cancellation not exposed | Not recorded |
| Trigger heartbeat | Health telemetry/event | Implemented | Triggered idle is healthy while heartbeat continues even if no trigger frames arrive | Not recorded |
| Hardware I/O line read | Query | Implemented | Model-reported opto/GPIO lines; stopped-state requirements apply | Not recorded |
| Hardware I/O configuration | Command/configuration | Implemented | Direction, signal format, inversion, input activation/debounce, output mode, strobe timing; transactional readback/rollback | Not recorded |
| Embedded light source | Query/command | Implemented | Level range validation and readback | Not recorded |
| Filter wheel | Query/command | Implemented | Motion submission is asynchronous; poll state for confirmed position | Not recorded |
| ST4 guide | Command/query | Implemented | Directional pulse duration in milliseconds; Stop uses duration zero; preserve S_OK/S_FALSE state meaning | Not recorded |
| Astronomy auto-focuser | Query/command/telemetry | Implemented | Separate from lens focus; exposes position/range/step/motion and optional temperatures; Move/Halt | Not recorded |
| Lens focus | Query/command | Implemented | Manual focus requires physical MF mode and reported near/far limits | Not recorded |
| Lens aperture | Command | Implemented | `posAM` index maps to `arrayFN`; only after calibration; device completion asynchronous | Not recorded |
| Lens autofocus mode switching | Not exposed | Deferred | Required working-distance parameters not documented | Not applicable |

## 7. Qt behavior and UI state

| Behavior | Source state | Automated evidence | Remaining work |
|---|---|---|---|
| One bounded FIFO Qt camera-operation worker (256 pending requests) | Implemented in `Amscope::Qt` | Qt request tests cover asynchronous closed-session errors, FIFO writes, callback thread, stale descriptor suppression, and synchronous/queued mode conflict | Keep the FIFO here; `Camera::Qt` has no camera-operation queue |
| Scheduled/active session generations | Implemented | Partially covered by Qt request tests | Expand for lifecycle interleavings and real device |
| Feature-state generation | Implemented | Partially covered | Test feature descriptor invalidation after real dependent writes |
| UI callback delivered on QObject affinity thread | Implemented | Qt request tests | Hardware regression |
| Synchronous vs queued mode commitment | Implemented | Qt request tests verify a queued instance rejects synchronous feature access and a synchronous instance rejects queued requests | No additional Phase 3 work identified for mode exclusion |
| Latest-frame mailbox / Qt notification | Header-only `camera::qt::FrameStreamMailbox` from `Camera::Qt`, embedded at original end-of-object position; original `QtAmcamCamera` signal/notification target retained | Dedicated `camera_qt_frame_stream_tests` covers latest lease, notification coalescing/re-arm, in-flight publication guard, clear, and Qt affinity | Physical high-rate frame delivery remains part of integrated hardware qualification |
| Descriptor-driven feature editor | Implemented | Preview compile; common validation tests | Extract reusable UI widgets |
| Trigger panel | Implemented | Preview compile | Hardware UI test |
| Lens focus/aperture panel | Implemented | Preview compile | Hardware/accessory test |
| Accessory panels | Partial | Preview source | Add missing native I/O UI if required |
| Separate `camera_amscope_control` | Intentionally not introduced | `AmcamCamera` already provides the non-Qt programmatic feature/command surface | A forwarding-only target would duplicate the existing API without establishing a new ownership boundary |
| Separate Qt streaming mechanism | Implemented in `Camera::Qt` as header-only `FrameStreamMailbox`, embedded by value in `QtAmcamCamera` | Dedicated unit test target; focused build/test pending | No runtime stream QObject, per-camera stream allocation, extra worker, or notification queue; signal remains on `QtAmcamCamera` |
| Reusable `camera_ui_qt` | Not delivered | None | Extract widgets from `PreviewWindow` |

## 8. Explicitly deferred or absent

The README identifies these as separate follow-up capabilities:

- Output-level GPIO writes.
- UART.
- Counter/PWM programming.
- Self-trigger parameterization.
- Synchronized multi-camera acquisition.
- GenTL routing.
- RAW demosaicing for visible preview.
- Concrete GPU renderer.
- Continuous trigger and cancellation exposure.
- Lens autofocus mode switching pending documented working-distance parameters.

Additional target-architecture gaps:

- Existing GenTL/GenApi C++ runtime dependency and CTI adapter are absent from this branch.
- Backend-neutral camera identity/capability/frame models are not fully extracted from `amscope`.
- `Amscope::Driver` remains a non-Qt target containing the native Amcam session and programmatic control/command API. `Camera::Qt` supplies the header-only backend-neutral mailbox; `Amscope::Qt` embeds it without a second runtime stream object. No forwarding-only control target is planned.
- Reusable backend-neutral UI library is not delivered.
- Exported CMake package and clean external-consumer qualification are missing.
- Vendor SDK redistribution terms have not been established in this inventory.

## 9. Automated tests currently present

| Test target | What it verifies | What it does not prove |
|---|---|---|
| `amscope_camera_types_tests` | Type and enum invariants, RAW representation, native auto-exposure symbols | Actual camera payload or settings |
| `amscope_acquisition_frame_gate_tests` | Frame gate reactivation/invalidation helper behavior | Native SDK callback drain or physical Stop behavior |
| `amscope_profile_tests` | Exact-model profile allow/deny, precedence, duplicate/missing profile rules | Actual model capability probes |
| `amscope_event_queue_tests` | Event queue helper behavior | Full callback and PullImage acquisition |
| `amscope_session_primitives_tests` | Production callback-drain tracker, retryable frame-cache flush obligation (including newer-generation preservation), and retained frame-slot lease selection | Full AmcamCamera Open/Start/Stop/Close fault injection, SDK callback ABI races, and hardware behavior |
| `amscope_frame_stall_watchdog_tests` | Video/heartbeat timeout calculations | Actual SDK timeout and heartbeat behavior |
| `amscope_qt_request_tests` | Qt worker request ordering, callback affinity, typed closed-session outcomes, request supersession, synchronous/queued mode exclusion | Real camera SDK operations or physical frame delivery |
| `camera_qt_frame_stream_tests` | Backend-neutral Qt frame coalescing, latest immutable lease identity, notification re-arm, in-flight publication guard, mailbox clear, QObject affinity | Real camera cadence or renderer/GPU behavior |
| `camera_common_feature_tests` | Feature value type/range/increment/enumeration/vector validation | Native feature readback |
| `camera_common_models_tests` | Optional camera identity fields, opaque-ID distinction, common resolution, and Unknown/Unsupported/Supported capability semantics | AmScope model-flag mapping on hardware or GenTL discovery |
| `camera_common_frame_tests` | Common frame payload/layout, opaque pixel-format scheme/value, optional sequence/timestamp/exposure, explicit timebases, and backend extension support | Real SDK payload layout/metadata accuracy and physical RAW verification |
| `camera_common_frame_rate_tests` | Host frame-rate estimator behavior | Camera frame cadence |

The previous eight-target CTest suite passed on both Intel x86_64 and Apple Silicon arm64 in workflow [#45059333867](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45059333867), associated with branch head `48be6c9b635fe9fa40760f66b2efec024bf4fec4`. The initial hardware-test executable was compiled and architecture/linkage-checked successfully on both architectures in workflow [#45062683602](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45062683602). That CI run predates the sequence-independent paint and exposure/speed diagnostic corrections. The corrected source is being checked in workflow [#45063651610](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45063651610), which is in progress at this inventory refresh. The new `amscope_session_primitives_tests` target was added after those earlier CI runs; CI for the current branch head is pending. The launcher found zero cameras in CI; physical-camera behavior remains a manual test.

The user subsequently completed a clean build at Phase 2.1 source revision `03f35bf`: all 10/10 CTest targets passed locally, including the dedicated Qt request test. GitHub Actions run [#45067873506](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45067873506) passed on Intel x86_64 and Apple Silicon arm64 at the same revision. This validates the discovery-model slice only.

Phase 2.2 adds `camera_common_frame_tests`, making the current suite 11 CTest targets. At branch head `9daf193c7a107ae93984f5bf00f56d0dfbd66b3b`, the user completed a clean Release build of the driver, Qt adapter, hardware-rate executable, preview application, and all test targets, followed by 11/11 passing CTest tests. GitHub Actions run [#45070941692](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45070941692) passed on Intel x86_64 and Apple Silicon arm64 for the same head. The fixed-8-bit RAW storage-mode correction is committed at `a16ae3bd77c56b14384736aa457e859c73a8072f`. A follow-up at `3ef46d3a5b85aa7ae2e22d160d92f61aecace5bb` introduced the initial common `RawStorageBitDepth` identifier and the Amcam 8/maximum-mode session fallback. That initial public name and its `8-bit`/`16-bit` labels were later corrected: the current `RawOutputBitDepth` feature exposes 8 bits and the SDK-reported maximum significant depth, while the 16-bit storage container remains a separate frame-layout property. The backend continues to accept a successful setter without mandatory readback and retains that accepted mode as a session fallback for both RAW live startup and still capture. If the SDK getter works, its value remains authoritative and mismatches are rejected. The user subsequently completed a clean Release build at branch head `7b81b5e639f8b8f4eae04f30c1145c975092073b`; all 11/11 CTest tests passed. Current-head GitHub Actions run [#45072725032](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45072725032) passed Apple Silicon arm64; Intel x86_64 was still in progress at this inventory refresh. The physical-camera regression, including processed still export plus RAW format/layout and RAW-sidecar metadata verification on a RAW-capable camera, remains outstanding.

The physical-camera performance executable is deliberately separate from CTest. It is built as `amscope_hardware_rate_test`, launched through `scripts/run-amscope.sh --hardware-rate-test`, and must be run manually with a physical camera. It selects and verifies the requested `AmcamResolutionIndex`, captures the original resolution, runs a visible RGB24 Qt preview, and reports event/pull/callback FPS, host-revision-based frame submissions, preview timer ticks, Qt paint events, actually painted frames, actual dimensions, optional device sequence/timestamp metadata, exposure-auto state and exposure readbacks at multiple lifecycle points, optional per-frame exposure metadata and frame-pool drops. It attempts to restore the original resolution after Stop. Its frame-metadata reader now checks common optional values and their timebases; it does not infer metadata availability from a nonzero payload.