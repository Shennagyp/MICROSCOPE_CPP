# AmScope Implementation Inventory

**Source snapshot:** `15be30850ea573dd06ffe3128befcdb27ce2e974` (latest implementation-bearing source: host-revision paint measurement, configured-exposure/speed diagnostics)  
**Branch head reviewed for this inventory refresh:** `c98be94d115a752638b4ca6153caf4c638aa55c7`  
**Purpose:** Phase 0 source-to-test-to-UI inventory. This document records source claims, automated coverage, and separately classified user-reported physical-camera observations; it does not claim full physical-camera qualification.

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
| Event queue | `AmcamEventQueue.h` | Bounded callback-to-worker event transfer |
| Watchdog | `detail/FrameStallWatchdog.h` | Video frame-stall and trigger heartbeat timing logic |
| Frame gate | `detail/AcquisitionFrameGate.h` | Session/generation-aware publication gate |
| Model policy | `AmcamCameraProfiles.h/.cpp` | Exact model allow/deny feature policy |
| AmScope types | `CameraTypes.h` | CameraInfo, Frame, resolution, state, SDK enums, trigger/I/O/accessory models |
| Common feature contract | `Camera/Common/include/camera/CameraFeatureAccess.h` | Feature kinds, typed values, descriptors, typed errors/results, feature interface |
| Common validation | `CameraFeatureValidation.h` | Shared type, range, increment, enum, vector, availability, and writability validation |
| Common FPS estimator | `FrameRateEstimator.h` | Host frame-rate estimation |
| Qt facade | `QtAmcamCamera.h/.cpp` | Qt QObject facade, lifecycle/request worker, queued feature/control operations, frame mailbox |
| Preview | `app/main.cpp` | Current single-window Qt Widgets application |\n| Physical-camera rate test | `app/hardware_rate_test.cpp` | Dedicated Qt preview/test executable measuring event, pull, callback-delivery and paint rates on a connected camera |
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
| Old-session frame rejection and atomic latest-frame mailbox | Source implemented | Frame gate test and Qt request tests cover portions; concurrent mailbox behavior has no dedicated stress test | Not recorded |
| Frame notifications bypass feature-request queue lock | Source implemented | No dedicated contention test | Not recorded |
| SDK image-event, successful-pull, and actual paint counters | Source implemented | Counter wiring is compile/CI pending; no unit-level hardware-rate test | Not recorded |
| Maximum supported frame rate selected on Start by model flags | Source implemented | No deterministic SDK-setting test yet | Not recorded |
| Generic SDK FPS cap removed (`AMCAM_OPTION_FRAMERATE=0`) | Source implemented | No deterministic SDK-setting test yet | Not recorded |\n| Physical-camera rate measurement | Test executable implemented; manual run required | `amscope_hardware_rate_test` reports event/pull/callback rates, preview poll/submission/paint counters, dimensions, SDK metadata, exposure-auto state/readbacks, and drops | Two MU503 runs report about 91.2 event/pull/callback FPS at 640×480 with zero drops; second run reports 14.14 painted FPS and a pre-Start 70 ms exposure readback. Active exposure and GUI-rate causes remain under diagnosis |
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
| Direct `QImage` wrapper over retained `FramePtr` and `QPainter` paint (no per-frame pixel copy or `QPixmap`) | Source implemented | No dedicated FrameView rendering test; branch-head CI passed on Intel x86_64 and Apple Silicon arm64 | Not recorded |
| RAW sensor display as RGB | Explicitly refused by current UI | Not applicable | Not recorded |
| RAW/Bayer demosaic for visible preview | Not present/deferred | No test target | Not recorded |
| GPU rendering | Not present/deferred | No test target | Not recorded |

## 5. Common feature interface and settings

The common contract currently supports Boolean, integer, integer vector, floating-point, string, and enumeration values. Enumeration symbols are distinct from ordinary strings. Descriptors carry availability, readability, writability, unit, scalar range/increment, vector cardinality, enumeration symbols, and group. Errors distinguish unsupported, not found, unavailable, not readable, not writable, type mismatch, invalid value, and backend failure.

