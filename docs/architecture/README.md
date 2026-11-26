# Architecture documentation

## Application system architecture

- [OPTIKA B293 Application System Architecture v1](OPTIKA_B293_APPLICATION_SYSTEM_ARCHITECTURE_V1.md) — authoritative product-level object ownership, stateful OOP/functional-core boundary, subsystem graph, concurrency, persistence and implementation order.

## Domain and platform architecture

- [Microscope Platform Architecture v0.9](MICROSCOPE_PLATFORM_ARCHITECTURE_V0.9.md) — authoritative microscope-domain model, C++ Core/Application boundaries, configuration, operation/feedback semantics, calibration, slide/acquisition records and persistence invariants.
- [Microscope Core and Application Contracts v0.9](MICROSCOPE_CORE_APPLICATION_CONTRACTS_V0.9.md) — C++17 identity, result, feedback and runtime contract conventions.
- [Microscope Platform Implementation Plan v0.9](../plans/MICROSCOPE_PLATFORM_IMPLEMENTATION_V0.9.md) — migration phases and acceptance criteria.

## Capability-specific architecture

- [Stage Working Surface and Axis Reference v2](STAGE_WORKING_SURFACE_AND_AXIS_REFERENCE_V2.md) — empty XY canvas, variable slide geometry, independent focus Z and stage referencing.
- [OPTIKA B293 Dual-Slide Application Architecture](OPTIKA_B293_DUALSLIDE_APPLICATION_ARCHITECTURE.md) — existing native UI/camera implementation reference and preview performance invariants.
- Other AmScope architecture documents describe camera-driver, SDK and native acquisition details. They remain applicable to the camera capability unless a later camera-specific decision supersedes them.

## Library implementation guide

- [C++ libraries README](../../Libraries/README.md) — standalone CMake targets and current library implementation boundary.
