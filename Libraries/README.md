# Microscope C++ libraries

The platform libraries are separate C++17 targets with compiler extensions disabled on the libraries and their contract-test targets:

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

## Current implementation boundary

The initial source slice includes:

- Core: strong opaque identities, distinct runtime/device-session generations, Result/Status, diagnostics, operation/step status values, typed feedback correlation/fact kinds, validated device timestamps, typed FeedbackIngressSequence and feedback receipt metadata.
- Application: runtime/operation handles and value projections scoped to runtime identity/generation. Core OperationSnapshot carries the microscope/runtime/runtime-generation/operation composite identity; OperationView verifies that it matches its runtime-scoped handle.

This is not a completed microscope platform. Equipment/configuration semantics, operation execution, JSON definition loading, concrete adapter composition, persistence, calibration and acquisition are implemented in their planned phases. No runtime/build/test result is implied by the existence of these targets.

Sample, Slide and other identifiers are opaque values, not filesystem path fragments. Storage adapters encode them into safe portable path segments or maintain an explicit mapping; they never concatenate arbitrary ID text directly into paths.

The existing camera capability remains independent. Its `Camera::Common` frame lease and mailbox are not duplicated in Core; persistent acquisition `ImageFrame` records are a separate domain concept.