The Amcam implementation caches its full descriptor inventory per camera state; individual `readFeature()` operations reuse that inventory instead of repeating the complete set of SDK probes under the SDK mutex. Successful feature writes invalidate the cache. The preview stops automatic descriptor refresh when Streaming so the full probe set does not contend with `Amcam_PullImageV4`.

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

All eight registered CTest targets passed on both Intel x86_64 and Apple Silicon arm64 in workflow [#45059333867](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45059333867), associated with branch head `48be6c9b635fe9fa40760f66b2efec024bf4fec4`. The initial hardware-test executable was compiled and architecture/linkage-checked successfully on both architectures in workflow [#45062683602](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45062683602). That CI run predates the sequence-independent paint and exposure/speed diagnostic corrections. The corrected source is being checked in workflow [#45063651610](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45063651610), which is in progress at this inventory refresh. The launcher found zero cameras in CI; physical-camera behavior remains a manual test.

The physical-camera performance executable is deliberately separate from CTest. It is built as `amscope_hardware_rate_test`, launched through `scripts/run-amscope.sh --hardware-rate-test`, and must be run manually with a physical camera. It selects and verifies the requested `AmcamResolutionIndex`, captures the original resolution, runs a visible RGB24 Qt preview, and reports event/pull/callback FPS, host-revision-based frame submissions, preview timer ticks, Qt paint events, actually painted frames, actual dimensions, device sequence/timestamp metadata, exposure-auto state and exposure readbacks at multiple lifecycle points, per-frame exposure metadata and frame-pool drops. It attempts to restore the original resolution after Stop.

### 9.1 First user-reported hardware rate run — 22 November 2026

The user reports running the test with an AmScope MU503 identified by the SDK as `MU503(USB2.0)`, selecting live mode `2:640x480`, and measuring for 15.090 seconds:

| Measurement | Observed |
|---|---:|
| SDK event rate | 91.253 FPS |
| Successful pull rate | 91.253 FPS |
| Driver frame callback rate | 91.253 FPS |
| Actual dimensions | 640×480 |
| Frame-pool drops | 0 |
| SDK frame sequence | 0 for first and last observed frame |
| Per-frame exposure metadata | 0 (no nonzero SDK exposure samples) |
| Stop | Succeeded |
| Original 1280×960 resolution restoration | Succeeded |
| Close | Succeeded |

The equal event, pull and callback counts show no measured loss between those three native stages during this run. The 91.253 FPS is the observed incoming camera rate; the requested 100 FPS target was not met. The first run's `paint_fps=0` and overall `FAIL` are not valid presentation evidence: the test rejected new frames when the SDK sequence value matched, and the preview/production telemetry also counted a frame as newly painted only when that SDK value changed. This MU503 run returned sequence zero for both first and last samples. The zero SDK per-frame exposure field also does not establish the configured exposure value.

The test and production preview have since been corrected to count a newly delivered frame when it is actually painted, independently of SDK sequence metadata. The next test revision additionally reads the configured `ExposureTime` feature before starting acquisition and reports that separately from per-frame SDK exposure metadata; it also reports timestamp samples where available. This first run is retained as a valid 91.253 FPS acquisition observation, but its paint-rate result must not be used.

Do not mark the physical camera performance target as met. The first and second runs show consistent native delivery near 91 FPS, but the 100 FPS target remains unmet and the active exposure/USB/presentation causes remain unresolved.


### 9.2 Second user-reported hardware rate run — 22 November 2026

The user rebuilt and ran the test after the missing `Measurement::exposureSamples` snapshot field was fixed (source revision `2da3f5d`). The run selected `2:640x480` on SDK device name `MU503(USB2.0)` and reported:

| Measurement | Observed |
|---|---:|
| Measurement duration | 14.498 s |
| SDK event rate | 91.183 FPS |
| Successful pull rate | 91.183 FPS |
| Driver callback rate | 91.183 FPS |
| Paint rate | 14.140 FPS (205 frames) |
| Pulled/delivered frames | 1,322 |
| Dropped frames | 0 |
| Applied speed level | 6 (model maximum 6) |
| Reported pre-Start exposure | 70,000 µs |
| Calculated pre-Start exposure rate | 14.286 FPS |
| SDK per-frame exposure samples | 0 |
| SDK sequence / timestamp samples | sequence unchanged at zero; timestamp samples unavailable |
| Actual frame dimensions | 640×480 |
| Stop / resolution restore / Close | All succeeded |

The native event/pull/callback counts again match, so the user-observed pipeline did not lose frames between those stages in this interval. The requested 100 FPS target was not met. The applied model speed level was already at its advertised maximum (6/6), so a lower selected speed level is not the explanation.

The `70,000 µs` value was read before Start. The test revision used for this run did not report `ExposureAuto` or sample exposure during the measurement. Therefore the derived `14.286 FPS` value is only arithmetic on a pre-Start readback; it is **not yet evidence that active exposure limited the stream**. The SDK's per-frame exposure metadata was zero, so it cannot resolve this. The 14.140 FPS paint rate is numerically close to the arithmetic value, but that coincidence does not establish a causal relationship. The run also did not count preview timer polls, frame submissions, or all paint events, so the location of the presentation-rate reduction is unresolved.

The next diagnostic revision samples automatic-exposure mode and exposure readback before Start, after Start, and at the beginning/end of the timed interval. It only publishes an exposure-derived FPS limit if automatic exposure is Off at both measurement boundaries and the exposure value is stable. It also reports preview poll ticks, frame submissions, Qt paint events, and painted frames separately. This diagnostic revision is committed after source revision `2da3f5d`; its local build and hardware run are still pending.

Do not change the acquisition mechanism or force a frame-rate value on the basis of this run. Also inspect the actual negotiated USB speed in macOS `system_profiler SPUSBDataType`; the SDK display name `MU503(USB2.0)` is a transport clue but is not a substitute for the operating system's negotiated-link report.

## 10. Current evidence classification

- Native AmScope source implementation: **present**.
- Common feature model and validator: **present**.
- Common camera identity/frame/capability model: **partial**.
- Qt worker and frame mailbox: **present in broad Qt facade**.
- Trigger/lens/accessory UI: **partial application implementation present**.
- Hardware-I/O UI: **incomplete relative to API**.
- Separate control and Qt streaming libraries: **not delivered**.
- Reusable backend-neutral Qt UI library: **not delivered**.
- Official GenApi 3.5.1 runtime and GenTL 1.6 ABI: **identified; package not yet integrated**.
- Existing GenTL Consumer implementation: **present in Swift AMSCOPE_DRIVER/macos-version-0.4; no direct C++ interface**.
- C++ GenTL Consumer/CTI adapter: **not delivered; integration choice pending**.
- Exported CMake package: **not established**.
- Intel and Apple Silicon CI: **previous branch head `48be6c9` passed in workflow [#45059333867](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45059333867); eight of eight registered CTest tests passed on both architectures. The hardware-test executable is added after that run; CI for `c98ab6b` is pending at this inventory refresh**.
- Targeted physical-camera smoke observation: **user reports that live resolution switching now works after a clean rebuild; see Section 10.1**.
- Physical AmScope hardware baseline: **not recorded; a full exact-commit regression record is still required**.
- Physical CTI-camera test: **not recorded**.

### 10.1 Targeted user-reported physical-camera smoke observation — 22 November 2026

The user reports the following result after removing the stale generated `build/amscope` directory and running `AMSCOPE_BUILD_TESTING=ON ./scripts/run-amscope.sh`:

- The clean rebuild completed and the preview application launched.
- The live `Resolution` feature in the feature panel successfully changed the camera's live resolution.
- The earlier filesystem error while writing `AmcamCamera.cpp.o` did not recur after the generated build tree was removed.

The later performance run identifies the device as `MU503(USB2.0)` and confirms live `640×480` frame dimensions, successful Stop, restoration to the original `1280×960` live mode, and successful Close. The exact camera serial, firmware, physical USB negotiated speed, still-capture dimensions, and full lifecycle baseline were not supplied. The result is a targeted performance observation, not a complete `Hardware verified` baseline.

Update this inventory when a source/API/test/UI change lands. Do not change a hardware-evidence cell from "Not recorded" without a completed record under `docs/testing/hardware/`.


## 11. Phase 1 preparation — native SDK seam

A static source scan of the current AmScope implementation identifies direct SDK entry points across enumeration/session lifecycle, callback-based acquisition and pulling, feature probes and writes, trigger/heartbeat setup, still capture, hardware I/O, and accessory operations. The native session currently has no injected SDK-call boundary, so the existing helper tests cannot force deterministic native Stop/Open/Start/Pull/Close failures.

The Phase 1 test seam and the initial lifecycle/acquisition API subset are specified in [AMSCOPE_NATIVE_DRIVER_TEST_SEAM.md](AMSCOPE_NATIVE_DRIVER_TEST_SEAM.md). The vendor header confirms the exact `PAMCAM_EVENT_CALLBACK` (`__stdcall`) and callback-based pull API, but its `Amcam_Close` comment does not explicitly state that no callback can begin after Close returns. The current driver waits for callbacks already counted as active after `Amcam_Close`; this is not by itself proof against a callback that begins after the counter reaches zero. Treat the post-Close callback-quiescence guarantee as an explicit SDK contract to establish with vendor documentation and controlled hardware observation. No source seam or native-session mock test has been implemented yet. Phase 0 physical-camera acceptance remains the prerequisite for behavior-changing Phase 1 work.


## 12. GenTL/GenApi dependency discovery

The dependency assessment is recorded in [AMSCOPE_GENTL_GENAPI_CPP_INTEGRATION_ASSESSMENT.md](AMSCOPE_GENTL_GENAPI_CPP_INTEGRATION_ASSESSMENT.md).

| Area | Discovery | Current C++ repository status |
|---|---|---|
| Official GenApi | EMVA GenICam 2026.07 includes GenApi 3.5.1; macOS Runtime and SDK packages exist; release notes state CMake package config was added | Candidate official runtime; not downloaded, architecture-inspected, or linked |
| Official GenTL | GenTL 1.6 standard and reference C ABI header are published | No C++ imported target/header in the branch |
| CTI Producer | Supplied by the camera/transport vendor; required for real transport enumeration and acquisition | No actual Producer .cti in the repository |
| GenTL Consumer | Existing GenTLNative implementation in AMSCOPE_DRIVER/macos-version-0.4; Swift API and dynamic CTI loader | No C++ interface or C ABI facade in the existing Swift branch |
| AmScope GenTL adapter | Swift AmcamGenTL maps documented Amcam SDK capabilities to a GenTL-shaped contract but does not create an AmScope .cti Producer | Continue the existing native Amcam SDK backend; never fabricate GenApi XML or register-port access |
| GenApi on AmScope | Swift conformance record says GenApi XML URL and physical port/register access are not implemented for Amcam | Not available through the inspected Amcam API path |

The Swift implementation's latest reviewed workflow [#44892073579](https://github.com/Shennagyp/AMSCOPE_DRIVER/actions/runs/44892073579) completed six CI jobs successfully, including the GenTLNative, GenTLAPI, GenTLRouter, Amcam adapter, and mock Producer test targets. This is software/mock evidence only; no real CTI camera test is recorded.

The Swift GenTLABI header's repository provenance is Aravis 0.9.2. For a new C++ build, prefer the official EMVA GenTL 1.6 reference header or perform an explicit byte/provenance check before reusing the Swift copy.

No GenTL/GenApi integration source change was made. Phase 5 remains blocked pending the Consumer integration choice, target CTI Producer, dependency distribution verification, and real CTI hardware.
