# OPTIKA B293 Dual-Slide Application

Build the native macOS application from the repository root.

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
  -DCMAKE_BUILD_TYPE=Release

cmake --build build/optika-b293-ninja --parallel
ctest --test-dir build/optika-b293-ninja --output-on-failure
```

After a successful build, locate and launch the app bundle:

```bash
APP_PATH="$(find build/optika-b293-ninja -type d -name 'optika_b293_dualslide_app.app' -print -quit)"
if [ -n "$APP_PATH" ]; then
  open "$APP_PATH"
else
  echo "Could not find optika_b293_dualslide_app.app under build/optika-b293-ninja" >&2
  exit 1
fi
```

Use a fresh build directory when changing CMake generators. CMake does not
allow an existing build tree configured with one generator to be reused with
another.

Camera remains a reusable capability under `Camera/Amscope`; the workstation UI,
workflows, adapters and microscope configuration are owned by this application.
