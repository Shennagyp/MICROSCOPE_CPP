#include "microscope/core/configuration/SampleMicroscopyRequirementAssessor.h"
#include "microscope/core/configuration/CameraSuitability.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace microscope::core {
namespace {

void appendDiagnostic(
    Diagnostics& diagnostics,
    DiagnosticSeverity severity,
    std::string code,
    std::string message,
    std::optional<std::string> entityType = std::nullopt,
    std::optional<std::string> entityId = std::nullopt) {
    auto diagnostic = Diagnostic::create(
        severity,
        std::move(code),
        std::move(message),
        DiagnosticContext{
            std::move(entityType),
            std::move(entityId),
            std::nullopt});
    if (diagnostic) {
        diagnostics.push_back(std::move(diagnostic).value());
    }
}

[[nodiscard]] const ObjectiveSpecification* findObjective(
    const MicroscopeEquipmentDefinition& equipment,
    const EquipmentComponentId& componentId) {
    const auto& values = equipment.objectiveSpecifications();
    const auto found = std::find_if(
        values.begin(), values.end(),
        [&componentId](const ObjectiveSpecification& value) {
            return value.componentId() == componentId;
        });
    return found == values.end() ? nullptr : &*found;
}

[[nodiscard]] const CondenserSpecification* findCondenser(
    const MicroscopeEquipmentDefinition& equipment,
    const EquipmentComponentId& componentId) {
    const auto& values = equipment.condenserSpecifications();
    const auto found = std::find_if(
        values.begin(), values.end(),
        [&componentId](const CondenserSpecification& value) {
            return value.componentId() == componentId;
        });
    return found == values.end() ? nullptr : &*found;
}

[[nodiscard]] const CameraSpecification* findCamera(
    const MicroscopeEquipmentDefinition& equipment,
    const EquipmentComponentId& componentId) {
    const auto& values = equipment.cameraSpecifications();
    const auto found = std::find_if(
        values.begin(), values.end(),
        [&componentId](const CameraSpecification& value) {
            return value.componentId() == componentId;
        });
    return found == values.end() ? nullptr : &*found;
}

void assessPreparation(
    Diagnostics& diagnostics,
    const SlidePreparationRequirements& requirements,
    const SlidePreparation& preparation,
    const SlideId& slideId) {
    if (!requirements.allowedMethods().empty() &&
        std::find(
            requirements.allowedMethods().begin(),
            requirements.allowedMethods().end(),
            preparation.method()) == requirements.allowedMethods().end()) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "sample.preparation.method_not_allowed",
            "The actual slide preparation method is not allowed by this SampleType.",
            "Slide",
            slideId.value());
    }

    if (!requirements.allowedFixationStates().empty() &&
        std::find(
            requirements.allowedFixationStates().begin(),
            requirements.allowedFixationStates().end(),
            preparation.fixationState()) ==
            requirements.allowedFixationStates().end()) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "sample.preparation.fixation_state_not_allowed",
            "The actual slide fixation state is not allowed by this SampleType.",
            "Slide",
            slideId.value());
    }

    const auto assessThickness = [&](
        const std::optional<double>& actual,
        const std::optional<double>& minimum,
        const std::optional<double>& maximum,
        const char* unknownCode,
        const char* belowCode,
        const char* aboveCode,
        const char* measurementName) {
        if (!minimum && !maximum) {
            return;
        }
        if (!actual) {
            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Warning,
                unknownCode,
                std::string("The slide ") + measurementName +
                    " is not recorded; the SampleType's preparation tolerance cannot be evaluated.",
                "Slide",
                slideId.value());
            return;
        }
        if (minimum && *actual < *minimum) {
            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Error,
                belowCode,
                std::string("The slide ") + measurementName +
                    " is below the SampleType minimum.",
                "Slide",
                slideId.value());
        }
        if (maximum && *actual > *maximum) {
            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Error,
                aboveCode,
                std::string("The slide ") + measurementName +
                    " exceeds the SampleType maximum.",
                "Slide",
                slideId.value());
        }
    };

    assessThickness(
        preparation.sectionThicknessMicrometers(),
        requirements.minimumSectionThicknessMicrometers(),
        requirements.maximumSectionThicknessMicrometers(),
        "sample.preparation.section_thickness_unknown",
        "sample.preparation.section_thickness_below_minimum",
        "sample.preparation.section_thickness_above_maximum",
        "section thickness");

    assessThickness(
        preparation.coverglassThicknessMicrometers(),
        requirements.minimumCoverglassThicknessMicrometers(),
        requirements.maximumCoverglassThicknessMicrometers(),
        "sample.preparation.coverglass_thickness_unknown",
        "sample.preparation.coverglass_thickness_below_minimum",
        "sample.preparation.coverglass_thickness_above_maximum",
        "coverglass thickness");

    if (requirements.requireMountingMedium() &&
        !preparation.mountingMediumName()) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "sample.preparation.mounting_medium_required",
            "This SampleType requires a recorded mounting medium, but none is present on the Slide preparation record.",
            "Slide",
            slideId.value());
    }

    for (const auto requiredKind : requirements.requiredLabelKinds()) {
        const bool found = std::any_of(
            preparation.labels().begin(),
            preparation.labels().end(),
            [requiredKind](const SpecimenLabel& label) {
                return label.kind() == requiredKind;
            });
        if (!found) {
            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Error,
                "sample.preparation.required_label_missing",
                "The Slide is missing a label class required by its SampleType.",
                "Slide",
                slideId.value());
        }
    }
}

