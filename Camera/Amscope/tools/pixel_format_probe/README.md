# Standalone Amcam SDK Pixel-Format Probe

This is a standalone C++ executable that includes the vendor's `amcam.h` and links directly to `libamcam.dylib`. It does not link to `amscope_driver`, `amscope_qt`, Qt, the preview application, or application feature-discovery code.

## What it reads

- SDK version and enumerated device/model identity.
- Complete model flag value and RAW-related model capability flags.
- `Amcam_get_MaxBitDepth` result, interpreted as the maximum depth when its successful return value is between 8 and 16.
- Current `AMCAM_OPTION_RAW`, `AMCAM_OPTION_BITDEPTH`, and `AMCAM_OPTION_PIXEL_FORMAT` values.
- Count and entries returned by `Amcam_get_PixelFormatSupport`.
- Current RAW FourCC and bits-per-pixel from `Amcam_get_RawFormat`.

The default probe is read-only. It does not call any `Amcam_put_*` function, change camera options, start acquisition, or capture frames. An additional opt-in RAW-mode test is available; see below.

## Run

Close the microscope application and any other program that may have the camera open. From the repository root:

```bash
git fetch origin
git switch amscope-native-driver
git pull --ff-only origin amscope-native-driver
bash scripts/run-amscope-sdk-probe.sh
```

With one connected camera, it selects the camera automatically. With multiple cameras, it prints the list and asks for an index. To select one directly:

```bash
bash scripts/run-amscope-sdk-probe.sh --index 0
```

The script builds the independent target under `build/amscope-sdk-probe`. It does not configure or launch the main application and does not require Qt.

### Probe the actual RAW output mode

The camera's current setting can be RGB mode (`AMCAM_OPTION_RAW=0`). In that mode, `Amcam_get_RawFormat` is not a sufficient measurement of the available sensor RAW output. The vendor SDK documentation says this option can only be changed while the camera is not running.

Close all camera applications, then run:

```bash
bash scripts/run-amscope-sdk-probe.sh --probe-raw-mode 2>&1 | tee amscope-sdk-probe-raw.txt
```

This opt-in mode temporarily requests `AMCAM_OPTION_RAW=1`, reads maximum bit depth, the legacy bit-depth option, current pixel format, the full pixel-format list, and RAW FourCC/bits-per-pixel, then restores and verifies the original RAW-mode setting. It does not start acquisition or modify the selected pixel format. **Do not unplug the camera or interrupt the program during this test.** If restoration fails, the program reports a critical warning; close/reopen or power-cycle the camera before using it again.

The default run remains strictly read-only. The RAW-mode run intentionally changes one setting temporarily and must be used only when the camera is stopped.

## Compare with the application

Run the probe with the application closed, then run the application and use **Refresh camera features**. Compare the enumerated model name and flags, `Amcam_get_MaxBitDepth`, legacy `AMCAM_OPTION_BITDEPTH`, pixel-format count and entries, current `AMCAM_OPTION_PIXEL_FORMAT`, and RAW mode/format.

- Matching model flags and pixel-format lists mean the application is not dropping formats advertised by the SDK.
- Additional formats in the direct probe point to a difference in the application's probe timing, camera handle, or feature-discovery path.
- If both report only `RAW8` and the model flags contain only `AMCAM_FLAG_RAW8` among the RAW precision flags, the installed SDK does not advertise a higher precision mode for this connected device.
- Different model flags between direct enumeration and the application indicate a model-descriptor discrepancy to investigate separately from the selector.

Save both complete terminal outputs and provide them alongside the application's diagnostic text. Compare the USB connection flag as well: `AMCAM_FLAG_USB30_OVER_USB20` indicates a USB 3.0 camera connected over USB 2.0. Re-run after connecting directly through a known USB 3-capable port and cable, with no USB 2.0 hub or adapter in the path. This probe establishes what the installed vendor SDK advertises and returns; it does not independently establish the sensor's physical ADC depth.
