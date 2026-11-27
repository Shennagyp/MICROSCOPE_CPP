# OPTIKA B293 v0.9 — Continuation Handoff

**Repository:** `Shennagyp/MICROSCOPE_CPP`  
**Target branch:** `OPTIKA-B293-DUALSLIDE-APP-v.0.9`  
**Latest source/test commit:** `0d51df7b6e9c863c2e09ae25a2aeaf0c700a2e13`  
**Architecture audit commit:** `983ae76c2502b38444f3f4dcad9782aeffae09d2`  
**Verification:** [platform CI run 45929942838](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45929942838) passed Ubuntu GCC, macOS Clang and Windows MSVC. The macOS job includes the native Optika application build and tests.  
**Camera dependency:** `camera-v.2.0` at `757f829406c27d2f5ee25032ffd9b5e7725c56b7`; separate from this branch and not yet integrated.  
**Current C++ implementation:** `67f1c084a8650893a7827b5dc7f869ba46b88c0c`. The ownership extraction passed three-platform CI. The next geometry change makes `SlideGeometry` axis-neutral and turns `SlideGeometry2D` into a compatibility-preserving stage adapter over the canonical value; CI for this follow-up is pending.

This document is the current continuation checkpoint for the Optika B293 application/domain model. It supersedes stale descriptions in older implementation-plan sections where they conflict with the ownership rules below.

## Domain invariants

- The application is a microscope workstation, not an LMS.
- There is no Patient, Sample instance, `SampleId`, patient identity or `SlideType` entity in this microscope model.
- `SampleType` is a reusable, versioned definition of material type and microscopy/preparation requirements.
- A physical `Slide` has its own immutable `SlideId`, human-facing code, geometry, observed `SlidePreparation`, exact pinned `SampleTypeId` + revision, zero-or-more `Stain` values and optional `OriginLabId`.
- A `Stain` is descriptive identity (StainId, name, optional description). Optional `MicroscopySuitability` metadata currently carries recommended contrast methods. It is advisory only. It is not a required stain rule, preparation requirement or hard compatibility constraint.
- A Slide can have no stains, one stain or multiple stains. An identity-only stain without suitability metadata remains valid.
- `OperationMode` expresses contrast intent only: microscope identity, mode identity/revision, name and ContrastMethod. It does not own a LightPath, objective, condenser, camera, source/channel, selected component or calibration.
- `OperationModeHardwareConfiguration` is separate and optional. When present, it is bound to the exact OperationMode revision and equipment-definition revision, and contains calibrated per-LightPath settings. It does not define the meaning or validity of the mode itself.
- A connected camera is derived from the selected LightPath's physical Imaging route. Camera discovery does not depend on saved calibration data.
- No code in the camera library owns microscope-level contrast selection, SampleType or Slide policy.

## Implemented in this change set

### Slide and Stain

The Core model already represented Stain identity and optional suitability, and the Slide SQLite payload v2 already serialized Stain data. The persistence tests now exercise:
- Human-facing Slide code and optional OriginLabId.
- Multiple Stains on one physical Slide.
- StainId, name and optional description round-trip.
- Optional recommended contrast methods round-trip.
- A second identity-only Stain with no suitability metadata.

No Slide payload version bump or schema migration was needed for these existing v2 fields.

### OperatingMode recommendations

`OperatingModeRecommender` returns a ranked candidate set with evidence:
- SampleType Required/Supported/Prohibited technique constraints determine eligibility.
- SampleType Preferred techniques are admissible candidates (including alongside a Supported set) and outrank Stain suitability recommendations.
- Stain advice only contributes when SampleType provides technique context; a Stain alone cannot select a mode.
- Equal top-ranked candidates remain ambiguous. The recommender does not automatically change hardware or claim the whole microscope configuration is compatible.

The current implementation ranks ContrastMethod candidates. It does not yet perform complete objective/condenser/filter/illumination/path candidate enumeration.

### Contrast intent and hardware realization

`OperationMode` no longer owns calibrated LightPath settings. A valid mode can be created without a LightPath or calibration.

`OperationModeHardwareConfiguration` validates:
- Microscope identity.
- OperationMode identity and exact revision.
- One consistent equipment-definition revision.
- Unique LightPath setting identities.
- Non-empty setting collection.

The request factory consumes the separate hardware configuration and validates its equipment revision. The sample illumination assessor requires it only when the SampleType has specimen-plane illumination constraints. Camera discovery remains topology-driven and can resolve a physically connected camera when a hardware realization is absent or stale.

### Persistence

`MicroscopeConfigurationSqliteStore` now exposes separate save/load behavior:
- Save OperationMode intent independently.
- Optionally persist its hardware realization in the same SQL transaction.
- Load current OperationMode intent without reading calibration child rows.
- Load the optional hardware realization separately and verify every calibration binding.

Existing SQL child tables are reused; the separation is semantic and does not require a database schema migration. Existing rows remain associated with their exact mode revision.

## Verification

The following source/test corrections are included in the verified code baseline:

- Updated optical topology tests to the current intent/hardware-realization split. Camera discovery tests no longer require saved LightPath calibration.
- Updated sample microscopy assessment tests to pass `OperationModeHardwareConfiguration`, not `OperationMode`.
- Fixed the missing return type on `MicroscopeConfigurationSqliteStore::loadCurrentOperationMode`.
- Corrected PreviewFrameRateMeter test timestamps to satisfy the meter's 0.5-second sampling interval and one-second stale threshold.

The complete three-platform matrix passed on the current code baseline:
- Core standalone and Core tests.
- Application standalone and Application tests.
- Root platform-only build and tests.
- Native Optika app build/tests on macOS.

This verifies the software build/test baseline, not physical microscope hardware execution.

## Remaining work

The complete library audit and dependency-ordered completion plan are maintained in [Microscope Platform Completion Audit v0.9](MICROSCOPE_PLATFORM_COMPLETION_AUDIT_V0.9.md).

Immediate next work is not more UI:
1. Establish an explicit relationship between the four-corner physical carrier registration and CAD working-surface placement. Preserve the affine transform, measured-corner provenance, fit residuals and registration revisions; do not silently coerce the measured registration into an orthogonal CAD rectangle.
2. Integrate the camera-v2.0 module as a controlled subtree, not a wholesale branch merge. Exclude the previously rejected AcquisitionIntegrityTracker and Qt adapters/preview; add a backend-neutral provider/session API.
3. Add authoritative equipment-definition JSON loading and revision persistence, plus mode/profile enumeration and database migration ownership.
4. Build the microscope-level Application composition root, observed-state snapshot and configuration reconciler.
5. Complete stage/optical hardware adapters and operation cancellation/timeout/late-completion semantics.
6. Add the Slide-to-Tile acquisition contract and one complete single-tile capture workflow.
7. Move domain decisions and persistence behind the C++ Application API; leave SwiftUI as presentation.

## Preservation constraints

Keep camera ownership and vendor lifecycles inside the camera module; do not leak AmScope or GenTL SDK types into Microscope.Core. Do not add the rejected acquisition-integrity qualification layer or Qt UI/preview components to the Optika integration. Keep Swift as the application layer for acquisition orchestration and image/tile organization, and SwiftUI views as presentation. The C++ Application API owns microscope-domain validation, equipment configuration, hardware orchestration and its persistence; do not duplicate those rules in Swift views. The physical dual-slide carrier remains two horizontal slots with four-corner registration per slide. ImageProcessing remains a separate project.