void assessTechnique(
    Diagnostics& diagnostics,
    const MicroscopyRequirements& requirements,
    ContrastMethod selectedMethod) {
    const MicroscopyTechniqueConstraint* required = nullptr;
    bool hasSupportedSet = false;
    bool selectedIsSupported = false;
    bool hasPreferredSet = false;
    bool selectedIsPreferred = false;

    for (const auto& constraint : requirements.techniques()) {
        if (constraint.kind == MicroscopyTechniqueConstraintKind::Required) {
            required = &constraint;
        } else if (constraint.kind == MicroscopyTechniqueConstraintKind::Supported) {
            hasSupportedSet = true;
            selectedIsSupported |= constraint.method == selectedMethod;
        } else if (constraint.kind == MicroscopyTechniqueConstraintKind::Preferred) {
            hasPreferredSet = true;
            selectedIsPreferred |= constraint.method == selectedMethod;
        } else if (constraint.kind == MicroscopyTechniqueConstraintKind::Prohibited &&
                   constraint.method == selectedMethod) {
            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Error,
                "sample.microscopy.technique_prohibited",
                "The selected contrast method is prohibited by this SampleType's microscopy requirements.",
                "SampleType");
        }
    }

    if (required != nullptr && required->method != selectedMethod) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "sample.microscopy.required_technique_mismatch",
            "The selected contrast method does not satisfy the required microscopy technique for this SampleType.",
            "SampleType");
    }

    if (hasSupportedSet &&
        !selectedIsSupported &&
        (required == nullptr || required->method != selectedMethod)) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "sample.microscopy.technique_not_supported",
            "The selected contrast method is not in the SampleType's supported technique set.",
            "SampleType");
    }

    if (hasPreferredSet && !selectedIsPreferred) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Warning,
            "sample.microscopy.preferred_technique_not_selected",
            "The selected contrast method is supported by the microscope profile but is not one of the preferred methods declared by this SampleType.",
            "SampleType");
    }
}

void assessObjective(
    Diagnostics& diagnostics,
    const ObjectiveMicroscopyRequirements& requirements,
    const ObjectiveSpecification& objective,
    const SlidePreparation& preparation) {
    const double magnification = objective.nominalMagnification();
    const double numericalAperture = objective.numericalAperture();

    if (requirements.minimumMagnification() &&
        magnification < *requirements.minimumMagnification()) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "sample.microscopy.objective_magnification_below_minimum",
            "The selected objective magnification is below the SampleType minimum.",
            "EquipmentComponent",
            objective.componentId().value());
    }
    if (requirements.maximumMagnification() &&
        magnification > *requirements.maximumMagnification()) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "sample.microscopy.objective_magnification_above_maximum",
            "The selected objective magnification exceeds the SampleType maximum.",
            "EquipmentComponent",
            objective.componentId().value());
    }
    if (requirements.minimumNumericalAperture() &&
        numericalAperture < *requirements.minimumNumericalAperture()) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "sample.microscopy.objective_na_below_minimum",
            "The selected objective numerical aperture is below the SampleType minimum.",
            "EquipmentComponent",
            objective.componentId().value());
    }

    if (!requirements.allowedImmersionMedia().empty()) {
        const auto& allowed = requirements.allowedImmersionMedia();
        if (std::find(
                allowed.begin(), allowed.end(), objective.immersionMedium()) ==
            allowed.end()) {
            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Error,
                "sample.microscopy.objective_immersion_incompatible",
                "The selected objective immersion medium is not allowed by the SampleType requirements.",
                "EquipmentComponent",
                objective.componentId().value());
        }
    }

    if (requirements.maximumCoverglassDesignMismatchMicrometers()) {
        const auto& actualCoverglass = preparation.coverglassThicknessMicrometers();
        const auto& designCoverglass = objective.designCoverglassThickness();
        if (!actualCoverglass) {
            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Warning,
                "sample.microscopy.coverglass_thickness_unknown",
                "The slide coverglass thickness is not recorded, so objective coverglass correction compatibility cannot be assessed.",
                "Slide");
        } else if (!designCoverglass) {
            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Warning,
                "sample.microscopy.objective_coverglass_design_unknown",
                "The selected objective has no recorded design coverglass thickness, so coverglass compatibility cannot be assessed.",
                "EquipmentComponent",
                objective.componentId().value());
        } else {
            const double mismatch = std::abs(
                *actualCoverglass - designCoverglass->value());
            if (mismatch >
                *requirements.maximumCoverglassDesignMismatchMicrometers()) {
                appendDiagnostic(
                    diagnostics,
                    DiagnosticSeverity::Error,
                    "sample.microscopy.coverglass_design_mismatch",
                    "The slide coverglass thickness differs from the selected objective's design thickness by more than the SampleType tolerance.",
                    "EquipmentComponent",
                    objective.componentId().value());
            }
        }
    }
}

