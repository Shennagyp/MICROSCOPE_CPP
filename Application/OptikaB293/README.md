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

This is the current stateful native macOS application. Its product-level object ownership, application lifetime, subsystem boundaries and implementation order are defined in the [OPTIKA B293 Application System Architecture](../../docs/architecture/OPTIKA_B293_APPLICATION_SYSTEM_ARCHITECTURE_V1.md). Microscope-domain and C++ library semantics are defined in the [platform architecture](../../docs/architecture/MICROSCOPE_PLATFORM_ARCHITECTURE_V0.9.md) and [Core/Application public contracts](../../docs/architecture/MICROSCOPE_CORE_APPLICATION_CONTRACTS_V0.9.md). The [stage architecture](../../docs/architecture/STAGE_WORKING_SURFACE_AND_AXIS_REFERENCE_V2.md) defines the physical XY working surface and independent focus Z. See the [C++ library build guide](../../Libraries/README.md) and [implementation plan](../../docs/plans/MICROSCOPE_PLATFORM_IMPLEMENTATION_V0.9.md). The existing AmScope driver and preview pipeline remain reusable capabilities and must be preserved during migration.
