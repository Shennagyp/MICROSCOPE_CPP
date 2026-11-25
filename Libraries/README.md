# Microscope C++ libraries

The platform libraries are separate C++17 targets:

| CMake target | Source directory | Responsibility |
|---|---|---|
| `Microscope::Core` | `Libraries/Microscope.Core` | Microscope-domain contracts and behavior |
| `Microscope::Application` | `Libraries/Microscope.Application` | Runtime handles, application composition and frontend API |

Application depends on Core. Core does not depend on Application, Camera::Common, AmScope, Qt, JSON, DICOM, Swift/Objective-C++ or operating-system APIs.

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

Set `-DBUILD_TESTING=OFF` to omit the focused contract-test targets. The default CTest configuration adds tests for the contracts owned by each library.

## Current implementation boundary

The initial source slice includes:

- Core: strong opaque identities, runtime generation, Result/Status, diagnostics, operation/step status values and typed feedback correlation/ingress contracts.
- Application: runtime/operation handles and value projections scoped to runtime identity/generation.

This is not a completed microscope platform. Equipment/configuration semantics, operation execution, JSON definition loading, concrete adapter composition, persistence, calibration and acquisition are implemented in their planned phases. No runtime/build/test result is implied by the existence of these targets.

Sample, Slide and other identifiers are opaque values, not filesystem path fragments. Storage adapters encode them into safe portable path segments or maintain an explicit mapping; they never concatenate arbitrary ID text directly into paths.

The existing camera capability remains independent. Its `Camera::Common` frame lease and mailbox are not duplicated in Core; persistent acquisition `ImageFrame` records are a separate domain concept.