void assessCondenser(
    Diagnostics& diagnostics,
    const CondenserMicroscopyRequirements& requirements,
    const CondenserSpecification& condenser) {
    if (requirements.minimumApertureNA() &&
        condenser.maximumApertureNA() < *requirements.minimumApertureNA()) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "sample.microscopy.condenser_aperture_below_minimum",
            "The installed condenser cannot reach the minimum aperture NA required by this SampleType.",
            "EquipmentComponent",
            condenser.componentId().value());
    }
    if (requirements.maximumApertureNA() &&
        condenser.minimumApertureNA() > *requirements.maximumApertureNA()) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "sample.microscopy.condenser_aperture_above_maximum",
            "The installed condenser's minimum aperture NA exceeds the maximum permitted by this SampleType.",
            "EquipmentComponent",
            condenser.componentId().value());
    }
    if (!requirements.allowedDesigns().empty() &&
        std::find(
            requirements.allowedDesigns().begin(),
            requirements.allowedDesigns().end(),
            condenser.design()) == requirements.allowedDesigns().end()) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "sample.microscopy.condenser_design_incompatible",
            "The selected condenser design is not allowed by this SampleType.",
            "EquipmentComponent",
            condenser.componentId().value());
    }
    if (!requirements.allowedImmersionMedia().empty() &&
        std::find(
            requirements.allowedImmersionMedia().begin(),
            requirements.allowedImmersionMedia().end(),
            condenser.immersionMedium()) ==
            requirements.allowedImmersionMedia().end()) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "sample.microscopy.condenser_immersion_incompatible",
            "The selected condenser immersion medium is not allowed by this SampleType.",
            "EquipmentComponent",
            condenser.componentId().value());
    }
}

[[nodiscard]] bool sourceBandHasPositivePower(
    const SpectralPowerDistribution& spectrum,
    const SpectralBand& band,
    bool& completeCoverage);

void assessIllumination(
    Diagnostics& diagnostics,
    const MicroscopeEquipmentDefinition& equipment,
    const IlluminationMicroscopyRequirements& requirements,
    const ConfigurationRequest& request,
    const OperationMode* operationMode) {
    const auto& binding = request.lightPathBinding();
    if (!binding) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "sample.microscopy.light_path_mode_binding_missing",
            "This SampleType has specimen-plane illumination constraints, but the resolved configuration is not bound to a selected LightPath and OperationMode revision.");
        return;
    }

    if (operationMode == nullptr) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "sample.microscopy.operation_mode_missing",
            "An OperationMode is required to evaluate the calibrated specimen-plane irradiance target.");
        return;
    }

    if (operationMode->microscopeId() != equipment.microscopeId() ||
        operationMode->id() != binding->operationModeId ||
        operationMode->revision() != binding->operationModeRevision ||
        binding->equipmentDefinitionRevision != equipment.revision()) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "sample.microscopy.operation_mode_revision_mismatch",
            "The supplied OperationMode does not match the microscope, equipment revision and OperationMode revision captured by the resolved camera configuration.");
        return;
    }

    const auto* setting =
        operationMode->findLightPathSetting(binding->lightPathId);
    if (setting == nullptr ||
        setting->equipmentDefinitionRevision() != equipment.revision()) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "sample.microscopy.light_path_mode_setting_missing",
            "The selected OperationMode has no valid calibrated setting for the selected LightPath and equipment revision.",
            "LightPath",
            binding->lightPathId.value());
        return;
    }

    if (setting->illuminationComponentId() != request.illuminationComponentId() ||
        setting->illuminationChannelId() != request.illuminationChannelId()) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "sample.microscopy.light_path_illumination_selection_mismatch",
            "The resolved configuration illumination source/channel differs from the source/channel calibrated by the selected OperationMode.",
            "LightPath",
            binding->lightPathId.value());
        return;
    }

    const double target =
        setting->targetIrradianceMicrowattsPerSquareMillimeter();
    if (requirements.minimumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter() &&
        target <
            *requirements.minimumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter()) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "sample.microscopy.specimen_irradiance_below_minimum",
            "The selected OperationMode's calibrated specimen-plane irradiance target is below the SampleType minimum.",
            "LightPath",
            binding->lightPathId.value());
    }
    if (requirements.maximumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter() &&
        target >
            *requirements.maximumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter()) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "sample.microscopy.specimen_irradiance_above_maximum",
            "The selected OperationMode's calibrated specimen-plane irradiance target exceeds the SampleType maximum.",
            "LightPath",
            binding->lightPathId.value());
    }

    const auto& illuminationSpecifications = equipment.illuminationSpecifications();
    const auto source = std::find_if(
        illuminationSpecifications.begin(),
        illuminationSpecifications.end(),
        [&request](const IlluminationSpecification& value) {
            return value.componentId() == request.illuminationComponentId();
        });
    if (source == illuminationSpecifications.end()) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "sample.microscopy.illumination_specification_missing",
            "The selected illumination source has no installed IlluminationSpecification.",
            "EquipmentComponent",
            request.illuminationComponentId().value());
        return;
    }

    const auto& channels = source->channels();
    const auto channel = std::find_if(
        channels.begin(), channels.end(),
        [&request](const IlluminationChannel& value) {
            return value.id() == request.illuminationChannelId();
        });
    if (channel == channels.end()) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "sample.microscopy.illumination_channel_missing",
            "The selected illumination source has no matching IlluminationChannel.",
            "IlluminationChannel",
            request.illuminationChannelId().value());
        return;
    }

    if (requirements.requireContinuouslyAdjustableSource()) {
        if (channel->controlKind() == IlluminationControlKind::Fixed) {
            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Error,
                "sample.microscopy.continuous_illumination_control_required",
                "The SampleType requires continuously adjustable illumination, but the selected source channel is fixed.",
                "IlluminationChannel",
                request.illuminationChannelId().value());
        } else if (channel->controlKind() != IlluminationControlKind::Continuous) {
            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Warning,
                "sample.microscopy.illumination_control_capability_unknown",
                "The selected source channel does not declare continuous control; its adjustable behavior is unknown.",
                "IlluminationChannel",
                request.illuminationChannelId().value());
        }
    }

    if (requirements.requiredSpectralBand()) {
        if (!channel->spectralPowerDistribution()) {
            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Warning,
                "sample.microscopy.illumination_spectrum_unknown",
                "The selected source channel has no spectral power distribution; its availability in the SampleType spectral band is unknown.",
                "IlluminationChannel",
                request.illuminationChannelId().value());
        } else {
            bool coverage = false;
            const bool hasPower = sourceBandHasPositivePower(
                *channel->spectralPowerDistribution(),
                *requirements.requiredSpectralBand(),
                coverage);
            if (!coverage) {
                appendDiagnostic(
                    diagnostics,
                    DiagnosticSeverity::Warning,
                    "sample.microscopy.illumination_spectrum_coverage_incomplete",
                    "The selected source spectrum does not cover the complete required spectral band. No extrapolation was performed.",
                    "IlluminationChannel",
                    request.illuminationChannelId().value());
            } else if (!hasPower) {
                appendDiagnostic(
                    diagnostics,
                    DiagnosticSeverity::Error,
                    "sample.microscopy.illumination_spectrum_missing_band",
                    "The selected source spectrum has no declared power in the SampleType's required spectral band.",
                    "IlluminationChannel",
                    request.illuminationChannelId().value());
            }
        }
    }
}

