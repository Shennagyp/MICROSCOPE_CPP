# Stage Working Surface and Axis Reference Model v1

**Status:** Initial Core contract implementation  
**Target branch:** OPTIKA-B293-DUALSLIDE-APP-v.0.9

## Physical working-surface model

The stage is one physical 3D working volume. It is not an automatically discovered collection of hardware slots; the user authors and edits the saved CAD layout. The slide map is a user-authored CAD drawing containing a variable number of slide rectangles. It is not limited to ten slides and is not an automatically discovered set of hardware slots.

Each placement retains its original stop-point and opposite XY endpoints, explicit length/width dimensional constraints, drawing tolerance, Z datum and thickness. The resulting 3D placement derives a right-handed orthonormal local X/Y/Z frame from the CAD rectangle. The working volume is explicitly bound to three distinct installed linear StageAxisIds; its micrometer bounds are checked against the corresponding configured axis travel (including unit conversion).

A stage coordinate is projected through the selected slide's inverse rigid transform. Projection remains defined when the stage is between slides or outside a slide footprint. The returned inside-slide and inside-working-volume flags are separate facts.

## Coordinate relationship

For a slide-local point p and placement i:

    p_stage = origin_i + x_i * p.x + y_i * p.y + z_i * p.z
    p_slide = R_i^T * (p_stage - origin_i)

The transform is rigid; its basis must be orthonormal and right-handed. Slide Z describes physical thickness/normal. It does not imply a motorized Z axis. A motorized Z axis has its own stage axis specification, actual position and reference lifecycle.

## Axis reference procedures

Every motorized linear axis declares one reference method and one boundary to establish.

- PhysicalEndstop: an active, axis-matched limit observation establishes the configured travel boundary.
- FullStrokeTraversal: the configured boundary is a software endstop. The procedure begins at the opposite, already-known boundary and traverses the entire configured range.

Full-stroke completion has two evidence outcomes:

- Encoder or external position measurement at the target boundary establishes a measured reference.
- Calibrated open-loop command completion establishes an estimated software-endstop reference only. It is never upgraded to measured evidence.

An unknown starting position cannot become known by commanding one nominal stroke length. Full-stroke traversal therefore requires a previously established starting datum. If the traversal is interrupted, its in-progress reference is discarded.

## Integration boundary

This implementation adds deterministic Core geometry and axis-reference state contracts. It does not yet connect the 3D position model to the live three-axis feedback aggregator, motion driver, reference-command executor, calibration persistence or SwiftUI projection. The old DualSlideCarrierState remains a legacy two-slide path during migration; the new model is the intended user-authored multi-slide working-surface representation.


## Live three-axis position projection

StagePosition3D is an immutable Core aggregate of the installed X, Y and Z StageAxisPositionState values. It accepts observations one axis at a time and only promotes a homing observation when the caller has already verified the corresponding reference procedure. A slide-relative estimate is unavailable until all three axes are referenced and have finite uncertainty bounds.

The inverse mapping returns coordinates even outside the selected slide footprint. It reports inside-slide and inside-working-volume independently. Local uncertainty is bounded conservatively by projecting each machine-axis error bound onto the slide basis and adding the stop-point datum uncertainty. Evidence and source are retained per axis.

This remains a Core model. The Application worker must still own incoming device feedback, update this state serially, publish immutable snapshots and coordinate all three axis-reference operations.


## Feedback integration contract

The Application layer now has a three-axis feedback reducer for the StagePosition3D aggregate. It accepts only current Core feedback dispositions, applies only X/Y/Z AxisPositionObservation payloads, and invalidates all three machine references when the feedback-loss latch is set. A complete XYZ HomeReferenceEstablished fact is accepted only when its exact device/operation/step/command correlation matches and the Core operation step is Verified.

Physical limit-switch reference is now a distinct StagePositionEvidence value. It can establish the known starting boundary of a later full-stroke traversal and remains distinguishable from encoder, external measurement and calibrated command estimates.


## CAD rectangle authoring

`StageWorkingSurface3D::SlidePlacements` is a variable-size collection of
`SlideRectangleDefinition2D` values. A working surface may have no rectangles
initially; the user adds as many as required in the saved CAD drawing.

Each rectangle retains:
- The first XY endpoint, which is the mechanical stop-point datum and local origin.
- The opposite XY endpoint.
- Whether slide length follows stage X or stage Y.
- Explicit length and width constraints.
- Z datum, slide thickness, dimensional tolerance and stop-point uncertainty.

The endpoint spans must satisfy the dimensional constraints within the drawing
tolerance. The 3D pose is derived from the rectangle but the original CAD
parameters remain stored and inspectable. This milestone does not yet implement
interactive dragging, constraint solving during edits, undo/redo, persistence,
or overlap/clearance checks.
