# OPTIKA B293 v0.9 — Continuation Handoff

**Repository:** `Shennagyp/MICROSCOPE_CPP`  
**Target branch:** `OPTIKA-B293-DUALSLIDE-APP-v.0.9`  
**Latest source/test commit:** `d828f4d8857a82f4fba6ec562516b58ac35c1118`  
**Architecture audit commit:** `c43a12577220ea0318112eb2c88e75bd1c559b9f`  
**Verification:** [run 45954245119](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45954245119) passed the ConfigurationProfileCatalog SQLite store on Ubuntu GCC, macOS Clang and Windows MSVC. [Run 45953056200](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45953056200) passed the CAD comparison/working-surface persistence change across the same platform matrix. The strict JSON equipment loader reached passing Ubuntu and macOS jobs, but its workflow was cancelled during subsequent commits before a complete three-platform result. The new equipment-definition SQLite store, tests and CMake registration are queued in [run 45957906429](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45957906429); do not mark this new store verified until that exact source commit has completed all applicable jobs.  
**Camera dependency:** `camera-v.2.0` at `757f829406c27d2f5ee25032ffd9b5e7725c56b7`; separate from this branch and not yet integrated.  
**Current C++ implementation:** `bbc426463866950883248ebff8dbf04c71051412`. In addition to the verified Core ownership/geometry work and current OperationMode enumeration, the change set adds `Microscope.Application::SlideRegistrationWorkingSurfaceComparison` and `StageWorkingSurfaceSqliteStore`. The comparison binds the physical four-corner registration and CAD rectangle only through an explicit cyclic/reflected corner correspondence, checks stage and axis identity, and reports per-corner deltas, correspondence-aware geometry extents, both revision IDs, measurement provenance/uncertainty and registration fit residuals. Its immutable snapshot preserves the original affine mapping and CAD handle/constraint data. The SQLite store persists CAD surface boundaries, axis identities, slide rectangle constraints and explicit corner mappings in append-only equipment-revision-bound snapshots; it uses an optimistic current-revision check and reloads through the installed `StageSpecification`. Physical measured corners remain exclusively in `DualSlideCarrierSqliteStore`. CI run [45953056200](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45953056200) is pending.

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

## Equipment-definition JSON and SQLite persistence

The static equipment-definition boundary is now implemented in two layers:

- `EquipmentDefinitionJsonLoader` accepts schema version 1 and reconstructs the entire `MicroscopeEquipmentDefinition` through Core factories. It rejects invalid UTF-8, duplicate keys, unknown fields, unsupported schema versions, oversized/deep documents, malformed values and invalid physical topology. The input is static equipment data only; runtime observations, device sessions, calibration measurements and acquisition state are intentionally excluded.
- `EquipmentDefinitionSqliteStore` persists the exact validated UTF-8 JSON source document as a SQLite BLOB. The database key is the embedded `MicroscopeId` plus the Core equipment revision. Revisions are immutable, a current pointer is advanced with an expected-current-revision check inside `BEGIN IMMEDIATE`, and a new revision must increase monotonically.
- Loads copy the BLOB while the SQLite statement is still positioned on its row, then re-run the strict loader. The store verifies that the reconstructed microscope identity and revision match the database key before returning the definition. This preserves the original document for diagnostics/export without maintaining a second serializer for the equipment graph.
- Store metadata/schema version and payload format version are explicit. This is a per-store schema owner, not the central database migration owner; the other stores still own their individual SQLite schema metadata.

The new store test target covers exact source-document round-trip after reopening, current/historical revision loading, independent microscope identities, stale-writer conflict, monotonicity, and invalid-document rejection. The test target is registered in the Application CMake project. The three-platform CI run is pending as linked above.

The ConfigurationProfileCatalog store is also implemented and verified: it persists immutable catalog revisions scoped to microscope and exact equipment-definition revision, uses explicit little-endian payload tags for enums/units, reconstructs values through Core factories, and loads both current and historical revisions. Its successful platform run is 45954245119.

## Physical carrier registration and CAD working-surface comparison

The fixed two-slot carrier and the CAD working surface remain separate Core models. The Application comparison joins them only for a specific physical `SlideId` and an explicit `SlideCornerCorrespondence`.

- The correspondence array is ordered by measured `TopLeft`, `TopRight`, `BottomRight`, `BottomLeft`; values identify CAD `StopPoint`, `LengthEdgeEnd`, `OppositeCorner`, or `WidthEdgeEnd`. Its cyclic winding determines whether measured local X corresponds to CAD local X (length) or local Y (width), so extent deltas follow the actual orientation.
- The factory accepts only a one-to-one cyclic rotation or reflection. It does not infer the stop point from the carrier slot.
- The comparison validates stage-component identity and the two physical stage-axis IDs. It handles the case where carrier horizontal/transverse axes are the reverse of working-surface X/Y axes by reordering coordinates using axis identity.
- It compares raw measured corners with the selected CAD corners and reports signed CAD-minus-measured X/Y deltas, per-corner distance, maximum and RMS discrepancy, and physical-versus-CAD X/Y extent deltas.
- Each corner result retains the original `SlideCornerMeasurement`, including its source and uncertainty. The comparison also retains the complete immutable `SlideRegistration` and `SlideRectangleDefinition2D` source values. Therefore the affine `toStage`/`toSlide` behavior, registration fit residuals, CAD draw handle, constrained dimensions and revision IDs are not flattened into the comparison.
- Physical-registration and CAD-layout revisions are reported independently. A mismatch sets `revisionsAligned() == false`; it does not prevent a diagnostic comparison.
- A comparison is not a recalibration, does not rewrite either source model and does not create a motion command.

