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

The configured camera adapter is the **ToupTek FMA050 (0.50×)**. This is a
user-selected optical-path setting, not a value detected from the camera
hardware. The application exposes adapter presets in the Microscope
Configuration window:

- ToupTek FMA050 — 0.50×
- OPTIKA M-115 — 0.35×
- OPTIKA M-114 — 0.50×
- Custom / combined optical path — user-entered magnification and optional path label

Selecting a named preset sets its nominal magnification. Custom mode accepts a
user-defined effective magnification and an optional path label. The numerical
value used by the optical calculation is the **total effective relay
magnification**, applied once between the microscope image plane and the sensor.
If multiple optical elements are physically stacked, select Custom and enter
their measured or calculated combined product. The application never assumes
that a camera's packaged reducer and a separate microscope adapter are both
installed, and it must not multiply them merely because both appear in product
documentation.

Older saved profiles contain only a numeric relay magnification and no adapter
identity. They are migrated to Custom while preserving that numeric value;
the application does not invent an adapter identity for an older profile. The
user can explicitly select FMA050 after confirming the physical path.

The configuration window also exposes sensor pixel pitch so the profile can be
reused with a different camera.

## Rayleigh sampling calculation

The initial target is H&E chromatin in the hematoxylin-sensitive band. Published
spectral measurements report a useful nuclear hematoxylin signature around
540–600 nm, but the precise spectrum depends on staining chemistry, mordant,
pH, tissue and measurement system. Eosin also contributes in the approximately
500–550 nm region, so H&E is not two perfectly non-overlapping spectral bands.

The profile therefore stores a configurable chromatin band, initially
540–600 nm, rather than one fixed 550 nm wavelength. For a candidate mode the
application calculates the Rayleigh distance across the band:

`d_R(\lambda) = 0.61 \lambda / NA_{objective}`

and the object-space pitch for a candidate sensor mode

`p_{object} = p_{sensor} \times s_{mode} /
  (M_{objective} \times M_{relay})`

where `lambda` is the configured reference wavelength in micrometres,
`p_sensor` is physical sensor pixel pitch in micrometres,
`s_mode` is the effective sampling scale of the candidate camera mode,
`M_objective` is objective magnification, and `M_relay` is the product of
camera-port relay magnifications.

The sampling factor is evaluated over the configured chromatin band:

`RF(\lambda) = d_R(\lambda) / p_{object}`

The value increases with wavelength. To preserve the finest detail across the
whole band, the initial automatic policy uses the **minimum RF at the shortest
configured wavelength** as its pass/fail condition. It also reports the factor
range across the band. The initial band is 540–600 nm and the target is
`RF >= 2.3`; both are configurable. This is a conservative band-edge model,
not a claim that every wavelength contributes equally.

A calibrated spectral model can later replace this conservative rule using
measured illumination spectral power, hematoxylin-versus-background contrast,
eosin contribution, optical transmission and camera spectral quantum efficiency.
Those curves must be measured for the installed microscope/camera and staining
protocol. Do not manufacture weights or infer them from RGB color labels.

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

For the 540–600 nm chromatin band, 2.2 µm sensor pixels, 0.5× total relay,
and target RF 2.3:

| Objective | Rayleigh distance band | Full-mode RF band | Predicted minimum mode* |
|---:|---:|---:|---|
| 4× / 0.10 | 3.294–3.660 µm | 2.99–3.33 | 2560×1922 |
| 10× / 0.25 | 1.318–1.464 µm | 2.99–3.33 | 2560×1922 |
| 40× / 0.65 | 0.507–0.563 µm | 4.61–5.12 | 1280×960 |
| 100× / 1.25 | 0.264–0.293 µm | 5.99–6.65 | 1280×960 |

*Predictions assume the camera modes are full-field 1×1, 2×2, and 4×4
sampling respectively. At 40× the predicted lower-edge factor in a 2×2 mode
is only about 2.30, almost exactly the 2.3 threshold. It should be considered
marginal until the optical path and effective sensor sampling are measured.

With a 0.35× total relay instead, the full-mode RF ranges across 540–600 nm
are approximately 2.10–2.33 (4× and 10×), 3.22–3.58 (40×), and 4.19–4.66
(100×). At the conservative 540 nm band edge, 4× and 10× do not reach the
2.3 target even at maximum camera resolution; Auto Res must report this
shortfall rather than claim the optical limit is fully sampled.

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