void assessCameraPerformance(
    Diagnostics& diagnostics,
    const CameraPerformanceRequirements& requirements,
    const CameraSpecification& camera) {
    if (!camera.sensorCharacteristics()) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Warning,
            "sample.microscopy.camera_sensor_profile_unknown",
            "The selected camera has no sensor-characteristics profile; camera performance requirements cannot be fully evaluated.",
            "EquipmentComponent",
            camera.componentId().value());
        return;
    }

    const auto& sensor = *camera.sensorCharacteristics();

    const auto assessMaximum = [&](const std::optional<double>& actual,
                                   const std::optional<double>& maximum,
                                   const char* unknownCode,
                                   const char* exceededCode,
                                   const char* metric,
                                   const char* unit) {
        if (!maximum) {
            return;
        }
        if (!actual) {
            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Warning,
                unknownCode,
                std::string("The selected camera's ") + metric +
                    " is unknown; the SampleType threshold cannot be evaluated.",
                "EquipmentComponent",
                camera.componentId().value());
        } else if (*actual > *maximum) {
            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Warning,
                exceededCode,
                std::string("The selected camera's ") + metric +
                    " exceeds the SampleType threshold (" + unit +
                    "). The camera remains selected; consider a different camera or acquisition strategy.",
                "EquipmentComponent",
                camera.componentId().value());
        }
    };

    const auto assessMinimum = [&](const std::optional<double>& actual,
                                   const std::optional<double>& minimum,
                                   const char* unknownCode,
                                   const char* belowCode,
                                   const char* metric) {
        if (!minimum) {
            return;
        }
        if (!actual) {
            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Warning,
                unknownCode,
                std::string("The selected camera's ") + metric +
                    " is unknown; the SampleType threshold cannot be evaluated.",
                "EquipmentComponent",
                camera.componentId().value());
        } else if (*actual < *minimum) {
            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Warning,
                belowCode,
                std::string("The selected camera's ") + metric +
                    " is below the SampleType threshold. The camera remains selected; consider a different camera or acquisition strategy.",
                "EquipmentComponent",
                camera.componentId().value());
        }
    };

    assessMinimum(
        sensor.maximumFrameRateFramesPerSecond,
        requirements.minimumFrameRateFramesPerSecond(),
        "sample.microscopy.camera_frame_rate_unknown",
        "sample.microscopy.camera_frame_rate_below_minimum",
        "maximum frame rate");

    assessMaximum(
        sensor.readNoiseElectronsRms,
        requirements.maximumReadNoiseElectronsRms(),
        "sample.microscopy.camera_read_noise_unknown",
        "sample.microscopy.camera_read_noise_above_maximum",
        "read noise",
        "electrons RMS");

    assessMaximum(
        sensor.darkCurrentElectronsPerPixelPerSecond,
        requirements.maximumDarkCurrentElectronsPerPixelPerSecond(),
        "sample.microscopy.camera_dark_current_unknown",
        "sample.microscopy.camera_dark_current_above_maximum",
        "dark current",
        "electrons/pixel/second");

    assessMinimum(
        sensor.fullWellCapacityElectrons,
        requirements.minimumFullWellCapacityElectrons(),
        "sample.microscopy.camera_full_well_unknown",
        "sample.microscopy.camera_full_well_below_minimum",
        "full-well capacity");
}

[[nodiscard]] const OpticalPath* findOpticalPath(
    const MicroscopeEquipmentDefinition& equipment,
    const OpticalPathId& pathId) {
    const auto& paths = equipment.opticalPaths();
    const auto found = std::find_if(
        paths.begin(), paths.end(),
        [&pathId](const OpticalPath& path) {
            return path.id() == pathId;
        });
    return found == paths.end() ? nullptr : &*found;
}

