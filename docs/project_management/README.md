# Project Management — Microscope Ecosystem

**Management branch:** \`project_manager\`  
**Created from:** \`OPTIKA-B293-DUALSLIDE-APP-v.0.9\` at \`6fdff2ac3907c22a35687606bf05d0cf7b150ce8\`  
**Baseline date:** 25 November 2026  
**Purpose:** one navigable project record for development history, current implementation, verification evidence and continuation handoffs.

This branch is a documentation/control plane. Product implementation remains in its owning repositories and branches. Do not use this branch as a place to merge unrelated source implementations.

## Workstream map

| Workstream | Authoritative implementation repository | Current source baseline | Management record |
|---|---|---|---|
| OPTIKA microscope application | [MICROSCOPE_CPP](https://github.com/Shennagyp/MICROSCOPE_CPP) | \`project_manager\` begins at v0.9 source/documentation head \`6fdff2a\`; application implementation is the OPTIKA B293 branch line | [Microscope application](microscope-app.md) |
| Camera | [MICROSCOPE_CPP / Camera](https://github.com/Shennagyp/MICROSCOPE_CPP/tree/project_manager/Camera) | Integrated AmScope camera implementation; historical branch sequence 0.3–0.7, then v0.8 application integration | [Camera](camera.md) |
| Controller and motion | [MICROSCOPE](https://github.com/Shennagyp/MICROSCOPE/tree/station-development) | Swift Station controller/motion/stage contracts; physical controller integration remains deferred | [Controller](controller.md) |
| Image processing | [ImageProcessing](https://github.com/Shennagyp/ImageProcessing) | \`wp1-target-inventory\` at \`2cbd4662dabf4e7cab26cf824bb3204fdd85b3fc\` | [Image processing](image-processing.md) |
| Slide scanning | [MICROSCOPE Manager and Station](https://github.com/Shennagyp/MICROSCOPE) | Manager \`refactor-2\`; Station \`station-development\` | [Slide scanning](slide-scanning.md) |

## One camera project

There is **one camera development line** in this project register. Its canonical implementation is the camera subsystem integrated under \`MICROSCOPE_CPP/Camera\`, with its AmScope provider and application adapters.

Do not create separate project-management tracks for “AmScope camera”, “MU503 camera” and “GenTL camera”. They are implementation history, device/provider work, or future capability seams within the camera line—not three independent camera products. Other repositories may be cited as historical research or supporting evidence, but they do not create additional camera workstreams here.

## Product and ownership boundaries

The ecosystem consists of multiple independently developed products. The names below identify responsibilities, not a claim that all code has already been integrated:

- **OPTIKA B293 workstation application:** the native macOS application and its C++ Core/Application migration in MICROSCOPE_CPP.
- **Camera:** acquisition, device/session handling, camera features and frame delivery. It does not own microscope configuration, slide workflows or image-processing policy.
- **Controller/Motion:** controller identity/capabilities, provider/session function contracts, Motion-axis semantics and Stage qualification. Physical actuation is not accepted merely because contracts exist.
- **ImageProcessing:** independent C++20 image document, operation, implementation, provider and execution-runtime library. It is not yet integrated into the Station processing workflow.
- **Slide scanning:** distributed Manager/Station workflow. Manager owns Sample/Slide identity, qualification and task allocation; Station owns physical execution and local image artifacts.

Keep the camera acquisition/frame-delivery path independent of controller execution, image processing and scan workflow orchestration.

## Continuation protocol

Each workstream document contains:
1. Source repositories, branch names and the evidence-based history.
2. Current implementation, distinguishing source presence from build/test/hardware acceptance.
3. Known boundaries, blockers and non-goals.
4. The next bounded task and instructions for the next continuation.

After every substantive implementation batch, update the relevant workstream record with the exact repository, branch, commit SHA, active milestone, changed behavior, tests written, actual CI/local/hardware result, open risks and next bounded task.

Never copy an old branch-tip statement forward without checking the actual current branch head. A queued or skipped workflow is not a pass. Source tests are not executed tests. A simulated provider is not physical hardware evidence.

## Baseline verification snapshot

- **MICROSCOPE_CPP:** branch \`project_manager\` was created from \`6fdff2a\`. The preceding operation-state source commit \`c730b56\` had a successful cross-platform workflow, run [45639182251](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45639182251). This verifies that commit, not a physical microscope workflow.
- **ImageProcessing:** latest observed head \`2cbd466\`; exact-head hosted workflows were queued when this record was prepared. Earlier 14/14 cross-platform results exist for earlier commits, but must not be silently attributed to the new head.
- **MICROSCOPE Manager/Station:** Manager and Station are separate branches and have separate verification gates. Station handoff still reports S1 in progress; Manager source and tests must be judged against its own actual runs.
- **Hardware:** physical MU503 evidence exists for narrow AmScope RGB24 and FrameView slices. It does not establish full current OPTIKA application, RAW, controller, stage or slide-scanning acceptance.

## Workstream links

- [OPTIKA microscope application — history and handoff](microscope-app.md)
- [Camera — unified camera line](camera.md)
- [Controller/Motion — contracts, implementation and safety boundary](controller.md)
- [ImageProcessing — C++20 library](image-processing.md)
- [Slide scanning — distributed Manager/Station workflow](slide-scanning.md)
