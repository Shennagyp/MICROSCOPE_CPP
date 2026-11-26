# Microscope Platform — v0.9 Implementation Plan

**Architecture reference:** ../architecture/MICROSCOPE_PLATFORM_ARCHITECTURE_V0.9.md  
**Target branch:** OPTIKA-B293-DUALSLIDE-APP-v.0.9  
**Status:** Phases 1 and 2 completed; Phase 3 declarative configuration and Phase 4 operation runtime/stage navigation are in progress. Core now has the stage/navigation model, bounded feedback inbox and serialized operation-state owner. Application now has the runtime wake primitive, owner-loop entry point and legacy dual-slide X/Y feedback reducer. A 3D working-surface CAD model, axis-reference contract and XYZ position projection have been added as the target model for hardware-coordinate migration.

## Objective

Migrate from the current native-app-centered microscope configuration into two independent C++ libraries:

- Microscope.Core: complete microscope model and behavior.
- Microscope.Application: JSON definition loading, adapter composition, runtime management and frontend-neutral application API.

Keep the current macOS application as one frontend. Preserve the existing camera/preview pipeline. Do not create speculative empty libraries or duplicate active implementations.

## Current repository baseline

The current branch contains:
- A root CMake project with optional native macOS application and AmScope targets. Core and Application are included independently; `OPTIKA_BUILD_AMSCOPE=OFF` permits a platform-only root configuration.
- `Microscope::Core` and `Microscope::Application` as separate C++17 static libraries with standalone CMake configuration; Application brings in Core as its source dependency when configured independently.
- `Camera/Amscope` as the existing reusable camera capability and `Application/OptikaB293` as the current SwiftUI/AppKit frontend, Objective-C++ bridge and transitional Swift microscope configuration/resolution implementation.
- Core contracts for strong IDs, distinct runtime/device-session generations, [[nodiscard]] Result/Status, validated diagnostics, operation/step status values, typed feedback correlation and ingress/receipt metadata.
- Application value contracts for runtime/operation handles, factory-created immutable RuntimeSnapshot values with validated lifecycle/positive revision, and Core-snapshot-backed OperationView with validated composite identity.
- Core identity/diagnostic/feedback and Application handle/projection test sources registered with CTest. They have not been built or executed here.
- Architecture, public-contract and standalone-library documentation.

The two libraries began as an initial Phase 1 source slice, not a completed platform implementation. Later work has added typed equipment/configuration contracts, stage navigation, a bounded feedback inbox, a serialized operation-state owner, and Application wake/owner-loop and stage-feedback-reduction primitives. Full configuration-plan execution, JSON loading, deployment adapter composition, persistence, calibrated motion execution, acquisition orchestration and hardware workflow integration are still outstanding.

## Migration rules

1. Preserve existing behavior unless a change is explicitly intended.
2. Do not rewrite the camera acquisition worker, frame pool, frame lease, latest-frame mailbox or preview pipeline as part of the library extraction.
3. Move one coherent behavior at a time: define Core contract, implement/test Core behavior, integrate through Application, then migrate the frontend.
4. Never keep two authoritative implementations of the same microscope rule. During a vertical-slice migration, the new C++ path becomes authoritative before the old Swift path is removed.
5. Keep JSON parsing outside Core.
6. Keep concrete hardware SDKs outside Core and Application.
7. Use simulated adapter implementations for Core runtime tests; do not require a connected microscope for pure/domain tests.
8. Distinguish dispatch, adapter/device acknowledgment, hardware completion, observation and Core verification.
9. Do not claim runtime or hardware success without evidence from the user's environment.

## Phases

### Phase 0 — Architecture baseline (documentation)
- Establish one authoritative platform architecture.
- Define Core/Application boundaries.
- Specify operation and feedback semantics.
- Record calibration, slide, acquisition and persistence invariants.
- Identify existing native app code as transitional migration source.

**Exit:** architecture and implementation plan are committed and linked from the architecture index.

### Phase 1 — C++ library foundations and public contracts

**Completed**

**Accepted evidence**
- Core and Application are separate C++17 static libraries; Core depends only on the C++ standard library, and Application links publicly to Core.
- Each target configures independently; Application's standalone project adds its Core source dependency. The root project can configure the platform libraries with AmScope and the native macOS application disabled.
- Strong identity values, [[nodiscard]] Result/Status, validated diagnostics, operation/step status values, immutable feedback envelopes/receipts, feedback correlation/provenance validation, device/runtime session generations, and immutable runtime/operation snapshots are implemented.
- Feedback ingress overflow, wakeup, ordering, provenance and correlation invariants are specified as contracts. The actual queue/executor is reserved for Phase 4.
- Core, Application and the root platform-only composition all built and passed their registered CTest suites on Ubuntu GCC, macOS Clang and Windows MSVC in [GitHub Actions run 6](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45583316832).
- The existing AmScope driver and Camera::Common pipeline remain separate targets; no camera acquisition/preview rewrite was part of Phase 1.

**Boundary of completion**
This closes the initial library/build/API foundation only. It does not claim a microscope equipment model, configuration resolver, operation executor, JSON loader, adapter registry, persistence, calibration, acquisition or frontend migration.

**Exit:** satisfied. Further runtime feedback lifecycle tests, simulated-device execution and hardware acceptance remain assigned to Phase 4 and hardware acceptance respectively.

### Phase 2 — Core equipment and optical model

**Completed — 2A through 2E validated**

#### 2A — Installed equipment identity and optical topology

- Added strong IDs for installed equipment components, optical ports, optical connections and optical paths.
- Added factory-created immutable OpticalPort, EquipmentComponent, OpticalConnection, OpticalPathStep and OpticalPath values.
- Added MicroscopeEquipmentDefinition as the immutable aggregate for one installed microscope definition.
- Component kind constrains port capability: optical elements need input/output capability; imaging devices need input; illumination devices need output; mechanical assemblies have no optical ports.
- Physical OpticalConnection is undirected adjacency between ports on different components. Ordered OpticalPathStep values define propagation direction. One physical connection can be reused in opposite directions when port capabilities permit.
- Shared components are stored once and referenced from multiple optical paths. The tests cover a shared condenser and beam splitter with alternative camera output paths, and one physical adjacency traversed in opposite directions.
- Validation covers duplicate/foreign identities, port ownership/direction, one connection per physical port, same-component connection rejection, required interior path ports and route continuity.
- Cross-platform CI passed: [run 22](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45587716888).

#### 2B — Objective and camera specifications

- Added distinct positive finite domain values for micrometers, nominal magnification and numerical aperture; no general-purpose units framework.
- ObjectiveSpecification attaches to the installed EquipmentComponentId and stores magnification, NA, working distance, chromatic/field correction, immersion medium and optional design coverglass thickness.
- CameraSpecification attaches to the installed imaging component and stores active sensor dimensions, independent X/Y pixel pitch and unique output modes.
- CameraOutputMode stores sensor ROI, binning, delivered dimensions, pixel layout and stable pixel-format code. Net sensor-pixels-per-delivered-pixel X/Y mapping is derived independently from ROI and delivered dimensions; hardware binning remains a separate fact.
- Aggregate validation rejects unknown/wrong-kind component references and duplicate specialized records.
- Tests cover NA above 1, finite/positive constraints, anisotropic pitch/mapping, ROI/binned output, invalid pixel modes, duplicate modes and invalid component associations.
- Cross-platform CI passed: [run 33](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45589429447).

#### 2C — Illumination and condenser specifications

- IlluminationSpecification attaches to an installed IlluminationDevice and supports multiple independently identified channels.
- Each channel may have an optional spectral band, an intensity-control kind and a unit-bearing intensity range. Continuous control requires a finite non-negative increasing range.
- CondenserSpecification attaches to an installed OpticalElement and stores aperture NA range (minimum may be zero), design, immersion medium and optional working distance.
- Aggregate validation rejects unknown/wrong-kind references and duplicate specifications.
- Tests cover spectral ranges, intensity units/control semantics, multiple channels, condenser aperture bounds and invalid roles.
- Cross-platform CI passed: [run 46](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45590795457).

#### 2D — Stage and axis specifications

