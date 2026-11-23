# Standalone Amcam SDK Pixel-Format Probe

This is a standalone C++ executable that includes the vendor's `amcam.h` and links directly to `libamcam.dylib`. It does not link to `amscope_driver`, `amscope_qt`, Qt, the preview application, or application feature-discovery code.

## What it reads

- SDK version, enumerated device/model identity, and camera revision, firmware, hardware, production-date, and FPGA metadata (where supported).
- Complete model flag value and RAW-related model capability flags.
- The SDK model's USB transport classification flag, explicitly distinguished from the negotiated physical USB link.
- `Amcam_get_MaxBitDepth` result, interpreted as the maximum depth when its successful return value is between 8 and 16.
- Current `AMCAM_OPTION_RAW`, `AMCAM_OPTION_BITDEPTH`, `AMCAM_OPTION_RGB`, and `AMCAM_OPTION_PIXEL_FORMAT` values. The RGB option is decoded separately because it controls processed RGB/Grey output representation, not RAW sensor precision.
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

### Capture RAW frames and validate acquisition data

For a one-frame baseline, close the microscope application and run:

\`\`\`bash
bash scripts/run-amscope-sdk-probe.sh --capture-raw-frame 2>&1 | tee amscope-raw-frame-capture.txt
\`\`\`

For the acquisition-validity test, capture eight consecutive frames:

\`\`\`bash
bash scripts/run-amscope-sdk-probe.sh --capture-raw-sequence 2>&1 | tee amscope-raw-sequence.txt
\`\`\`

Both modes temporarily enable RAW mode if necessary, read the active RAW FourCC and significant depth, allocate a buffer sized for up to 16-bit words, start pull-mode acquisition, stop acquisition, and restore/verify the original RAW-mode setting. The sequence mode records per-frame sequence number, timestamp, V4 metadata, dimensions, payload size, sample range, distinct-value count, sentinel-byte matches, and FNV-1a 64-bit checksum. It also reports a histogram of all callback event codes, trigger mode, configured and actual exposure time, callback-thread option, frame-info validity flags, selected error/timeout/disconnect counts, and the SDK software-dropped-frame counter delta when available. The previous generated `amscope-raw-frame.raw` is removed before acquisition; a new file is written only if at least one frame is pulled. This prevents a failed run from leaving a stale file that can be mistaken for a new capture. Copy a successful payload before the next capture.

The sequence diagnostic initializes the caller buffer to \`0xA5\` before each pull. A high count of remaining sentinel-valued bytes can indicate that the SDK did not overwrite the expected payload region; individual matches are not proof of an unwritten byte because a real sample may also equal \`0xA5\`. Advancing sequence/timestamp metadata does not, by itself, prove that image content is fresh. Compare dark and illuminated runs and retain both logs and binary payloads.

Example paired captures:

\`\`\`bash
bash scripts/run-amscope-sdk-probe.sh --capture-raw-sequence 2>&1 | tee amscope-raw-sequence-dark.txt
cp amscope-raw-frame.raw amscope-raw-sequence-dark.raw

# Change only the optical condition to normal illumination.
bash scripts/run-amscope-sdk-probe.sh --capture-raw-sequence 2>&1 | tee amscope-raw-sequence-lit.txt
cp amscope-raw-frame.raw amscope-raw-sequence-lit.raw
\`\`\`

Do not interrupt the process while it is stopping acquisition and restoring the original RAW setting. The full MU503 defect record, measured evidence, and next diagnostic gates are in [MU503_RAW_ACQUISITION_PROBLEM.md](../../../../docs/testing/hardware/MU503_RAW_ACQUISITION_PROBLEM.md).

## Interpreting the generic MU Series bit-depth manual

The MU Series Windows manual documents an 8-bit/16-bit **camera bit-depth control**, but it is a series-wide manual and does not establish that every MU model supports both modes. There is also a wording discrepancy inside the vendor SDK package: the C header defines legacy `AMCAM_OPTION_BITDEPTH` as 0 = 8-bit and 1 = 16-bit, while the SDK HTML guide describes 0 = 8-bit and 1 = the camera's maximum bit depth. Treat that legacy option as model-specific and do not infer support from the generic definition.

For this MU503, `Amcam_get_MaxBitDepth` returned 8, the modern pixel-format enumeration reported only `RAW8`, and reading `AMCAM_OPTION_BITDEPTH` returned `0x80004001` (`E_NOTIMPL`), including with RAW mode enabled. The camera therefore advertises no higher-precision RAW mode through this SDK. The SDK also has a separate `AMCAM_OPTION_RGB` setting for processed output: RGB48 and RGB64 require camera bit depth greater than 8, while Grey16 is restricted to monochrome cameras with bit depth greater than 8. This is not a mechanism for recovering precision that the camera does not supply. The probe now reads the current RGB-output setting, but does not change it.

The image-pull API's `bits` argument selects the returned processed-image buffer representation; the SDK explicitly says this argument is ignored in RAW mode. Consequently, a 16-bit buffer/container alone is not evidence of more than 8 bits of sensor information.

## Compare with the application

Run the probe with the application closed, then run the application and use **Refresh camera features**. Compare the enumerated model name and flags, `Amcam_get_MaxBitDepth`, legacy `AMCAM_OPTION_BITDEPTH`, pixel-format count and entries, current `AMCAM_OPTION_PIXEL_FORMAT`, and RAW mode/format.

- Matching model flags and pixel-format lists mean the application is not dropping formats advertised by the SDK.
- Additional formats in the direct probe point to a difference in the application's probe timing, camera handle, or feature-discovery path.
- If both report only `RAW8` and the model flags contain only `AMCAM_FLAG_RAW8` among the RAW precision flags, the installed SDK does not advertise a higher precision mode for this connected device.
- Different model flags between direct enumeration and the application indicate a model-descriptor discrepancy to investigate separately from the selector.

Save both complete terminal outputs and provide them alongside the application's diagnostic text. The `AMCAM_FLAG_USB30_OVER_USB20` bit is an SDK model-descriptor classification, not a measurement of the negotiated USB link. The SDK display name may also include a transport suffix that conflicts with the operating system's live topology. Verify physical link speed independently through the host OS; do not infer USB2 fallback from this flag alone. This probe establishes what the installed vendor SDK advertises and returns; it does not independently establish the sensor's physical ADC depth.
