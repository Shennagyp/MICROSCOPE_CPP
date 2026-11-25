# Project Management — Microscope Ecosystem

**Management branch:** `project_manager`  
**Created from:** `OPTIKA-B293-DUALSLIDE-APP-v.0.9` at `6fdff2ac3907c22a35687606bf05d0cf7b150ce8`  
**Current source-branch checkpoint:** `OPTIKA-B293-DUALSLIDE-APP-v.0.9` at `c7bb4b4dc41441989d71ee800c6d33b51dc54abe` (25 November 2026)  
**Purpose:** one navigable project record for development history, current implementation, verification evidence and continuation handoffs.

This branch is a documentation/control plane. Product implementation continues on its owning source branches. The selected active implementation branch for the OPTIKA B293 application, Controller/Motion/Stage and Slide Scanning lines is `OPTIKA-B293-DUALSLIDE-APP-v.0.9`. Do not move implementation to `project_manager`.

## Workstream map

| Development line | Active implementation location | Historical/reference material | Management record |
|---|---|---|---|
| OPTIKA microscope application | [MICROSCOPE_CPP — `OPTIKA-B293-DUALSLIDE-APP-v.0.9`](https://github.com/Shennagyp/MICROSCOPE_CPP/tree/OPTIKA-B293-DUALSLIDE-APP-v.0.9) | Earlier OPTIKA and AmScope application branches | [Microscope application](microscope-app.md) |
| Camera — one unified line | [MICROSCOPE_CPP / Camera](https://github.com/Shennagyp/MICROSCOPE_CPP/tree/OPTIKA-B293-DUALSLIDE-APP-v.0.9/Camera) | Camera branch history 0.3–0.8 and MU503 evidence | [Camera](camera.md) |
| Controller / Motion / Stage | [MICROSCOPE_CPP — `OPTIKA-B293-DUALSLIDE-APP-v.0.9`](https://github.com/Shennagyp/MICROSCOPE_CPP/tree/OPTIKA-B293-DUALSLIDE-APP-v.0.9) | Earlier contracts and packages in [MICROSCOPE / `station-development`](https://github.com/Shennagyp/MICROSCOPE/tree/station-development), plus Manager records on `refactor-2` | [Controller](controller.md) |
| Slide Scanning | [MICROSCOPE_CPP — `OPTIKA-B293-DUALSLIDE-APP-v.0.9`](https://github.com/Shennagyp/MICROSCOPE_CPP/tree/OPTIKA-B293-DUALSLIDE-APP-v.0.9) | Earlier Sample/Slide, FQP and Manager/Station workflow work in [MICROSCOPE](https://github.com/Shennagyp/MICROSCOPE) | [Slide scanning](slide-scanning.md) |
| Image processing | [ImageProcessing](https://github.com/Shennagyp/ImageProcessing) — `wp1-target-inventory` | Its own C++20 development history | [Image processing](image-processing.md) |

## Shared branch, separate development lines

The application, Controller/Motion/Stage and Slide Scanning lines share the `OPTIKA-B293-DUALSLIDE-APP-v.0.9` source branch. Sharing a Git branch does **not** merge their responsibilities, milestones or acceptance criteria.

Track them independently:

- **Application line:** owns product composition, Core/Application boundaries, configuration, operation semantics and frontend integration.
- **Camera line:** one subsystem and one workstream; owns camera device/session interaction, acquisition and frame delivery.
- **Controller line:** owns controller identity/provider/session, hardware command and feedback boundaries. Motion owns provider-neutral movement semantics; Stage owns microscope-specific stage/carrier topology and qualification.
- **Slide Scanning line:** owns Sample/Slide scanning concepts, scan planning/execution lifecycle, coverage and image provenance. It consumes Camera and Controller/Motion/Stage capabilities; it does not implement their drivers.
- **ImageProcessing line:** remains in the separate `Shennagyp/ImageProcessing` repository. It is not moved into this branch by documenting or integrating its interface.

The previous `MICROSCOPE` Manager (`refactor-2`) and Station (`station-development`) branches remain historical sources of requirements, design decisions and earlier implementations. They are not the active implementation location for the Controller and Slide Scanning work now being continued in the OPTIKA v0.9 branch. Do not treat their source packages as if they were already ported into MICROSCOPE_CPP.

A commit may touch more than one line when integration requires it. Its change description and the corresponding handoff must identify every affected line. A feature completed in one line does not imply completion of a dependent line.

## One camera project

There is **one camera development line** in this project register. Its canonical implementation is the camera subsystem integrated under `MICROSCOPE_CPP/Camera`, with its AmScope provider and application adapters.

Do not create separate project-management tracks for “AmScope camera”, “MU503 camera” and “GenTL camera”. They are implementation history, device/provider work, or future capability seams within the camera line—not three independent camera products. Other repositories may be cited as historical research or supporting evidence, but they do not create additional camera workstreams.

## Product and ownership boundaries

The ecosystem consists of independently tracked capabilities coordinated by the OPTIKA application. The names below identify responsibilities, not a claim that all code has already been implemented or integrated:

- **OPTIKA B293 workstation application:** native macOS application, C++ Core/Application architecture and product composition.
- **Camera:** acquisition, device/session handling, camera features and frame delivery.
- **Controller/Motion/Stage:** physical controller/provider boundary, movement semantics and microscope stage topology/qualification.
- **Slide Scanning:** Sample/Slide identity and scan workflow, plan/run lifecycle, coverage and acquisition provenance.
- **ImageProcessing:** independent C++20 image document, operation, implementation, provider and execution-runtime library.

The application coordinates these lines. Slide Scanning requests capabilities through application/Core contracts; it does not directly own camera sessions or controller SDK handles. Camera acquisition remains independent of scan orchestration and image-processing policy.

## Continuation protocol

Each workstream document contains:
1. Source repository and active branch, with historical branches explicitly labelled as history/reference.
2. Development scope and boundaries from adjacent workstreams.
3. Evidence-based implementation history and current source status.
4. Milestone, tests and acceptance evidence, distinguishing source presence from build/test/hardware acceptance.
5. Known blockers and the next bounded continuation task.

After every substantive implementation batch, update only the affected workstream records plus the application integration record when a cross-line contract changes. Record the exact repository, branch, commit SHA, changed behavior, tests written, actual CI/local/hardware result, open risks and next bounded task.

Never copy an old branch-tip statement forward without checking the actual current branch head. A queued or skipped workflow is not a pass. Source tests are not executed tests. A simulated provider is not physical hardware evidence. UI slide presentation is not a scan workflow. Stage specifications are not a controller runtime.

## Baseline verification snapshot

- **MICROSCOPE_CPP source branch:** current checked head `c7bb4b4` is a documentation/plan update after the v0.9 base `6fdff2a`. The preceding operation-state source commit `c730b56` had a successful cross-platform workflow, run [45639182251](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45639182251). This verifies that commit, not a physical microscope workflow.
- **Controller line:** the checked v0.9 tree contains microscope Stage specification code, but no accepted physical controller provider, motion executor or stage-actuation path.
- **Slide Scanning line:** the checked v0.9 tree contains slide presentation/geometry elements, but no accepted Sample/Slide scan-planning and physical scan-execution workflow.
- **ImageProcessing:** latest recorded head `2cbd4662dabf4e7cab26cf824bb3204fdd85b3fc`; exact-head hosted workflows were queued when its record was prepared. Earlier 14/14 cross-platform results must not be attributed to that newer head.
- **Hardware:** physical MU503 evidence exists for narrow AmScope RGB24 and FrameView slices. It does not establish full OPTIKA application, RAW, controller, stage or slide-scanning acceptance.

## Workstream links

- [OPTIKA microscope application — history and handoff](microscope-app.md)
- [Camera — unified camera line](camera.md)
- [Controller/Motion/Stage — history and handoff](controller.md)
- [ImageProcessing — separate C++20 library](image-processing.md)
- [Slide Scanning — separate workflow line](slide-scanning.md)
