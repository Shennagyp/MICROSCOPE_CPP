# Camera Common Type Ownership

**Revision:** 23 November 2026  
**Branch:** `amscope-native-driver-0.6`  
**Purpose:** Phase 2 semantic ownership audit for the AmScope-native release.

This document records which public values belong to the backend-neutral camera contract and which intentionally remain in the AmScope adapter. It is an ownership decision, not a requirement to move every public C++ type into a common namespace.

## 1. Backend-neutral contract

| Type | Common meaning | Boundary |
|---|---|---|
| `CameraIdentity` | Optional identity facts reported by discovery | An absent fact remains absent; an opaque device ID is not a serial number |
| `CameraDeviceInfo` | Discovered candidate with opaque ID, display label, identity, capabilities and preview dimensions | Backend routing identity stays opaque |
| `ImageResolution` | Width and height as a dimension value | It is not a vendor resolution-table index |
| `CameraCapability` / `CameraCapabilities` | Known/unknown hardware capability facts | `Unknown`, `Unsupported` and `Supported` remain distinct; capability does not imply current feature availability |
| `Frame` / `FramePtr` | Immutable image payload and common layout/metadata | Live/still marker, pixel layout, exact format identity, source significant depth and storage width are distinct |
| `FrameTimestamp` | Backend timestamp ticks and explicit ticks-per-second | Clock origin/domain remains unknown unless the producer reports it |
| `FrameExposure` | Per-frame measured exposure duration and its timebase | Not the configured exposure feature value |
| `CameraFeatureKind` / `CameraFeatureValue` | Shared scalar/vector/enum value categories | Vendor feature names and native codes remain opaque to common consumers |
| `CameraFeatureDescriptor` | Availability, access, unit, constraints and editor semantics for a feature | A descriptor is per-device/per-state; it does not invent capability |
| `CameraFeatureAccessError` / `CameraFeatureResult<T>` | Typed result/error for common feature access | `nativeCode` is an opaque backend code; common code does not reinterpret it |
| `CameraIntegerRange` | Exact signed-integer bounds and optional integer increment | No conversion through floating-point |
| `CameraFloatingPointRange` | Floating-point bounds and optional floating increment | Finite-value and tolerance checks apply |
| `FrameRateEstimator` | Host-observed arrival cadence from monotonic host timestamps | It is not camera configuration or device-reported FPS telemetry |

The common layer remains header-only/interface-oriented and must not acquire Qt, Amcam SDK, GenTL, or vendor-header dependencies.

## 2. AmScope-owned values

| Type | Why it remains under `amscope` |
|---|---|
| `CameraState` | Native Amcam lifecycle states and failure transitions; another backend may have different lifecycle semantics |
| `CameraError` | Amcam operation/SDK failure with native vendor code and operation context; it is not the common feature-access error |
| `CameraInfo` extension fields | Amcam model code/flags, speed/fan limits and native I/O-line facts extend common discovery data |
| `AmcamFrame` | SDK flags, exposure gain percent and black level are backend metadata; the object extends common `Frame` |
| `AmcamAutoExposureMode` | Native SDK values `Off=0`, `Continuous=1`, `Once=2` |
| `ExposureSettings` | Includes Amcam auto-mode and gain-percent semantics; configured exposure is not per-frame exposure metadata |
| `WhiteBalanceSettings` | Amcam temperature/tint model and units |
| `FrameRateStatistics` | `Amcam_get_FrameRate` device/SDK telemetry and total-frame counter; it is not the common host-side estimator |
| `StillResolution` | Native still-resolution table index plus dimensions; only the dimensions are common |
| Trigger, GPIO/I/O, light-source, filter-wheel, ST4, focuser and lens structures/enums | These preserve Amcam accessory and command semantics, including native enumerations, units, asynchronous completion and hardware constraints |

The common feature interface may expose a vendor feature when its value genuinely fits a common kind. That does not move its native command, accessory state, or SDK-specific completion semantics into the common layer.

## 3. Numeric range correction in Phase 2

The earlier common feature descriptor represented both integer and floating-point ranges with `double`. That is not type-correct for an `int64_t` feature: IEEE-754 double cannot represent every adjacent integer above (2^{53}). Converting an integer request and its bounds to `double` can collapse distinct values and corrupt range/increment validation.

The common contract now uses a discriminated range:

- `CameraIntegerRange`: `int64_t` minimum, maximum and optional increment.
- `CameraFloatingPointRange`: `double` minimum, maximum and optional increment.
- The range alternative must match the descriptor's scalar feature kind. A mismatched range is an invalid backend descriptor, not a value error.
- Integer increment alignment uses unsigned modular difference after validating the ordering and positive increment. This avoids signed overflow even across the full `int64_t` domain.
- Floating-point range validation retains finite-value and tolerance-based increment checks.

Automated tests cover values above (2^{53}), adjacent off-increment integers, mismatched range alternatives, non-positive increments, and the full signed 64-bit range.

## 4. Error and measured-rate decisions

No additional generic camera-operation error hierarchy is introduced in Phase 2.

- Feature reads/writes use `CameraFeatureAccessError`, which is already common.
- AmScope open/start/stop/close, still capture, trigger and accessory operations use `amscope::CameraError` at this release boundary. Its operation and native SDK code remain available.
- No universal camera lifecycle enum is introduced.
- `FrameRateEstimator` represents host-side arrival cadence.
- `FrameRateStatistics` represents Amcam SDK/device telemetry. The two must not be aliased or reported as the same measurement.

This avoids false equivalence between backend error spaces, lifecycle models and rate sources. A second real backend may justify a common operation-result contract later, but Phase 2 does not speculate about one.

## 5. Phase 2 physical frame verification

The native-only probe reports the first delivered common frame's:

- Width and height.
- Live/still marker.
- Stride in bytes and actual payload byte count.
- Common pixel-layout enum.
- Optional opaque format scheme/value.
- Significant source depth and storage width.
- Sequence, timestamp and exposure availability, including timebases.

Run the probe against the physical MU503 and record the `FRAME` line. Compare the observed payload against the reported layout. For packed RGB24, the row payload is `width * 3`; for Raw8/Raw16, the storage row is based on the declared storage width. Do not infer RAW source precision or sequence/timestamp availability from an absent field or a zero value.

The vendor-only `WaitImageV3` probe is not evidence for the common `camera::Frame` adapter because it bypasses `AmcamCamera`.

The MU503 is not assumed to support every RAW mode. A RAW identity/depth test is required only when the connected model reports and successfully selects a supported RAW format. Otherwise record RAW verification as not supported/not run for that model and keep the common model claim limited to the verified RGB path.

## 6. Phase 2 completion gate

Phase 2 can be accepted when all of these are true:

1. The type ownership classifications above match the public headers and implementation.
2. Common headers compile through `Camera::Common` without Qt or vendor SDK dependencies.
3. Common contract tests pass, including exact integer range validation.
4. A physical AmScope frame delivered through `AmcamCamera` agrees with the common frame layout and metadata representation.
5. Any unavailable optional metadata and unsupported RAW capabilities are recorded as such; no values are fabricated.

A passing build or the vendor-only probe alone does not close the physical common-frame gate.
