# OPTIKA B293 Dual-Slide Application

Build the native macOS application from the repository root. The default
minimum deployment target is macOS 11.0, matching the supported minimum of the
current libc++ toolchain.

The application enables CMake's Swift language support. Two generators are
available for the native macOS target:

- **Ninja** works with the standalone Command Line Tools toolchain when CMake
  can discover Swift. CMake's Ninja Swift support is experimental.
- **Xcode** requires the full Xcode app to be installed and selected as the
  active developer directory. The standalone Command Line Tools package does
  not include `xcodebuild`.

For a machine using Command Line Tools, first verify the required tools:

```bash
cmake --version
swiftc --version
ninja --version
```

Then configure, build and test with Ninja:

```bash
cmake -S . -B build/optika-b293-ninja -G Ninja \
  -DOPTIKA_BUILD_NATIVE_APP=ON \
  -DAMSCOPE_BUILD_QT_ADAPTER=OFF \
  -DAMSCOPE_BUILD_PREVIEW_APP=OFF \
  -DBUILD_TESTING=ON \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build/optika-b293-ninja --parallel
ctest --test-dir build/optika-b293-ninja --output-on-failure
```

Launch the app with the repository launcher:

```bash
./scripts/run-optika-b293.sh
```

The launcher resolves the repository root from its own location, configures the
existing Ninja build directory, builds the native target, verifies the expected
bundle path, and opens it. It does not depend on the caller's working directory
or a shell-side bundle search.

Use a fresh build directory when changing CMake generators. CMake does not
allow an existing build tree configured with one generator to be reused with
another.

## Build the SwiftUI application alongside the Qt preview

The SwiftUI application and the Qt preview are separate frontends over the same
AmScope driver. They can be enabled in one macOS build without replacing,
disabling, or linking Qt into the SwiftUI executable. Use a separate build
directory so this combined configuration does not change the standalone
launcher configuration above.

With Qt 6 (Core and Widgets), Swift, CMake and Ninja installed:

```bash
cmake -S . -B build/dual-ui-ninja -G Ninja \
  -DOPTIKA_BUILD_NATIVE_APP=ON \
  -DOPTIKA_BUILD_AMSCOPE=ON \
  -DAMSCOPE_BUILD_QT_ADAPTER=ON \
  -DAMSCOPE_BUILD_PREVIEW_APP=ON \
  -DBUILD_TESTING=ON \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build/dual-ui-ninja \
  --target optika_b293_dualslide_app amscope_preview --parallel
ctest --test-dir build/dual-ui-ninja --output-on-failure
```

The native bundle is
`build/dual-ui-ninja/Application/OptikaB293/optika_b293_dualslide_app.app`;
the Qt preview executable is
`build/dual-ui-ninja/Camera/Amscope/amscope_preview`.

Launch either frontend independently:

```bash
open build/dual-ui-ninja/Application/OptikaB293/optika_b293_dualslide_app.app
./build/dual-ui-ninja/Camera/Amscope/amscope_preview
```

The SwiftUI target links `Microscope::Application`, `Microscope::Core` and
`Amscope::Driver`. The Qt target continues to link `Amscope::Qt` and
`Camera::QtWidgets`. Neither frontend owns or changes the other's UI code.

This target is the current native macOS frontend and transitional implementation. The target platform separates the C++ `Microscope.Core` and `Microscope.Application` libraries from this frontend. Microscope definitions are JSON data, and microscope behavior migrates into Core incrementally. See the [platform architecture](../../docs/architecture/MICROSCOPE_PLATFORM_ARCHITECTURE_V0.9.md), [Core/Application public contracts](../../docs/architecture/MICROSCOPE_CORE_APPLICATION_CONTRACTS_V0.9.md), [C++ library build guide](../../Libraries/README.md) and [implementation plan](../../docs/plans/MICROSCOPE_PLATFORM_IMPLEMENTATION_V0.9.md). The existing AmScope driver and preview pipeline remain reusable capabilities and must be preserved during migration.
