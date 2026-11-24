# OPTIKA B293 Dual-Slide Application

Build the native macOS application from the repository root:

```bash
cmake -S . -B build/optika-b293 \
  -DOPTIKA_BUILD_NATIVE_APP=ON \
  -DAMSCOPE_BUILD_QT_ADAPTER=OFF \
  -DAMSCOPE_BUILD_PREVIEW_APP=OFF \
  -DBUILD_TESTING=ON
cmake --build build/optika-b293 --config Release --parallel
open build/optika-b293/Application/OptikaB293/optika_b293_dualslide_app.app
```

Camera remains a reusable capability under `Camera/Amscope`; the workstation UI,
workflows, adapters and microscope configuration are owned by this application.
