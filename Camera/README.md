# Camera Module package

The reusable package entry point is this directory, separate from the
application-oriented repository root:

```sh
cmake -S Camera -B build/camera -DCAMERA_MODULE_BUILD_GENTL=ON -DCAMERA_MODULE_BUILD_AMSCOPE=OFF
cmake --build build/camera
cmake --install build/camera --prefix build/camera-install
```

Consumers request only the components they use:

| Component | Exported target | Dependency |
|---|---|---|
| Common | Camera::Common | C++17 |
| GenTL | Camera::GenTLConsumerBridge | Common |
| GenTLGenApi | Camera::GenTLGenApi | GenTL and official GenApi 3.5.1 CMake package |
| Amscope | Amscope::Driver | Common; macOS vendor SDK |
| Qt | Camera::Qt | Qt 6 Core |
| QtWidgets | Camera::QtWidgets | Qt 6 Widgets |
| AmscopeQt | Amscope::Qt | Amscope, Camera::Qt and Qt 6 Core |

Example consumer:

```cmake
find_package(CameraModule 2 CONFIG REQUIRED COMPONENTS GenTL)
target_link_libraries(my_app PRIVATE Camera::GenTLConsumerBridge)
```

Common is always loaded as the base contract. Optional dependencies are
resolved only for requested components. A package configured with GenApi
requires the official GenApi 3.5.1 CMake package; exported targets must not
contain machine-specific SDK include or library paths.

The AmScope package installs the driver but does **not** bundle the vendor
`libamcam.dylib` runtime by default. Set
`CAMERA_MODULE_INSTALL_AMSCOPE_VENDOR_RUNTIME=ON` only after confirming that
the applicable AmScope SDK terms permit redistribution. When disabled, the
consumer must supply a compatible vendor runtime and make it available beside
the driver (the driver uses its loader-relative runtime path). The installed
package config exposes `CameraModule_Amscope_VENDOR_RUNTIME_INCLUDED`.

The checked-in vendor SDK tree contains no license/notice/EULA file. AmScope's
[software downloads page](https://amscope.com/pages/software-downloads) points
to software licensing terms, but the applicable SDK redistribution grant is
not captured in this repository. Treat redistribution of both the linked
driver binary and vendor runtime as unresolved until the actual SDK terms or
written vendor permission are obtained. Package CI enables the runtime option
only for ephemeral internal validation; that CI package is not a release
artifact.

GenTL packaging exports the C++ library and headers only. It does not bundle
a vendor CTI Producer or the separately supplied GenTL C ABI bridge runtime.

CAMERA_MODULE_BUILD_PREVIEW_APP is an optional local application build, not
a library package component. Qt remains optional: Common, GenTL and native
AmScope consumers do not acquire Qt unless they request a Qt component.

The AmScope preview executable's `--list-cameras` diagnostic queries both
native AmScope enumeration and the GenTL `ProducerCatalog`. GenTL discovery
scans the `GENICAM_GENTL64_PATH` directories for CTI Producers and continues
past individual Producer failures. The diagnostic reports each backend
separately and combines the discovered-device count. The interactive preview
and camera-control session remains AmScope-specific; listing a GenTL device
does not imply that this preview application can open or control it.

External-consumer fixtures under tests/consumers configure against an
installed, relocated prefix. They do not use source-tree include paths or
open a physical camera. CI runs them to verify linking and runtime loading.
They are not the complete Phase 7 acceptance test: the original roadmap also
requires installed-artifact physical smoke with an AmScope camera (non-Qt and
Qt consumers) and a GenTL CTI camera. Vendor SDK redistribution requirements
must be documented before publishing; full provider/model qualification is
retained for Phase 8.

The initial supported platform scope remains macOS Intel x86_64 and Apple
Silicon arm64. Do not infer Windows/Linux support from the platform-neutral
GenTL source or the presence of unrelated vendor binaries.

## Architecture and continuation

For the complete Camera v2.0 architecture, OOP/class relationships, lifecycle
diagrams, frame-memory rules, provider functionality, build/package boundaries,
test evidence and known limitations, see:

- [Camera v2.0 Architecture and OOP Reference](../docs/architecture/CAMERA_V2_ARCHITECTURE_AND_OOP.md)
- [Current development status and evidence ledger](../docs/architecture/CAMERA_V2_DEVELOPMENT_STATUS.md)
- [Current continuation handoff](../docs/architecture/NEXT_AI_HANDOFF.md)
- [Common/provider-specific type ownership](../docs/architecture/CAMERA_COMMON_TYPE_OWNERSHIP.md)
- [Phase 6 capability audit](../docs/architecture/CAMERA_V2_PHASE6_CAPABILITY_AUDIT.md)

The Camera library owns camera discovery, camera sessions, acquisition, frame
representation, camera features/commands and transport telemetry. Microscope
optics, stage/controller, slide/rack mapping, WSI tile scheduling and durable
tile storage, stitching and image processing belong to the consuming
microscope application or their separate libraries. The Optika SwiftUI
application is a consumer of the native camera API, not a dependency of
Camera.Common.
