#pragma once

#include <optional>
#include <string>
#include <vector>

namespace microscope::core {

enum class DiagnosticSeverity {
    Information,
    Warning,
    Error
};

// Context is descriptive metadata, not a machine-readable error identity.
struct DiagnosticContext final {
    std::optional<std::string> entityType;
    std::optional<std::string> entityId;
    std::optional<std::string> fieldPath;
};

struct Diagnostic final {
    DiagnosticSeverity severity = DiagnosticSeverity::Information;
    std::string code;
    std::string message;
    std::optional<DiagnosticContext> context;
};

using Diagnostics = std::vector<Diagnostic>;

} // namespace microscope::core
