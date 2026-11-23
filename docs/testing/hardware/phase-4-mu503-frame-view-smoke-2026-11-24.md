# Phase 4 — MU503 FrameView integration smoke check

## 1. Test identity

- Phase: Phase 4 — Extract reusable Qt camera UI
- Test date: 24 November 2026
- Tester: Project owner
- Repository: `Shennagyp/MICROSCOPE_CPP`
- Branch: `amscope-native-driver-v0.7`
- Tested checkout commit: `5339c441659b45a3bdb30fdce6971f6dc991f885`
- Tested implementation source commit: `892b91a57016e570a1e972343f78ed7c42be801c`
- Source commits after the implementation commit and before this smoke check: documentation-only commits; the preview executable source was unchanged.

## 2. Physical camera and environment

- Camera: AmScope MU503 (previously identified by the owner as MU503, USB 2.0)
- Host: Apple MacBook Air, macOS
- CPU architecture and exact macOS, Clang, CMake and Qt versions: Not recorded for this smoke check.
- Firmware, hardware revision, serial and connected accessories: Not recorded.
- Trigger/I/O wiring: Not applicable to this frame-view smoke check.

## 3. Phase-specific procedure and results

The project owner reports that the requested post-extraction preview sequence worked:

| # | Operation | Result | Evidence / boundary |
|---|---|---|---|
| 1 | Launch the v0.7 `amscope_preview` executable | PASS | Owner confirmed the application worked after the successful Release build |
| 2 | Discover/select the physical MU503 | PASS | Owner confirmed the requested camera preview sequence worked |
| 3 | Open the camera | PASS | Owner confirmed |
| 4 | Start live acquisition and observe changing frames in extracted `camera::qtwidgets::FrameView` | PASS | Owner confirmed |
| 5 | Stop acquisition | PASS | Owner confirmed |
| 6 | Close the camera | PASS | Owner confirmed |

## 4. Result and limits

- Phase-specific FrameView integration smoke result: **PASS**.
- This confirms the composed AmScope preview can deliver live camera frames to the extracted generic FrameView and complete the requested Open/Start/Stop/Close sequence.
- The separate offscreen `camera_qt_widgets_frame_view_tests` passed in the owner's Release CTest run at implementation source commit `892b91a`; the full suite passed 13/13.
- No frame rate, dropped-frame count, exposure, resolution, payload metadata, still capture, feature readback, sustained acquisition duration, restart cycle, or close/reopen cycle was measured in this smoke check.
- Exact camera firmware, OS version, toolchain versions and CPU architecture were not captured. Do not infer these fields from earlier tests.
- This is not the full Phase 4 hardware exit test: changing a supported feature through the composed UI remains unverified. It is also not Phase 7 release qualification.

## 5. Acceptance

- The post-extraction physical preview boundary for `Camera::QtWidgets::FrameView`: **PASS**.
- Phase 4 overall: **IN PROGRESS**. Remaining UI extraction and the supported-feature interaction gate are still open.
