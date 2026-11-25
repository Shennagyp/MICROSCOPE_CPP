# Workstream — OPTIKA Microscope Application

**Product:** OPTIKA B293 native macOS microscope workstation  
**Repository:** [Shennagyp/MICROSCOPE_CPP](https://github.com/Shennagyp/MICROSCOPE_CPP)  
**Management branch:** \`project_manager\`  
**Source baseline:** created from \`OPTIKA-B293-DUALSLIDE-APP-v.0.9\` at \`6fdff2ac3907c22a35687606bf05d0cf7b150ce8\`  
**Latest source CI evidence reviewed:** operation lifecycle source \`c730b568a8eb43ef9ba55b93c966928041679f86\`, run [45639182251](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45639182251), success.

## 1. Product definition

The OPTIKA B293 application is a microscope workstation, not merely a camera viewer. It composes camera acquisition with microscope equipment/configuration semantics, a dual-slide workspace, image presentation and future acquisition workflows.

The target architecture separates:
- \`Microscope.Core\`: microscope identity/specifications, equipment and optical topology, configuration resolution, operation/step semantics, evidence evaluation, calibration and future scientific image/slide semantics.
- \`Microscope.Application\`: definition loading, adapter composition, runtime ownership, application use cases and a frontend-neutral interface.
- **Native macOS frontend:** SwiftUI/AppKit presentation and user interaction.
- **Camera capability:** existing AmScope native camera subsystem, preserved as an independent provider boundary.

The application frontend must not become the owner of microscope semantics or call vendor hardware APIs directly. Core must not depend on SwiftUI/AppKit, Qt, JSON, vendor SDKs or persistence implementations.


### Separate development lines on the shared v0.9 branch

The following lines share the `OPTIKA-B293-DUALSLIDE-APP-v.0.9` implementation branch but are not one undifferentiated feature stream:

| Line | Owns | Does not own |
|---|---|---|
| Microscope application | Core/Application composition, product use cases, frontend integration and cross-capability orchestration | Vendor camera/controller sessions or image-processing implementation |
| Camera | The single camera subsystem: device/session, capture and frame delivery | Microscope configuration, controller movement or scan-plan policy |
| Controller / Motion / Stage | Controller provider/session/feedback, movement semantics and microscope-specific stage topology | Sample/Slide scan lifecycle or camera acquisition |
| Slide Scanning | Sample/Slide workflow, per-Slide scan plan/run, coverage and acquisition provenance | Camera SDK, controller transport, stage movement implementation or image-processing algorithms |
| ImageProcessing | Separate image-processing library in `Shennagyp/ImageProcessing` | Microscope hardware ownership or scan orchestration |

The Controller and Slide Scanning lines are now continued in this OPTIKA v0.9 branch. Their earlier implementation/design history in the separate `MICROSCOPE` repository (`station-development` and `refactor-2`) remains reference material; it is not the active source location and must not be counted as already ported. A shared branch does not merge their milestones or acceptance criteria.

The workstream records are maintained separately: [Controller/Motion/Stage](controller.md) and [Slide Scanning](slide-scanning.md). The application line integrates their contracts but does not absorb their responsibilities.

## 2. Development history

### Camera-driver foundation to reusable camera subsystem

The application history grew out of the AmScope implementation:
- \`amscope-native-driver-0.3\` and \`0.4\`: native acquisition/session and feature-access foundation.
- \`0.5\`: diagnostic probes and controlled MU503 evidence; diagnostic vendor-wait code remained separate from the production acquisition implementation.
- \`0.6\`: \`Camera::Common\`, typed frame/feature contracts and Qt streaming separation.
- \`0.7\`: reusable Qt Widgets extraction, including \`FrameView\`, \`CameraControlPanel\` and \`ImageInfoBar\`.

### Native application

- \`amscope-native-app-mac.v.0.8\`: native macOS application composition around the camera subsystem.
- \`amscope-native-app-mac.v.0.8-docksystem\`: document-oriented workspace, tool windows, docking/persistence and application-level controls.

### OPTIKA B293 / v0.9 refactor

The lineage continues through:
- \`OPTIKA-B293-DUALSLIDE-APP\`
- \`OPTIKA-B293-DUALSLIDE-APP-v.0.2\`
- \`OPTIKA-B293-DUALSLIDE-APP-v.0.9\`

The v0.9 refactor moves application ownership outside \`Camera/\`, extracts pure application/microscope calculations, separates camera bridge and workspace responsibilities, and begins the independent C++ Core/Application libraries. The latest v0.9 commits close configuration planning and add Core operation/step lifecycle transitions.

This history is a continuous evolution from driver-oriented work to a microscope application architecture. It does not mean every earlier UI is already retired or that every target Core feature is implemented.

## 3. Current implementation

### Application boundary and source organization

The latest source tree contains:
- \`Application/OptikaB293\`: native application target, application adapters, features, presentation and tests.
- \`Camera/Amscope\`: native camera driver/provider and supporting abstractions.
- \`Libraries/Microscope.Core\`: standalone microscope Core library.
- \`Libraries/Microscope.Application\`: standalone application composition/runtime library.
- Root CMake composition for product targets and test targets.

The application target is a transition implementation. The eventual target is to make the native frontend a client of the C++ Application API, not to keep expanding Swift-owned parallel microscope policy.

### v0.9 implementation phase

| Phase | Current evidence-based state |
|---|---|
| 1 — Product/application build boundary | Source migration committed; owner reported successful macOS configure/build and 15/15 deterministic CTest at the initial v0.9 migration checkpoint. |
| 2 — Functional Core extraction | Calibration, image geometry, automatic-resolution policy and slide overlay value models extracted. Owner-reported 15-test run passed at the initial extraction checkpoint. |
| 3 — Declarative configuration | 3A–3C resolution and profile validation, 3D transition DAG and 3E brightfield/phase/DIC/polarization representative profile coverage implemented. Cross-platform CI passed on source preceding the newest Phase 4 slice. |
| 4 — Core operation runtime | 4A pure operation/step lifecycle transitions implemented. Exact-source GitHub Actions run 45639182251 at \`c730b56\` succeeded on the workflow matrix. 4B command/port contracts are next. |
| 5 — JSON and application composition | Not implemented to the target architecture. |
| 6 — Calibration in the new Core | Planned; the earlier app contains calibration-related source, but this is not the complete Core calibration runtime. |
| 7 — Sample, slides and persistence | Planned in the v0.9 Core roadmap. |
| 8 — Acquisition | Planned; preserve the existing camera frame/lease path when implementing. |
| 9 — Image documents and annotations | Planned in Core. |
| 10 — Frontend migration/acceptance | Planned; transitional UI remains until replacement behavior is accepted. |

### Operation lifecycle semantics already in Core

The first 4A source slice defines pure operation and step transition functions. It distinguishes command dispatch, adapter acceptance, device acknowledgement, device progress, device-action completion, device-state observation and Core verification.

Important invariant: a command being dispatched, accepted, acknowledged or reported complete does not by itself mean that the desired state has been verified. Device action completion moves the step to awaiting observation; only an explicit verification-succeeded transition marks it verified. Cancellation remains requested until cancellation is confirmed.

The implementation is lifecycle semantics only. It is not yet a command executor, hardware runtime, callback router or persistence/event-sourcing system.

## 4. Verification and acceptance boundary

- Cross-platform Core/Application/root-composition CI passed for the Phase 3 source matrix.
- The Phase 4A source commit \`c730b56\` passed run 45639182251.
- Earlier owner-reported macOS build and 15/15 CTest results validate their exact older app revisions.
- The latest source branch is not yet evidence of a complete operational microscope workflow.
- The latest workflow does not establish physical controller/stage movement, full scan execution, or current application GUI and hardware acceptance.
- Slide presentation/geometry types are not a Sample/Slide persistence and scanning workflow. Stage specifications are not a controller/provider/motion runtime. Track those as separate workstreams in [Controller](controller.md) and [Slide Scanning](slide-scanning.md).
- Do not attribute AmScope MU503 frame tests to controller or slide-scanning acceptance.

## 5. Architectural constraints

1. Keep Core deterministic and adapter-neutral.
2. Keep one owner for each mutable runtime state.
3. Do not add command execution before the typed command/feedback boundary is defined.
4. Do not conflate desired configuration, command dispatch, adapter acceptance, device acknowledgement, observed state and verified completion.
5. Keep the existing camera acquisition worker, frame pool, immutable frame leases, mailbox and single preview consumer intact.
6. Do not add a second frame queue, per-frame pixel copy or a second camera session owner.
7. Keep camera, Motion and ImageProcessing as sibling capabilities coordinated by the application.
8. Do not put image-processing algorithms, controller drivers or scan orchestration inside the camera subsystem.
9. Preserve explicit unknown/unavailable states; do not synthesize optical, sensor, calibration or hardware facts.
10. No node-editor or visual graph-editor frontend is part of this workstream.

## 6. Continuation handoff

**Active milestone:** Phase 4 — Core operation runtime.  
**Completed slice:** 4A — pure operation/step lifecycle semantics.  
**Next bounded slice:** 4B — typed command values, command correlation scope and capability-specific adapter ports.

### Next task

Define the command boundary from a Core operation step to an adapter without leaking native SDK handles or stringly typed commands into Core.

The contract must identify:
- microscope runtime identity and generation;
- logical DeviceId distinct from provider SDK/session handles;
- OperationId and StepId;
- CommandId where the feedback fact is command-specific;
- typed capability-specific command payloads;
- dispatch acceptance/rejection as the adapter's synchronous responsibility;
- later device acknowledgement/progress/completion as separate feedback facts.

Do not implement the serialized operation runtime or plan executor as part of 4B. Those belong to 4C and 4D. Do not mark 4B complete merely because an interface or enum exists; define and test its invariants.

### Required continuation procedure

1. Fetch the current head of \`project_manager\` and compare it with the source head of the work being continued.
2. Read \`docs/plans/MICROSCOPE_PLATFORM_IMPLEMENTATION_V0.9.md\`, \`docs/architecture/MICROSCOPE_CORE_APPLICATION_CONTRACTS_V0.9.md\` and \`docs/architecture/MICROSCOPE_PLATFORM_ARCHITECTURE_V0.9.md\`.
3. Add contract tests before production implementation.
4. Preserve the separation between the lifecycle reducer and future ingress/serialization/execution services.
5. Run the repository's applicable CI and record the exact commit/run. Do not infer a pass from a previous commit.
6. Update this handoff and the implementation plan after each substantive slice.
7. Keep application GUI and physical device acceptance as separate evidence gates.
8. Update the Controller and/or Slide Scanning workstream record when their contracts or source change. Do not mark either line complete because an application-level integration compiles.

### Current blockers / non-completions

- No complete Core operation executor.
- No Application-owned JSON definition/deployment loader and adapter factory composition.
- No Core-owned full calibration lifecycle.
- No complete Sample/Slide/Tile persistence model in this target architecture.
- No acquisition plan/run and persistent image-document workflow in Core.
- No completed frontend migration to the C++ Application API.
- No completed physical controller/stage actuation or end-to-end slide-scanning acceptance.

## 7. Authoritative source links

- [v0.9 implementation plan](../plans/MICROSCOPE_PLATFORM_IMPLEMENTATION_V0.9.md)
- [v0.9 platform architecture](../architecture/MICROSCOPE_PLATFORM_ARCHITECTURE_V0.9.md)
- [Core/Application contracts](../architecture/MICROSCOPE_CORE_APPLICATION_CONTRACTS_V0.9.md)
- [OPTIKA refactor plan](../plans/OPTIKA_B293_DUALSLIDE_REFACTOR_PLAN.md)
- [Phase 4A source commit](https://github.com/Shennagyp/MICROSCOPE_CPP/commit/c730b568a8eb43ef9ba55b93c966928041679f86)
