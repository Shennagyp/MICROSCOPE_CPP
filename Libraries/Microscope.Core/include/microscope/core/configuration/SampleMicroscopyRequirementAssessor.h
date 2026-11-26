#pragma once

#include "microscope/core/configuration/Configuration.h"
#include "microscope/core/configuration/OperationMode.h"
#include "microscope/core/diagnostics/Diagnostic.h"
#include "microscope/core/equipment/OpticalTopology.h"
#include "microscope/core/samples/SampleModels.h"

namespace microscope::core {

class SampleMicroscopyAssessment final {
public:
    [[nodiscard]] bool isCompatible() const noexcept;
    [[nodiscard]] const Diagnostics& diagnostics() const noexcept;
    [[nodiscard]] bool hasWarnings() const noexcept;

private:
    friend class SampleMicroscopyRequirementAssessor;
    explicit SampleMicroscopyAssessment(Diagnostics diagnostics);

    Diagnostics diagnostics_;
};

class SampleMicroscopyRequirementAssessor final {
public:
    // Evaluates one prepared specimen against its exact SampleType revision
    // and a previously resolved microscope configuration. It does not mutate
    // the configuration or choose different equipment.
    [[nodiscard]] static SampleMicroscopyAssessment assess(
        const MicroscopeEquipmentDefinition& equipment,
        const SampleType& sampleType,
        const Sample& sample,
        const Specimen& specimen,
        const ResolvedConfiguration& configuration,
        const OperationMode* operationMode = nullptr);
};

} // namespace microscope::core