- StageSpecification attaches to an installed MechanicalAssembly and contains one or more StageAxisId values.
- Axis specifications distinguish linear/rotary/other/unknown motion and micrometer/millimeter/degree/radian/device-defined/unknown coordinate units.
- Each axis stores a finite signed operating range and positive nominal resolution.
- Optional accuracy, repeatability/precision and measurement uncertainty are separate values. Accuracy/repeatability preserve test conditions; repeatability preserves its metric. No metric is derived from resolution.
- Position-feedback capability and measurement location are explicit; no-feedback axes cannot claim a measurement location.
- Tests cover linear/rotary units, signed ranges, unit mismatches, independent performance values, test conditions, feedback semantics and stage-axis identity.
- Cross-platform CI passed: [run 58](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45592166839).

#### 2E — Complete definition validation

- MicroscopeEquipmentDefinition validates non-empty installed component sets; uniqueness of component, port, connection and path IDs; component and port ownership; path entry/exit direction; adjacency continuity; and physical component sharing.
- Objective, camera, condenser, illumination and stage specifications bind to installed component identities of the correct broad component kind.
- Duplicate specialization records are rejected. One physical OpticalElement cannot simultaneously be classified by both ObjectiveSpecification and CondenserSpecification.
- Stage axes are unique within their stage; camera output modes and illumination channels are unique within their owning device specification.
- Tests cover route reversal across one shared physical connection and conflicting optical specialization roles.
- Full Core standalone, Application standalone and root platform-only build/test matrices passed on Ubuntu GCC, macOS Clang and Windows MSVC: [run 72](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45595205508).

**Phase 2 boundary**
The equipment definition now describes installed topology and static capabilities/specifications. It does not store current device state, selected objective, aperture position, current illumination intensity, camera online status, calibrated positions or adapter-specific bindings. Those belong to configuration/runtime/calibration/deployment phases.

**Exit:** satisfied. Phase 2 equipment model and contract validation are complete. JSON parsing remains Phase 5; calibration remains Phase 6; operation execution remains Phase 4.

### Phase 3 — Declarative configuration

**Phase 3A–3E baseline implemented; Phase 3 is being extended for editable composable modes, candidate selection, route-derived cameras and optical sampling. Phase 3 is not exit-ready against the revised requirements until 3F–3H are implemented and validated.**

**Objective:** resolve a requested microscope configuration from installed equipment and declarative profile data using deterministic pure functions. The resolver must produce a validated desired configuration, diagnostics and a transition/dependency description; it must not communicate with hardware or execute transitions.

**Modeling sequence**
1. **3A — Configuration vocabulary:** objective selection, optical path, contrast method, camera output mode, illumination channel/preset, condenser configuration, phase/DIC/polarization components, exposure preset and exposure policy.
2. **3B — Profile applicability:** declarative profile requirements, selectors and compatibility constraints over installed component identity/capability data. Profiles must not contain C++ class names, SDK calls or executable callbacks.
3. **3C — Pure resolution:** request + equipment definition + profile catalog → resolved desired configuration + typed diagnostics. Current/effective state comparison is not part of this pure target-compatibility step.
4. **3D — Transition description:** emit the full desired requirement set and dependency DAG from ResolvedConfiguration, including safety preconditions and final verification. The current planner does not diff unverified current state; Phase 4 reconciles against verified effective state and executes.
5. **3E — Representative data-driven profiles:** brightfield, phase contrast, DIC and polarization cases, including unavailable/mismatched installed equipment.


**Phase 3A committed source progress — cross-platform CI passed**
- Added ConfigurationProfileId, immutable ConfigurationProfile and ConfigurationProfileCatalog values. Profiles declare compatible installed objective/condenser/path/camera/illumination identities and optional required path components.
- Added immutable ConfigurationRequest with explicit contrast method, installed component selections, one-or-more unique optical path IDs, camera output mode, illumination channel and explicit exposure policy.
- Added pure ConfigurationResolver over typed equipment/profile/request values. It validates profile membership, installed specifications, camera mode/channel availability and component membership across the union of selected paths.
- Multiple paths are supported by design; a split illumination route and imaging route can jointly satisfy one configuration without requiring all components on every path.
- Resolution returns either the immutable accepted request or stable Error diagnostics. It does not access current runtime state, issue hardware commands or claim hardware completion.
- Added contract tests for a valid brightfield path, separate illumination/imaging paths, unavailable camera modes/channels, profile conflicts, missing paths, required path components and invalid request/profile/catalog values.
- Cross-platform CI passed on Ubuntu GCC, Windows MSVC and macOS Clang for Core standalone, Application standalone and root platform-only builds/tests: [GitHub Actions run 85](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45597867770).

**Phase 3B committed source progress — cross-platform CI passed**
- Added OpticalElementFunction and OpticalFunctionCapability on installed optical components, including phase plate/annulus, polarizer, analyzer, DIC prism, excitation/emission filters, dichroic mirror and darkfield stop.
- Added optional OpticalCompatibilityId on capabilities so mating optical components can be matched by data rather than class behavior.
- Added OpticalFunctionRequirement to profiles with optional compatibility identity and minimum distinct-component count. Duplicate function/compatibility requirements and zero counts are rejected.
- Extended ConfigurationRequest with explicit additional optical component selections. Duplicate selections and overlap with primary objective/condenser selections are rejected.
- Resolver verifies each additional selection is installed, is an OpticalElement, belongs to a selected path and satisfies at least one optical-function requirement in the selected profile.
- Resolver counts required functions across the selected objective, condenser and additional optical components. One physical component counts once; compatibility identity must match exactly when specified.
- Added split-path data-driven phase-contrast and DIC tests. Phase plate/annulus may lie on the illumination/condenser path, while downstream DIC prism/analyzer and camera components are validated over the imaging path. DIC requires a matching polarizer, analyzer and two prisms.
- Negative tests cover mismatched compatibility identities, insufficient prism count and unrelated additional optical component selection.

**Phase 3C committed source progress — cross-platform CI passed**
- Added CameraSpecification exposure-time capability range as an optional installed camera capability, expressed in positive microseconds.
- Added profile target values for condenser aperture NA, unit-bearing illumination intensity and an initial exposure preset.
- Resolver validates aperture against the selected condenser range, illumination target against the selected channel's continuous range and exact unit, and exposure preset against the selected camera's declared exposure-time range.
- Missing exposure-time capability data rejects a profile preset rather than treating the camera as unlimited.
- Added microscope-scoped ResolvedConfiguration, retaining MicroscopeId, immutable request, optional condenser aperture target, optional illumination intensity target and optional exposure preset.
- The request's ExposurePolicy remains separate. A retained preset does not issue a command or overwrite Manual/Automatic/ConstrainedAutomatic.
- Added tests for valid retained targets, aperture/intensity/exposure out-of-range values, intensity unit mismatch, missing camera exposure capability and microscope-scoped resolved output.
- Full Core standalone, Application standalone and root platform-only build/test matrix passed on Ubuntu GCC, macOS Clang and Windows MSVC: [GitHub Actions run 114](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45629930294).

**Phase 3E committed profile coverage — current-head CI pending**
- The Core resolver suite now exercises brightfield, phase contrast, DIC and polarized-light profiles using declarative optical-function requirements.
- Phase contrast requires a matched phase plate and condenser phase annulus across the selected path set.
- DIC requires a compatible polarizer, analyzer and two DIC prism components. The transition planner emits one typed selection requirement per explicitly selected additional optical component.
- Polarized light requires a compatible polarizer/analyzer pair; a negative case rejects the missing analyzer.
- The DIC transition-plan test checks that every additional component selection precedes dependent camera-output and illumination-channel requirements.
- Representative profiles are constructed as typed test data. This does not seed a universal profile catalog or bind profile values to a vendor microscope; JSON profile loading remains Application Phase 5.
- The representative profile suite passed the complete three-platform matrix in [run 122](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45636792527).

**Implementation constraints**
- Objective, contrast method, optical path, camera mode and illumination remain independent dimensions. The resolver combines them; behavior is not implemented in Objective4x/PhaseObjective/DICObjective subclasses.
- Objective selection may select an initial exposure preset, but ExposurePreset and ExposurePolicy are distinct. Manual/automatic/constrained-auto policies must not be overwritten implicitly by a profile.
- Phase contrast requirements describe the compatible phase ring/condenser arrangement. DIC requirements describe the required polarizer/analyzer/prism arrangement and their optical route. The installed definition determines whether the arrangement exists.
- Profiles are data and constraints. Pure resolver functions are deterministic and testable without a microscope, JSON parser, frontend or adapter.
- Unknown/unspecified equipment data is not silently treated as compatible. Return explicit diagnostics when required information is absent.
- Resolver output must not claim hardware state was changed or verified. Transition execution and device feedback remain Phase 4.
- Keep JSON schema/loading in Application Phase 5. Phase 3 operates on typed Core values.

