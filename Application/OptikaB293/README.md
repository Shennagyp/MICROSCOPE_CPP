# OPTIKA B293 Dual-Slide Application

Build the native macOS application from the repository root. **Use the Xcode
generator**: this target enables the Swift language, which CMake supports with
the Xcode generator on macOS.

```bash
cmake -S . -B build/optika-b293-xcode -G Xcode \
  -DOPTIKA_BUILD_NATIVE_APP=ON \
  -DAMSCOPE_BUILD_QT_ADAPTER=OFF \
  -DAMSCOPE_BUILD_PREVIEW_APP=OFF \
  -DBUILD_TESTING=ON

cmake --build build/optika-b293-xcode --config Release --parallel
ctest --test-dir build/optika-b293-xcode -C Release --output-on-failure

APP_PATH="$(find build/optika-b293-xcode -type d -name 'optika_b293_dualslide_app.app' -print -quit)"
if [ -n "$APP_PATH" ]; then
  open "$APP_PATH"
else
  echo "Could not find optika_b293_dualslide_app.app under build/optika-b293-xcode" >&2
  exit 1
fi
```

Use a fresh build directory when changing CMake generators. CMake does not
allow an existing build tree configured with Unix Makefiles to be reused as an
Xcode build tree.

Camera remains a reusable capability under `Camera/Amscope`; the workstation UI,
workflows, adapters and microscope configuration are owned by this application.
