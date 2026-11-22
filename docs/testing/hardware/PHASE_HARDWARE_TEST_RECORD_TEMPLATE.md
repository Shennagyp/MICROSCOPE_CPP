# Physical Camera Test Record — Template

> This is a blank template. It is not evidence that a camera test has been run.

## 1. Test identity

- Phase ID/name:
- Test date/time and timezone:
- Tester:
- Repository:
- Branch:
- Exact commit SHA:
- Relevant source changes since previous hardware test:
- Hardware test record filename:

## 2. Physical camera and environment

- Manufacturer:
- Model name:
- Exact SDK-reported model code:
- Serial/device identifier (record only as appropriate for the project's private repository):
- Firmware version:
- Hardware revision:
- Sensor type / color or monochrome:
- Amcam SDK version:
- Selected vendor SDK library path and architecture:
- Connected accessories:
- Trigger/I/O wiring and safe-load arrangement:
- Host computer:
- macOS version:
- CPU architecture:
- Xcode/Clang version:
- CMake version:
- Qt version:

## 3. Safety and initial state

- Initial camera state:
- Initial exposure mode/time/gain:
- Initial white balance:
- Initial resolution:
- Initial pixel format / RAW state:
- Initial trigger mode/source:
- Initial I/O state:
- Thermal/fan state:
- Other settings that will be changed:
- Safety review completed:
- Settings to restore after test:

## 4. Baseline regression procedure

Record **PASS**, **FAIL**, **NOT SUPPORTED BY THIS MODEL**, or **NOT RUN** for each item.

| # | Test | Result | Observations / measurements |
|---|---|---|---|
| 1 | Enumerate camera and record model identity/flags | | |
| 2 | Open using the enumerated opaque device ID | | |
| 3 | Read camera state and all currently available descriptors | | |
| 4 | Read supported baseline feature values | | |
| 5 | Start live video and receive actual frames | | |
| 6 | Verify frame dimensions, format, stride, sequence/timestamp, exposure metadata | | |
| 7 | Change one safe/reversible supported setting and verify native readback | | |
| 8 | Restore the original setting and verify restoration | | |
| 9 | Stop and start again; verify new acquisition frames | | |
| 10 | Close, reopen, start again, and verify frames | | |
| 11 | Still capture, when supported | | |
| 12 | RAW capture/acquisition, when supported and safe | | |

## 5. Phase-specific procedure

- Phase-specific test cases:
- Expected behavior:
- Actual behavior:
- Failure/recovery cases attempted:
- Fault-injection tests run in addition to hardware test:
- Why any case could not be safely reproduced on hardware:

## 6. Acquisition interval

- Required interval for this phase:
- Actual continuous acquisition duration:
- Resolution:
- Pixel format:
- Exposure:
- Configured frame rate (if applicable):
- SDK-reported frame rate:
- Host-observed preview frame rate:
- Total frames:
- Dropped frames:
- Timeouts/errors:
- Unexpected disconnects:
- Stale frames observed:
- Memory/resource observations:

Do not enter the planned duration as the actual duration. Record the measured duration.

## 7. Capability-specific tests

Only test a capability that the physical model reports as supported and that can be exercised safely.

| Capability | Advertised by model/SDK? | Test performed? | Result | Readback/completion evidence |
|---|---|---|---|---|
| Exposure / gain | | | | |
| White balance | | | | |
| Resolution / pixel format | | | | |
| RAW output / bit depth | | | | |
| Image processing | | | | |
| Sensor modes | | | | |
| Thermal controls / telemetry | | | | |
| ROI / binning / skipping | | | | |
| Processed still capture | | | | |
| RAW still capture | | | | |
| Trigger / heartbeat | | | | |
| Software trigger burst | | | | |
| External trigger | | | | |
| Hardware I/O | | | | |
| Embedded light source | | | | |
| Filter wheel | | | | |
| ST4 | | | | |
| Astronomy auto-focuser | | | | |
| Lens focus / aperture | | | | |

## 8. Restoration and closeout

- Original settings restored:
- Restoration verification:
- Final camera state:
- Camera cleanly closed:
- Re-enumeration/reopen successful:
- Logs/diagnostics attached:
- Known defects:
- Unresolved limitations:
- Follow-up issue/commit:

## 9. Result

- Overall phase hardware result: PASS / FAIL / BLOCKED / PARTIAL
- Acceptance decision:
- Reason:
- Exact tested commit:
- Does any later source change invalidate this evidence?:
- Reviewer:

A phase cannot be marked complete if the overall physical-camera result is not PASS. A capability absent from this physical model must be marked NOT SUPPORTED BY THIS MODEL; test it on another appropriate physical model before claiming that capability is hardware-qualified.
