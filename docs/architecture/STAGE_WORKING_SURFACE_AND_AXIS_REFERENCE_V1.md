# Stage Working Surface and Axis Reference Model v1

**Status:** Initial Core contract implementation  
**Target branch:** OPTIKA-B293-DUALSLIDE-APP-v.0.9

## Physical working-surface model

The stage is one physical 3D working volume. It is not a dynamically resized collection of slide slots. The installed hardware definition contains exactly ten fixed slide placements in a fixed-size array.

Each placement contains a slide identity, registration revision, physical length/width/thickness, a mechanical stop-point datum that defines its local origin, and a right-handed orthonormal local X/Y/Z basis expressed in stage coordinates.

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

This commit adds deterministic Core geometry and axis-reference state contracts. It does not yet connect the 3D position model to the live three-axis feedback aggregator, motion driver, reference-command executor, calibration persistence or SwiftUI projection. The old DualSlideCarrierState remains a legacy two-slide path during migration; the new model is the intended fixed ten-slide working-surface representation.
