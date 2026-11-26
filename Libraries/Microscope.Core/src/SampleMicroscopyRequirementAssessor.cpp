#include "microscope/core/configuration/SampleMicroscopyRequirementAssessor.h"

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
    const SpecimenPreparation& preparation) {
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
                "The specimen coverglass thickness is not recorded, so objective coverglass correction compatibility cannot be assessed.",
                "Specimen");
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
                    "The specimen coverglass thickness differs from the selected objective's design thickness by more than the SampleType tolerance.",
                    "EquipmentComponent",
                    objective.componentId().value());
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

void assessFluorescenceLabels(
    Diagnostics& diagnostics,
    const MicroscopyRequirements& requirements,
    const SpecimenPreparation& preparation,
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
                ? "This SampleType requires fluorescent-label metadata, but the prepared specimen has no fluorescent label."
                : "The prepared specimen has no fluorescent-label metadata. Autofluorescence may still be present, but a task-specific detector-side spectral estimate cannot be derived from a label profile.",
            "Specimen");
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
    const Sample& sample,
    const Specimen& specimen,
    const ResolvedConfiguration& configuration) {
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

    if (sample.sampleTypeId() != sampleType.id() ||
        sample.sampleTypeRevision() != sampleType.revision()) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "sample.microscopy.sample_type_revision_mismatch",
            "The Sample is bound to a different SampleType identity or revision than the supplied SampleType definition.",
            "Sample",
            sample.id().value());
        return SampleMicroscopyAssessment(std::move(diagnostics));
    }

    if (specimen.sampleId() != sample.id() ||
        specimen.sampleRevision() != sample.revision()) {
        appendDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "sample.microscopy.specimen_sample_revision_mismatch",
            "The Specimen was prepared from a different Sample identity or revision than the supplied Sample.",
            "Specimen",
            specimen.id().value());
        return SampleMicroscopyAssessment(std::move(diagnostics));
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
                specimen.preparation());
        }
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
        specimen.preparation(),
        request.contrastMethod());

    return SampleMicroscopyAssessment(std::move(diagnostics));
}

} // namespace microscope::core
