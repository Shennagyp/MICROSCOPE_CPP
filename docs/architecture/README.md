# Architecture documentation

## Authoritative platform target

- [Microscope Platform Architecture v0.9](MICROSCOPE_PLATFORM_ARCHITECTURE_V0.9.md) — authoritative ownership, C++ library boundaries, JSON microscope definitions, adapter composition, operation/feedback lifecycle, calibration, slides, acquisition, persistence and frontend independence.
- [Microscope Platform Implementation Plan v0.9](../plans/MICROSCOPE_PLATFORM_IMPLEMENTATION_V0.9.md) — incremental migration phases and acceptance criteria.

## Existing implementation references

- [OPTIKA B293 Dual-Slide Application Architecture](OPTIKA_B293_DUALSLIDE_APPLICATION_ARCHITECTURE.md) — retained reference for the existing native macOS application's composition, current camera/preview pipeline and implementation-specific performance invariants. Where it conflicts with the platform architecture above on target ownership (Core/Application, microscope behavior, JSON definitions, operation feedback or frontend boundaries), the platform architecture is authoritative.
- Other AmScope architecture documents describe camera-driver and SDK integration details. They remain applicable to the camera capability unless a later camera-specific decision supersedes them.
