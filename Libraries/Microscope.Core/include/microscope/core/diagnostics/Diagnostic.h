#pragma once

#include "microscope/core/result/Result.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace microscope::core {

enum class DiagnosticSeverity {
    Information,
    Warning,
    Error
};

struct DiagnosticContext final {
    std::optional<std::string> entityType;
    std::optional<std::string> entityId;
    std::optional<std::string> fieldPath;
};

enum class DiagnosticError {
    InvalidSeverity,
    EmptyCode,
    InvalidCode,
    EmptyMessage,
    EmptyContextValue
};

// A diagnostic has a stable machine-readable code and non-empty display
// message. It remains distinct from a Result failure: successful validation
// may return warnings/information alongside a value.
class Diagnostic final {
public:
    [[nodiscard]] static Result<Diagnostic, DiagnosticError> create(
        DiagnosticSeverity severity,
        std::string code,
        std::string message,
        std::optional<DiagnosticContext> context = std::nullopt) {
        switch (severity) {
        case DiagnosticSeverity::Information:
        case DiagnosticSeverity::Warning:
        case DiagnosticSeverity::Error:
            break;
        default:
            return Result<Diagnostic, DiagnosticError>::failure(
                DiagnosticError::InvalidSeverity);
        }

        if (code.empty()) {
            return Result<Diagnostic, DiagnosticError>::failure(
                DiagnosticError::EmptyCode);
        }
        if (code.find_first_of(" \\t\\n\\r\\f\\v") != std::string::npos) {
            return Result<Diagnostic, DiagnosticError>::failure(
                DiagnosticError::InvalidCode);
        }
        if (message.find_first_not_of(" \t\n\r\f\v") == std::string::npos) {
            return Result<Diagnostic, DiagnosticError>::failure(
                DiagnosticError::EmptyMessage);
        }
        if (context &&
            ((context->entityType && context->entityType->empty()) ||
             (context->entityId && context->entityId->empty()) ||
             (context->fieldPath && context->fieldPath->empty()))) {
            return Result<Diagnostic, DiagnosticError>::failure(
                DiagnosticError::EmptyContextValue);
        }

        return Result<Diagnostic, DiagnosticError>::success(
            Diagnostic(
                severity,
                std::move(code),
                std::move(message),
                std::move(context)));
    }

    [[nodiscard]] DiagnosticSeverity severity() const noexcept {
        return severity_;
    }

    [[nodiscard]] const std::string& code() const noexcept {
        return code_;
    }

    [[nodiscard]] const std::string& message() const noexcept {
        return message_;
    }

    [[nodiscard]] const std::optional<DiagnosticContext>& context() const noexcept {
        return context_;
    }

private:
    Diagnostic(
        DiagnosticSeverity severity,
        std::string code,
        std::string message,
        std::optional<DiagnosticContext> context)
        : severity_(severity),
          code_(std::move(code)),
          message_(std::move(message)),
          context_(std::move(context)) {}

    DiagnosticSeverity severity_;
    std::string code_;
    std::string message_;
    std::optional<DiagnosticContext> context_;
};

using Diagnostics = std::vector<Diagnostic>;

} // namespace microscope::core
