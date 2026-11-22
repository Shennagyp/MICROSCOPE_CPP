# AmScope Implementation Inventory

**Source snapshot:** `a3a03fbd1fe652fd4c1dfa8d2082ef86a2972f13`  
**Branch head reviewed for this inventory refresh:** `1e44fa8bab23f26587adffe8c5e06471956d9e54` (documentation-only relative to the source snapshot)  
**Purpose:** Phase 0 source-to-test-to-UI inventory. This document records source claims and visible test/UI coverage; it does not claim physical-camera verification.

---

## 1. Evidence classification

Use the following labels consistently:

- **Source implemented:** implementation is present in the inspected C++ source.
- **Automated test present:** a test target exists for at least part of the behavior. Read the test before assuming full coverage.
- **CI passed:** the current CI revision compiled/ran the relevant test.
- **Hardware verified:** a completed physical-camera record exists for the exact relevant source revision and camera model.
- **Not present / deferred:** current source/README explicitly says the feature is not delivered.
- **Unknown:** not established by the inspected source or evidence.

No feature in this inventory is labeled Hardware verified. The reviewed CI run found zero cameras and is not hardware evidence.

## 2. Source layout and responsibilities

| Area | Main files | Current responsibility |
|---|---|---|
| Native AmScope API | `AmcamCamera.h`, `AmcamCamera.cpp` | Owns native SDK session, handle, lifecycle, feature access, commands, telemetry, still capture, and native acquisition |
| Event queue | `AmcamEventQueue.h` | Bounded callback-to-worker event transfer |
| Watchdog | `detail/FrameStallWatchdog.h` | Video frame-stall and trigger heartbeat timing logic |
| Frame gate | `detail/AcquisitionFrameGate.h` | Session/generation-aware publication gate |
| Model policy | `AmcamCameraProfiles.h/.cpp` | Exact model allow/deny feature policy |
| AmScope types | `CameraTypes.h` | CameraInfo, Frame, resolution, state, SDK enums, trigger/I/O/accessory models |
| Common feature contract | `Camera/Common/include/camera/CameraFeatureAccess.h` | Feature kinds, typed values, descriptors, typed errors/results, feature interface |
| Common validation | `CameraFeatureValidation.h` | Shared type, range, increment, enum, vector, availability, and writability validation |
| Common FPS estimator | `FrameRateEstimator.h` | Host frame-rate estimation |
| Qt facade | `QtAmcamCamera.h/.cpp` | Qt QObject facade, lifecycle/request worker, queued feature/control operations, frame mailbox |
| Preview | `app/main.cpp` | Current single-window Qt Widgets application |
| Build/CI | `CMakeLists.txt`, `.github/workflows/amscope-macos.yml` | macOS driver/Qt/preview build, CTest, architecture and launcher checks |

## 3. Session lifecycle and live acquisition

| Behavior | Source state | Automated test evidence | Real-camera evidence |
|---|---|---|---|
| Device enumeration and opaque ID open | Source implemented | No driver-level hardware enumeration test in the eight-test suite | Not recorded |
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
| Old-session frame rejection and latest mailbox | Source implemented | Frame gate test and Qt request tests cover portions | Not recorded |
| Frame-pool exhaustion and dropped-frame accounting | Source implemented/documented | No dedicated frame-pool behavior test identified | Not recorded |

## 4. Frame and image payload behavior

| Behavior | Source state | Automated test evidence | Real-camera evidence |
|---|---|---|---|
| Packed RGB24 live frames | Source implemented/documented | No camera payload integration test identified | Not recorded |
| RAW8 and RAW16 live frame representation | Source implemented/documented | Camera type tests verify representation fields, not sensor payload bytes | Not recorded |
| Sensor FourCC and significant depth separate from storage container width | Source implemented | Camera type tests cover representation distinction | Not recorded |
| Sequence, timestamp, exposure, gain, black-level metadata | Source implemented/documented | No full live metadata hardware test | Not recorded |
| Four immutable `FramePtr` leases | Source implemented/documented | No dedicated pool/retention stress test identified | Not recorded |
| Private discard buffer when every lease is retained | Source implemented/documented | No direct pool exhaustion test identified | Not recorded |
| QImage/QPixmap preview detachment | Source/UI documented | Preview build only; no live device test | Not recorded |
| RAW sensor display as RGB | Explicitly refused by current UI | Not applicable | Not recorded |
| RAW/Bayer demosaic for visible preview | Not present/deferred | No test target | Not recorded |
| GPU/zero-copy rendering | Not present/deferred | No test target | Not recorded |

## 5. Common feature interface and settings

The common contract currently supports Boolean, integer, integer vector, floating-point, string, and enumeration values. Enumeration symbols are distinct from ordinary strings. Descriptors carry availability, readability, writability, unit, scalar range/increment, vector cardinality, enumeration symbols, and group. Errors distinguish unsupported, not found, unavailable, not readable, not writable, type mismatch, invalid value, and backend failure.

