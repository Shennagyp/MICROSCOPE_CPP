# Microscope C++ libraries

The C++ platform is split by ownership and lifecycle. All targets require exactly C++17 with compiler extensions disabled.

| CMake target | Source directory | Responsibility |
|---|---|---|
| Camera::Common | Camera/Common | Backend-neutral camera identity, discovery/model and frame/session contracts |
| Microscope::Core | Libraries/Microscope.Core | Microscope-domain values, installed equipment specifications, command/feedback contracts and operation primitives |
| Controller::Common | Controller/Common | Live microscope/stage OOP model, axes, configurations, StageSlots, motion planning and Core command translation |
| Microscope::Application | Libraries/Microscope.Application | Application-side persistence, Slide/StageSlot assignments, registration and capability dispatch primitives |

Dependency direction:

~~~text
Camera::Common             (independent capability)
Microscope::Core           (domain + backend-neutral contracts)
        |
        v
Controller::Common         (runtime stage mechanics)
        |
        v
Microscope::Application    (Slide association/persistence + dispatch integration)
~~~

Microscope::Core does not depend on Controller, Application, Camera, AmScope, GenTL, Qt, JSON, DICOM, Swift/Objective-C++ or operating-system APIs. Controller::Common depends on Core only. The Application target uses Core, Controller and Camera::Common.

## Stage ownership

Controller/ is the sole C++ runtime owner of the physical Stage. MicroscopeInstance owns Stage; Stage owns installed LinearAxis/RotationAxis objects, StageConfiguration values and N StageSlots, global machine position evidence and pending motion plans.

There is no second stage model in Application. The retired Core canvas/fixed-carrier/dual-slide-navigation and Application navigation runtimes are removed from the canonical build. Application retains only Slide-to-StageSlot assignment and measured StageSlot registration, because Slide records belong to the application.

The physical adapter target does not exist yet. A concrete controller adapter belongs under Controller/Adapters/<controller> and implements the existing Core IStageMotionPort. It must not add another Stage or driver-port abstraction.

## Build without AmScope

The root project includes Camera::Common, Core, Controller and Application. On a non-macOS host:

~~~sh
cmake -S . -B build/platform-libraries \
  -DOPTIKA_BUILD_AMSCOPE=OFF \
  -DOPTIKA_BUILD_NATIVE_APP=OFF \
  -DBUILD_TESTING=ON
cmake --build build/platform-libraries --parallel
ctest --test-dir build/platform-libraries --output-on-failure
~~~

The native OPTIKA B293 application is macOS-only and requires the AmScope capability to be enabled.

## Standalone targets

Core:

~~~sh
cmake -S Libraries/Microscope.Core -B build/microscope-core -DBUILD_TESTING=ON
cmake --build build/microscope-core --parallel
ctest --test-dir build/microscope-core --output-on-failure
~~~

Controller/Common:

~~~sh
cmake -S Controller/Common -B build/controller-common -DBUILD_TESTING=ON
cmake --build build/controller-common --parallel
ctest --test-dir build/controller-common --output-on-failure
~~~

Application standalone configuration adds its Core, Camera::Common and Controller dependencies:

~~~sh
cmake -S Libraries/Microscope.Application -B build/microscope-application -DBUILD_TESTING=ON
cmake --build build/microscope-application --parallel
ctest --test-dir build/microscope-application --output-on-failure
~~~

These commands are instructions only; no build or test result is implied by this document.

## Current implementation boundary

- **Core:** equipment topology and specifications, versioned StageConfiguration/StageSlot specifications, strong identifiers, result/status and motion command/feedback contracts, operation runtime primitives and sample/Slide domain values.
- **Controller:** MicroscopeInstance, Stage, LinearAxis, RotationAxis, StageConfiguration, StageSlot, correlated StageMotionPlan lifecycle, homing/reference semantics, refreshPosition and Core command translation. Per-axis measured-position/uncertainty reconciliation and the separate Stage calibration lifecycle remain outstanding.
- **Application:** Slide persistence, StageSlotAssignments, StageSlotRegistration, append-only assignment/registration snapshots, equipment JSON/SQLite persistence and correlated Stage command dispatch. It does not own the physical Stage runtime model.
- **Hardware adapter:** no concrete IStageMotionPort implementation is present yet. Controller model and backend-neutral port exist; concrete device protocol, channel mapping, homing and feedback implementation remain.
- **Camera:** the Optika branch contains its current AmScope arrangement. The newer reusable Camera capability with AmScope and GenTL is on camera-v.2.0 and has not yet been integrated into this branch. Keep camera ownership, vendor SDK lifecycles and frame delivery inside Camera.
- **Acquisition:** Swift owns acquisition orchestration, capture workflow, Slide folder/image-tile organization, persistence and gallery lifecycle. Do not create a parallel C++ acquisition implementation.
- **ImageProcessing:** remains a separate repository.

See docs/architecture/MICROSCOPE_PLATFORM_COMPLETION_AUDIT_V0.9.md and docs/architecture/CONTROLLER_STAGE_AND_HARDWARE_ADAPTER_CONTRACT_V1.md for the current gap inventory and adapter input checklist.
