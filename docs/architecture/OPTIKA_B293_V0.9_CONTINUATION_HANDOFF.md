# OPTIKA B293 v0.9 — Continuation Handoff

**Repository:** `Shennagyp/MICROSCOPE_CPP`  
**Target branch:** `OPTIKA-B293-DUALSLIDE-APP-v.0.9`  
**Latest source/test commit:** `1b5d154c4c3ca7cced30917f75888040d8c8fc56`  
**Architecture audit commit:** `cf24ebd9672a84dec9050ef1c5e1e81eb331049c`  
**Verification:** [run 45958819531](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45958819531) completed successfully on Ubuntu GCC, macOS Clang and Windows MSVC. The macOS job also built and tested the native Optika B293 application. The Application suite passed on all three platforms; Ubuntu CTest reported 52/52 passing tests, including `microscope_application_equipment_definition_store_tests`. The root platform-only composition also built and tested on all three platforms.  
**Camera dependency:** `camera-v.2.0` at `757f829406c27d2f5ee25032ffd9b5e7725c56b7`; separate from this branch and not yet integrated.  
**Current C++ implementation:** `1b5d154c4c3ca7cced30917f75888040d8c8fc56`. In addition to the verified Core ownership/geometry work and current OperationMode enumeration, the change set adds `Microscope.Application::SlideRegistrationWorkingSurfaceComparison` and `StageWorkingSurfaceSqliteStore`. The comparison binds the physical four-corner registration and CAD rectangle only through an explicit cyclic/reflected corner correspondence, checks stage and axis identity, and reports per-corner deltas, correspondence-aware geometry extents, both revision IDs, measurement provenance/uncertainty and registration fit residuals. Its immutable snapshot preserves the original affine mapping and CAD handle/constraint data. The SQLite store persists CAD surface boundaries, axis identities, slide rectangle constraints and explicit corner mappings in append-only equipment-revision-bound snapshots; it uses an optimistic current-revision check and reloads through the installed `StageSpecification`. Physical measured corners remain exclusively in `DualSlideCarrierSqliteStore`. CI run [45953056200](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45953056200) is pending.

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

The static equipment-definition boundary is implemented and now three-platform verified:

- `EquipmentDefinitionJsonLoader` accepts schema version 1 and reconstructs the full `MicroscopeEquipmentDefinition` through Core factories. It rejects invalid UTF-8, duplicate keys, unknown fields, unsupported schema versions, oversized/deep documents, malformed values and invalid physical topology. The schema contract is documented in `EQUIPMENT_DEFINITION_JSON_SCHEMA_V1.md`.
- `EquipmentDefinitionSqliteStore` persists the exact validated source JSON in a SQLite `TEXT` column. The API receives an explicit `MicroscopeId`, checks it against the ID embedded in the parsed document, and requires the submitted equipment revision to equal `expectedCurrentRevision + 1`.
- The schema uses separate immutable revision and current-pointer tables. SQL triggers enforce sequential revision insertion, prohibit revision updates/deletes, require the initial current revision to be 1, permit the current pointer to advance only by one, and prohibit deleting that pointer. Save uses `BEGIN IMMEDIATE` to append the revision and advance the current pointer atomically.
- Load copies `sqlite3_column_text` bytes while the statement is still positioned on its row, before advancing/finalizing the statement. It then re-runs the strict loader and checks the embedded microscope identity and revision against the database key. Structured `EquipmentDefinitionJsonError` details are retained by the store error.
- The store owns its local schema metadata for now. This is not the central shared database/migration owner; store connections and schema versions remain independently owned.

The dedicated test target covers exact source-document round-trip, current and historical revision loading, sequential revision enforcement, stale-current conflicts, microscope identity mismatch, immutable revision/pointer triggers, malformed documents and controlled corruption rejection. The test passed in the Application suite on Ubuntu GCC, macOS Clang and Windows MSVC in run 45958819531.

The ConfigurationProfileCatalog store is a separate binary persistence boundary: it stores immutable catalog revisions in a BLOB using explicit little-endian tags for contrast methods, optical functions, path roles and illumination units. It reconstructs through Core factories, rejects unknown/malformed payload values, copies BLOB bytes while the SQLite row is active, and loads current/historical catalogs scoped to microscope + equipment-definition revision. Its earlier three-platform run is 45954245119.

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

This store does not persist or duplicate physical corner measurements. Those remain owned by `DualSlideCarrierSqliteStore`. The Application comparison joins the two stores after loading using physical `SlideId`, stage component/axis identity and explicit corner correspondence. The comparison, CAD persistence and equipment-definition persistence tests are all included in successful three-platform run [45958819531](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45958819531).

## Verification

The current source/test baseline `1b5d154c4c3ca7cced30917f75888040d8c8fc56` passed [run 45958819531](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45958819531) on Ubuntu GCC, macOS Clang and Windows MSVC. All three jobs passed Core build/tests, Application build/tests, and root platform-only build/tests. macOS additionally passed native Optika B293 configure/build/tests. Ubuntu's Application CTest invocation reported 52/52 passing tests; the equipment-definition JSON loader and SQLite store tests passed in the Application suite on all three platforms.

During validation, CI exposed and the branch corrected three issues: the SQLite text-bind helper returned the inverse of `SQLITE_OK`; the equipment-store CMake test target had been registered twice; and a JSON test fixture used a `const char*` anchor while calling `.size()`. The corrected source and tests are included in the passing run.

CI verifies software build/tests, not physical microscope hardware execution.

## Remaining work

The complete library audit and dependency-ordered completion plan are maintained in [Microscope Platform Completion Audit v0.9](MICROSCOPE_PLATFORM_COMPLETION_AUDIT_V0.9.md).

Immediate next work is not more UI:
1. Implement the central SQLite database owner with one shared connection and serialized connection mutex, dependency-ordered migrations and genuine cross-store transaction semantics. The existing stores still open independent SQLite connections; this refactor must change their connection ownership, not just wrap their current factories.
2. Build the microscope-level Application composition root, revision-bound equipment owner and UI-facing immutable microscope snapshot.
3. Implement observed-state reconciliation for optical/configuration hardware: skip already-satisfied settings, execute only required actions, preserve operation identity/deadlines and verify resulting state.
4. Complete concrete stage-controller and optical-component adapters, including cancellation, timeout, late-completion and verified-idle semantics.
5. Integrate camera-v2.0 only after the C++ platform contract is stable, as a controlled subtree rather than a wholesale branch merge. Exclude AcquisitionIntegrityTracker and Qt adapters/preview; preserve Common abstraction over AmScope and GenTL.
6. Move automatic-resolution policy and objective/exposure coordination behind Core/Application contracts, using explicit per-axis camera output mapping and configurable optical data.
7. Integrate the existing Swift acquisition coordinator with authoritative physical `SlideId`, camera sessions and stage/optical services. Keep capture orchestration, image/tile organization, persistence and gallery lifecycle in Swift; do not introduce a duplicate C++ acquisition workflow.

## Preservation constraints

Keep camera ownership and vendor lifecycles inside the camera module; do not leak AmScope or GenTL SDK types into Microscope.Core. Do not add the rejected acquisition-integrity qualification layer or Qt UI/preview components to the Optika integration. Keep Swift as the application layer for acquisition orchestration and image/tile organization, and SwiftUI views as presentation. The C++ Application API owns microscope-domain validation, equipment configuration, hardware orchestration and its persistence; do not duplicate those rules in Swift views. The physical dual-slide carrier remains two horizontal slots with four-corner registration per slide. ImageProcessing remains a separate project.
