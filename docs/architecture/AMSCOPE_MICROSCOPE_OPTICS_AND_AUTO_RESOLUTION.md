# Microscope optical profile and automatic camera resolution

## Purpose

The native application models the microscope's optical train independently of
the camera driver's hardware feature list. Camera resolution is an acquisition
policy derived from the active objective, sensor pixel pitch, camera-port relay,
and a selected optical sampling criterion.

The profile is editable in a dedicated **File → Microscope Configuration…**
window. The compact main-window dock exposes the current objective selector
and the **Auto Res** enable/disable control. The existing Camera Controls window
continues to own camera-device and low-level camera feature settings.

## Default instrument: OPTIKA B-293

| Property | Default |
|---|---:|
| Microscope | OPTIKA B-293 |
| Objective series | N-PLAN (Plan Achromat) |
| Optical tube system | Finite, 160 mm |
| Nosepiece | Quadruple, manually rotated |
| Condenser | Abbe, NA 1.25 |
| Illumination | X-LED3 white LED; 6300 K color temperature |

| Objective | NA | Working distance | Immersion |
|---:|---:|---:|---|
| 4× | 0.10 | 15.2 mm | Dry |
| 10× | 0.25 | 5.5 mm | Dry |
| 40× | 0.65 | 0.45 mm | Dry |
| 100× | 1.25 | 0.13 mm | Oil / Water |

These values are from the OPTIKA B-293 technical datasheet. Do not silently
substitute B-293PLi specifications: B-293PLi is a separate IOS/infinity-corrected
model. The B-293 profile records the objective currently selected by the user
because the B-293's revolving nosepiece is manual and does not report its
position to the application.

The condenser NA is retained as part of the optical instrument model. The
initial lateral Rayleigh estimate uses objective NA; the condenser aperture and
illumination coherence affect the real optical transfer function and remain
separate optical parameters rather than being multiplied into the objective NA.

## Camera profile and relay

The initial camera profile is the user's AmScope MU503:

- Aptina MT9P006 color sensor.
- 2.2 µm sensor pixel pitch.
- Advertised 1×1, 2×2, and 4×4 binning modes.
- Advertised output modes 2560×1922, 1280×960, and 640×480.

The default total camera relay magnification is 0.5×, matching the MU503
compensating/reduction lens specification. It is editable because the OPTIKA
M-115 camera adapter is a 0.35× accessory. The configured value represents the
**product of all optical relay elements** between the microscope camera port and
the sensor. For example, 0.35× and 0.5× elements stacked together represent
0.175× total relay. The user must enter the actual installed optical path;
software cannot infer which reducer is physically fitted.

The configuration window also exposes sensor pixel pitch so the profile can be
reused with a different camera.

## Rayleigh sampling calculation

The initial model uses the lateral Rayleigh distance

`d_R = 0.61 \lambda / NA_{objective}`

and the object-space pitch for a candidate sensor mode

`p_{object} = p_{sensor} \times s_{mode} /
  (M_{objective} \times M_{relay})`

where `lambda` is the configured reference wavelength in micrometres,
`p_sensor` is physical sensor pixel pitch in micrometres,
`s_mode` is the effective sampling scale of the candidate camera mode,
`M_objective` is objective magnification, and `M_relay` is the product of
camera-port relay magnifications.

The displayed Rayleigh sampling factor is

`RF = d_R / p_{object}`

The initial target is `RF >= 2.3`. The value 2.3 is a practical sampling
target; it is not the Rayleigh criterion itself. The default reference wavelength
is 550 nm (green reference), not a measured spectral peak of the B-293's 6300 K
white LED. Wavelength and target factor are configurable.

For the current camera mode list, the initial implementation infers relative
sampling scale from the ratio between the largest reported dimensions and the
candidate mode dimensions. This assumes the lower modes retain the full field
of view and increase effective sample pitch in proportion to their reduction.
That assumption must be checked against the actual camera/SDK mode behavior.
If a mode is a cropped ROI rather than a full-field binned/readout mode, the
mapping must be replaced by explicit per-mode sensor/ROI metadata before the
automatic selector is considered calibrated.

The selection policy is:

1. Read the camera's reported resolution modes; never invent unsupported modes.
2. Calculate RF for each mode using the active objective and configured imaging
   path.
3. Reject modes below the target RF.
4. Select the valid mode with the fewest output pixels.
5. If no mode meets the target, select the maximum reported mode and report the
   RF shortfall. Never present the shortfall as a successful match.
6. If the mode already matches, do not stop/restart acquisition.

A resolution mutation continues through the existing serialized native controller
operation. No SwiftUI view calls the vendor SDK and no additional acquisition
worker or frame queue is introduced.

## Initial predicted choices

For 550 nm, 2.2 µm sensor pixels, 0.5× total relay, and target RF 2.3:

| Objective | Rayleigh distance | Full-mode RF | Predicted minimum mode* |
|---:|---:|---:|---|
| 4× / 0.10 | 3.355 µm | 3.05 | 2560×1922 |
| 10× / 0.25 | 1.342 µm | 3.05 | 2560×1922 |
| 40× / 0.65 | 0.516 µm | 4.69 | 1280×960 |
| 100× / 1.25 | 0.268 µm | 6.10 | 1280×960 |

*Predictions assume the camera modes are full-field 1×1, 2×2, and 4×4
sampling respectively. For 40×, the predicted RF at 2×2 is about 2.35,
just above the 2.3 target. This is a narrow margin and must be checked with
optical calibration and the camera's actual mode behavior.

With a 0.35× total relay instead, the full-mode factors are approximately 2.13,
2.13, 3.28, and 4.27. Under a 2.3 target, the full-resolution mode is selected
for all four objectives.

## State and interaction boundaries

- The configured objective list describes the microscope; the active objective
  is a manually selected report of the physical nosepiece position.
- Selecting an objective in the dock updates the active optical model and asks
  Auto Res to reconcile the camera mode.
- Editing the microscope profile is performed in the separate configuration
  window and applied as one configuration update, avoiding repeated camera
  restarts while text fields are being edited.
- Auto Res is a policy setting persisted with application preferences.
- Selecting a resolution manually switches Auto Res off.
- Switching Slide 1 / Slide 2 changes the live frame destination only. It does
  not actuate the nosepiece, objective, stage, or slide changer.
- Preview-pane expansion is a display-layout policy, not a camera sampling
  policy. It must preserve each slide's retained image and zoom/viewport state.
  Camera acquisition resolution and preview layout must not recursively control
  each other.

## Verification required

The user's Mac must verify the B-293 preset and manual objective selection;
confirm the installed camera relay magnification; verify MU503 mode dimensions
and full-field binning/readout behavior; check predicted factors with a stage
micrometer; verify Auto/manual switching and Stop/apply/restart; and confirm
frame-pool availability.

No compile, hardware, or optical calibration result is claimed by this design
note.
