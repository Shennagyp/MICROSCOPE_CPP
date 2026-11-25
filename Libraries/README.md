# Microscope C++ libraries

The platform libraries are separate C++17 targets with the language standard pinned to exactly C++17 and compiler extensions disabled on the libraries and their contract-test targets:

| CMake target | Source directory | Responsibility |
|---|---|---|
| `Microscope::Core` | `Libraries/Microscope.Core` | Microscope-domain contracts and behavior |
| `Microscope::Application` | `Libraries/Microscope.Application` | Runtime handles, application composition and frontend API |

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

Phase 2 is complete: 2A typed equipment topology with undirected physical adjacency and ordered directional paths; 2B objective/camera specifications with independent X/Y pixel mapping; 2C multichannel illumination and condenser aperture/design specifications; 2D stage/axis specifications with independent resolution, accuracy, repeatability, uncertainty and feedback-location data; and 2E cross-definition validation, including opposite-direction reuse of physical connections and objective/condenser role exclusivity. All specialized records attach to installed component identity. Phase 3A–3C now add declarative configuration profiles/catalogs, typed optical-function requirements, immutable requests and a pure resolver that validates multi-path compatibility plus condenser aperture, illumination intensity and camera exposure targets. The microscope-scoped ResolvedConfiguration retains desired targets without executing hardware transitions or overriding ExposurePolicy. Phase 3A resolver evidence: [run 85](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45597867770); the full Phase 3B/3C implementation and target-validation matrix passed: [run 114](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45629930294). This is not a completed microscope platform: Phase 3D transition planning, Phase 4 operation execution, JSON profile loading, concrete adapter composition, persistence, calibration and acquisition remain in their planned phases. The Phase 2A model passed the three-platform CI matrix: [run 22](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45587716888). The Phase 2B objective/camera model and tests passed: [run 33](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45589429447). The Phase 2C condenser/illumination model and tests passed: [run 46](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45590795457). The Phase 2D stage/axis model and tests passed: [run 58](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45592166839). Phase 2E topology/cross-role validation passed on all platforms: [run 72](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45595205508).

Sample, Slide and other identifiers are opaque values, not filesystem path fragments. Storage adapters encode them into safe portable path segments or maintain an explicit mapping; they never concatenate arbitrary ID text directly into paths.

The existing camera capability remains independent. Its `Camera::Common` frame lease and mailbox are not duplicated in Core; persistent acquisition `ImageFrame` records are a separate domain concept.
