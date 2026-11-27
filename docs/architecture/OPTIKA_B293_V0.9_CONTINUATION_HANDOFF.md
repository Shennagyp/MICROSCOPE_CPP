# OPTIKA B293 v0.9 — Continuation Handoff

**Repository:** `Shennagyp/MICROSCOPE_CPP`  
**Target branch:** `OPTIKA-B293-DUALSLIDE-APP-v.0.9`  
**Prepared commit:** `dd1528af3548ce8ce70571c65b2cde3e6099ec02` (before this handoff document is committed)  
**Verification:** source and test changes are authored; the updated branch has not yet been advanced and CI has not yet run for this change set.

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
- SampleType Preferred technique evidence outranks Stain suitability recommendations.
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

## Tests authored; CI pending

New/updated Core tests cover:
- Intent-only OperationMode construction.
- Hardware realization exact mode/equipment revision checks, duplicate LightPath rejection and multi-LightPath values.
- Camera discovery independent of stale calibration; request creation rejects stale hardware realization.
- SampleType hard/preferred technique precedence over Stain advice.
- Multiple stains, identity-only stains, stain-only recommendation rejection and ambiguous ties.
- Slide/SampleType identity boundaries.

Application tests cover:
- Stained Slide payload round-trip.
- Separate OperationMode intent and hardware-realization persistence.
- Intent-only mode persistence with no hardware realization.
- Calibration binding rejection and optimistic revision conflicts.

These are authored and registered test sources, not passing-test evidence. The v0.9 branch must be advanced to the final commit, then GitHub Actions must build and execute the full CTest suite on the supported matrix. Native macOS application build/test must be checked separately from Core/Application CTest.

## Remaining work, in dependency order

1. Advance the target branch only after verifying its current ref still equals the original parent `7d295e9d885914ab4de3e42e1f13e09e0439f9f2`; do not force-push.
2. Run CI. Fix compiler errors and contract-test failures before adding more scope. Confirm Linux GCC, Windows MSVC and macOS Clang/Core tests, then the native OPTIKA app job.
3. Recheck all OperatingMode factory, request-factory and assessor call sites for old embedded-hardware assumptions.
4. Complete whole-configuration candidate discovery: objective, condenser, filter/optical function, illumination and LightPath alternatives must be resolved as coherent physical configurations. The current resolver remains selection-driven.
5. Extend the calibrated optical-state fingerprint beyond component IDs to the actual adjustable route/optical settings and measurement provenance.
6. Persist the full microscope equipment constitution and its revisions. Revalidate calibration/hardware realizations against this authoritative definition.
7. Implement actual optical device command/observation/verification adapters. Core resolution and a declarative transition DAG do not prove that physical hardware changed.
8. Integrate the recommendation service into the Application workflow/UI as a recommendation with reasons and explicit selection, not a hidden mode switch.
9. Complete stage-controller lifecycle work (controlled cancellation/stop, timeout/deadline handling, verified idle and late-completion reconciliation) and connect a concrete adapter only after the installed controller protocol is specified.

## Preservation constraints

Keep the existing AmScope camera ownership, SDK lifecycle, bounded acquisition, frame-lease and preview behavior intact. Do not replace it with a simulated controller path. Keep the native SwiftUI layer as presentation/composition over Core/Application; do not move domain validation or SQLite ownership into views. The dual-slide carrier remains two horizontal physical slots with four-corner registration per slide.
