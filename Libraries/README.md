# Microscope C++ libraries

The platform libraries are separate C++17 targets with the language standard pinned to exactly C++17 and compiler extensions disabled on the libraries and their contract-test targets:

| CMake target | Source directory | Responsibility |
|---|---|---|
| `Microscope::Core` | `Libraries/Microscope.Core` | Microscope-domain contracts and behavior |
| `Microscope::Application` | `Libraries/Microscope.Application` | SQLite persistence adapters, runtime handles, stage-navigation runtimes and application orchestration primitives; the full microscope composition root/frontend use-case API is still outstanding |

Application depends on Core. Core does not depend on Application, Camera::Common, AmScope, Qt, JSON, DICOM, Swift/Objective-C++ or operating-system APIs.

## Root-project configuration without AmScope

The root project includes Core and Application by default. To configure/build only the microscope platform libraries on a non-macOS host, disable the existing macOS-only camera capability:

```sh
cmake -S . -B build/platform-libraries \
  -DOPTIKA_BUILD_AMSCOPE=OFF \
  -DOPTIKA_BUILD_NATIVE_APP=OFF
cmake --build build/platform-libraries
ctest --test-dir build/platform-libraries --output-on-failure
```

The default `OPTIKA_BUILD_AMSCOPE=ON` preserves the repository's existing camera/application composition. The native OPTIKA B293 app requires AmScope and cannot be configured with that option disabled.

## Standalone configuration

Configure and build Core independently:

```sh
cmake -S Libraries/Microscope.Core -B build/microscope-core
cmake --build build/microscope-core --target microscope_core
```

Configure and build Application independently. Its CMake project adds the required Core source target:

```sh
cmake -S Libraries/Microscope.Application -B build/microscope-application
cmake --build build/microscope-application --target microscope_application
```

To build and run the focused tests in the user's development environment, build the default targets and invoke CTest:

```sh
cmake --build build/microscope-core
ctest --test-dir build/microscope-core --output-on-failure

cmake --build build/microscope-application
ctest --test-dir build/microscope-application --output-on-failure
```

The Application standalone build also configures its Core source dependency, so its CTest directory contains both libraries' tests. Set `-DBUILD_TESTING=OFF` to omit the focused contract-test targets.

These commands are instructions only; no build or test result is implied by this document.

## Continuous integration

The `Microscope platform libraries` GitHub Actions workflow independently configures/builds/tests Core, independently configures/builds/tests Application (including its Core dependency), and configures/builds/tests the root platform-only composition with AmScope/native app disabled. It runs on Ubuntu, macOS and Windows. The existing AmScope workflow remains responsible for its macOS camera capability and integration checks. CI results are separate evidence from any local or hardware validation.

## Current implementation boundary

The initial source slice includes:

- Core: strong opaque identities with nonblank/NUL-free text validation, distinct runtime/device-session generations, [[nodiscard]] Result/Status, factory-validated diagnostics, operation/step status values, typed immutable factory-created feedback envelopes, correlation/fact kinds with factory-enforced enum/correlation/provenance requirements, validated device timestamps/clock domains, runtime-scoped FeedbackIngressSequence and factory-validated feedback receipts. The feedback ingress contract specifies fail-safe overflow latching; the queue/runtime implementation belongs to Phase 4.
- Application: runtime/operation handles and immutable projections scoped to runtime identity/generation. RuntimeSnapshot is factory-created and validates lifecycle/revision; Core OperationSnapshot validates status/revision and carries the microscope/runtime/runtime-generation/operation composite identity; OperationView's factory rejects mismatched handle/snapshot identity with a typed error.