**Phase 3D committed source progress — cross-platform CI passed**
- Added ConfigurationTransitionStepId, typed ConfigurationTransitionRequirement variants, immutable ConfigurationTransitionStep and prerequisite-to-dependent ConfigurationTransitionDependency values.
- Added microscope-scoped ConfigurationTransitionPlan with step/edge identity validation, reference validation, duplicate/self-edge rejection and cycle detection.
- The plan requires one AcquisitionMustBeInactive root and one DesiredConfigurationMustBeVerified sink. Every requirement is directly gated by acquisition inactivity and directly precedes final verification.
- Added deterministic Kahn topological execution layers; independent path/objective/condenser selections can share a layer while dependent camera/illumination/target requirements follow their declared prerequisites.
- Added ConfigurationTransitionPlanner to emit the complete desired requirement set from ResolvedConfiguration: optical paths, objective/condenser/additional optics, camera mode, illumination channel, optional aperture/intensity/exposure targets and explicit exposure policy.
- The planner does not diff current observations, issue commands or claim completion. Phase 4 owns verified-effective-state reconciliation and hardware execution.
- Added tests for required typed nodes, root/sink ordering, parallel selection layers, optional-target omission, malformed path requirements, self-dependencies and cycles.
- The corrected transition model and expanded representative-profile tests passed the complete current-head matrix on Ubuntu GCC, macOS Clang and Windows MSVC, including Core standalone, Application standalone and root platform-only builds/tests: [run 122](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45636792527).

**Phase 3A–3C exit:** satisfied and cross-platform validated. **Phase 3D source exit:** the deterministic adapter-neutral dependency DAG, safety root, verification sink, cycle checks and execution layers are implemented; final current-head cross-platform validation is pending. **Phase 3E source exit:** representative brightfield, phase, DIC and polarized-light profile cases are present; current-head CI is pending. Verified-state reconciliation and execution remain Phase 4.

**Revised Phase 3 work — required before Phase 3 exit**

The earlier 3A–3E implementation established useful typed equipment, optical-function, resolver and transition-plan foundations. Its current request shape requires explicit component identities and treats camera compatibility as a profile list. Those semantics are too restrictive for the revised operating model. The following work replaces those assumptions; it is not a reason to restart the already completed Phase 4 runtime work.

**3F — Composable, editable operation-mode model**
- Add stable technique identities and declarative technique definitions. A technique declares required optical functions/compatibility, route-position constraints, exclusions and optional target contributions.
- Define an operation mode as editable data that composes a set of techniques plus objective constraints and operating targets; do not create one class per magnification/technique combination.
- Preserve manual operation: exact component identities can be supplied as optional hard constraints. Omitted identities activate equipment-based discovery.
- Support required versus preferred per-microscope component bindings. Preferred values rank valid candidates; required values constrain them.
- Merge contributions explicitly and report conflicts when multiple selected techniques demand incompatible components or settings.
- Keep persistence out of Core. Application will load/save equipment and mode revisions in Phase 5; Core consumes already materialized typed definitions.

**3G — Complete candidate resolution and path-derived camera**
- Retain the existing `OpticalPath` as one ordered, physically validated route. Add a typed route purpose (at minimum Imaging, Illumination and Other) so the resolver knows which endpoint semantics apply.
- Add an equipment-owned `LightPath` aggregate: a named, selectable arrangement referencing the `OpticalPath` IDs that operate together. This separates one physical route from the user-visible path configuration and matches the hardware-configuration concept used by commercial microscope software.
- A LightPath must contain at least one imaging route for digital acquisition. An Imaging route must terminate at an installed `EquipmentComponentKind::ImagingDevice` whose same component ID has exactly one `CameraSpecification`; the camera input port must be connected to the previous route step by a validated `OpticalConnection`.
- Do not add an independent camera array to LightPath. Derive its camera endpoint(s) by following the Imaging route terminal steps. The LightPath references routes; route steps reference the shared equipment component identity; CameraSpecification describes the same component's sensor. This prevents duplicate camera identity and topology drift.
- For an ordinary single-camera acquisition, require exactly one camera endpoint on the selected LightPath. A multi-camera LightPath is valid only when the equipment constitution declares the physical branch/splitter and each imaging route terminates at a connected camera. The acquisition request must explicitly choose a supported multi-camera capture arrangement; simultaneous capture also needs declared trigger/synchronization capability.
- Derive camera identity from the selected LightPath's Imaging route endpoint(s). Remove camera identity as an unrelated selection dimension; retain an optional explicit camera assertion only if it matches one of those derived endpoints.
- Enumerate supported output modes only for the path-derived camera(s). A camera installed elsewhere, enumerated by a driver but not on the selected LightPath, or attached to another LightPath is never eligible.
- Enumerate complete compatible configurations rather than accepting the first locally valid component. Discover required phase/DIC/polarization components from all routes grouped by the selected LightPath and validate their physical route position and compatibility identities.
- The selected LightPath may group separate illumination and imaging routes. They can share installed components. Do not require a condenser/phase annulus to appear on the imaging route if its correct physical position is on the illumination route; do not satisfy requirements using components outside the selected LightPath.
- Return a complete resolved desired configuration, including the selected LightPath, constituent route IDs, derived camera endpoint(s), camera output mode(s), selected objective/condenser/optical components, illumination channel(s), and all requested targets. The transition planner must consume that resolved value, not reach back into an incomplete original request for independently supplied component IDs.
- If a LightPath contains several equally valid camera/route arrangements or several candidate LightPaths satisfy the request, return complete alternatives and a selection-required outcome. Do not silently choose the first item in a vector or insertion order.
- Give resolution explicit outcomes: Resolved, SelectionRequired (valid alternatives tied), InputRequired (an essential intent field is absent), and Unsupported (no valid configuration). SelectionRequired returns complete alternatives; InputRequired names the required semantic inputs. A null magnification alone is not InputRequired when NA and sampling data permit automatic selection.
- In a multi-microscope host, evaluate the same semantic request against each instrument's own constitution. Never reuse component IDs across microscopes. Return capable microscope alternatives or request a target when the host cannot choose safely.
- Return no candidate with diagnostics when no installed LightPath satisfies the combined mode and technique requirements.

**3G topology groundwork — source implemented; resolver integration remains pending**
- Added strong `LightPathId`, `OpticalPathRole` (`Imaging`, `Illumination`, `Other`) and the immutable named `LightPath` aggregate, which owns route IDs rather than a copied camera list.
- Added `LightPath` ownership to `MicroscopeEquipmentDefinition`. Its factory validates route references and requires every selectable acquisition LightPath to include at least one Imaging route.
- An Imaging `OpticalPath` must terminate at an installed `ImagingDevice` component with a matching `CameraSpecification`. Existing port ownership, direction and connection-continuity checks ensure that the terminal camera input is physically connected to the preceding route step.
- Added `MicroscopeEquipmentDefinition::cameraEndpointIds(lightPathId)`; it derives unique camera component IDs from the final steps of the selected LightPath's Imaging routes. Camera IDs are not duplicated inside LightPath.
- Added `BeamSplitter` as an optical-function capability. A LightPath deriving multiple distinct camera endpoints is accepted only when its imaging routes share a declared beam-splitter component, use the same physical input port and use distinct output ports.
- Added topology contract cases for two camera branches, excluding an unrelated installed camera, non-camera imaging terminals, LightPaths without imaging routes, and multi-camera paths without a declared splitter. These tests are authored but have not been run in this update.
- This is the topology/modeling slice only. The existing `ConfigurationRequest` and `ConfigurationResolver` still use the legacy explicit camera/path selections. The next 3G implementation must make LightPath the resolver input, derive camera/output-mode candidates from its endpoints, and remove independent camera choice from the normal request contract.