[[nodiscard]] bool componentOccursOnSelectedRoute(
    const MicroscopeEquipmentDefinition& equipment,
    const ConfigurationRequest& request,
    OpticalPathRole role,
    const EquipmentComponentId& componentId) {
    for (const auto& pathId : request.opticalPathIds()) {
        const auto* path = findOpticalPath(equipment, pathId);
        if (path == nullptr || path->role() != role) {
            continue;
        }
        const bool found = std::any_of(
            path->steps().begin(), path->steps().end(),
            [&componentId](const OpticalPathStep& step) {
                return step.componentId() == componentId;
            });
        if (found) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] const OpticalFilterSpecification* findSelectedFilter(
    const MicroscopeEquipmentDefinition& equipment,
    const ConfigurationRequest& request,
    const OpticalCompatibilityId& compatibilityId,
    OpticalFilterRole role,
    Diagnostics& diagnostics,
    const SpecimenLabel& label) {
    const OpticalFilterSpecification* selected = nullptr;
    for (const auto& filter : equipment.filterSpecifications()) {
        if (filter.role() != role ||
            filter.compatibilityId() !=
                std::optional<OpticalCompatibilityId>{compatibilityId} ||
            std::find(
                request.selectedAdditionalOpticalComponentIds().begin(),
                request.selectedAdditionalOpticalComponentIds().end(),
                filter.componentId()) ==
                request.selectedAdditionalOpticalComponentIds().end()) {
            continue;
        }

        if (selected != nullptr) {
            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Error,
                "sample.microscopy.fluorescence_filter_set_ambiguous",
                "More than one selected filter satisfies the same role and compatibility identity for this fluorescent label.",
                "SpecimenLabel",
                label.name());
            return nullptr;
        }
        selected = &filter;
    }

    if (selected == nullptr) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "sample.microscopy.fluorescence_filter_missing",
            "The selected configuration does not include an installed filter with the required fluorescence filter-set role and compatibility identity.",
            "SpecimenLabel",
            label.name());
    }
    return selected;
}

void assessFluorescenceFilterSet(
    Diagnostics& diagnostics,
    const MicroscopeEquipmentDefinition& equipment,
    const ConfigurationRequest& request,
    const SpecimenLabel& label) {
    if (!label.filterSetCompatibilityId()) {
        return;
    }

    const auto* excitationFilter = findSelectedFilter(
        equipment,
        request,
        *label.filterSetCompatibilityId(),
        OpticalFilterRole::Excitation,
        diagnostics,
        label);
    const auto* emissionFilter = findSelectedFilter(
        equipment,
        request,
        *label.filterSetCompatibilityId(),
        OpticalFilterRole::Emission,
        diagnostics,
        label);
    const auto* dichroic = findSelectedFilter(
        equipment,
        request,
        *label.filterSetCompatibilityId(),
        OpticalFilterRole::Dichroic,
        diagnostics,
        label);

    if (excitationFilter != nullptr &&
        !componentOccursOnSelectedRoute(
            equipment,
            request,
            OpticalPathRole::Illumination,
            excitationFilter->componentId())) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "sample.microscopy.excitation_filter_wrong_route",
            "The matched excitation filter is not on a selected illumination route.",
            "EquipmentComponent",
            excitationFilter->componentId().value());
    }

    if (emissionFilter != nullptr &&
        !componentOccursOnSelectedRoute(
            equipment,
            request,
            OpticalPathRole::Imaging,
            emissionFilter->componentId())) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "sample.microscopy.emission_filter_wrong_route",
            "The matched emission filter is not on a selected imaging route.",
            "EquipmentComponent",
            emissionFilter->componentId().value());
    }

    if (dichroic != nullptr) {
        const bool onIllumination = componentOccursOnSelectedRoute(
            equipment, request, OpticalPathRole::Illumination,
            dichroic->componentId());
        const bool onImaging = componentOccursOnSelectedRoute(
            equipment, request, OpticalPathRole::Imaging,
            dichroic->componentId());
        if (!onIllumination || !onImaging) {
            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Error,
                "sample.microscopy.dichroic_not_shared_by_excitation_and_detection",
                "The matched dichroic must participate in both selected illumination and imaging routes.",
                "EquipmentComponent",
                dichroic->componentId().value());
        }
    }

    const auto assessFilterBand = [&](
        const OpticalFilterSpecification* filter,
        const SpectralBand& band,
        bool reflection,
        const char* coverageCode,
        const char* noResponseCode,
        const char* metricName) {
        if (filter == nullptr) {
            return;
        }
        const auto response =
            filterBandHasPositiveResponse(*filter, band, reflection);
        if (!response) {
            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Warning,
                coverageCode,
                std::string("The selected ") + metricName +
                    " curve does not cover the complete declared band; compatibility remains unknown rather than extrapolated.",
                "EquipmentComponent",
                filter->componentId().value());
        } else if (!*response) {
            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Error,
                noResponseCode,
                std::string("The selected ") + metricName +
                    " curve covers the declared band but has no positive spectral response in it.",
                "EquipmentComponent",
                filter->componentId().value());
        }
    };

    if (label.excitationBand()) {
        assessFilterBand(
            excitationFilter,
            *label.excitationBand(),
            false,
            "sample.microscopy.excitation_filter_spectral_coverage_incomplete",
            "sample.microscopy.excitation_filter_has_no_band_transmission",
            "excitation-filter transmission");
        assessFilterBand(
            dichroic,
            *label.excitationBand(),
            true,
            "sample.microscopy.dichroic_excitation_coverage_incomplete",
            "sample.microscopy.dichroic_has_no_excitation_reflection",
            "dichroic reflection");
    }

    if (label.emissionBand()) {
        assessFilterBand(
            emissionFilter,
            *label.emissionBand(),
            false,
            "sample.microscopy.emission_filter_spectral_coverage_incomplete",
            "sample.microscopy.emission_filter_has_no_band_transmission",
            "emission-filter transmission");
        assessFilterBand(
            dichroic,
            *label.emissionBand(),
            false,
            "sample.microscopy.dichroic_emission_coverage_incomplete",
            "sample.microscopy.dichroic_has_no_emission_transmission",
            "dichroic transmission");
    }
}

