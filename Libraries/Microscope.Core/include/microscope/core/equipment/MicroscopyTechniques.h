#pragma once

namespace microscope::core {

// Imaging contrast/technique selected for one acquisition configuration.
// Shared by equipment configuration and specimen-side microscopy requirements.
enum class ContrastMethod {
    Brightfield,
    PhaseContrast,
    DifferentialInterferenceContrast,
    PolarizedLight,
    Fluorescence,
    Darkfield,
    Other,
    Unknown
};

} // namespace microscope::core