**3H — Objective constraints and camera sampling**
- Add an objective requirement with minimum NA and optional requested nominal magnification. If magnification is omitted, choose the lowest installed magnification that satisfies the NA, modality-specific resolution and image-sampling constraints.
- Treat the optical-resolution criterion as an imaging-modality input, not one universal objective formula. For transmitted widefield brightfield, use the declared illumination wavelength and the selected condenser NA as well as objective NA: (r_{lat}=1.22\lambda_{illum}/(NA_{obj}+NA_{cond})). For reflected/episcopic widefield reflectance, use (r_{lat}=0.61\lambda/NA_{obj}) under the objective-as-illuminator approximation. Do not apply the reflectance expression to epi-fluorescence by substituting emission wavelength: fluorescence requires a model using both excitation and emission wavelengths and their combined PSF/transfer functions; until supported, return sampling adequacy as unknown. Phase contrast must use the selected annular pupil's effective illumination NA; DIC and other contrast methods likewise need their own supported transfer/resolution model. Confocal resolution must use a model that includes excitation wavelength and pinhole/Airy-unit behavior; until that model is supported, return sampling adequacy as unknown rather than applying a widefield shortcut.
- Evaluate the path-derived camera's physical X/Y pixel pitch and the selected CameraOutputMode's true sensor-to-delivered-pixel mapping, including hardware binning, sensor ROI, decimation and downscaling. Upscaling/interpolation must not be counted as additional independent sensor samples.
- Calculate specimen-plane pixel pitch as (p_{obj,a}=p_{sensor,a}\,s_a/M_{eff}) for each axis (a\in\{x,y\}), where (s_a\ge1) is the effective number of physical sensor pixels represented by one delivered pixel and (M_{eff}) is total specimen-to-sensor magnification along the selected imaging route, including objective, system/tube-lens and camera-relay contributions.
- Apply the configured sampling policy (k=2.3) as (r_{lat}/p_{obj,x}\ge2.3) and (r_{lat}/p_{obj,y}\ge2.3). The required total magnification is (M_{eff}\ge\max(2.3\,p_{sensor,x}s_x/r_{lat},\;2.3\,p_{sensor,y}s_y/r_{lat})). Keep the factor configurable as an explicit imaging policy; it is not the Rayleigh constant.
- Distinguish sensor pixel pitch from pixel count. Pitch and the readout mapping determine sampling interval. The active delivered pixel dimensions determine field of view: (FOV_x=N_xp_{obj,x}), (FOV_y=N_yp_{obj,y}). Megapixel count alone cannot establish optical resolution or sampling adequacy.
- Include objective immersion medium, working distance, coverglass design/correction, chromatic/field correction and field-number/sensor compatibility in hard candidate qualification where those specifications are relevant.
- Use the total magnification data only from components on the selected imaging route. Camera relay magnification can affect detector sampling even where it is after an intermediate optical plane; do not take relay values from unrelated paths. Missing wavelength, condenser setting/NA, relay magnification or pixel mapping is unknown/insufficient evidence, not a passing result.
- Rank candidates using declared preferences from a specimen/task imaging profile and/or microscope-specific mode binding only after hard optical/detector constraints pass. For example, an H&E task may prefer 0.75 NA over 0.4 NA when both meet all constraints. No sample-specific hard-coded branch is permitted.

**3F–3H acceptance**
- A 20× phase request discovers a valid matched phase plate/annulus without requiring the user to name those components; the annulus may be on the selected illumination route and the phase plate on the selected imaging route.
- Phase contrast and polarization may be composed when their combined LightPath route/component constraints are satisfiable; incompatible combinations are rejected with diagnostics.
- A missing or mismatched component invalidates only the relevant candidate; another valid LightPath may still be offered.
- Every imaging route terminates at a Camera component with a matching CameraSpecification, and its input port is physically connected to the previous path step. A non-camera terminal is invalid for an Imaging route.
- A camera installed elsewhere or present on another LightPath is never eligible. One-camera LightPaths derive exactly one camera; a multi-camera LightPath requires explicitly modelled splitter branches and a declared supported capture mode.
- Once a connected camera is manually assigned and driver-operable, a camera performance/profile mismatch is advisory only. The resolver returns warning diagnostics and retains the user's selection; it does not reject or silently replace the camera.
- Automatically discovered cameras are ranked by task-relevant sensor evidence. Global shutter is a final preference tie-breaker among otherwise equivalently suitable cameras, not a universal hard requirement.
- Camera profile matching is by manufacturer/model/variant. If runtime discovery omits a variant and multiple catalog profiles match, return an ambiguous model-profile result instead of choosing one arbitrarily.
- Two valid connected LightPaths/camera candidates are returned as alternatives; equal-ranked candidates require explicit selection.
- A per-microscope preferred objective resolves a valid tie; a required objective binding rejects fallback.
- For two 20× objectives with NA 0.4 and 0.75, an H&E imaging preference ranks 0.75 higher only when both otherwise satisfy the hard optical, field, working-distance, correction and camera-sampling constraints.
- When magnification is unspecified, the lowest objectively adequate magnification is selected only after NA and modality-specific resolution/sampling have been evaluated for both sensor axes.
- Transmitted widefield tests use objective and selected condenser NA; reflected-light reflectance tests use objective NA; epi-fluorescence tests require a separate excitation/emission model; unsupported DIC/confocal sampling does not pass through a generic widefield formula.
- Tests cover non-square pixel pitch, active pixel count versus field of view, binned/unbinned/decimated/downscaled modes, upscaling that must not improve sampling, objective and condenser NA, separate illumination/imaging routes, relay magnification, missing wavelength/relay/pixel mapping, camera endpoint connectivity, camera outside selected LightPath, splitter-backed multi-camera routes and all resolution outcomes.
- Missing mandatory intent such as a minimum objective NA may produce InputRequired; null magnification by itself does not.

