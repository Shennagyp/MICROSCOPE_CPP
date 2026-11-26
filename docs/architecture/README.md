# Architecture documentation

## Application system architecture

- [OPTIKA B293 Application System Architecture v1](OPTIKA_B293_APPLICATION_SYSTEM_ARCHITECTURE_V1.md) — authoritative product-level object ownership, stateful OOP/functional-core boundary, complete subsystem graph, concurrency, persistence and implementation order.

## Domain/platform architecture

- [Microscope Platform Architecture v0.9](MICROSCOPE_PLATFORM_ARCHITECTURE_V0.9.md) — authoritative microscope-domain model, Core/Application library boundary, configuration, operation/feedback semantics, calibration, slides, acquisition and persistence invariants.
- [Microscope Core and Application Contracts v0.9](MICROSCOPE_CORE_APPLICATION_CONTRACTS_V0.9.md) — C++17 identity, result, feedback and runtime contract conventions.
- [Microscope Platform Implementation Plan v0.9](../plans/MICROSCOPE_PLATFORM_IMPLEMENTATION_V0.9.md) — migration phases and acceptance criteria.

## Capability-specific architecture

- [Stage Working Surface and Axis Reference v2](STAGE_WORKING_SURFACE_AND_AXIS_REFERENCE_V2.md) — physical XY canvas, variable slide geometry, independent focus Z and stage position/reference semantics.
- [OPTIKA B293 Dual-Slide Application Architecture](OPTIKA_B293_DUALSLIDE_APPLICATION_ARCHITECTURE.md) — existing native UI/camera implementation reference and preview performance constraints.
## Library implementation guide

- [C++ libraries README](../../Libraries/README.md) — standalone CMake configuration, target names and current implementation boundary.

## Existing implementation references

- [OPTIKA B293 Dual-Slide Application Architecture](OPTIKA_B293_DUALSLIDE_APPLICATION_ARCHITECTURE.md) — retained reference for the existing native macOS application's composition, current camera/preview pipeline and implementation-specific performance invariants. Where it conflicts with the platform architecture above on target ownership (Core/Application, microscope behavior, JSON definitions, operation feedback or frontend boundaries), the platform architecture is authoritative.
- Other AmScope architecture documents describe camera-driver and SDK integration details. They remain applicable to the camera capability unless a later camera-specific decision supersedes them.
