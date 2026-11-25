# Workstream — Slide Scanning

**System repository:** [MICROSCOPE](https://github.com/Shennagyp/MICROSCOPE)  
**Manager branch:** \`refactor-2\`  
**Station branch:** \`station-development\`  
**Status:** Manager and Station implementation tracks exist independently. The bridge and physical end-to-end slide scanning are not accepted as complete.

## 1. System responsibility

Slide scanning is a distributed workflow, not a camera feature.

### Manager — central authority

The .NET 10 Manager owns:
- Station and microscope registration/identity;
- registered equipment and installed-component specifications;
- Sample and Slide identity and relationships;
- per-Slide scan requirements;
- Functional Qualification Pipeline (FQP) evaluation;
- microscope qualification and allocation per Slide;
- intended technique and scan-level requirements;
- task assignment, task state and Manager-issued stop authority;
- central acquisition/Gallery metadata and storage references.

One Sample can contain several Slides. Each Slide is qualified and allocated independently; Slides from one Sample may be assigned to different microscopes and Stations. The Manager does not open cameras, move stages or execute physical scans.

### Station — local physical execution

The Swift Station owns:
- local hardware discovery and device sessions;
- camera acquisition and live view;
- microscope-local configuration and calibration execution;
- assigned physical scan execution;
- local original image assets, gallery, measurement and analysis;
- physical stop execution and evidence of the stop outcome;
- report of what was achieved, not an assertion that the Manager's requested configuration was necessarily achieved.

The Station executes the assignment it receives. It does not select a replacement microscope or reallocate a Slide.

The Manager and Station are intentionally developed independently. A complete integration/bridge phase requires an explicit contract and separate acceptance.

## 2. Development history

### Earlier microscope application and composition work

Earlier MICROSCOPE branches explored microscope equipment composition, camera registration, stage/carrier topology, calibration, acquisition, local gallery and station workspaces. The current distributed architecture supersedes the earlier single-process .NET hosting model.

### Manager line

The .NET Manager implementation progressed on \`refactor-2\`:
- separate domain models and EF Core/SQLite persistence;
- Station and microscope registry;
- Station registration and equipment snapshot;
- duplicate attachment investigation and audit;
- central stage-calibration JSON history;
- typed Manager/Station command/report contracts;
- capability and physical-compatibility evaluation;
- Sample/Slide intake, slot occupancy and scan requirements;
- per-Slide FQP and microscope allocation;
- durable scan-task and stop-order coordination;
- execution reports and metadata-only Gallery/DICOM references;
- operator workflow/query projections and security source.

The latest inspected Manager branch tip was \`5d2346b\` on 14 November 2026. The Manager milestones document M9 planning and M10 task/stop source as implemented, while transport integration, versioned multi-step workflow definitions, operator acceptance and other release gates remain open.

### Station line

The independent Swift Station work proceeded on \`station-development\`:
- local microscope/camera registration and per-microscope camera runtime;
- camera acquisition/live view;
- local Sample/Slide workspace and placement/observation records;
- original image persistence, integrity checks and recovery/retry boundaries;
- capture-to-Slide-observation commit and local gallery workflow;
- Manager registration projection contracts and partial adapter work;
- processing catalogue/domain groundwork.

The current Station milestones explicitly state S0 established and S1 in progress. Source code contains partial capabilities associated with later milestones, but later milestones are not thereby complete.

## 3. Current implementation status

### Manager source

The Manager roadmap describes substantial source implementation for:
- M1 domain/persistence foundation;
- M2 host and registry;
- M3 registration and synchronization vertical slices;
- M4 duplicate-attachment detection/audit;
- M5 central stage-calibration document storage/history;
- M6 command/report coordination contracts;
- M7 physical compatibility evaluators;
- M8 Sample/Slide intake and slot occupancy;
- M9 per-Slide FQP qualification/allocation;
- M10 ScanTask dispatch, acknowledgements, stop requests and execution-report reconciliation;
- M11 metadata-only acquisition/Gallery/DICOM references;
- M12 operator workflow and query projections;
- M13 security/reliability/deployment work.

These are roadmap/source claims with milestone-specific open gates. A milestone is not complete merely because endpoints, entities or test source exist. The Manager README and milestone plan identify unverified owner-run build/migration/acceptance and outstanding Manager/Station and Station/LMS integration.

### Station source

The Station current source includes:
- camera provider integration and managed camera sessions;
- local microscope/Slide workspace;
- frame display and still capture;
- immutable original-image persistence and asset recovery;
- Slide observations tied to exact image, microscope-definition, imaging-configuration and placement revisions;
- Manager registration-projection contracts and partial synchronization code;
- image-processing domain groundwork.

The current Station milestone authority says:
- S0 is established;
- S1 remains in progress;
- S9 Python image-processing pipelines are requirements-only, not implemented;
- S14 controller/stage integration is deferred.

The latest Station continuation handoff says local-first Station operation remains the active boundary. Manager transport/registration is not required for the current local milestone; older paired-server procedures are historical and must not be used as the current Station run procedure.

### What is not established

- Manager-to-Station bridge is not complete.
- A full FQP → ScanTask → Station physical execution → result report → Gallery/DICOM workflow is not demonstrated end to end.
- Physical slide movement or motorized scanning is not accepted.
- Controller/stage physical operation remains S14-deferred.
- Station S9 Python processing pipelines are not implemented.
- A Station application source test or CI declaration is not a target-Mac interactive acceptance.
- Local camera frame evidence does not establish full slide-scanning acceptance.

## 4. Key invariants

1. Sample and Slide have distinct stable identities; every Slide belongs to exactly one Sample.
2. A ScanRequest targets one Slide.
3. FQP allocation is performed per Slide by Manager, not by Station UI or a local heuristic.
4. A ScanTask targets one selected microscope and its currently associated Station.
5. One Sample can be distributed across multiple microscopes/Stations via its Slides.
6. Manager requested/intended configuration and Station achieved configuration remain distinct facts.
7. Station owns physical operations, timing, camera/controller sessions and image bytes.
8. Manager stores canonical shared identity and metadata/storage references; it does not become a hardware driver or image-byte store.
9. Station local workflows remain usable when Manager is unavailable, subject to explicit freshness and authorization rules for actions requiring Manager decisions.
10. Each physical image preserves its Slide, scan/run, microscope, configuration, calibration and placement provenance.
11. A Station cannot silently reallocate a Slide or substitute another microscope.
12. Manager-issued stop authority is distinct from Station-confirmed physical stop completion.
13. Smart Gallery and DICOM production are Station-owned; Manager stores only agreed metadata and references.

## 5. Verification state

### Manager

The Manager branch documentation records owner-reported earlier .NET build/test and SQLite persistence checks, plus substantial test source. The algorithm coverage document warns that the available CI attempts did not reach test execution for the reviewed revision; exact current status must be checked before claiming the Manager suite passed. Physical Station integration and positive end-to-end FQP/acquisition coverage remain release gates.

### Station

The Station handoff requires, for each milestone:
- TDD acceptance tests first;
- hosted Swift package/XCTest verification;
- target-Mac app build and real GUI launch;
- interaction with the milestone's workflow;
- milestone-specific physical hardware acceptance where required.

The last reviewed Station source checkpoint is 17 November 2026. Its handoff reports source-level changes but no build, XCTest, app launch or hardware operation for the later edits. S1 remains open.

## 6. Continuation handoff

**Active Manager line:** \`refactor-2\`; next work must follow the current Manager milestone/continuation handoff.  
**Active Station line:** \`station-development\`, S1 in progress.  
**Integration:** deferred; do not merge the Manager and Station branches or invent a wire protocol as part of routine independent development.

### Next bounded Manager task

Read the latest Manager \`docs/distributed-application-milestones.md\`, \`docs/dotnet-continuation-handoff.md\`, \`docs/README.md\` and \`verification/ALGORITHM_COVERAGE.md\`. Reconcile the exact current head with the M7–M13 source/test/verification ledger. Continue the earliest incomplete Manager gate, not a later feature. Do not call a cross-repository scan workflow complete without a positive end-to-end acceptance record.

### Next bounded Station task

Read the latest \`docs/station-continuation-handoff.md\` first, then \`docs/station-development-milestones.md\` and \`docs/station-milestone-acceptance-records.md\`. Continue the active S1 gate only. Preserve the per-microscope camera session model, local-first offline behavior, source-image integrity, Slide observation provenance and recovery boundaries. Do not jump to S9 processing or S14 controller/stage integration.

### Future bridge gate

Only after Manager and Station independently satisfy their approved gates:
1. review the full assignment, status, stop, report and media-reference contracts together;
2. define versioning, identity/revision fences, idempotency and failure/retry semantics;
3. implement Manager and Station adapters in their own repositories;
4. verify one end-to-end Sample with multiple Slides, including allocation across microscope/station boundaries;
5. verify stop request versus physical stop confirmation and actual acquired-result provenance;
6. verify Station-originated image/Gallery/DICOM metadata references without transferring image-byte ownership to Manager.

## 7. Authoritative records

### Manager

- [Manager milestones](https://github.com/Shennagyp/MICROSCOPE/blob/refactor-2/docs/distributed-application-milestones.md)
- [Manager continuation handoff](https://github.com/Shennagyp/MICROSCOPE/blob/refactor-2/docs/dotnet-continuation-handoff.md)
- [Documentation authority map](https://github.com/Shennagyp/MICROSCOPE/blob/refactor-2/docs/README.md)
- [Algorithm and test coverage](https://github.com/Shennagyp/MICROSCOPE/blob/refactor-2/verification/ALGORITHM_COVERAGE.md)

### Station

- [Station milestones](https://github.com/Shennagyp/MICROSCOPE/blob/station-development/docs/station-development-milestones.md)
- [Station continuation handoff](https://github.com/Shennagyp/MICROSCOPE/blob/station-development/docs/station-continuation-handoff.md)
- [Station milestone acceptance records](https://github.com/Shennagyp/MICROSCOPE/blob/station-development/docs/station-milestone-acceptance-records.md)
- [Station runtime model](https://github.com/Shennagyp/MICROSCOPE/blob/station-development/docs/station-microscope-runtime-model.md)
