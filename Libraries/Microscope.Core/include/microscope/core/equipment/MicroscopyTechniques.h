#pragma once

namespace microscope::core {

// Imaging contrast/technique selected for one acquisition configuration.
// Shared by equipment configuration and specimen-side microscopy requirements.
// Numeric values are stable persistence identities. Add new values without renumbering.
enum class ContrastMethod {
    Brightfield = 0,
    PhaseContrast = 1,
    DifferentialInterferenceContrast = 2,
    PolarizedLight = 3,
    Fluorescence = 4,
    Darkfield = 5,
    Other = 6,
    Unknown = 7
};

} // namespace microscope::core