[[nodiscard]] const OpticalFilterSpecification* findSelectedFilterForLabel(
    const MicroscopeEquipmentDefinition& equipment,
    const ConfigurationRequest& request,
    const SpecimenLabel& label,
    OpticalFilterRole role) {
    const OpticalFilterSpecification* selected = nullptr;
    for (const auto& filter : equipment.filterSpecifications()) {
        if (filter.role() != role ||
            std::find(
                request.selectedAdditionalOpticalComponentIds().begin(),
                request.selectedAdditionalOpticalComponentIds().end(),
                filter.componentId()) ==
                request.selectedAdditionalOpticalComponentIds().end()) {
            continue;
        }
        if (label.filterSetCompatibilityId() &&
            filter.compatibilityId() != label.filterSetCompatibilityId()) {
            continue;
        }
        if (selected != nullptr) {
            return nullptr;
        }
        selected = &filter;
    }
    return selected;
}

[[nodiscard]] const IlluminationChannel* findSelectedIlluminationChannel(
    const MicroscopeEquipmentDefinition& equipment,
    const ConfigurationRequest& request) {
    const auto& specifications = equipment.illuminationSpecifications();
    const auto specification = std::find_if(
        specifications.begin(), specifications.end(),
        [&request](const IlluminationSpecification& value) {
            return value.componentId() == request.illuminationComponentId();
        });
    if (specification == specifications.end()) {
        return nullptr;
    }

    const auto& channels = specification->channels();
    const auto channel = std::find_if(
        channels.begin(), channels.end(),
        [&request](const IlluminationChannel& value) {
            return value.id() == request.illuminationChannelId();
        });
    return channel == channels.end() ? nullptr : &*channel;
}

[[nodiscard]] std::optional<bool> filterBandHasPositiveResponse(
    const OpticalFilterSpecification& filter,
    const SpectralBand& band,
    bool reflection);

[[nodiscard]] std::optional<double> spectralPowerAt(
    const SpectralPowerDistribution& spectrum,
    double wavelengthNanometers) {
    const auto& samples = spectrum.samples();
    if (samples.empty() ||
        wavelengthNanometers < samples.front().wavelengthNanometers ||
        wavelengthNanometers > samples.back().wavelengthNanometers) {
        return std::nullopt;
    }

    const auto upper = std::lower_bound(
        samples.begin(), samples.end(), wavelengthNanometers,
        [](const SpectralPowerSample& sample, double wavelength) {
            return sample.wavelengthNanometers < wavelength;
        });
    if (upper == samples.end()) {
        return samples.back().relativePower;
    }
    if (upper->wavelengthNanometers == wavelengthNanometers ||
        upper == samples.begin()) {
        return upper->relativePower;
    }

    const auto& right = *upper;
    const auto& left = *(upper - 1);
    const double fraction =
        (wavelengthNanometers - left.wavelengthNanometers) /
        (right.wavelengthNanometers - left.wavelengthNanometers);
    return left.relativePower +
           fraction * (right.relativePower - left.relativePower);
}

// nullopt means the measured curve does not characterize the whole band;
// false means it covers the band but declares zero response; true means some
// positive response exists. Unknown coverage must never be confused with zero.
[[nodiscard]] std::optional<bool> filterBandHasPositiveResponse(
    const OpticalFilterSpecification& filter,
    const SpectralBand& band,
    bool reflection) {
    const auto evaluate = [&](double wavelength) {
        return reflection
            ? filter.reflectionAt(wavelength)
            : filter.transmissionAt(wavelength);
    };
    const double minimum = band.minimumWavelengthNanometers();
    const double maximum = band.maximumWavelengthNanometers();
    const auto lower = evaluate(minimum);
    const auto upper = evaluate(maximum);
    if (!lower || !upper) {
        return std::nullopt;
    }
    if (*lower > 0.0 || *upper > 0.0) {
        return true;
    }

    for (const auto& sample : filter.spectralResponse()) {
        const double wavelength = sample.wavelength.value();
        if (wavelength < minimum || wavelength > maximum) {
            continue;
        }
        const auto response = evaluate(wavelength);
        if (response && *response > 0.0) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool sourceBandHasPositivePower(
    const SpectralPowerDistribution& spectrum,
    const SpectralBand& band,
    bool& completeCoverage) {
    const double minimum = band.minimumWavelengthNanometers();
    const double maximum = band.maximumWavelengthNanometers();
    const auto lower = spectralPowerAt(spectrum, minimum);
    const auto upper = spectralPowerAt(spectrum, maximum);
    completeCoverage = lower.has_value() && upper.has_value();
    if (!completeCoverage) {
        return false;
    }
    if (*lower > 0.0 || *upper > 0.0) {
        return true;
    }

    for (const auto& sample : spectrum.samples()) {
        if (sample.wavelengthNanometers >= minimum &&
            sample.wavelengthNanometers <= maximum &&
            sample.relativePower > 0.0) {
            return true;
        }
    }
    return false;
}

void assessExcitationAvailability(
    Diagnostics& diagnostics,
    const MicroscopeEquipmentDefinition& equipment,
    const ConfigurationRequest& request,
    const SpecimenLabel& label) {
    if (!label.excitationBand()) {
        return;
    }

    const auto* channel = findSelectedIlluminationChannel(equipment, request);
    if (channel == nullptr || !channel->spectralPowerDistribution()) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Warning,
            "sample.microscopy.excitation_source_spectrum_unknown",
            "The selected illumination channel has no spectral power distribution; excitation availability in the fluorophore band is unknown.",
            "IlluminationChannel",
            request.illuminationChannelId().value());
    } else {
        bool completeCoverage = false;
        const bool positivePower = sourceBandHasPositivePower(
            *channel->spectralPowerDistribution(),
            *label.excitationBand(),
            completeCoverage);
        if (!completeCoverage) {
            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Warning,
                "sample.microscopy.excitation_source_spectrum_coverage_incomplete",
                "The selected illumination source spectrum does not cover the complete excitation band; excitation availability cannot be established without extrapolation.",
                "IlluminationChannel",
                request.illuminationChannelId().value());
        } else if (!positivePower) {
            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Error,
                "sample.microscopy.excitation_source_has_no_band_power",
                "The selected illumination channel has no declared spectral power in the fluorophore excitation band.",
                "IlluminationChannel",
                request.illuminationChannelId().value());
        }
    }

    if (!label.filterSetCompatibilityId()) {
        return;
    }

    const auto* excitationFilter = findSelectedFilterForLabel(
        equipment, request, label, OpticalFilterRole::Excitation);
    const auto* dichroic = findSelectedFilterForLabel(
        equipment, request, label, OpticalFilterRole::Dichroic);

    const auto assessResponse = [&](
        const OpticalFilterSpecification* filter,
        bool reflection,
        const char* coverageCode,
        const char* noResponseCode,
        const char* metric) {
        if (filter == nullptr) {
            return;
        }
        const auto response = filterBandHasPositiveResponse(
            *filter, *label.excitationBand(), reflection);
        if (!response) {
            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Warning,
                coverageCode,
                std::string("The selected ") + metric +
                    " curve does not cover the complete excitation band; no extrapolation was used.",
                "EquipmentComponent",
                filter->componentId().value());
        } else if (!*response) {
            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Error,
                noResponseCode,
                std::string("The selected ") + metric +
                    " curve has no positive response in the excitation band.",
                "EquipmentComponent",
                filter->componentId().value());
        }
    };

    assessResponse(
        excitationFilter,
        false,
        "sample.microscopy.excitation_filter_spectral_coverage_incomplete",
        "sample.microscopy.excitation_filter_has_no_band_transmission",
        "excitation-filter transmission");
    assessResponse(
        dichroic,
        true,
        "sample.microscopy.dichroic_excitation_coverage_incomplete",
        "sample.microscopy.dichroic_has_no_excitation_reflection",
        "dichroic reflection");
}

