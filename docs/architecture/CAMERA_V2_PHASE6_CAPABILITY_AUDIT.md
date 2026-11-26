# Camera v2.0 — Phase 6 Capability, Feature, Command and Model Audit

**Repository:** `Shennagyp/MICROSCOPE_CPP`  
**Branch:** `camera-v.2.0`  
**Reviewed provider checkpoint:** `ea356d5d2cc412e8c6aa5a3600e65c650bf9d284`  
**Current Qt/UI checkpoint:** `37a9b544dd30aac2acd6aac349c399d2617477c2`  
**Review date:** 26 November 2026  
**Status:** Phase 6 remains open and is the active software workstream by owner direction. Phase 4 visual/physical acceptance remains pending; the owner postponed the physical MU503 test and explicitly directed continued Phase 6 work.

## 1. Authority and scope

The current owner-directed phase ledger is
[CAMERA_V2_DEVELOPMENT_STATUS.md](CAMERA_V2_DEVELOPMENT_STATUS.md), and the
operational rules are in [NEXT_AI_HANDOFF.md](NEXT_AI_HANDOFF.md).
Phase 5 GenTL/GenApi integration is complete by owner direction. This audit
does not reopen it. Original Phases 3 and 4 remain in scope: Phase 3 is accepted,
and Phase 4 reusable Qt UI source/offscreen work is complete with visual and
physical owner acceptance still pending. On 26 November 2026 the owner
postponed the physical MU503 test and directed continued Phase 6 development.
This is a sequencing instruction, not a claim that Phase 4 has been accepted.
Phase 4 acceptance remains pending and does not block Phase 6 software work.
AmScope source/tests/contracts/behavior are frozen and must not be modified.

Older AmScope-only plans use different phase numbering. Their phase labels do
not override the current owner-directed Phase 0–8 ledger. In particular,
packaging must not be pulled forward from an older document's Phase 6 label.

This audit inspects:
- Common contracts and validation;
- AmScope discovery, model facts, settings, commands, telemetry and accessories;
- GenTL discovery, frame/device access, GenApi features and commands;
- adjacent Qt UI integration as a Phase 4 cross-check, not as proof of backend capability;
- automated tests and the evidence limits of CI.

Source presence, automated test coverage, CI success, physical hardware
verification and owner acceptance are separate evidence categories.

## 2. Executive result

Three concrete AmScope gaps were corrected:
1. A failed RAW pixel-format enumeration call could be misreported as an
   unsupported feature rather than an SDK/backend failure.
2. The RAW significant-bit-depth selector could bypass the model's
   `PixelFormatSelection` allow-list policy.