Phase 2 is complete: 2A typed equipment topology with undirected physical adjacency and ordered directional paths; 2B objective/camera specifications with independent X/Y pixel mapping; 2C multichannel illumination and condenser aperture/design specifications; 2D stage/axis specifications with independent resolution, accuracy, repeatability, uncertainty and feedback-location data; and 2E cross-definition validation, including opposite-direction reuse of physical connections and objective/condenser role exclusivity. All specialized records attach to installed component identity. Phase 3A–3C add declarative configuration profiles/catalogs, typed optical-function requirements, immutable requests and a pure resolver that validates multi-path compatibility plus condenser aperture, illumination intensity and camera exposure targets. The microscope-scoped ResolvedConfiguration retains desired targets without executing hardware transitions or overriding ExposurePolicy. Phase 3D adds a typed, microscope-scoped transition dependency DAG with an acquisition-inactive root, separate exposure-policy and optional exposure-preset requirements, typed desired-state nodes, deterministic topological layers and final verification; it emits no adapter commands and does not diff current observations. Phase 3E covers representative brightfield, phase-contrast, DIC and polarized-light profiles, including split optical paths and additional component selections. At the Phase 3 implementation checkpoint, the matrix passed on Ubuntu GCC, macOS Clang and Windows MSVC for Core standalone, Application standalone and root platform-only builds/tests: [run 122](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45636792527). This is not yet a completed microscope platform. Partial persistence and calibration are implemented in Application (SampleType/Slide, dual-slide carrier, OperationMode/LightPath calibration and camera-model stores), and stage-navigation runtimes are present. Still outstanding are authoritative equipment-definition JSON loading/persistence, mode/profile enumeration, a complete microscope-level Application composition root, observed-state reconciliation and hardware execution, and concrete stage/optical adapters. The Swift application owns acquisition orchestration and image/tile organization; those workflows are not missing C++ library responsibilities. The Phase 2A model passed the three-platform CI matrix: [run 22](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45587716888). The Phase 2B objective/camera model and tests passed: [run 33](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45589429447). The Phase 2C condenser/illumination model and tests passed: [run 46](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45590795457). The Phase 2D stage/axis model and tests passed: [run 58](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45592166839). Phase 2E topology/cross-role validation passed on all platforms: [run 72](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45595205508).

Sample, Slide and other identifiers are opaque values, not filesystem path fragments. Storage adapters encode them into safe portable path segments or maintain an explicit mapping; they never concatenate arbitrary ID text directly into paths.

The camera capability remains a separate library. The Optika branch currently contains the older Common + AmScope arrangement; the newer installable Camera module (Common, AmScope and GenTL) is on `camera-v.2.0` and has not yet been integrated. That branch includes the advanced camera-side preview, discovery, streaming and frame-delivery pipeline. Its Common frame contract includes image offset, row padding, trailing padding and PFNC packed-format metadata. Do not duplicate camera frame ownership in Core. The Swift application owns acquisition orchestration and durable image/tile organization; C++ should expose the microscope hardware/domain services that workflow consumes.

## Current implementation status

- **Core:** physical equipment topology, optical paths, objective/camera/condenser/illumination/stage specifications, SampleType/Slide/Stain, OperatingMode recommendation, intent/hardware-realization separation, LightPath calibration, configuration resolution/transition DAG and stage error-budget/homing models are present.
- **Application:** SQLite stores and stage runtime primitives are present, but no complete `MicroscopeRuntime` composition root, UI-facing microscope snapshot/event facade, JSON equipment loader or physical optical/stage adapters exist yet.
- **Stage:** the fixed two-slot carrier with four-corner registration and the newer working-surface navigation model both exist. Their authority/mapping must be resolved before the UI relies on stage coordinates.
- **Acquisition boundary:** the Swift application owns acquisition orchestration, capture workflow, slide-folder/image-tile organization and gallery lifecycle; the camera-v2.0 branch owns camera preview/stream/frame delivery. C++ does not need a parallel acquisition workflow or image/tile persistence model. It must expose reliable camera, stage and optical services plus authoritative microscope state. ImageProcessing remains separate.
- **Camera integration:** the newer `camera-v.2.0` line contains GenTL and AmScope implementations; the Optika branch still needs a controlled Camera subtree integration and backend-neutral provider/session API.

See [Microscope Platform Completion Audit v0.9](../docs/architecture/MICROSCOPE_PLATFORM_COMPLETION_AUDIT_V0.9.md) for the dependency-ordered gaps and UI-readiness acceptance gate. The current Optika three-platform build/test matrix passed: [run 45929942838](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45929942838). This is software CI evidence, not physical hardware qualification.