void assessFluorescenceCameraSuitability(
    Diagnostics& diagnostics,
    const MicroscopeEquipmentDefinition& equipment,
    const ConfigurationRequest& request,
    const SlidePreparation& preparation,
    const std::optional<CameraPerformanceRequirements>& requirements) {
    if (!requirements) {
        return;
    }
    const auto* camera = findCamera(equipment, request.cameraComponentId());
    if (camera == nullptr) {
        return;
    }

    for (const auto& label : preparation.labels()) {
        if (label.kind() != SpecimenLabelKind::Fluorescent ||
            !label.emissionBand()) {
            continue;
        }

        std::optional<OpticalFilterSpecification> dichroic;
        std::optional<OpticalFilterSpecification> emissionFilter;
        if (label.filterSetCompatibilityId()) {
            if (const auto* selected = findSelectedFilterForLabel(
                    equipment, request, label, OpticalFilterRole::Dichroic)) {
                dichroic = *selected;
            }
            if (const auto* selected = findSelectedFilterForLabel(
                    equipment, request, label, OpticalFilterRole::Emission)) {
                emissionFilter = *selected;
            }
        }

        FluorescenceCameraRequirements cameraRequirements{
            *label.emissionBand(),
            requirements->minimumMeanQuantumEfficiency(),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            requirements->preferMonochrome(),
            requirements->preferGlobalShutter(),
            label.emissionSpectrum(),
            std::move(dichroic),
            std::move(emissionFilter)};

        auto report = CameraSuitabilityAssessor::assessFluorescence(
            *camera, cameraRequirements);
        diagnostics.insert(
            diagnostics.end(),
            report.warnings.begin(),
            report.warnings.end());
    }
}

void assessFluorescenceLabels(
    Diagnostics& diagnostics,
    const MicroscopyRequirements& requirements,
    const SlidePreparation& preparation,
    ContrastMethod selectedMethod) {
    if (selectedMethod != ContrastMethod::Fluorescence) {
        return;
    }

    const auto& labels = preparation.labels();
    const auto fluorescentCount = static_cast<std::size_t>(std::count_if(
        labels.begin(), labels.end(),
        [](const SpecimenLabel& label) {
            return label.kind() == SpecimenLabelKind::Fluorescent;
        }));

    if (fluorescentCount == 0) {
        appendDiagnostic(
            diagnostics,
            requirements.requireFluorescentLabelMetadata()
                ? DiagnosticSeverity::Error
                : DiagnosticSeverity::Warning,
            "sample.microscopy.fluorescent_label_missing",
            requirements.requireFluorescentLabelMetadata()
                ? "This SampleType requires fluorescent-label metadata, but the prepared slide has no fluorescent label."
                : "The prepared slide has no fluorescent-label metadata. Autofluorescence may still be present, but a task-specific detector-side spectral estimate cannot be derived from a label profile.",
            "Slide");
        return;
    }

    for (const auto& label : labels) {
        if (label.kind() != SpecimenLabelKind::Fluorescent) {
            continue;
        }

        const bool hasExcitation = label.excitationBand().has_value();
        const bool hasEmission = label.emissionBand().has_value();
        if (!hasExcitation || !hasEmission) {
            appendDiagnostic(
                diagnostics,
                requirements.requireFluorescentLabelMetadata()
                    ? DiagnosticSeverity::Error
                    : DiagnosticSeverity::Warning,
                "sample.microscopy.fluorescent_label_spectral_bands_incomplete",
                "A fluorescent specimen label is missing excitation or emission band metadata.",
                "SpecimenLabel",
                label.name());
        }
        if (hasEmission && !label.emissionSpectrum()) {
            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Warning,
                "sample.microscopy.fluorescent_label_emission_spectrum_unknown",
                "The fluorescent label has an emission band but no measured or sourced emission spectrum. Full emission-weighted camera QE evaluation is unavailable.",
                "SpecimenLabel",
                label.name());
        }
    }
}

} // namespace

