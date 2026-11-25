# Development Line — Slide Scanning

**Active implementation repository:** [MICROSCOPE_CPP](https://github.com/Shennagyp/MICROSCOPE_CPP)  
**Active implementation branch:** [`OPTIKA-B293-DUALSLIDE-APP-v.0.9`](https://github.com/Shennagyp/MICROSCOPE_CPP/tree/OPTIKA-B293-DUALSLIDE-APP-v.0.9)  
**Source checkpoint inspected:** `c7bb4b4dc41441989d71ee800c6d33b51dc54abe` (25 November 2026)  
**Historical/reference repository:** [MICROSCOPE](https://github.com/Shennagyp/MICROSCOPE), Manager `refactor-2` and Station `station-development`  
**Line status in the v0.9 target:** slide presentation/geometry exists; the Sample/Slide scan-planning and physical acquisition workflow is not established.

Slide Scanning is a separate application development line from Controller/Motion/Stage and Camera. All three currently share the OPTIKA v0.9 implementation branch, but they have separate ownership, milestones, tests and acceptance criteria.

## 1. Responsibility and boundaries

Slide Scanning owns the workflow that turns an identified Slide and an approved scan request into a planned, executed and traceable acquisition result.

Its scope includes:
- Sample and Slide identity and their relationship;
- per-Slide scan requirements and requested acquisition configuration;
- scan-plan/run lifecycle and coverage/position planning;
- acquisition sequencing and the association of each captured image with its Slide and scan context;
- progress, pause/cancel/stop and terminal-result semantics at the workflow level;
- provenance linking acquired image data to Slide, scan/run, microscope, configuration, calibration and position;
- reporting the achieved result separately from the requested plan.

It does **not** own:
- camera device discovery, camera sessions, native SDK handles, frame buffers or the capture implementation — those belong to the single Camera line;
- controller transport, physical motion, sensor/endstop access or stage mechanics — those belong to the Controller/Motion/Stage line;
- image-processing algorithms, provider execution or GPU resource lifetime — those belong to the separate ImageProcessing repository;
- general microscope configuration semantics or low-level operation lifecycle reducers — those belong to Microscope Core/Application.

The OPTIKA application composes these capabilities. Scan policy must call through stable application/Core contracts and must not reach into vendor camera or controller APIs.

## 2. Core workflow invariants

Preserve these domain invariants while deciding the concrete v0.9 application model:

1. A Sample may contain multiple Slides.
2. Each Slide belongs to exactly one Sample.
3. A scan request targets one specific Slide. A multi-Slide Sample is not itself an implicit scan target.
4. Each scan plan/run has a stable identity and a clear relationship to its target Slide.
5. A requested scan configuration and the achieved execution configuration are different facts.
6. The workflow may request physical positioning and image capture, but it does not claim either occurred until the corresponding capability reports evidence.
7. Every acquired image must retain provenance to its Slide and scan/run, microscope configuration, calibration revision and relevant stage position/placement.
8. A missing frame, uncertain position, interrupted run or unconfirmed stop must remain an explicit incomplete/unknown outcome; it must not be recorded as successful coverage.
9. Camera acquisition and stage movement have separate lifecycles. Scan orchestration coordinates them without taking ownership of their internal device sessions.
10. Image processing consumes acquired image assets/documents through a defined boundary; it does not become part of the camera driver or scan planner.

The earlier Manager/Station system also established per-Slide qualification/allocation and the distinction between Manager stop authority and Station-confirmed physical stop. Retain those as historical requirements where relevant, but do not automatically introduce a distributed Manager/Station runtime into the OPTIKA desktop application.

## 3. Development history

### Earlier MICROSCOPE Manager line — historical reference

The .NET Manager work on `refactor-2` developed:
- Sample/Slide intake, identity and slot occupancy;
- per-Slide scan requirements;
- Functional Qualification Pipeline (FQP) evaluation and microscope allocation per Slide;
- durable ScanTask assignment, task state and Manager-issued stop orders;
- execution reports and metadata-only Gallery/DICOM references;
- operator workflow and query projections.

The earlier model allowed Slides from one Sample to be allocated independently across microscopes/Stations. Manager owned canonical shared identity and coordination, not camera sessions, physical movement or image bytes.

### Earlier MICROSCOPE Station line — historical reference

The Swift Station work on `station-development` developed:
- local microscope and camera registration;
- local Sample/Slide workspace and placement/observation records;
- capture-to-Slide-observation commit;
- original image persistence, integrity checks and recovery/retry boundaries;
- local Gallery and measurement groundwork;
- partial Manager registration-projection contracts.

Station owned physical execution and local image assets. The Manager and Station were intentionally independent; their complete bridge and physical end-to-end scan workflow were not accepted.

These two branches provide useful domain history and failure/ownership lessons. They are not the active implementation location for the Slide Scanning line now being continued in MICROSCOPE_CPP. Do not assume their Manager/Station split is the architecture of the OPTIKA v0.9 application without an explicit design decision.

## 4. Current OPTIKA v0.9 source status

The checked v0.9 source contains slide-related presentation and geometry elements, including:
- `Application/OptikaB293/Sources/Workspace/NativeSlideID.swift`;
- `Application/OptikaB293/Sources/Features/Slides/SlideImageGeometry.swift`;
- `Application/OptikaB293/Sources/Features/Slides/SlideOverlayModels.swift`;
- slide preview and image presentation components;
- `SlideImageGeometryTests`.

The source also contains the microscope Core Stage specification layer and the Phase 4A operation/step lifecycle reducer.

These elements establish slide presentation/geometry and related microscope semantics. They do **not** establish:
- a persisted Sample aggregate and Sample-to-Slide relationship in the v0.9 Core;
- a canonical persisted Slide entity and lifecycle;
- ScanRequest, ScanPlan, ScanRun or acquisition-task contracts;
- scan coverage, tile/position planning or a complete traversal strategy;
- acquisition orchestration joining stage movement and camera capture;
- restart/recovery semantics for partially completed scans;
- persistent original-image and scan-result provenance owned by the target v0.9 architecture;
- a verified physical scan or a complete scan-result workflow.

A slide identifier, preview surface, overlay, Stage specification or generic Core operation state is not itself a Slide Scanning implementation.

## 5. Relationship to the other development lines

| Dependency line | Provides to Slide Scanning | Slide Scanning must not do |
|---|---|---|
| Microscope application/Core | Product composition, domain contracts, operation lifecycle and use-case boundary | Create a parallel Core lifecycle or hide policy in UI state |
| Camera | Camera session, acquisition command path, immutable captured-frame/image asset and capture facts | Open a second camera session or own frame buffers |
| Controller/Motion/Stage | Capability-qualified positioning, motion execution and physical/observed stage state | Call vendor controller APIs or infer physical position from a requested move |
| ImageProcessing | Image-document and processing operations through a separate library/API boundary | Copy algorithm/provider execution into the scan workflow |

The dependency direction is deliberate: Slide Scanning orchestrates capabilities; it does not absorb them. Camera and Controller remain sibling workstreams. Neither line is complete merely because the scan workflow has a mock or a UI.

## 6. v0.9 roadmap alignment

The v0.9 implementation plan places related work in distinct application/Core phases:

- **Phase 4 — operation runtime:** typed commands, adapter ports, feedback ingress, execution and verification lifecycle. This is a prerequisite for reliable hardware-backed scan execution.
- **Phase 6 — calibration:** establishes calibration semantics needed to interpret optical/stage positions and acquisition provenance.
- **Phase 7 — Sample, Slides and persistence:** the principal domain/persistence foundation for this workstream.
- **Phase 8 — acquisition:** plan/run execution and integration with the Camera and Controller/Motion/Stage capability lines.
- **Phase 9 — image documents and annotations:** image-document semantics and downstream annotation, with ImageProcessing integration handled through an explicit boundary.

The Slide Scanning line remains separately tracked while these shared platform phases are implemented. Do not mark a v0.9 phase complete because a historical Manager or Station branch had analogous source.

## 7. Verification and acceptance boundary

Evidence must be labelled by what it proves:

- Slide presentation/geometry tests prove their own deterministic contracts, not persistence or scanning.
- Core lifecycle tests prove the state-transition contract, not execution against hardware.
- A fake camera/controller or simulated scan proves orchestration against that fake only.
- Camera frame-delivery evidence proves camera acquisition for that tested setup, not stage movement or scan coverage.
- A controller movement test proves only the exact device, axes, ranges, calibration and safety conditions exercised.
- A scan workflow is not accepted until requested coverage, actual movement/position observations, captured image provenance, interruptions/recovery and terminal scan result have been verified together.

The checked v0.9 baseline does not demonstrate a physical Slide Scanning workflow. No complete sample-to-scan-to-result end-to-end acceptance should be claimed.

## 8. Continuation handoff

**Active implementation branch:** `MICROSCOPE_CPP / OPTIKA-B293-DUALSLIDE-APP-v.0.9`  
**Current target state:** slide UI/geometry groundwork exists; Sample/Slide persistence and scan workflow are not yet established.  
**Roadmap dependency:** Phase 7 Sample/Slide persistence and Phase 8 acquisition, with Phase 4 runtime contracts and Controller/Camera capabilities as prerequisites.

### Next bounded task

Establish the target v0.9 Sample/Slide/Scan domain contract before building a scan UI or physical scan loop:

1. Read `docs/plans/MICROSCOPE_PLATFORM_IMPLEMENTATION_V0.9.md`, `docs/architecture/MICROSCOPE_CORE_APPLICATION_CONTRACTS_V0.9.md` and `docs/architecture/MICROSCOPE_PLATFORM_ARCHITECTURE_V0.9.md`.
2. Inspect current `NativeSlideID`, `SlideImageGeometry`, `SlideOverlayModels`, Core strong IDs, persistence plans and the existing Gallery/capture boundary. Identify which current UI types are presentation-only.
3. Extract the reusable Sample/Slide/Scan invariants from the historical Manager/Station design. Explicitly decide which ideas apply to a local OPTIKA workstation and which depended on the old distributed architecture.
4. Define stable Sample and Slide identities, the one-Sample-per-Slide ownership rule, and the identity/lifecycle of a per-Slide scan request, plan and run.
5. Define requested-versus-achieved configuration and the minimum provenance required for every captured image.
6. Define the workflow boundary against Core Phase 4 command/feedback semantics, Camera acquisition and Controller/Motion/Stage operations. Do not invent independent hardware states inside the scan feature.
7. Define persistence/recovery requirements for partial runs before implementing a long-running scan executor.
8. Add contract tests first. Keep simulated orchestration results explicitly separate from camera, controller and physical scan acceptance.

Do not start by adding a tile loop or a scan button. First make Sample, Slide, ScanRequest/Plan/Run identity, lifecycle, provenance and recovery semantics internally consistent.

### Handoff update requirements

After each substantive slice, record:
- exact MICROSCOPE_CPP commit and branch;
- Sample, Slide, ScanPlan, ScanRun or workflow contract changed;
- identity, ownership and persistence decisions;
- Camera and Controller/Motion/Stage contracts consumed;
- tests written and actual CI result for that exact commit;
- whether execution was simulated, camera-backed, controller-backed or physically scanned;
- exact hardware, coverage, calibration, provenance and recovery evidence;
- unresolved behavior and next bounded task.

Do not report Sample/Slide UI, geometry tests, frame preview, a fake scan or a successful camera stream as physical slide-scanning acceptance.

## 9. Historical authoritative records

These links describe prior Manager/Station design and implementation. They are reference material, not the current v0.9 implementation branch:

### Manager — `refactor-2`

- [Manager milestones](https://github.com/Shennagyp/MICROSCOPE/blob/refactor-2/docs/distributed-application-milestones.md)
- [Manager continuation handoff](https://github.com/Shennagyp/MICROSCOPE/blob/refactor-2/docs/dotnet-continuation-handoff.md)
- [Algorithm and test coverage](https://github.com/Shennagyp/MICROSCOPE/blob/refactor-2/verification/ALGORITHM_COVERAGE.md)

### Station — `station-development`

- [Station milestones](https://github.com/Shennagyp/MICROSCOPE/blob/station-development/docs/station-development-milestones.md)
- [Station continuation handoff](https://github.com/Shennagyp/MICROSCOPE/blob/station-development/docs/station-continuation-handoff.md)
- [Station milestone acceptance records](https://github.com/Shennagyp/MICROSCOPE/blob/station-development/docs/station-milestone-acceptance-records.md)
- [Station runtime model](https://github.com/Shennagyp/MICROSCOPE/blob/station-development/docs/station-microscope-runtime-model.md)

## 10. Active OPTIKA source references

- [OPTIKA v0.9 source branch](https://github.com/Shennagyp/MICROSCOPE_CPP/tree/OPTIKA-B293-DUALSLIDE-APP-v.0.9)
- [Slide presentation and feature source](https://github.com/Shennagyp/MICROSCOPE_CPP/tree/OPTIKA-B293-DUALSLIDE-APP-v.0.9/Application/OptikaB293/Sources)
- [v0.9 implementation plan](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/OPTIKA-B293-DUALSLIDE-APP-v.0.9/docs/plans/MICROSCOPE_PLATFORM_IMPLEMENTATION_V0.9.md)
- [Core/Application contracts](https://github.com/Shennagyp/MICROSCOPE_CPP/blob/OPTIKA-B293-DUALSLIDE-APP-v.0.9/docs/architecture/MICROSCOPE_CORE_APPLICATION_CONTRACTS_V0.9.md)