**Commercial and physics reference set for this phase**
- Nikon NIS-Elements documents Light Paths as arrangements of cameras and illumination devices used together, and associates Optical Configurations with a Light Path: [Device Manager](https://www.nisoftware.net/NikonSaleApplication/Help/Docs-AR/eng_ar/visual.device.manager.html) and [Experiment Acquisition](https://www.nisoftware.net/NikonSaleApplication/Help/Docs-D/eng_d/experiment.acquisition.html).
- ZEISS ZEN documents Smart Setup and reproducible acquisition settings: [ZEN microscopy software](https://www.zeiss.com/microscopy/en/products/software/zeiss-zen.html).
- Evident cellSens documents encoded-device setting recall and simultaneous multicolor imaging using two cameras or an image splitter: [cellSens](https://evidentscientific.com/en/products/software/cellsens).
- Physics inputs are cross-checked against the [Nikon microscopy resolution calculator](https://www.microscope.healthcare.nikon.com/microtools/resolution-calculator/) and [Hamamatsu camera sampling guide](https://camera.hamamatsu.com/jp/en/learn/technical_information/thechnical_guide/dissecting_camera/dissecting_camera02.html).

**3I — Camera model catalog and fluorescence suitability**
- **Source groundwork implemented:** `Camera/Common` now has reusable `CameraModelProfile`/`CameraModelCatalog` types keyed by manufacturer, model and optional variant, with profile/catalog revisions and provenance references. Exact matching is distinct from candidate matching; incomplete runtime identity may return multiple catalog candidates and must not be resolved by insertion order.
- **Source groundwork implemented:** common camera discovery includes shutter/sensor capability flags and optional sensor characteristics. The AmScope backend maps only the vendor model facts it exposes (including global shutter, monochrome, CMOS and CCD indicators); it does not invent QE/noise/pixel data.
- **Source groundwork implemented:** Core `CameraSpecification` can carry optional sensor technology, shutter type, color mode, wavelength-dependent QE, read noise, dark current, full well, frame rate and provenance. `CameraSuitabilityAssessor` evaluates fluorescence preferences and returns Warning diagnostics. The legacy profile camera mismatch is now a warning rather than a rejection.
- **Source tests authored:** model catalog identity/ambiguity/provenance, QE validity, warning-only manual camera choice, and global-shutter tie-breaking. Tests are registered in the relevant existing targets but have not been run.
- **Remaining:** connect the Camera catalog to Application persistence; merge catalog profiles into live discovery by stable model identity; project enriched discovery into the selected LightPath camera's Core specification; make the configuration resolver consume the derived LightPath camera, not the legacy request camera ID; retain selection warnings in the resolved configuration/acquisition snapshot.
- **Remaining:** replace the initial unweighted average QE over a band with a spectral calculation weighted by the detected emission spectrum and selected dichroic/emission-filter transmission. Include exposure duration, camera mode, sensor readout mapping, signal/noise and task acquisition requirements. Unknown evidence remains a warning/unknown assessment, never a fabricated value.
- **Remaining:** finish advisory candidate ranking. Compare wavelength-relevant QE, read noise, dark current, full well, pixel pitch, active readout/mode, frame rate and color-filter-array penalty where relevant. Recommend global shutter only when candidates are otherwise equivalently suitable or the task declares a temporal requirement that makes it decisive. A manual camera selection with a performance mismatch remains usable with warnings.

**3J — Spectral illumination sources and optical filters**
- **Source groundwork implemented:** `IlluminationSourceTechnology` includes LED, halogen, xenon arc, mercury arc, metal-halide arc and laser; illumination channels can carry an ordered spectral power distribution.
- **Source groundwork implemented:** `OpticalFilterSpecification` represents excitation, emission, dichroic, neutral-density and other filter roles with wavelength-dependent transmission and optional reflection response. Equipment-definition validation binds filter role and compatibility identity to the installed optical-function capability.
- **Source tests authored:** illumination spectrum ordering/values, filter spectral response, dichroic transmission/reflection energy validation, filter role/function matching and compatibility identity. Tests have not been run.
- **Remaining:** validate each filter/source against actual LightPath membership and direction; implement spectral interpolation/integration; compose source SPD × excitation filter × dichroic reflection for excitation, and fluorophore emission × collection path × dichroic transmission × emission filter × camera QE for detector-side suitability. Preserve manufacturer/measured response provenance, wavelength step/resolution, AOI and polarization conditions where they materially affect the filter response.
- **Remaining:** model source channel control ranges and fixed-output sources consistently with adapters. A relative source SPD is not a specimen irradiance measurement. Keep per-channel output data and the optical route's attenuation/throughput separate.

**3K — Per-operation-mode LightPath illumination settings (Core contract groundwork)**
- **Source groundwork implemented:** `LightPathCalibration` stores measured source-drive-to-specimen-irradiance points and uncertainty, bound to microscope identity, equipment-definition revision, operation-mode revision, LightPath, source component/channel and configured optical component identities.
- **Source groundwork implemented:** `LightPathModeSetting` stores one operation mode's target specimen-plane irradiance and exact calibration revision. It resolves a device drive value only when microscope/equipment/mode/path/source/channel/component bindings match. Targets outside the measured range do not extrapolate.
- **Source tests authored/registered:** interpolation, range rejection, stale mode/equipment revision, cross-microscope/path, changed filter identity, non-monotonic curve and duplicate component checks. Tests have not been run.
- **Remaining:** replace/supplement the component-ID list with a canonical optical-state fingerprint covering selected route revision, objective, condenser/annulus/stop, filters, beam splitters, aperture/iris/selector states and all other settings that alter irradiance. Capture the measurement plane/instrument, instrument calibration, conditions, timestamp, uncertainty and source-drive limits. A component identity list alone does not identify the physical optical state.
- **Remaining:** persist one calibration binding for each microscope-specific operation-mode LightPath configuration. Reuse is permitted only when the complete optical-state fingerprint and calibration conditions match. A global revision bump must not be the sole invalidation logic for unrelated edits.
- **Remaining:** connect the mode setting to a real source command/feedback workflow and verified effective irradiance. A source drive percentage is never treated as irradiance. Fixed or unregulated sources can retain measured fixed output but must not be presented as controllable.
- **Mode example:** separate 4× Brightfield, 40× Brightfield and 4× Darkfield modes can retain different calibrated specimen-plane irradiance targets. A user may set 4× Brightfield lower than 40× Brightfield and 4× Darkfield. No universal intensity-versus-magnification formula is assumed; darkfield stop/condenser geometry and filter attenuation are part of the calibration fingerprint.

### Phase 4 — Core operation runtime

**In progress — 4A lifecycle reducer validated; 4B stage/navigation/position-state contracts implemented; 4C bounded feedback ingress, single-writer runtime, wake/worker primitives, X/Y reducer, Application StageNavigationRuntime and a bounded typed stage-control queue are implemented. Live host callback composition, real drivers and verified feedback-loss reconciliation remain outstanding.**

**Objective:** execute adapter-neutral configuration plans through capability-specific ports while keeping one serialized state owner per microscope runtime. An operation may succeed only after its required completion evidence has been evaluated by Core.

**Implementation sequence**
1. **4A — Lifecycle semantics:** immutable operation/step state and pure legal-transition functions. Distinguish CommandDispatched, AdapterAccepted, DeviceAcknowledged, DeviceProgress, DeviceActionCompleted, DeviceStateObserved and StepVerified. DeviceActionCompleted moves to AwaitingObservation; it never marks a step verified by itself.
2. **4B — Command and port contracts:** define command correlation scope and capability-specific command values/ports. Implement the typed stage-motion port (MoveAxesTo, MoveAxesBy, HomeAxes, StopAxes) in physical axis units. Keep logical DeviceId separate from SDK handles and controller-native steps/protocol values. The adapter's synchronous dispatch result reports only accepted/rejected responsibility; it is not device acknowledgement. Do not add a generic string-command API.
3. **4C — Serialized runtime and ingress:** implement one single-writer operation runtime per microscope incarnation, thread-safe feedback ingress, bounded FIFO receipts and atomic Core-assigned ingress sequence. SDK callbacks enqueue only; they never mutate operation state directly. Application supplies RuntimeWakeSignal and a worker-loop entry point that binds the runtime to a host-owned thread, drains feedback before host-typed controls and waits against the pre-drain wake generation.
4. **4D — Plan execution:** execute ConfigurationTransitionPlan dependency layers through the relevant typed ports, retain command/step correlation and dispatch dependent work only when its prerequisites reach required state. Resource claims, not topological-layer membership alone, determine safe concurrency.
5. **4E — Verification and failure lifecycle:** evaluate typed observations against step requirements and evidence policies; implement timeout, cancellation, adapter failure, stale generation/late feedback, queue overflow, partial completion and verified-state reconciliation.

**4A committed source progress**
- Added OperationLifecycleEvent and StepLifecycleEvent plus pure applyOperationLifecycleEvent / applyStepLifecycleEvent transitions.
- Operation success is allowed only from Verifying. Cancellation remains requested until cancellation confirmation.
- Command dispatch, adapter acceptance and device acknowledgement are separate step transitions.
- DeviceActionCompleted moves a step to AwaitingObservation; DeviceStateObserved moves it to Verifying. Core also has an explicit VerificationStarted control transition from AwaitingObservation for a calibrated command-derived estimate; only a Core VerificationSucceeded event marks it Verified.
- Manual steps have an AwaitingOperator path. OperatorConfirmed enters Verifying and is not itself proof of completion.
- Timeout, adapter failure/rejection, partial completion and terminal-state behavior have focused tests.
- Core standalone, Application standalone and root platform-only build/tests passed on Ubuntu GCC, Windows MSVC and macOS Clang: [run 124](https://github.com/Shennagyp/MICROSCOPE_CPP/actions/runs/45639182251).

**4B source progress — initial stage/navigation contracts**
- Added IStageMotionPort with absolute/relative multi-axis movement, homing and controlled stop. Multi-axis intent must be preserved or rejected; the adapter cannot silently serialize it.
- Added typed motion values, operation/device/command correlation, immediate adapter submission result and normalized position/state/limit/action/fault feedback payloads.
- Position feedback uses AxisPositionObservation: axis coordinate, measurement class and uncertainty in that coordinate's declared unit. The feedback envelope carries source provenance and correlation separately. Command-derived estimates are produced by Core, not sent back as device measurements.
- Added DualSlideCarrierSpecification to the installed equipment definition. The OPTIKA arrangement is two horizontal side-by-side slots on one X/Y plane, not a vertical rack.
- Added independent SlideGeometry/SlideRegistration values. Four measured corners are fitted to an affine mapping; individual residuals and maximum/RMS fit residuals are retained and an acceptance threshold gates registration.
- Added DualSlideCarrierState with left/right occupancy and separate SlideId registrations. A requested SlideId/local point resolves through its own registration to a common stage-plane target, converts micrometers into the declared X/Y axis units and rejects targets outside installed stage travel.
- Added StageAxisPositionState. Command-derived movement accumulates a conservative error bound; controller-reported coordinates do not reset open-loop uncertainty; encoder/external measurements may reset it to their measurement uncertainty; invalid reference or unknown/exceeded error requires homing.
- Added StagePositionState, an immutable aggregate for the carrier's in-plane X/Y position. It updates absolute/relative commanded positions, measurement observations and verified homing observations per axis; reference invalidation applies to both axes as one plane. It maps current machine coordinates back to slide-local coordinates only for the same installed stage.
- Added the application-facing StageNavigationPlanner::planToSlidePoint entry point using the complete StagePositionState aggregate. It creates the revision-bound target, checks physical slide bounds/stage travel and rejects cross-stage state. The aggregate resolveAfterHoming path re-resolves the preserved slide-local target through the unchanged registration after verified homing.

- Bound every StageAxisPositionState to its installed EquipmentComponentId and StageAxisId; planning and slide position estimation reject cross-stage state even where two microscopes reuse the same axis identifiers.
- Retain the registration's configured fit-residual acceptance threshold with the CalibrationRevisionId, alongside actual per-corner, maximum and RMS residual evidence.
- Added CalibrationRevisionId to each SlideRegistration and SlideNavigationTarget to preserve the exact slide placement used by a navigation request.
- Implemented BoundedFeedbackInbox<TPayload> as a fixed-capacity thread-safe ring with Core-assigned FIFO sequences, runtime-scope rejection, explicit close and an IFeedbackRuntimeSignal control path. The noexcept signal distinguishes successful enqueue, feedback loss and ingress closure. QueueFull does not consume a sequence; overflow remains latched until the runtime acknowledges reconciliation.
- Added RuntimeDeviceSessionBinding and ActiveOperationStepBinding to reject feedback from prior device-session generations and superseded operation-step/command identities before state reduction.
- Implemented SerializedOperationRuntime<TPayload> as a Core single-writer operation/step state owner. It binds mutations to one owner thread, validates runtime/device/session/operation/step/command identity, applies the lifecycle reducers, returns the typed payload with its receipt to the owner, and retires active work when a device session is replaced or closed.
- Added Application RuntimeWakeSignal implementing IFeedbackRuntimeSignal and SerializedRuntimeWorker::run. The host starts the worker thread; the worker binds Core ownership, drains feedback FIFO before the host's non-blocking typed control drain, and waits against a wake generation captured before draining.
- Added StagePositionFeedbackReducer. It applies only fresh current-session AxisPositionObservation payloads to the configured X/Y plane, ignores non-position and non-planar feedback, and invalidates both machine references while feedback loss remains unresolved. Normal telemetry never establishes a home reference.
- Added `StageHomeReferenceObservation` and `HomeReferenceEstablished` feedback. The fact carries a complete set of referenced-axis coordinates and exact HomeAxes command correlation; Core moves the matching step to Verifying only. Application restores X/Y machine references only after validating the exact HomeAxes request and live Core step status Verified.
- Feedback queue loss blocks command dispatch and step/operation verification until the inbox is drained and the owner acknowledges reconciliation. A completed device action remains AwaitingObservation; only an explicit verification transition marks a step Verified. Application now has RuntimeWakeSignal and SerializedRuntimeWorker; the worker drains receipts on the Core owner thread and runs the host-provided typed control-request drain. Subscriber notification delivery and the concrete host control queue remain outstanding.
- Added tests for capacity/closure, queue overflow and loss callback, runtime-scope rejection, gap-free sequence assignment and concurrent multi-producer FIFO order.
- Added StageNavigationPlanner and StageHomingPolicy. It validates the target and registration before planning, selects direct coordinated X/Y movement while within both error thresholds, or homes both axes otherwise. After verified homing it re-resolves the saved slide-local point only if the registration revision is unchanged.
- Propagates slide-registration uncertainty through the inverse affine transform with the rectangular affine interpolation L1 bound and refuses slide-local position estimates outside the selected slide geometry.
- Added focused Core tests and CMake targets for the motion port, dual-slide mapping, position tracking and homing/navigation planning. Hardware drivers and serialized runtime execution are not part of this slice.

**Stage-navigation owner integration — source implemented; current-head CI pending**
- Added Application StageNavigationRuntime as the owner-thread stage subsystem. It owns the active dual-slide carrier, X/Y StagePositionState, homing policy, calibrated movement-error budget, verification policy and one active navigation execution.
- It consumes the existing StageNavigationPlanner, StageNavigationExecutor, StagePositionCommandReducer and StagePositionFeedbackReducer rather than duplicating their calculations.
- BoundedCommandEstimate mode supports open-loop drivers: after correlated DeviceActionCompleted, Core applies the calibrated error contribution and the Application evidence policy starts verification through the explicit VerificationStarted control transition. No synthetic position observation is created.
- DevicePositionObservations mode requires fresh, command-correlated X and Y position observations and checks target deviation plus the retained uncertainty bound before verification.
- A complete, correlated HomeReferenceEstablished fact is checked against the requested two-axis home and its uncertainty policy. Only then is the home step verified, the X/Y reference restored, and a new absolute move resolved from the preserved SlideId/local point/registration revision.
- Added focused Application tests for bounded open-loop completion, measured X/Y completion and home-then-restored-move. The test target is registered in Microscope.Application/CMakeLists.txt; no build or test result is claimed here.
- Added StageNavigationControlQueue, a bounded stage-specific FIFO that wakes RuntimeWakeSignal and rejects full/closed submissions explicitly. Its owner-thread drain creates operation/step state through StageNavigationRuntime::beginRequest and starts at most one active navigation; queued work remains ordered until the active navigation completes.
- The control drain also observes the independent feedback-loss latch. It invalidates both X/Y machine references even when no feedback receipt is available, leaves the loss latch unacknowledged, and suspends dispatch until host reconciliation.
- StageNavigationRuntime binds to the serialized operation-runtime owner thread. The first feedback callback can bind it after SerializedRuntimeWorker has bound Core; the control-queue drain also ensures binding before dispatch.
- The application host still owns the dedicated worker thread, invokes StageNavigationRuntime::processFeedback from the feedback handler and StageNavigationControlQueue::drain from the control handler, and publishes the resulting immutable operation/position projections. No thread or universal command bus is created by these stage-specific types.

**Implementation constraints**
- Per-runtime state transitions have one logical owner. Adapter callbacks enqueue immutable feedback and never call the reducer directly from device/SDK threads.
- Every operation-correlated feedback item must match MicroscopeId, RuntimeId, RuntimeGeneration, DeviceId, DeviceSessionGeneration, OperationId and StepId; CommandId is required where the fact is command-specific.
- Unsolicited telemetry has no operation correlation and cannot satisfy an operation step.
- A timeout/cancellation request is not evidence that hardware stopped. Late feedback is correlated and reconciled; it cannot silently revive a terminal operation.
- Queue overflow must latch feedback loss and mark affected state unknown through a control path independent of the saturated feedback queue.
- Core must not fabricate hardware feedback or promote operator confirmation/calibrated estimates into stronger evidence than the selected completion policy allows.
- Current operation/step snapshots are runtime state, not event sourcing. Persist only the history required by recovery/audit use cases.
- Existing camera acquisition workers, frame pool, leases and mailbox remain separate from the operation event channel.

**Exit:** no operation is marked complete solely because a command was dispatched, accepted or reported action-complete. It succeeds only after every required step and the final desired configuration satisfy their explicit evidence predicates.
### Phase 5 — JSON and application composition
- Define versioned microscope-definition JSON schema.
- Define deployment JSON schema for adapter bindings.
- Add Application-owned definition catalog/loader.
- Add explicit adapter factory registration and binding.
- Add per-device resource claims and runtime manager.
- Create/open/close multiple microscope instances.

**Exit:** application can load a JSON definition, resolve its adapter factories and create a validated Core runtime.

### Phase 6 — Calibration and slide navigation

**Partially implemented in Phase 4B/4C. Stage physical calibration and durable storage remain outstanding. The initial per-mode LightPath irradiance calibration value contracts are now present in Core; measurement workflow, physical state fingerprint, hardware feedback and persistence remain outstanding.**
- **Source groundwork implemented under Phase 3K:** `LightPathCalibration` and `LightPathModeSetting` model a per-mode measured drive-to-specimen-irradiance relationship. They are not yet a hardware calibration workflow or persistence implementation.
- **Implemented:** slide geometry; OPTIKA two-slot horizontal carrier specification; independent four-corner affine registrations; fit residual and uncertainty propagation; slide-local/stage-plane transforms; slide and stage boundary validation.
- **Implemented:** X/Y machine-reference and position state; accumulated conservative command-error bounds; homing threshold policy; preserved slide-local target and registration-revision validation after homing.
- **Implemented:** Application navigation execution for both bounded command-derived estimates and correlated controller position observations.
- Implement nominal optical estimation and measured ruler calibration; define optical calibration keys and reuse/invalidation.
- Implement linear/rotary physical-axis calibration and evidence quality, distinct from controller steps-per-unit configuration. The current StageMotionErrorBudget is supplied by the host; no fitted calibration model produces its contributions yet.
- Add slide-corner acquisition/registration workflows, registration acceptance, occupancy changes and reseat invalidation.
- Persist immutable motion-calibration and slide-registration revisions through appropriate persistence ports.
- Integrate host typed control requests, a real driver binding and feedback-loss reconciliation before claiming hardware navigation acceptance.

**Exit:** Core can navigate to any accepted point on either registered slide with a traceable physical error bound; both slide maps remain independent; homing does not imply slide re-registration or erase motion-calibration error.

### Phase 7 — Sample, slides and persistence
- Define distinct strong identity types for SampleId, SlideId and TileId; identifiers are opaque and independent of labels, paths and display order.
- Define Sample and SampleType as source-specimen identity and taxonomy.
- Define Slide and SlideType as physical preparation identity and taxonomy.
- Enforce the baseline relationship: each Slide references exactly one existing Sample; one Sample may have many Slides.
- Make Slide.SampleId the authoritative relationship. Do not persist an authoritative SlideIds collection on Sample; provide reverse lookup through a query/index.
- Define the Sample record fields: stable SampleId, SampleTypeId when classified, display label, optional external accession and source/collection metadata.
- Define the Slide record fields: stable SlideId, required SampleId, SlideTypeId when classified, display label, physical SlideGeometry, versioned preparation snapshot and package reference.
- Persist each Slide's independent registration and four corner correspondences, fit residuals, provenance and calibration revision.
- Define Tile metadata: stable TileId/SlideId, optional display label/order hint and optional slide-local/stage-machine position evidence; never treat display order as physical coordinates.
- Define the versioned SlideConfigurationSnapshot and immutable Sample/Slide identity semantics.
- Make Tile belong to one Slide; permit no physical position and do not interpret ordering/index as coordinates.
- Define archive/deletion constraints: Sample archival does not cascade-delete slides or image data; a referenced Sample cannot be destructively deleted.
- Define independent Sample and Slide package persistence. Slide packages retain SampleId and remain independently movable; the Sample record is not duplicated inside the Slide package as another authority.
- Implement on-demand object materialization: load a Sample, Slide, Tile, document or acquisition aggregate when it is opened or first required by a workflow, not by loading the entire workspace at startup.
- Retain each active persistent identity as one canonical in-memory object within its workspace/runtime scope; consumers access it through its owner API or read-only projections, and only the designated owner mutates it.
- Load required related metadata in bounded batches. Do not use database-backed lazy properties, getter-triggered reads, per-command rereads or N+1 relationship loading.
- Apply changes to the resident object and persist at explicit transaction/save or recovery-checkpoint boundaries. A failed save must retain the dirty in-memory object; it must not be replaced by a stale reload.
- Release clean objects only after they are no longer referenced by an open document or active workflow. Reopening after release loads the latest durable revision.
- Keep large image payloads file-backed or tile/frame-decoded through bounded buffers while their document and metadata objects remain resident.
- Define portable relative payload references and a rebuildable workspace catalog/index.
- Define deterministic safe storage-key encoding/mapping for opaque IDs; do not use raw IDs as filesystem path segments.
- Enforce mandatory active slide session in Core.
- Support opening/browsing Sample and Slide packages without a microscope connected.

**Exit:** one sample can have many slides; the two carrier slots independently reference persistent Slide identities; registration does not change Slide/Sample ownership; unpositioned fields remain valid.

### Phase 8 — Acquisition
- Define distinct AcquisitionPlanId, AcquisitionRunId and ImageFrameId identities.
- Associate each AcquisitionPlan with exactly one Slide; freeze its intended settings when execution begins.
- Associate each AcquisitionRun with exactly one Slide, one AcquisitionPlan and the actual MicroscopeId used.
- Resolve the Sample context through Slide.SampleId rather than maintaining a second editable Sample relationship on the run.
- Define Tile as a stable field identity: a run can produce multiple tiles, and a tile can receive frames from multiple runs.
- Require each ImageFrame to reference exactly one AcquisitionRun and one Tile belonging to that run's Slide.
- Define ImageFrame metadata: immutable frame identity, run/tile references, payload reference and integrity metadata, capture timestamp, delivered dimensions/pixel format, camera/readout context and actual settings when available.
- Preserve original frame payloads immutably and separate payload storage from capture metadata.
- Keep requested values distinct from actual device observations; missing readbacks remain unknown.
- Commit a recoverable run identity before dispatch; persist payload/integrity metadata before publishing a frame as available.
- Add optional position evidence, explicit missing/failed frames, Z planes, exposure brackets and partial-run persistence.
- Integrate existing camera frame delivery through its adapter without changing the existing bounded frame/lease/preview pipeline.

**Exit:** plans and actual runs remain distinct; every frame has valid run/tile provenance; repeated acquisitions can contribute to an existing tile; cross-slide frame references are rejected; incomplete runs recover correctly; original frames remain immutable; no additional preview queues or pixel copies are introduced.

### Phase 9 — Image documents and annotation
- Define persistent annotation document and geometry types.
- Keep geometry in source-image coordinates.
- Store measurement calibration revision, unit, result and provenance.
- Add image processing provenance for HDR/focus stack.

**Exit:** reopening a saved image reconstructs the same original pixels and annotation state without a connected microscope.

### Phase 10 — Frontend migration
- Add Objective-C++ bridge to Microscope.Application.
- Replace Swift-owned microscope configuration behavior with the C++ application API.
- Keep SwiftUI/AppKit presentation and transient interaction state.
- Preserve native frame streaming as a separate path.
- Add CLI/service host only when needed.

**Exit:** frontends consume one application API and do not contain microscope policy.

## Verification strategy

- **Static/model validation:** identity integrity, graph/topology consistency, schema validation and dependency direction.
- **Sample/slide contract tests:** unknown SampleId rejection, Sample-to-many-Slides cardinality, distinct SampleType/SlideType semantics, stable IDs after folder moves, non-authoritative reverse indexes and archive/deletion referential integrity.
- **Acquisition contract tests:** plan/run distinction, actual MicroscopeId capture, one-slide run constraint, frame-to-run/tile consistency, cross-slide rejection, multi-tile runs, repeated tile acquisition, idempotent duplicate frame delivery and interrupted-run recovery.
- **Pure unit tests:** configuration resolution, calibration fitting, unit conversion, transition planning and state reducers.
- **Runtime contract tests:** simulated adapter responses, delayed callbacks, duplicates, stale/late events, queue saturation/feedback-loss latching, reconciliation before verification, timeout, failure and manual confirmation.
- **Application integration tests:** definition loading, adapter registration, resource conflicts, runtime lifecycle and frontend projections.
- **Hardware acceptance:** separate user-run checks for actual camera, stage, objective selector, illumination, optical selectors and calibration equipment.

Do not run hardware tests in CI. Do not interpret pure test success as proof of actual hardware precision or device behavior.

## Immediate next implementation task

The Core inbox/runtime and Application stage-navigation runtime now provide the state and execution path for stage navigation over the selected slide registration. The runtime worker still accepts a host-supplied typed control drain; it does not create a thread or a universal command bus.

**Next implementation work**
1. Compose StageNavigationControlQueue::drain and StageNavigationRuntime::processFeedback into the live host's SerializedRuntimeWorker callbacks; publish operation snapshots and machine/slide-local position projections through the Application API.
2. Implement the physical motion-calibration model that supplies StageMotionErrorBudget contributions from measured displacement, residuals and uncertainty. The current budget is an injected value, not a fitted calibration.
3. Implement feedback-loss reconciliation: stop/inspect the controller, read back or re-home as appropriate, re-establish references, and acknowledge the runtime latch only after reconciliation.
4. Add stage motion-state, limit and fault reduction plus controlled-stop/timeout handling.
5. Bind real external driver libraries through IStageMotionPort and perform user-side hardware acceptance. The main library must not implement STEP/DIR pulse generation or vendor protocols.

Controller-specific STEP/DIR generation and vendor protocols remain in external driver libraries.


## Current stage coordinate model

The active Core model is split by physical responsibility:

- StageWorkingSurface2D is created as an empty XY canvas from a user-defined stage-coordinate origin and positive X/Y travel extents. It derives the far boundaries and validates the complete canvas against the configured X/Y axis operating ranges.
- The canvas starts with zero slide rectangles; the user adds, replaces or removes slide rectangles as immutable layout snapshots. The workspace origin is not silently reused as any slide's stop-point origin.
- Each CAD rectangle retains its fixed mechanical stop-point endpoint, initial opposite-corner handle, solved opposite endpoint and constrained length/width. The Core API exposes all four stage-coordinate corners derived from those constraints; the two adjacent corners are not independent measurements.
- SlidePlacement2D maps between stage XY and the selected slide's local XY. It stores no Z, slide thickness, drawing tolerance or stop-point uncertainty.
- StagePosition3D tracks machine X/Y plus microscope focus Z. positionOnSlide() requires referenced X/Y only; the current focus Z coordinate is exposed independently and cannot affect slide-local XY.

The microscope focus Z operating range is an AxisOperatingRange on the microscope's axis specification. A physical endstop uses an active matching limit-switch observation. A no-switch axis can use FullRangeSweep toward the configured upper or lower boundary: the workflow commands one complete configured travel range in that direction from an unknown starting coordinate that is assumed to be within the physical travel envelope. The sweep is permitted only when the focus mechanism declares ContinuousKnobRotationWithoutClash, so continued knob rotation after the carriage reaches the travel boundary cannot collide. Without independent encoder/external measurement, the datum is a calibrated estimate, never a measured position.

Core geometry, the full-range reference state transition, position projection and tests are implemented. Application execution of the correlated sweep command, promotion of its resulting snapshot into the Z position state, host composition, persisted CAD layout revisions and frontend projection remain outstanding.

### Stage feedback update — XYZ position reducer

The Application layer now has StagePosition3DFeedbackReducer for serialized worker use. It applies current-session XYZ position observations to the immutable Core aggregate, ignores non-position payloads and unrelated axes, and invalidates all three absolute references on true whole-device feedback loss. Its verified-home path accepts exactly the non-empty requested subset of the stage's X/Y/Z axes, validates exact axis-set equality and command correlation, and applies the home fact only after Core marks the associated operation step Verified. Slide-plane navigation therefore references X/Y only; focus Z retains an independent reference lifecycle.

Physical endstop datum evidence is represented separately from encoder and external measurement and from command-derived estimates. The Core and Application tests were registered; they were not executed here. The existing application worker still requires a concrete host composition to invoke the reducer and publish its resulting snapshots.


### Remaining CAD authoring work

Core can now create a blank workspace from the user's origin and travel extents, then add, replace or remove slide rectangles through immutable snapshots. The rectangle's first endpoint remains a user-supplied slide stop-point coordinate; it is never inferred from the workspace origin.

Interactive pointer-driven drawing/editing, incremental constraint solving while dragging, undo/redo, persisted layout revisions and a configurable positive clearance distance remain outstanding. Core validates that every solved rectangle fits inside the workspace, slide identities are unique, and distinct rectangles do not have positive-area overlap. Exact edge/corner contact is allowed; no arbitrary clearance is imposed.

## Authoritative correction — empty XY canvas and variable slide count

This section supersedes any earlier plan language that assumes a fixed ten-slide layout, two permanent slide slots, or an implicit slide rectangle. The active model is one stage with an initially empty XY canvas and a user-defined, variable number of slide rectangles.

### Canvas and geometry

- The user defines the workspace origin as a stage-coordinate `(x, y)` point and defines positive X and Y travel extents. The far workspace boundaries are origin plus the respective travel extents.
- The new canvas contains zero slide rectangles. No slide geometry, count or slide-local origin is inferred.
- Each slide is drawn independently. The user supplies that slide's mechanical stop-point endpoint, the initial opposite-corner handle, the length-axis assignment (StageX or StageY), and constrained length and width.
- The Core model exposes all four constrained stage-coordinate corners. The slide's stop point is independent of the workspace origin.
- A slide must fit wholly inside the workspace. Distinct rectangles may share exact edges/corners, but positive-area overlap is rejected. A positive clearance margin requires an explicit mechanical-holder rule and is not inferred.
- Layout edits are immutable snapshots: add, replace and remove. This gives undo/redo a stable basis, but durable revision storage is still outstanding.

### Generic navigation implementation

Core now contains a revision-bound slide navigation target and a slot-independent planner. A target stores the slide identity, local XY point and slide registration revision. It is rejected if that registration is replaced before execution. A planned movement is an absolute two-axis X/Y command; focus Z remains a separate machine coordinate.

The Application library now contains the generic executor, owner-thread runtime and bounded control queue. It preserves device/session/operation/step/command correlation, verifies exact home-reference axis sets, homes X/Y only for slide-plane navigation, restores the same slide-local target after verified homing, and supports bounded command estimates or fresh device position observations. XY operation failures invalidate XY only; true whole-device feedback loss invalidates X/Y/Z.

The previous `DualSlideCarrierState`-based runtime and queue remain compiled for compatibility and regression coverage. They are legacy and must not be used for new navigation.

### Verification status and remaining work

Core and Application test executables have been added to their respective CMake test configurations. Source-level assertions cover empty-canvas construction, input validation, independent slide origins, four-corner geometry, overlap rules, arbitrary slide identities, revision invalidation, unit conversion, X/Y-only homing, independent focus Z, correlated feedback, malformed XY observations, off-target measured poses, adapter rejection and bounded queue behavior. **No build, test, debugger or CI execution has been performed here.** Do not describe these tests as passing.

The generic C++ runtime is not yet wired into the SwiftUI composition or the physical stage-driver host. The next implementation boundary is the long-lived host composition that owns the C++ stage objects and the concrete physical-stage `IStageMotionPort` adapter. The host supplies the microscope/runtime/device-session correlation once for that session; the persistent `StageWorkingSurfaceNavigationRuntime` allocates operation, step and command identities internally. Hardware observations enter the retained serialized feedback ingress, and the same owner thread advances the existing objects through their lifecycles.

The Z full-range sweep still requires its Application execution state machine and correlated completion handling. The CAD editor, undo/redo, durable layout persistence, calibration workflow, holder-clearance specification and recovery UI also remain outstanding.

## Stateful OOP application and functional-core boundary

The application is one long-lived native process with an object graph rooted in `OptikaB293Composition` and retained by `OptikaB293ApplicationDelegate`. It is not a collection of stateless REST endpoints and must not reconstruct device/session state for individual UI commands.

For the stage capability, the intended retained object graph is:
- Application composition / microscope session owns the stage subsystem and driver adapter.
- The stage subsystem owns one current `StageWorkingSurface2D`, one current `StagePosition3D`, calibration/reference state, homing and verification policy, active navigation and the navigation-identity sequence.
- The session retains one `SerializedOperationRuntime` for device-session and operation/step/command state, one typed control queue and one wake signal.
- A dedicated owner thread processes feedback and control commands serially. The physical adapter remains alive for the session and submits feedback through the ingress only.
- UI-facing models hold immutable projections/snapshots and submit domain commands. They do not share or mutate the worker-owned C++ state directly.

The new control queue now contains only a revision-bound `SlideNavigationTarget2D`. It does not carry `OperationId`, `StepId`, `CommandId`, or raw `SlideId + local point` fields. The persistent stage runtime allocates operation correlation identities when it accepts the target. The target's stored registration revision prevents a queued click from being reinterpreted after a slide is redrawn.

The functional-core boundary remains explicit: geometry, coordinate conversion, navigation planning, error-budget evaluation and reducers are deterministic calculations over values. The stateful Application objects own their lifetimes, choose when a candidate result is accepted, mutate the current session state on the owner thread, and publish new immutable snapshots.

The current camera composition already follows the long-lived-object approach. Stage hardware integration remains incomplete because the concrete physical-stage `IStageMotionPort` adapter and its device lifecycle are not yet present in this repository. Do not create a fake adapter or a per-request runtime to fill that gap. Implement the concrete adapter and compose it once into the existing application lifetime root.