| Feature family | Current source coverage | Key semantics/constraints | Test/UI coverage | Hardware evidence |
|---|---|---|---|---|
| Exposure auto | `ExposureAuto` | Off/Once/Continuous maps native values 0/2/1 as declared; Once is native one-shot, not emulated | Feature validator and type tests; descriptor-driven UI | Not recorded |
| Exposure time | `ExposureTime` | Integer microseconds; writable only when current exposure mode permits | Feature validator; descriptor UI | Not recorded |
| Analog gain | `AmcamAnalogGainPercent` | Percentage, not GenICam dB | Descriptor UI | Not recorded |
| Color matrix | `AmcamColorMatrixEnabled` | SDK/model-probed setting | Descriptor UI | Not recorded |
| Pixel format | `AmcamPixelFormat` | Native SDK format codes; not automatically PFNC | Descriptor UI | Not recorded |
| White balance temperature/tint | `AmcamWhiteBalanceTemperature`, `AmcamWhiteBalanceTint` | Temperature/tint are coupled where SDK setter takes pair; preserve unchanged component | Descriptor UI | Not recorded |
| RGB white-balance gain | `AmcamWhiteBalanceRGBGain` | Three-element integer vector | Common vector validation; descriptor UI | Not recorded |
| Fixed FPS | `AmcamFixedFrameRate` | Configured fixed FPS in 0.1 FPS increments when model/SDK proves support | Common numeric validation; UI | Not recorded |
| Frame speed | `AmcamFrameSpeedLevel` | Discrete speed level, not FPS | Descriptor UI | Not recorded |
| Resolution index | `AmcamResolutionIndex` | Enumerated index/width/height; stopped-state write | Descriptor UI | Not recorded |
| RAW output | `AmcamRawOutputEnabled`, `AmcamRawBitDepth` | Stopped-state-only; model flag, SDK probe, and model policy must agree | Camera type tests cover representation, not hardware setting application | Not recorded |
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
| One bounded FIFO Qt camera-operation worker (256 pending requests) | Implemented | Qt request tests cover asynchronous closed-session errors, FIFO writes, callback thread, stale descriptor suppression | Preserve when control/streaming libraries are separated |
| Scheduled/active session generations | Implemented | Partially covered by Qt request tests | Expand for lifecycle interleavings and real device |
| Feature-state generation | Implemented | Partially covered | Test feature descriptor invalidation after real dependent writes |
| UI callback delivered on QObject affinity thread | Implemented | Qt request tests | Hardware regression |
| Synchronous vs queued mode commitment | Implemented | Source contract; tests may cover closed-session use only | Add explicit mode-conflict test if absent |
| Latest-frame mailbox | Implemented | Frame gate helpers tested; no real-frame test | Test high-rate frame delivery and retained frames on hardware |
| Descriptor-driven feature editor | Implemented | Preview compile; common validation tests | Extract reusable UI widgets |
| Trigger panel | Implemented | Preview compile | Hardware UI test |
| Lens focus/aperture panel | Implemented | Preview compile | Hardware/accessory test |
| Accessory panels | Partial | Preview source | Add missing native I/O UI if required |
| Separate `camera_amscope_control` | Not delivered | None | Extract non-Qt programmatic control |
| Separate Qt streaming library | Not delivered | None | Extract frame delivery from broad `QtAmcamCamera` |
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
- Driver/control/Qt streaming target separation is incomplete.
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
| `amscope_frame_stall_watchdog_tests` | Video/heartbeat timeout calculations | Actual SDK timeout and heartbeat behavior |
| `amscope_qt_request_tests` | Qt worker request ordering, callback affinity, typed closed-session outcomes, request supersession | Real camera SDK operations |
| `camera_common_feature_tests` | Feature value type/range/increment/enumeration/vector validation | Native feature readback |
| `camera_common_frame_rate_tests` | Host frame-rate estimator behavior | Camera frame cadence |

All eight tests passed on both Intel x86_64 and Apple Silicon arm64 in workflow run [#45051151740](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45051151740), on branch head `4de57806a898799173adb04af1272e00ff00fb24`. The launcher found zero cameras in CI. This list must be expanded when tests are added; it must not be interpreted as complete native lifecycle coverage.

## 10. Current evidence classification

- Native AmScope source implementation: **present**.
- Common feature model and validator: **present**.
- Common camera identity/frame/capability model: **partial**.
- Qt worker and frame mailbox: **present in broad Qt facade**.
- Trigger/lens/accessory UI: **partial application implementation present**.
- Hardware-I/O UI: **incomplete relative to API**.
- Separate control and Qt streaming libraries: **not delivered**.
- Reusable backend-neutral Qt UI library: **not delivered**.
- Existing GenTL/GenApi C++ integration: **not delivered / dependency blocker**.
- Exported CMake package: **not established**.
- Intel and Apple Silicon CI: **passed at branch head `4de57806` in workflow #45051151740; eight of eight CTest tests passed on both architectures**.
- Physical AmScope hardware baseline: **not recorded**.
- Physical CTI-camera test: **not recorded**.

Update this inventory when a source/API/test/UI change lands. Do not change a hardware-evidence cell from "Not recorded" without a completed record under `docs/testing/hardware/`.


## 11. Phase 1 preparation — native SDK seam

A static source scan of the current AmScope implementation identifies direct SDK entry points across enumeration/session lifecycle, callback-based acquisition and pulling, feature probes and writes, trigger/heartbeat setup, still capture, hardware I/O, and accessory operations. The native session currently has no injected SDK-call boundary, so the existing helper tests cannot force deterministic native Stop/Open/Start/Pull/Close failures.

The Phase 1 test seam and the initial lifecycle/acquisition API subset are specified in [AMSCOPE_NATIVE_DRIVER_TEST_SEAM.md](AMSCOPE_NATIVE_DRIVER_TEST_SEAM.md). The vendor header confirms the exact `PAMCAM_EVENT_CALLBACK` (`__stdcall`) and callback-based pull API, but its `Amcam_Close` comment does not explicitly state that no callback can begin after Close returns. The current driver waits for callbacks already counted as active after `Amcam_Close`; this is not by itself proof against a callback that begins after the counter reaches zero. Treat the post-Close callback-quiescence guarantee as an explicit SDK contract to establish with vendor documentation and controlled hardware observation. No source seam or native-session mock test has been implemented yet. Phase 0 physical-camera acceptance remains the prerequisite for behavior-changing Phase 1 work.
