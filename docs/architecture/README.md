# Architecture documentation

## Application system architecture

- [OPTIKA B293 Application System Architecture v1](OPTIKA_B293_APPLICATION_SYSTEM_ARCHITECTURE_V1.md) — authoritative product-level object ownership, stateful OOP/functional-core boundary, subsystem graph, concurrency, persistence and implementation order.

## Domain and platform architecture

- [Microscope Platform Architecture v0.9](MICROSCOPE_PLATFORM_ARCHITECTURE_V0.9.md) — authoritative microscope-domain model, C++ Core/Application boundaries, configuration, operation/feedback semantics, calibration, slide/acquisition records and persistence invariants.
- [Microscope Core and Application Contracts v0.9](MICROSCOPE_CORE_APPLICATION_CONTRACTS_V0.9.md) — C++17 identity, result, feedback and runtime contract conventions.
- [Equipment Definition JSON Schema v2](EQUIPMENT_DEFINITION_JSON_SCHEMA_V2.md) — canonical installed Stage axes/configurations/StageSlots. [Schema v1](EQUIPMENT_DEFINITION_JSON_SCHEMA_V1.md) is retained only as a legacy import format.
- [Equipment Definition JSON Schema v2](EQUIPMENT_DEFINITION_JSON_SCHEMA_V2.md) — canonical installed Stage axis/configuration/StageSlot definition; [v1](EQUIPMENT_DEFINITION_JSON_SCHEMA_V1.md) is legacy migration input.
- [Microscope Platform Implementation Plan v0.9](../plans/MICROSCOPE_PLATFORM_IMPLEMENTATION_V0.9.md) — migration phases and acceptance criteria.

- [OPTIKA B293 v0.9 Continuation Handoff](OPTIKA_B293_V0.9_CONTINUATION_HANDOFF.md) — current branch checkpoint, implemented ownership boundaries, verification status and next repository work.
- [Microscope Platform Completion Audit v0.9](MICROSCOPE_PLATFORM_COMPLETION_AUDIT_V0.9.md) — current Core/Application/Camera/Stage/Acquisition audit, branch-integration constraints and UI-readiness acceptance gate.

## Capability-specific architecture

- [Controller Stage and Hardware Adapter Contract v1](CONTROLLER_STAGE_AND_HARDWARE_ADAPTER_CONTRACT_V1.md) — authoritative Stage OOP model, nanometre/radian coordinates, StageConfiguration/StageSlot ownership, motion/feedback contract and adapter readiness checklist.
- [OPTIKA B293 Dual-Slide Application Architecture](OPTIKA_B293_DUALSLIDE_APPLICATION_ARCHITECTURE.md) — existing native UI/two-pane camera implementation reference and preview performance invariants; its pane count is not the physical StageSlot model.
- Other AmScope architecture documents describe camera-driver, SDK and native acquisition details. They remain applicable to the camera capability unless a later camera-specific decision supersedes them.

## Library implementation guide

- [C++ libraries README](../../Libraries/README.md) — standalone CMake targets and current library implementation boundary.