Both AmScope corrections have cross-architecture CI evidence. No physical
hardware verification was added. GenApi negative-path coverage has since been
added and passed official GenApi 3.5.1 integration CI on both architectures:
malformed XML rejection/recovery, unsupported public generic-node handling,
feature read/write failure recovery, command execution failure recovery and
command-completion polling failure recovery.
The exact green checkpoint is `ea356d5d2cc412e8c6aa5a3600e65c650bf9d284`,
workflow [#45767832186](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45767256996).

Phase 6 remains open. AmScope provider-level feature/command behavior remains
a known gap but is blocked by the explicit AmScope freeze. The GenTL discovery
model decision and vector descriptor consumer requirement are the next
provider-neutral audit decisions. Phase 4 visual/physical acceptance remains
pending; its physical MU503 test is postponed and does not block Phase 6.
Reusable command and capability presentation are implemented.

## 3. Coverage matrix

| Surface | Current implementation | Existing automated evidence | Audit result |
|---|---|---|---|
| Common identity/capabilities | `CameraDeviceInfo`, optional identity fields, and tri-state `Unknown/Unsupported/Supported` capability set | `CameraModelsTests` checks unknown, supported, unsupported and invalid sentinel behavior | Semantics are sound at the common value-model layer; provider mapping is separate work |
| Common feature contract | Typed bool/int64/double/string/enum/vector values; descriptors include kind, unit, numeric range, discrete values, enum symbols and vector cardinality | `CameraFeatureValidationTests` covers type mismatch, exact large int64 increments, floating increments/non-finite values, enum/discrete sets, vector length, unavailable/read-only and invalid descriptors | Strong common validation coverage; no provider behavior is implied by these tests |
| Common command contract | Separate `ICameraCommandAccess`; command issue and completion polling are distinct | `CameraCommandAccessTests` checks accepted issue, incomplete→complete polling and missing command errors | Contract semantics covered; provider adapters need their own coverage |
| AmScope discovery | `CameraInfo` extends `CameraDeviceInfo`; SDK model flags populate known capability facts; missing model descriptor leaves capability state Unknown | Common model tests cover the value type, not AmScope flag-to-capability mapping | Mapping has source coverage but no direct provider mapping test |
| AmScope model profiles | Exact model-code lookup; normalized surrounding whitespace; allow-list for configured profiles; deny precedence; last duplicate wins; missing profile preserves runtime-probed support. All 53 profile enum values are referenced in the provider source | `AmcamCameraProfileTests` covers registry semantics | Registry tests are present, but feature/command enforcement is not tested end-to-end against provider descriptors/actions |
| AmScope feature inventory | 39 setting descriptors plus 31 read-only model-fact descriptors. The 39 settings include 36 writable paths and three read-only values: RAW sensor format, sensor temperature and TEC voltage. Typed reads cover the exposed setting/model-fact inventory | No direct non-Qt provider feature tests. Existing Qt request test checks queued calls against a Closed camera and typed errors, not SDK feature semantics | **High-priority test gap** |
| AmScope feature writes | Numeric, enum, boolean and vector writes; stopped-state restrictions; provider validation; readback for settings where promised; SDK error mapping | Common validator tests do not execute Amcam SDK branches; no feature-specific SDK dispatch seam is currently implemented | **High-priority test gap**; see next work package |
| AmScope commands/telemetry/accessories | Auto white balance, frame-rate telemetry, trigger configuration/software trigger, I/O, embedded light, filter wheel, ST4, astronomy focuser, lens focus/aperture, still-resolution and still capture | Existing Qt request tests exercise request serialization/closed-state failures. No direct non-Qt provider tests cover native action preconditions, SDK result mapping, readback, or asynchronous state transitions | **High-priority test gap** |
| GenTL discovery | `Producer::discover` returns provider-specific `DeviceDescriptor` with Producer/interface/device routing identifiers and vendor/model/serial strings | Mock Producer tests cover discovery and routing/ownership | Provider discovery is tested, but it does not itself project into common `CameraDeviceInfo`/`CameraCapabilities` |
| GenTL frames | Data stream preserves payload and optional metadata; safe PFNC projection supports only explicitly verified layouts and leaves unsupported layouts opaque | Mock Producer/frame-adapter tests cover lifecycle, metadata, payload and layout checks | Good deterministic software coverage; real CTI camera evidence remains separate Phase 8 work |
| GenTL GenApi features | Boolean, Integer, Float, String and Enumeration nodes; Root-category traversal; dynamic availability/access modes; units/ranges/enumeration symbols; validated writes with GenApi verification | XML-backed tests cover all five scalar kinds, nested categories, internal-register exclusion, dynamic availability, read-only access and readback | Positive-path coverage is meaningful |
| GenTL GenApi commands | `ICommand` discovery, issue and `IsDone` polling through common command contract | XML-backed tests cover root/nested commands, internal command-register rejection, issue and incomplete→complete transition; read-only command rejection | Positive-path coverage is meaningful; malformed/failure cases remain |
| Cross-backend discovery semantics | AmScope discovery exposes common `CameraDeviceInfo`; GenTL discovery exposes `DeviceDescriptor`; GenApi features/commands use common access contracts | No shared provider/discovery abstraction or explicit GenTL→Common projection appears in the current public headers | **API alignment decision required**, not permission to restart Phase 5 |
| Qt streaming and reusable UI (Phase 3/4 cross-check) | Camera::Qt, Camera::QtWidgets, Amscope::Qt and amscope_preview exist; CameraFeatureEditor, ImageInfoBar and CameraCapabilityPanel are integrated into the preview; CameraCommandPanel represents no-argument ICameraCommandAccess commands; parameterized AmScope actions remain host-owned | Workflow [#45759739032](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45759739032) built the preview and passed 24/24 Qt CTest tests on both architectures, including the offscreen composition/dock-state smoke | Phase 4 remains open for human visual inspection and physical MU503 UI smoke. The offscreen composition test is not hardware qualification. Widget presence is not evidence of provider hardware capability |

### 3.1 AmScope feature inventory accounting

The current provider source builds:
- 39 setting descriptors;
- 31 model-fact descriptors;
- 36 setting write dispatch branches, including the common
  `RawOutputBitDepth` feature;
- three read-only setting descriptors: `AmcamRawSensorFormat`,
  `AmcamSensorTemperature`, and `AmcamTecVoltage`.

The model-fact descriptors are read-only. The source has matching read
dispatch for the setting inventory and model facts. These counts establish
source inventory coverage only; they do not establish that each feature is
correct for every camera model.

### 3.2 AmScope command surface

The public native API intentionally keeps parameterized actions outside the
simple GenApi `ICameraCommandAccess` contract. Examples include finite
software-trigger bursts, I/O configuration, filter-wheel target/direction,
ST4 direction/duration, focuser target position and lens position/aperture.
Do not flatten these into a no-argument command interface.

One-shot auto white balance and still capture also remain actions rather than
ordinary writable settings. Device-measured frame rate remains telemetry.

## 4. Corrected Phase 6 defects

### 4.1 RAW bit-depth SDK error classification

**Commit:** `a1be35dcc79241c61d4f1700b5243ff7e5c37ba7`

Before the correction, every failed first call to
`Amcam_get_PixelFormatSupport` fell through to `FeatureUnavailable`.
The vendor header defines `E_NOTIMPL` as unsupported/not implemented; other
failed HRESULTs represent an operational SDK/backend failure.

The provider now:
- returns `FeatureUnavailable` for `E_NOTIMPL`;
- returns `BackendFailure` and preserves the native HRESULT for other failed
  queries;
- rejects invalid counts before reserving or iterating;
- continues to map per-entry query failures to `BackendFailure`.

Added private helper and tests for `E_NOTIMPL`, `E_FAIL`, empty/invalid
counts and a valid count.

Files:
- `Camera/Amscope/src/AmcamCamera.cpp`
- `Camera/Amscope/src/AmcamFeatureAccessValidation.h`
- `Camera/Amscope/tests/AmcamFeatureAccessValidationTests.cpp`
- `Camera/Amscope/CMakeLists.txt`

CI: [#45742832263](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45742832263)
passed Intel x86_64 and Apple Silicon arm64.

### 4.2 RAW bit-depth model-profile enforcement

**Commit:** `b8148eeabf374e79182187051da4b871320ce58b`

`RawOutputBitDepth` writes `AMCAM_OPTION_PIXEL_FORMAT`, the same SDK
setting controlled by `AmcamPixelFormat`. It previously did not apply the
`PixelFormatSelection` profile policy, allowing this narrower selector to
bypass a configured model allow-list.

The descriptor now uses the same `pixelFormatAllowed` policy for availability
and writability. The profile test asserts that an omitted
`PixelFormatSelection` entry is denied, and the AmScope README documents the
mapping.

Files:
- `Camera/Amscope/src/AmcamCamera.cpp`
- `Camera/Amscope/tests/AmcamCameraProfileTests.cpp`
- `Camera/Amscope/README.md`

CI: [#45742968336](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45742968336)
passed Intel x86_64 and Apple Silicon arm64.

### 4.3 RAW bit-depth profile-denial explanation

**Commit:** `a4fc39b3d876ba301a2f1c8a916216c94186cd3b`

The RAW bit-depth descriptor already applied the `PixelFormatSelection`
profile allow-list to availability and writability. However, when the model
profile denied selection, the descriptor could have no `disabledReason`,
leaving the UI unable to explain why the control was unavailable.

The descriptor factory now supplies the explicit reason:
"Pixel-format selection is blocked by the active camera feature profile."
A regression assertion constructs the profile-denied descriptor without
supplying a reason and verifies the exact resulting text. Hardware read
capability remains separately represented; profile policy does not rewrite
the underlying SDK probe result.

Files:
- `Camera/Amscope/src/AmcamFeatureAccessValidation.h`
- `Camera/Amscope/tests/AmcamFeatureAccessValidationTests.cpp`

CI: [#45759739032](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45759739032)
passed all four non-Qt/Qt jobs on Intel x86_64 and Apple Silicon arm64. The
AmScope feature-validation test passed on both architectures; the Qt suite
passed 24/24 tests on each architecture.

## 5. Prioritized remaining gaps

| Priority | Gap | Evidence | Next bounded action |
|---|---|---|---|
| P1 | AmScope feature behavior has no direct provider tests | The complete non-Qt AmScope test directory has no tests for `featureDescriptors`, `readFeature` or `writeFeature`; Qt tests only check queued error paths while Closed | Design the smallest private test boundary for descriptor/probe/write semantics. Prefer extracting pure descriptor policy/mapping logic where possible; introduce per-instance SDK dispatch only for calls that genuinely need injection. Preserve one handle, SDK serialization and the public ABI. |
| P1 | AmScope command/accessory behavior has no direct provider tests | Public API includes trigger, I/O, light, filter-wheel, ST4, focuser, lens and still-capture operations; current tests do not exercise their provider-side preconditions, rollback, readback or asynchronous completion | Build a separate command-family test matrix. Do not merge settings, telemetry and actions. Start with one deterministic, low-risk command family after its SDK semantics are traced. |
| P2 | GenApi vendor/device behavior remains unverified | Synthetic XML and mock-port tests now cover malformed XML rejection/recovery, unsupported public generic-node classification, feature read/write failure recovery and command execution failure recovery. No vendor CTI or physical GenTL camera is part of this evidence. | Keep vendor XML/CTI/device qualification in Phase 8. Do not treat mock/XML CI as hardware evidence. |
| P2 | GenTL discovery-to-Common projection decision | `Producer::discover` returns provider-specific `DeviceDescriptor`; `CameraDeviceInfo` is a data model, not a required cross-provider discovery API. The GenTL descriptor carries Producer path, interface ID and device ID required for routing. | **Decision: retain provider-specific `DeviceDescriptor`; do not add a Common projection without a concrete multi-provider consumer.** A future projection must preserve the routing descriptor separately and define a collision-safe opaque identity. No source/API change is justified now. |
| P2 | Vector descriptors expose cardinality but not per-element semantics | Common descriptor has `elementCount`; AmScope ROI/sharpening writes enforce element-specific constraints in provider code. No concrete generic consumer requiring per-element labels/ranges has been established. | **Decision: defer contract expansion.** Revisit only when a generic consumer needs to render or validate per-element constraints. Keep provider validation authoritative. |
| P3 | Hardware/model evidence remains narrow | Existing MU503 short runs are useful regression observations, not evidence for every supported model/feature/accessory or GenTL CTI camera | Carry the exact supported model/backend matrix and records into Phase 8. Do not claim hardware support from SDK flags, mocks or XML alone. |

## 6. Explicitly deferred or unsupported behavior

The AmScope README records separate follow-ups including self-trigger
parameterization, continuous-trigger/cancellation behavior, additional
trigger-output programming, output-level writes, UART, counter/PWM programming,
lens autofocus mode switching until SDK working-distance semantics are
documented, synchronized multi-camera acquisition, RAW demosaicing for visible
preview and a concrete GPU renderer.

These are not automatically Phase 6 implementation tasks. First classify
each against the current product scope and SDK evidence. Preview/UI remains an
original Phase 3/4 deliverable, tracked separately from this provider audit;
widget existence must never be used as evidence that a camera feature is
supported or hardware-qualified.

## 7. Next work package

1. Continue Phase 6 software work now, as explicitly directed by the owner.
   Phase 4 visual/workspace acceptance remains pending and physical MU503
   testing is postponed; neither blocks Phase 6. Do not mark Phase 4 accepted.
2. Preserve the AmScope freeze. The known provider-level AmScope feature and
   command test gaps remain documented but must not be addressed by modifying
   AmScope source, tests, public contracts or behavior.
3. GenApi malformed XML, unsupported public generic-node, feature read/write
   failure recovery and command execution failure recovery tests are complete
   at `a07c8a2`; workflow #45767832186 passed Intel x86_64 and Apple Silicon
   arm64 with the official GenApi 3.5.1 integration.
4. Retain provider-specific GenTL `DeviceDescriptor` for now. It carries the
   Producer/interface/device routing identity needed by `openDevice`; there
   is no established generic multi-provider discovery consumer that justifies
   a lossy Common projection. Revisit only when such a consumer exists.
5. Defer per-element vector descriptor expansion until a concrete generic
   consumer requires per-element metadata. Provider-side validation remains
   authoritative.
6. Continue the next provider-neutral Phase 6 coverage gap. Keep vendor CTI,
   vendor GenApi XML and physical camera qualification as Phase 8 evidence;
   mock/XML CI is not hardware verification.

**No local build/test/debug run or physical-camera test was performed for this
audit.** The CI results above are the exact remote workflow evidence.
