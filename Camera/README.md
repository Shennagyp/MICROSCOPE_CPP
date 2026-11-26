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

The AmScope package installs the vendor runtime library beside its driver;
SDK redistribution approval remains a Phase 8 gate. GenTL packaging exports
the C++ library and headers only. It does not bundle a vendor CTI Producer or
the separately supplied GenTL C ABI bridge runtime.

CAMERA_MODULE_BUILD_PREVIEW_APP is an optional local application build, not
a library package component. Qt remains optional: Common, GenTL and native
AmScope consumers do not acquire Qt unless they request a Qt component.

External-consumer fixtures under tests/consumers configure against an
installed, relocated prefix. They do not use source-tree include paths or
open a physical camera.
