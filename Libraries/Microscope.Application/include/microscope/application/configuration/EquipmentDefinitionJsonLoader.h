#pragma once

#include "microscope/core/equipment/OpticalTopology.h"
#include "microscope/core/result/Result.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace microscope::application::configuration {

enum class EquipmentDefinitionJsonErrorCode {
    EmptyDocument,
    DocumentTooLarge,
    FileOpenFailed,
    FileReadFailed,
    JsonSyntaxError,
    UnsupportedSchemaVersion,
    MissingField,
    UnknownField,
    WrongType,
    InvalidValue,
    DomainValidationFailed
};

struct EquipmentDefinitionJsonError final {
    EquipmentDefinitionJsonErrorCode code;
    std::size_t offset = 0;
    std::size_t line = 1;
    std::size_t column = 1;
    std::string path;
    std::string detail;
};

// Decodes the versioned, static microscope-equipment JSON document into the
// canonical Core model. The JSON document is data only; runtime observations,
// device sessions, calibration measurements and acquisition state are not
// represented here.
class EquipmentDefinitionJsonLoader final {
public:
    static constexpr std::uint32_t kSupportedSchemaVersion = 1;
    static constexpr std::size_t kMaximumDocumentBytes = 16U * 1024U * 1024U;

    [[nodiscard]] static core::Result<
        core::MicroscopeEquipmentDefinition,
        EquipmentDefinitionJsonError> parse(std::string_view document);

    [[nodiscard]] static core::Result<
        core::MicroscopeEquipmentDefinition,
        EquipmentDefinitionJsonError> loadFile(const std::string& path);
};

} // namespace microscope::application::configuration
