# Stage Working Surface and Microscope Axis Reference Model v2

**Version:** 2  
**Status:** Core XY CAD layout and per-axis reference contracts implemented; live host integration remains in progress.  
**Target branch:** OPTIKA-B293-DUALSLIDE-APP-v.0.9

## 1. Physical ownership

The microscope has one physical motorized stage with an XY working surface and a separate focus Z axis.

The stage working surface is strictly 2D. It owns:
- X/Y stage axis identities and units.
- A user-defined XY origin and positive X/Y travel extents. The far boundaries are derived as origin plus travel extent and validated against the stage axes' configured operating ranges.
- A variable-size, user-authored collection of slide rectangles, which is empty when a new canvas is created.
- Immutable add, replace and remove operations for slide-rectangle snapshots.

The workspace origin is not automatically the stop-point origin of any slide. Each slide's first CAD endpoint is supplied independently when that slide is drawn; the two origins may coincide only by explicit user choice.

The initial canvas has no implicit slide count, default slide geometry or pre-populated rectangle. The empty workspace still has a valid machine-coordinate area and X/Y axis binding; slide-local projection is unavailable until a slide rectangle has been added.

The current slide CAD orientation is axis-aligned: slide length is assigned to stage X or stage Y, and the initial handle selects the positive/negative direction on each axis. Slide rectangles may touch at an edge or corner, but positive-area overlap is rejected. No additional clearance margin is implied; a positive gap requirement must be represented as an explicit physical holder constraint.

It does not own or store:
- Z datum or slide height.
- Slide thickness.
- Drawing tolerance.
- Stop-point uncertainty.

The focus Z axis belongs to the microscope's axis specification and runtime position model. Its operating range is declared by AxisOperatingRange, independently of the slide layout. Its top/bottom reference policy is represented by StageAxisReferenceSpecification.

## 2. CAD slide rectangle

Each saved slide rectangle contains:
- A first XY endpoint that is fixed as the physical mechanical stop-point datum and slide-local origin.
- An initial opposite-corner handle that chooses the rectangle quadrant.
- A solved opposite endpoint.
- All four stage-coordinate corners, with the two adjacent corners derived from the slide-local orthogonal basis.
- An explicit assignment of slide length to stage X or stage Y.
- Constrained length and width.

The four stage-coordinate corners are constrained geometry results derived from the user-supplied stop point and dimensions. They are not four separate sensor measurements or independent calibration observations.

The dimensions solve the final opposite corner while the stop point remains fixed. The original handle and solved corner are both retained. The slide's coordinate transform is two-dimensional.

For slide-local point p_s=(u,v), stage point p_m is:

    p_m = stopPoint + xAxis * u + yAxis * v

The inverse applies the transpose of the 2D orthonormal basis. Mapping remains valid outside the selected slide footprint; insideSlide and insideWorkingSurface are reported separately.

There is no slide-local Z coordinate in this rectangle model. A selected slide determines the XY reference frame only.

The slide's stop-point coordinate is a stage-coordinate point supplied by the user for that rectangle. It is checked against the workspace through all four solved corners, but it is not inferred from the workspace origin.

## 3. Actual position and focus Z

StagePosition3D is an aggregate of the physical machine X, Y and focus Z axis states:
- X/Y are projected into the selected slide's 2D CAD coordinate frame.
- Z remains the microscope focus coordinate and is exposed independently through StagePosition3D::z().

positionOnSlide() requires valid X and Y references only. It must not fail merely because focus Z has not yet been referenced. Z focus movements and observations cannot alter slide-local XY coordinates.

## 4. Axis endstop and focus reference policy

Every axis declares its physical operating range in its axis specification. Its reference procedure is configured separately.

### Physical endstop

When an axis has a physical limit-switch endstop, a matching active AxisLimitObservation at the configured end establishes the reference datum. This is physical switch evidence.

### Focus axis without an endstop sensor

When Z has no limit-switch sensor, the reference workflow performs a full-range sweep in one direction toward the configured upper/top or lower/bottom boundary.

The full-range sweep:
1. Reads the configured Z operating range.
2. Chooses the requested boundary (AxisLimitEnd::Positive or Negative).
3. Commands a displacement equal to the entire configured travel range in that direction.
4. Waits for the exact correlated motion command to complete.
5. Assigns the selected boundary as a calibrated estimated reference.

This procedure deliberately does not require a known starting Z coordinate. It relies on the carriage starting somewhere within its physical travel envelope: traversing one complete travel-range distance in the selected direction is sufficient to reach that end. The microscope focus mechanism must explicitly declare ContinuousKnobRotationWithoutClash, meaning the knob/drive can continue rotating safely after the carriage reaches its travel boundary. The reference specification rejects this procedure if that mechanical capability is not declared.

This mechanical capability is a safety precondition; it is not position feedback and does not prove the exact carriage coordinate. Without an independent encoder/measurement, the resulting reference remains ReferencedEstimatedFullRangeSweep with CalibratedCommandFullRangeSweep evidence and a configured uncertainty bound. It is never promoted to measured evidence.

If encoder or external measurement is available, a target-boundary observation can instead complete the sweep as measured evidence.

An interrupted or rejected sweep does not establish a reference. The exact command completion must be correlated and verified before the Application promotes the Core axis reference state.

## 5. Position semantics

A controller-reported or encoder-measured position is an observation. A calibrated full-range sweep establishes a command-derived estimated reference. The app preserves this evidence distinction.

The actual focus Z coordinate is independent of the slide rectangle and is not folded into the slide-local XY coordinates.

## 6. Integration boundary

Core now defines the XY CAD working surface, an initially empty canvas created from the user-defined origin and X/Y travel extents, immutable slide-rectangle add/replace/remove snapshots, the slide rectangle transform, the three-axis machine-position aggregate, physical-endstop reference, and safe full-range sweep state. The remaining Application work is to execute the full-range motion command through the correlated operation runtime, update the Z axis state from the resulting reference snapshot, publish immutable X/Y/Z snapshots, and expose interactive CAD drawing, undo/redo and persisted layout revisions to the UI.


## 7. Microscope focus Z and no-switch homing

The focus Z travel range is defined by the focus axis AxisOperatingRange, not by any slide rectangle. If the focus axis has a physical limit switch, that switch establishes its configured top/bottom reference.

If no limit-switch sensor exists, the focus axis uses a FullRangeSweep toward the configured upper or lower boundary. The procedure commands one full configured travel distance in that direction without requiring a known start coordinate. This depends on the focus mechanism's ContinuousKnobRotationWithoutClash capability so continued knob/drive rotation after the carriage reaches its physical travel boundary cannot cause collision. Without that declared mechanical capability, the Core reference specification rejects the no-switch sweep.

The resulting coordinate is a calibrated estimate unless an independent encoder/external position observation verifies the boundary.
