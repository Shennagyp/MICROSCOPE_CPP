# AmScope MU503 — RAW Acquisition Failure Record

**Status:** OPEN — acquisition data validity is not established  
**Scope:** Exact physical camera tested on macOS; native Amcam SDK RAW pull path  
**First recorded:** 2026-11-23  
**Repository branch at investigation start:** \`amscope-native-driver\`  
**Branch head at investigation start:** \`a65bfeb92ff64bc009dd7da51662c4b1bb1c2f66\`

> This record separates measured facts, vendor-advertised capabilities, external sensor specifications, and hypotheses. A successful SDK return code is not treated as proof that the returned image bytes are valid live sensor data.

## 1. Problem statement

The MU503 is enumerated and opened by the Amcam SDK. The SDK accepts the transition to RAW mode, starts pull-mode acquisition, signals an image event, and returns success from \`Amcam_PullImageV4\`. However, the captured RAW payload consists entirely of byte value \`255 (0xFF)\`.

The same all-\`0xFF\` result was obtained in two deliberately different optical conditions:

1. **Dark capture:** illuminator switched off and optical path blocked.
2. **Illuminated capture:** normal illumination with a specimen in the optical path.

Both captures reported a 1280 × 960 frame, 1,228,800 payload bytes, SDK significant depth of 8 bits, observed range 255–255, and exactly one distinct sample value.

Therefore, ordinary optical saturation is not a sufficient explanation. The current evidence does **not** establish that the bytes returned by the RAW pull path represent fresh sensor samples. It also does not yet establish whether the fault is in the camera, firmware, SDK, callback/pull lifecycle, or this diagnostic program.

This is an acquisition-validity problem. It is separate from the question of whether the camera can expose more than 8 bits.

## 2. Physical device identity and host transport

### 2.1 SDK identity

| Property | Observed value |
|---|---|
| SDK display/model name | \`MU503\` |
| Camera ID at this connection | \`tp-1-2-0547-3511\` |
| USB VID:PID | \`0547:3511\` |
| VID decimal | \`1351\` |
| PID decimal | \`13585\` |
| Model flags, decimal | \`2164260961\` |
| Model flags, hexadecimal | \`0x81000061\` |
| RAW-related model flag | \`AMCAM_FLAG_RAW8\` |
| SDK transport flag | \`AMCAM_FLAG_USB30\` set |
| Hardware revision | \`2.0\` |
| Firmware | \`1.4.4.20200314\` |
| Production date | \`20210721\` |
| FPGA version query | \`E_NOTIMPL (0x80004001)\` |
| Amcam SDK | \`58.28273.20250415\` |

The unsupported FPGA-version query only means that this SDK/API does not provide that value for this device. It is not evidence that the camera has no FPGA.

The camera ID is an opaque connection identifier and may change after reconnect/restart. Do not use it as a persistent hardware identity.

### 2.2 Independently observed USB link

macOS \`ioreg -p IOUSB -l -w 0\` showed:

- Camera is a direct child of the host controller, not downstream of the USB 2.0 hub.
- \`USBSpeed=4\`.
- \`UsbLinkSpeed=5000000000\` (5 Gbit/s).
- \`bcdUSB=768\` (USB 3.0).
- \`Device Speed=3\`.
- Product string: \`USB3.0 Camera\`.
- \`locationID=18874368\`.
- VID/PID agree with the SDK identity.

**Transport conclusion:** the tested camera was physically negotiated at USB 3.0 SuperSpeed (5 Gbit/s). A USB 2.0 fallback is not a supported explanation for the all-\`0xFF\` payload in this test. The SDK model flag and the live OS link measurement are separate sources; the latter is authoritative for the negotiated link.

No cable/port troubleshooting is currently justified by these measurements.

## 3. RAW capability and bit-depth observations

The direct SDK probe produced the following results:

| API / property | Result |
|---|---|
| \`Amcam_get_MaxBitDepth\` | \`8\` |
| \`AMCAM_OPTION_RAW\` before test | \`0\` (processed RGB mode) |
| \`AMCAM_OPTION_RAW\` after temporary write | \`1\`, readback confirmed |
| \`AMCAM_OPTION_BITDEPTH\` | \`E_NOTIMPL (0x80004001)\` |
| \`AMCAM_OPTION_RGB\` | \`0\` = RGB24 |
| \`AMCAM_OPTION_PIXEL_FORMAT\` | \`0\` = RAW8 |
| \`Amcam_get_PixelFormatSupport\` | count 1; only RAW8 |
| \`Amcam_get_RawFormat\` | FourCC \`RGGB\`, 8 bits per pixel |
| RAW mode restoration | original value \`0\` restored and read back |

The modern pixel-format API enumerates only RAW8. The legacy bit-depth option is unsupported. Enabling RAW mode does not reveal another format or a higher bit depth.

The bundled SDK header defines these pixel-format flags and codes:

- \`AMCAM_FLAG_RAW8\` / \`AMCAM_PIXELFORMAT_RAW8\`
- \`AMCAM_FLAG_RAW10\` / \`AMCAM_PIXELFORMAT_RAW10\`
- \`AMCAM_FLAG_RAW12\` / \`AMCAM_PIXELFORMAT_RAW12\`
- \`AMCAM_FLAG_RAW14\` / \`AMCAM_PIXELFORMAT_RAW14\`
- \`AMCAM_FLAG_RAW16\` / \`AMCAM_PIXELFORMAT_RAW16\`

The existence of generic SDK constants does not mean this MU503 advertises those formats.

### 3.1 Sensor silicon versus camera output

The official AmScope MU503 product page identifies the sensor as an Aptina/ON Semiconductor MT9P006 CMOS color sensor and advertises 5 MP, USB 3.0, and up to 2560 × 1922 resolution.

The MT9P006 datasheet specifies a 12-bit ADC and 12-bit parallel sensor output (\`DOUT[11:0]\`). This establishes a capability of the sensor IC, not the bit depth delivered by the complete MU503 camera to the host.

A third-party MU503 reseller listing describes a “12-bit Parallel” A/D converter and “8-bit R.G.B to computer.” This is consistent with—but does not prove—a camera pipeline that converts the sensor's 12-bit output to 8-bit before USB delivery.

**Defensible conclusion:** the MT9P006 silicon supports 12-bit conversion, while this MU503 + current firmware + installed SDK combination advertises only RAW8 to the host. The physical camera may down-convert internally. The evidence does not prove that the MU503's controller/firmware can never expose RAW12 through another supported configuration or firmware.

### 3.2 The generic MU Series 16-bit manual

The generic MU Series Windows manual contains an 8 Bits / 16 Bits control. It is a series-wide manual, not a MU503-specific capability declaration. Its presence does not override the exact model's runtime enumeration.

The manual also contains an apparent technical error describing 16-bit storage as holding “32 different color values.” This wording must not be used as a technical basis for interpreting sensor precision.

The SDK's legacy \`AMCAM_OPTION_BITDEPTH\` documentation is inconsistent: the C header describes 0 = 8-bit and 1 = 16-bit, while the SDK HTML guide describes 0 = 8-bit and 1 = camera maximum. The MU503 returns \`E_NOTIMPL\` for this option, so this ambiguity does not establish a usable mode on this device.

The \`bits\` parameter of \`Amcam_PullImageV4\` selects the representation of processed output and is explicitly ignored in RAW mode. A 16-bit container would not, by itself, prove more than 8 significant sensor bits.

## 4. Original one-frame RAW capture

The diagnostic sequence was:

1. Read original \`AMCAM_OPTION_RAW\`.
2. Temporarily set RAW mode to 1 and verify by readback.
3. Read FourCC and significant depth.
4. Read final acquisition dimensions.
5. Allocate a buffer for up to two bytes per pixel.
6. Start \`Amcam_StartPullModeWithCallback\`.
7. On the first \`AMCAM_EVENT_IMAGE\`, call \`Amcam_PullImageV4(camera, buffer, 0, 0, -1, &info)\`.
8. Save the unprocessed payload.
9. Stop acquisition.
10. Restore and verify the original RAW mode.

The SDK header states that \`AMCAM_EVENT_IMAGE\` means a live image arrived and that the application should call a \`PullImage\` function to retrieve it. It also explicitly prohibits calling \`Amcam_Stop\` or \`Amcam_Close\` from the callback context. The probe calls Stop from the main thread.

### 4.1 Dark capture

- Optical path blocked.
- Illuminator switched off.
- SDK acquisition and pull calls returned success.
- Frame dimensions: 1280 × 960.
- Payload bytes: 1,228,800.
- Significant depth: 8 bits per pixel.
- Payload storage width: 8 bits.
- Minimum sample: 255.
- Maximum sample: 255.
- Distinct values: 1 / 256.
- RAW mode restored to original value 0.

### 4.2 Illuminated capture

- Normal illuminator enabled.
- Specimen/normal optical path.
- SDK acquisition and pull calls returned success.
- Frame dimensions: 1280 × 960.
- Payload bytes: 1,228,800.
- Significant depth: 8 bits per pixel.
- Payload storage width: 8 bits.
- Minimum sample: 255.
- Maximum sample: 255.
- Distinct values: 1 / 256.
- RAW mode restored to original value 0.

### 4.3 What this establishes

- The SDK accepts RAW mode and returns successful acquisition/pull results.
- The returned payload did not respond to a large change in optical input.
- The same all-\`0xFF\` payload was observed in both conditions.
- The original RAW-mode setting was restored successfully.

### 4.4 What this does not establish

- It does not prove that the sensor itself is saturated.
- It does not prove that the sensor is defective.
- It does not prove that the camera firmware is defective.
- It does not prove that the SDK is defective.
- It does not prove that the callback/pull pairing is correct for this exact camera/SDK combination in practice.
- It does not prove that the returned buffer contains a fresh frame.
- It does not prove that the entire allocated buffer was written by the SDK.
- It does not prove the official AmScope application has the same failure.

The first-frame probe accepted one successful pull and did not inspect frame sequence/timestamp, compare consecutive frames, or initialize the buffer with a sentinel. Therefore it could not distinguish a valid all-white image from stale, unwritten, or otherwise invalid data.

## 5. Current defect classification

**Primary defect:** RAW frame payload validity / freshness is unverified and observed payload is invariant at all-\`0xFF\` under dark and illuminated conditions.

**Affected path:**

\`MU503 sensor → camera controller / ISP / firmware → USB transport → Amcam SDK frame queue → AMCAM_EVENT_IMAGE callback → Amcam_PullImageV4 → caller buffer\`

The failure could be at any stage from sensor readout through host buffer delivery. The current evidence localizes the symptom to the returned RAW payload but does not yet isolate the failing stage.

### 5.1 Candidate causes, ordered by diagnostic priority

1. **Buffer/pull validity:** SDK returns success but does not overwrite all or part of the caller buffer, or the caller is interpreting the returned frame layout incorrectly.
2. **Frame freshness / queue semantics:** the callback is associated with an image event, but the pull returns stale or cached data, or repeated data is being delivered.
3. **RAW-mode path:** the camera's RAW mode behaves differently from its processed RGB path, or RAW mode is not fully applied in the way assumed.
4. **Camera configuration / sensor state:** exposure, shutter, trigger, sensor readout, black-level, or another device state prevents meaningful output.
5. **SDK/model/firmware interaction:** this installed SDK or firmware has a MU503-specific RAW acquisition problem.
6. **Camera hardware fault:** sensor, sensor interface, controller, memory, or another hardware stage is faulty. This remains possible but is not established.

These are hypotheses, not findings. No firmware flash or undocumented register write is authorized by this record.

## 6. Development performed after the initial failure

The standalone C++ probe has been extended from a one-frame-only capture to support a multi-frame validity test.

New sequence mode:

\`\`\`bash
bash scripts/run-amscope-sdk-probe.sh --capture-raw-sequence 2>&1 \
  | tee amscope-raw-sequence.txt
\`\`\`

The sequence mode requests eight consecutive RAW frames. For each pulled frame it records:

- SDK frame sequence number (\`AmcamFrameInfoV3::seq\`).
- SDK frame timestamp in microseconds (\`timestamp\`).
- V4 \`framecount\`, \`timecount\`, and \`uFV\` metadata.
- Width, height, and computed payload byte count.
- Sample minimum and maximum.
- Number of distinct sample values.
- Number of payload bytes equal to the pre-pull sentinel value \`0xA5\`.
- FNV-1a 64-bit payload checksum.

It also reports:

- Image callback count.
- Number of successful frames pulled.
- SDK error, disconnect, and frame/packet timeout events.
- Software-dropped frame counter before and after acquisition, when supported.
- Number of distinct frame sequence values, timestamps, and payload checksums.

The first unmodified payload is written to \`amscope-raw-frame.raw\`. The diagnostic does not debayer, demosaic, scale, or transform the sample data.

The existing single-frame mode remains available:

\`\`\`bash
bash scripts/run-amscope-sdk-probe.sh --capture-raw-frame 2>&1 \
  | tee amscope-raw-frame-capture.txt
\`\`\`

The original setting is still restored after acquisition. Stop and restoration remain outside the callback.

### 6.1 Interpretation of the new diagnostics

- **All bytes remain 0xA5:** strong evidence that the SDK did not write the corresponding payload region. A sentinel match is a diagnostic indicator, not absolute proof for an individual byte because a real sample can coincidentally equal 0xA5.
- **Sequence/timestamp do not advance:** the callback/pull path may be returning repeated or stale frame metadata.
- **Sequence/timestamp advance but checksums remain identical and all samples are 255:** frames are being delivered with changing metadata, but image content remains invariant; investigate camera-side output and RAW pipeline next.
- **Checksums change and sample distribution changes:** the earlier one-frame result was not representative or the capture lifecycle changed; compare dark and illuminated sequence runs.
- **Pull or callback error/timeout/disconnect appears:** use the reported event and HRESULT as the next fault boundary.
- **Software dropped-frame counter increases:** investigate queue pressure and frame delivery. A zero delta does not prove freshness.
- **Official application works while this RAW sequence fails:** prioritize RAW-mode SDK behavior and the native probe contract.
- **Official application also shows a static/white image under the same optical conditions:** prioritize camera configuration, firmware/support diagnostics, and hardware isolation.

A unique sequence number alone does not prove that payload bytes are fresh. A changing checksum alone does not prove optical responsiveness. The dark-versus-lit comparison remains necessary.

## 6.2 Follow-up hardware run — 2026-11-23

The user pulled the sequence diagnostic and ran it under dark and illuminated conditions.

### Dark run

- Build succeeded on the user's Apple Silicon Mac.
- SDK enumerated the MU503 normally.
- RAW mode enabled and read back successfully.
- \`Amcam_StartPullModeWithCallback\` succeeded.
- One \`AMCAM_EVENT_IMAGE\` was received.
- One \`Amcam_PullImageV4\` succeeded.
- The pulled 1280 × 960 payload contained:
  - minimum = 77
  - maximum = 166
  - distinct sample values = 89
  - sentinel-valued bytes = 3
  - FNV-1a 64-bit checksum = \`0xDA9B498EF1A3E86F\`
- Reported frame metadata fields (\`seq\`, timestamp, \`framecount\`, \`timecount\`, \`uFV\`) were all zero.
- The eight-frame request then timed out after 30 seconds with only one image event.
- No SDK error, disconnect, or frame/packet timeout callback was counted.
- Software dropped-frame counter remained 0.
- RAW mode was restored and verified as 0.

**Interpretation:** unlike the previous all-\`0xFF\` result, this capture contained a broad distribution of sample values and only three sentinel matches. This strongly suggests that the SDK wrote most of the expected payload region on this run. It does not establish freshness or optical response. The frame-info validity bitmask was not previously printed, so zero metadata fields cannot be interpreted as valid zero sequence/timestamp values.

### Illuminated run

- SDK enumerated the MU503 normally.
- RAW mode enable/readback succeeded.
- Pull-mode start succeeded.
- Zero image events were received in 30 seconds.
- Zero frames were pulled.
- No SDK error, disconnect, or frame/packet timeout callback was counted.
- Software dropped-frame counter remained 0.
- RAW mode was restored and verified as 0.

**Important artifact-integrity caveat:** the capture program previously left an existing \`amscope-raw-frame.raw\` file untouched when no frame was captured. The shell command then unconditionally copied that path to \`amscope-raw-sequence-lit.raw\`. Because the dark run had already created the file, the purported illuminated binary may simply be a copy of the dark run. Do not compare or interpret the illuminated \`.raw\` artifact as a new capture. The illuminated terminal log is valid evidence of zero image events; the binary artifact is not established as fresh.

### Revised diagnosis

The previous hypothesis “the RAW pull path always returns all-255 data” is rejected. The new dark capture demonstrates at least one run in which the pulled payload was not constant and the caller buffer was almost entirely overwritten.

The current defect is more precisely described as **intermittent or absent RAW image-event delivery, with frame freshness and optical response still unverified**.

The new evidence does not yet distinguish among:
- camera/SDK acquisition lifecycle state;
- trigger mode or exposure duration;
- callback/event delivery behavior;
- camera-side stream start/readout;
- host-side SDK buffer queue behavior.

### Instrumentation added after this run

The probe now:
- records a histogram of every callback event code instead of only selected terminal events;
- reads and reports \`AMCAM_OPTION_TRIGGER\` before acquisition;
- reads configured and actual exposure time before acquisition;
- reads the dedicated callback-thread option;
- prints the V3 frame-info flag bitmask and labels sequence/timestamp/count/autofocus metadata as valid only when their corresponding flags are set;
- removes the previous generated \`amscope-raw-frame.raw\` artifact before starting a new capture, preventing a failed run from leaving a stale payload that can be copied as if it were fresh.

The removal behavior applies only to the probe's documented generated output path. Copy any prior capture before starting another run.

## 7. Required next investigation

### Gate A — Establish whether the camera works in the official application

This has not yet been answered in the investigation record.

With the native probe closed, open the official AmScope application for the MU series and verify:

1. Does it show a live image?
2. Does the displayed image change when the illuminator is switched off and the optical path is blocked?
3. Does it change again under normal illumination?
4. Does the application show the expected MU503 identity and resolution?

Record the application name/version and the observed behavior. Do not run the official application and the native probe simultaneously; the camera may be exclusively opened.

### Gate B — Run the new multi-frame RAW validity test

After the new branch commit has been pulled and compiled by the user:

\`\`\`bash
git fetch origin
git switch amscope-native-driver
git pull --ff-only origin amscope-native-driver

bash scripts/run-amscope-sdk-probe.sh --capture-raw-sequence 2>&1 \
  | tee amscope-raw-sequence-dark.txt
cp amscope-raw-frame.raw amscope-raw-sequence-dark.raw
\`\`\`

Repeat with normal illumination and specimen:

\`\`\`bash
bash scripts/run-amscope-sdk-probe.sh --capture-raw-sequence 2>&1 \
  | tee amscope-raw-sequence-lit.txt
cp amscope-raw-frame.raw amscope-raw-sequence-lit.raw
\`\`\`

Preserve both logs and both binary payloads. The capture command overwrites \`amscope-raw-frame.raw\` on each run; copy it immediately after each capture.

### Gate C — Compare RAW and processed acquisition

Only after Gate A and Gate B:

- Add or run a processed RGB24 pull using the same callback/pull lifecycle, with RAW mode disabled.
- Compare processed and RAW payload freshness, metadata progression, and optical response.
- If processed RGB responds but RAW remains all 255, isolate the RAW mode/format/pull path.
- If both remain invariant, investigate general acquisition state and camera-side readout before attributing the fault to RAW conversion.

Do not change multiple camera controls in one experiment. Preserve a baseline and change one reversible variable at a time.

### Gate D — Vendor support / firmware investigation

If the official application and both native acquisition paths fail, provide AmScope support with:

- Exact MU503 identity and firmware/hardware revision.
- SDK version and model flags.
- Dark/illuminated test evidence.
- Callback event counts and frame metadata.
- The binary RAW payload and checksums.
- Official application version and observed behavior.

Ask specifically whether firmware \`1.4.4.20200314\` is the latest supported MU503 firmware and whether this model is expected to provide live RAW8 data through SDK 58.28273.20250415 on macOS Apple Silicon.

**Do not flash firmware** unless AmScope supplies a verified MU503-specific image and explicit procedure for this hardware revision. The presence of a newer date on another model or generic MU-series package is not sufficient.

## 8. Acceptance criteria for closing this defect

The acquisition-validity defect is not closed until all of the following are true:

1. Multiple consecutive pulls succeed without SDK error, disconnect, or timeout.
2. Frame dimensions and payload layout are internally consistent.
3. Frame sequence/timestamp metadata advances as expected.
4. The sentinel diagnostic indicates that the expected payload region is being written.
5. Payload statistics/checksums are recorded for every frame.
6. Dark and illuminated captures produce a demonstrable image-data response, or a vendor-confirmed explanation establishes why they should not.
7. Processed and RAW paths are compared, or the official application result provides a decisive isolation boundary.
8. Original camera settings are restored and the camera can be reopened and started again.
9. The exact tested source commit and hardware test logs are recorded.

A successful compile or CI run validates the diagnostic program's build, not the physical camera's acquisition behavior. The defect remains OPEN until physical-camera evidence satisfies these criteria.

## 9. Source references

### Official / primary references

- [AmScope MU503 product page](https://amscope.com/products/mu503) — model, sensor identification, resolution, frame-rate specifications.
- [AmScope MU Series software downloads](https://amscope.com/pages/software-download) — official software distribution and model selection.
- [AmScope MU Series complete manual](https://storage.googleapis.com/software-download-d79bb.appspot.com/software/AmScope/MU-Series-Complete-Manual-Complete.pdf) — generic series UI documentation; not model-specific proof of bit depth.
- [ON Semiconductor MT9P006 datasheet](https://www.mouser.com/datasheet/2/308/1/MT9P006_D-3006492.pdf) — sensor IC ADC and parallel-output specification.
- Repository-bundled \`Camera/VendorSDK/AmscopeVendorSDK/inc/amcam.h\` and \`doc/en.html\` — exact installed SDK ABI and documentation; primary source for API/event/format semantics.

### Secondary reference

- [Third-party MU503 listing](https://amscope.binocularsi.com/products/amscope-5mp-usb3-0-microscope-digital-camera.html) — lists 12-bit parallel conversion and 8-bit RGB to computer. Treat as corroborating secondary evidence only.

## 10. Investigation history

| Date | Event |
|---|---|
| 2026-11-23 | MU503 enumerated and opened through Amcam SDK. |
| 2026-11-23 | SDK capability probe reported maximum depth 8, RAW8 only, and legacy bit-depth option unsupported. |
| 2026-11-23 | macOS USB topology independently confirmed direct USB 3.0 SuperSpeed 5 Gbit/s link. |
| 2026-11-23 | First RAW capture returned 1280 × 960 and 1,228,800 bytes, all 255. |
| 2026-11-23 | Dark capture and illuminated capture both returned all 255. |
| 2026-11-23 | Initial probe build portability issue (\`E_FAIL\` undeclared) was fixed by initializing the HRESULT storage with 0; user subsequently reported successful build and capture. |
| 2026-11-23 | Multi-frame sequence, metadata, sentinel, checksum, callback-error, and drop-counter diagnostics added to the standalone probe. |

### Current state

- [x] Camera identity and USB link recorded.
- [x] SDK-advertised RAW capability recorded.
- [x] Dark-versus-lit one-frame result recorded.
- [x] One-frame probe extended with multi-frame validity instrumentation.
- [ ] Official AmScope application behavior recorded.
- [ ] New dark multi-frame capture executed.
- [ ] New illuminated multi-frame capture executed.
- [ ] Processed RGB versus RAW path isolated.
- [ ] Vendor firmware/support response recorded.
- [ ] Physical acquisition validity accepted.