### CAD working-surface persistence

`StageWorkingSurfaceSqliteStore` persists the CAD model separately from the physical carrier-registration store.

- The revision key is scoped by `MicroscopeId`, equipment-definition revision and stage component identity.
- The parent revision stores stage X/Y axis IDs and the working-surface minimum/maximum boundaries.
- One immutable child row per CAD slide stores its physical `SlideId`, the CAD rectangle's revision ID, stop-point coordinates, original drawn handle, length-axis assignment, constrained length/width, and the explicit four-corner correspondence.
- Correspondence values use stable explicit SQLite tags and are rebuilt through `SlideCornerCorrespondence::create`; corrupted or non-cyclic mappings are rejected.
- Save requires exactly one binding for every rectangle, validates the layout against the supplied installed `StageSpecification`, and uses `expectedCurrentRevision` with a SQLite write transaction. Parent/child historical rows cannot be updated/deleted; the current pointer advances monotonically.
- Load reconstructs rectangles and validates current stage axes, coordinate units and travel ranges before returning the snapshot. An equipment/stage mismatch is not silently remapped.

This store does not persist or duplicate physical corner measurements. Those remain owned by `DualSlideCarrierSqliteStore`. The Application comparison joins the two stores after loading using physical `SlideId`, stage component/axis identity and explicit corner correspondence. The new store and cross-model tests are in CI run [45952613208](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45952613208), which is still pending.

## Verification

The following source/test corrections are included in the preceding verified code baseline:

- Updated optical topology tests to the current intent/hardware-realization split. Camera discovery tests no longer require saved LightPath calibration.
- Updated sample microscopy assessment tests to pass `OperationModeHardwareConfiguration`, not `OperationMode`.
- Fixed the missing return type on `MicroscopeConfigurationSqliteStore::loadCurrentOperationMode`.
- Corrected PreviewFrameRateMeter test timestamps to satisfy the meter's 0.5-second sampling interval and one-second stale threshold.

The preceding complete three-platform matrix passed on Core/Application/native-app source baseline `9f7b4f708c15b6635d8f67276d493a33e06ba7ad` in run [45943921361](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45943921361). The later comparison feature reached passing Core/Application/root build and test stages on Ubuntu and macOS; Windows also passed Core and Application build/tests before its root stage. The current CAD persistence change, including the post-publication insert guard, is being validated by run [45953056200](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45953056200). Do not claim that current commit is verified until all jobs finish.

CI verifies software build/tests, not physical microscope hardware execution.

## Remaining work

The complete library audit and dependency-ordered completion plan are maintained in [Microscope Platform Completion Audit v0.9](MICROSCOPE_PLATFORM_COMPLETION_AUDIT_V0.9.md).

Immediate next work is not more UI:
1. Verify the equipment-definition JSON + SQLite store on Ubuntu GCC, Windows MSVC, macOS Clang and the native Optika build; fix compiler/test failures before further persistence work.
2. Implement a central SQLite database owner with shared connection ownership, ordered migrations and real cross-store transaction semantics. Do not substitute an orchestrator that merely opens independent stores and cannot make initialization atomic.
3. Build the microscope-level Application composition root, revision-bound equipment owner and UI-facing immutable microscope snapshot.
4. Implement observed-state reconciliation for optical/configuration hardware: skip already-satisfied settings, execute only required actions, preserve operation identity/deadlines and verify resulting state.
5. Complete concrete stage-controller and optical-component adapters, including cancellation, timeout, late-completion and verified-idle semantics.
6. Integrate camera-v2.0 only after the C++ platform contract is stable, as a controlled subtree rather than a wholesale branch merge. Exclude AcquisitionIntegrityTracker and Qt adapters/preview; preserve Common abstraction over AmScope and GenTL.
7. Integrate the existing Swift acquisition coordinator with authoritative physical `SlideId`, camera sessions and stage/optical services. Keep capture orchestration, image/tile organization, persistence and gallery lifecycle in Swift; do not introduce a duplicate C++ acquisition workflow.

## Preservation constraints

Keep camera ownership and vendor lifecycles inside the camera module; do not leak AmScope or GenTL SDK types into Microscope.Core. Do not add the rejected acquisition-integrity qualification layer or Qt UI/preview components to the Optika integration. Keep Swift as the application layer for acquisition orchestration and image/tile organization, and SwiftUI views as presentation. The C++ Application API owns microscope-domain validation, equipment configuration, hardware orchestration and its persistence; do not duplicate those rules in Swift views. The physical dual-slide carrier remains two horizontal slots with four-corner registration per slide. ImageProcessing remains a separate project.