SampleMicroscopyAssessment::SampleMicroscopyAssessment(
    Diagnostics diagnostics)
    : diagnostics_(std::move(diagnostics)) {}

bool SampleMicroscopyAssessment::isCompatible() const noexcept {
    return std::none_of(
        diagnostics_.begin(),
        diagnostics_.end(),
        [](const Diagnostic& diagnostic) {
            return diagnostic.severity() == DiagnosticSeverity::Error;
        });
}

const Diagnostics& SampleMicroscopyAssessment::diagnostics() const noexcept {
    return diagnostics_;
}

bool SampleMicroscopyAssessment::hasWarnings() const noexcept {
    return std::any_of(
        diagnostics_.begin(),
        diagnostics_.end(),
        [](const Diagnostic& diagnostic) {
            return diagnostic.severity() == DiagnosticSeverity::Warning;
        });
}

SampleMicroscopyAssessment SampleMicroscopyRequirementAssessor::assess(
    const MicroscopeEquipmentDefinition& equipment,
    const SampleType& sampleType,
    const Slide& slide,
    const ResolvedConfiguration& configuration,
    const OperationMode* operationMode) {
    Diagnostics diagnostics;

    if (configuration.microscopeId() != equipment.microscopeId()) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "sample.microscopy.microscope_mismatch",
            "The resolved microscope configuration belongs to a different microscope than the supplied equipment definition.",
            "Microscope",
            configuration.microscopeId().value());
        return SampleMicroscopyAssessment(std::move(diagnostics));
    }


    if (slide.sampleTypeId() != sampleType.id() ||
        slide.sampleTypeRevision() != sampleType.revision()) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "sample.microscopy.slide_sample_type_revision_mismatch",
            "The Slide pins a different SampleType identity or revision than the supplied SampleType definition.",
            "Slide",
            slide.id().value());
        return SampleMicroscopyAssessment(std::move(diagnostics));
    }

    if (sampleType.preparationRequirements()) {
        assessPreparation(
            diagnostics,
            *sampleType.preparationRequirements(),
            slide.preparation(),
            slide.id());
    }

    const auto& request = configuration.request();
    const auto& requirements = sampleType.microscopyRequirements();

    assessTechnique(
        diagnostics, requirements, request.contrastMethod());

    if (requirements.objective()) {
        const auto* objective = findObjective(
            equipment, request.objectiveComponentId());
        if (objective == nullptr) {
            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Error,
                "sample.microscopy.objective_specification_missing",
                "The selected objective has no installed ObjectiveSpecification.",
                "EquipmentComponent",
                request.objectiveComponentId().value());
        } else {
            assessObjective(
                diagnostics,
                *requirements.objective(),
                *objective,
                slide.preparation());
        }
    }

    if (requirements.condenser()) {
        const auto* condenser = findCondenser(
            equipment, request.condenserComponentId());
        if (condenser == nullptr) {
            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Error,
                "sample.microscopy.condenser_specification_missing",
                "The selected condenser has no installed CondenserSpecification.",
                "EquipmentComponent",
                request.condenserComponentId().value());
        } else {
            assessCondenser(
                diagnostics, *requirements.condenser(), *condenser);
        }
    }

    if (requirements.illumination()) {
        assessIllumination(
            diagnostics,
            equipment,
            *requirements.illumination(),
            request,
            operationMode);
    }

    if (requirements.camera()) {
        const auto* camera = findCamera(
            equipment, request.cameraComponentId());
        if (camera == nullptr) {
            appendDiagnostic(
                diagnostics,
                DiagnosticSeverity::Error,
                "sample.microscopy.camera_specification_missing",
                "The selected camera has no installed CameraSpecification.",
                "EquipmentComponent",
                request.cameraComponentId().value());
        } else {
            assessCameraPerformance(
                diagnostics, *requirements.camera(), *camera);
        }
    }

    assessFluorescenceLabels(
        diagnostics,
        requirements,
        slide.preparation(),
        request.contrastMethod());

    if (request.contrastMethod() == ContrastMethod::Fluorescence) {
        for (const auto& label : slide.preparation().labels()) {
            if (label.kind() != SpecimenLabelKind::Fluorescent) {
                continue;
            }
            assessExcitationAvailability(
                diagnostics, equipment, request, label);
            if (label.filterSetCompatibilityId()) {
                assessFluorescenceFilterSet(
                    diagnostics, equipment, request, label);
            }
        }
    }

    if (request.contrastMethod() == ContrastMethod::Fluorescence) {
        assessFluorescenceCameraSuitability(
            diagnostics,
            equipment,
            request,
            slide.preparation(),
            requirements.camera());
    }

    return SampleMicroscopyAssessment(std::move(diagnostics));
}

} // namespace microscope::core
