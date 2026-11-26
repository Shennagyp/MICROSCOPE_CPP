#pragma once

#include "microscope/core/equipment/IlluminationSpecifications.h"
#include "microscope/core/ids/Ids.h"
#include "microscope/core/result/Result.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace microscope::core {

struct LightPathCalibrationPoint {
    double driveValue = 0.0;
    double irradianceMicrowattsPerSquareMillimeter = 0.0;
    double relativeUncertaintyPercent = 0.0;
};

enum class LightPathCalibrationError {
    ZeroEquipmentRevision,
    ZeroOperationModeRevision,
    InvalidDriveUnit,
    TooFewPoints,
    InvalidPoint,
    DriveValuesNotIncreasing,
    IrradianceValuesNotIncreasing,
    DuplicateOpticalComponent
};

// A measured transfer curve for one operation-mode configuration on one
// physical LightPath. It is revision-bound so a changed filter/objective/
// condenser/source configuration cannot silently reuse stale calibration.
class LightPathCalibration final {
public:
    [[nodiscard]] static Result<
        LightPathCalibration,
        LightPathCalibrationError> create(
            CalibrationRevisionId revisionId,
            MicroscopeId microscopeId,
            std::uint64_t equipmentDefinitionRevision,
            MicroscopeId microscopeId,
            std::uint64_t equipmentDefinitionRevision,
            OperationModeId operationModeId,
            std::uint64_t operationModeRevision,
            LightPathId lightPathId,
            EquipmentComponentId illuminationComponentId,
            IlluminationChannelId illuminationChannelId,
            std::vector<EquipmentComponentId> configuredOpticalComponentIds,
            IlluminationIntensityUnit driveUnit,
            std::vector<LightPathCalibrationPoint> points);

    [[nodiscard]] const CalibrationRevisionId& revisionId() const noexcept;
    [[nodiscard]] const MicroscopeId& microscopeId() const noexcept;
    [[nodiscard]] std::uint64_t equipmentDefinitionRevision() const noexcept;
    [[nodiscard]] const MicroscopeId& microscopeId() const noexcept;
    [[nodiscard]] std::uint64_t equipmentDefinitionRevision() const noexcept;
    [[nodiscard]] const OperationModeId& operationModeId() const noexcept;
    [[nodiscard]] std::uint64_t operationModeRevision() const noexcept;
    [[nodiscard]] const LightPathId& lightPathId() const noexcept;
    [[nodiscard]] const EquipmentComponentId& illuminationComponentId() const noexcept;
    [[nodiscard]] const IlluminationChannelId& illuminationChannelId() const noexcept;
    [[nodiscard]] const std::vector<EquipmentComponentId>&
    configuredOpticalComponentIds() const noexcept;
    [[nodiscard]] IlluminationIntensityUnit driveUnit() const noexcept;
    [[nodiscard]] const std::vector<LightPathCalibrationPoint>& points() const noexcept;

    // Returns no value when the target is outside the measured calibration
    // interval. Extrapolation is deliberately not permitted.
    [[nodiscard]] std::optional<double> driveSettingForTargetIrradiance(
        double irradianceMicrowattsPerSquareMillimeter) const noexcept;

private:
    LightPathCalibration(
        CalibrationRevisionId revisionId,
        MicroscopeId microscopeId,
        std::uint64_t equipmentDefinitionRevision,
        OperationModeId operationModeId,
        std::uint64_t operationModeRevision,
        LightPathId lightPathId,
        EquipmentComponentId illuminationComponentId,
        IlluminationChannelId illuminationChannelId,
        std::vector<EquipmentComponentId> configuredOpticalComponentIds,
        IlluminationIntensityUnit driveUnit,
        std::vector<LightPathCalibrationPoint> points);

    CalibrationRevisionId revisionId_;
    MicroscopeId microscopeId_;
    std::uint64_t equipmentDefinitionRevision_;
    MicroscopeId microscopeId_;
    std::uint64_t equipmentDefinitionRevision_;
    OperationModeId operationModeId_;
    std::uint64_t operationModeRevision_;
    LightPathId lightPathId_;
    EquipmentComponentId illuminationComponentId_;
    IlluminationChannelId illuminationChannelId_;
    std::vector<EquipmentComponentId> configuredOpticalComponentIds_;
    IlluminationIntensityUnit driveUnit_;
    std::vector<LightPathCalibrationPoint> points_;
};

enum class LightPathModeSettingError {
    ZeroEquipmentRevision,
    ZeroOperationModeRevision,
    InvalidTargetIrradiance,
    DuplicateOpticalComponent
};

// Persisted desired illumination for one mode/light-path/source-channel.
// The target is specimen-plane irradiance; its device drive value is derived
// only from the exact calibration revision referenced by this setting.
class LightPathModeSetting final {
public:
    [[nodiscard]] static Result<
        LightPathModeSetting,
        LightPathModeSettingError> create(
            MicroscopeId microscopeId,
            std::uint64_t equipmentDefinitionRevision,
            OperationModeId operationModeId,
            std::uint64_t operationModeRevision,
            LightPathId lightPathId,
            CalibrationRevisionId calibrationRevisionId,
            EquipmentComponentId illuminationComponentId,
            IlluminationChannelId illuminationChannelId,
            std::vector<EquipmentComponentId> configuredOpticalComponentIds,
            double targetIrradianceMicrowattsPerSquareMillimeter);

    [[nodiscard]] const MicroscopeId& microscopeId() const noexcept;
    [[nodiscard]] std::uint64_t equipmentDefinitionRevision() const noexcept;
    [[nodiscard]] const OperationModeId& operationModeId() const noexcept;
    [[nodiscard]] std::uint64_t operationModeRevision() const noexcept;
    [[nodiscard]] const LightPathId& lightPathId() const noexcept;
    [[nodiscard]] const CalibrationRevisionId& calibrationRevisionId() const noexcept;
    [[nodiscard]] const EquipmentComponentId& illuminationComponentId() const noexcept;
    [[nodiscard]] const IlluminationChannelId& illuminationChannelId() const noexcept;
    [[nodiscard]] const std::vector<EquipmentComponentId>&
    configuredOpticalComponentIds() const noexcept;
    [[nodiscard]] double targetIrradianceMicrowattsPerSquareMillimeter() const noexcept;

    [[nodiscard]] std::optional<double> resolveDriveSetting(
        const LightPathCalibration& calibration) const noexcept;

private:
    LightPathModeSetting(
        MicroscopeId microscopeId,
        std::uint64_t equipmentDefinitionRevision,
        OperationModeId operationModeId,
        std::uint64_t operationModeRevision,
        LightPathId lightPathId,
        CalibrationRevisionId calibrationRevisionId,
        EquipmentComponentId illuminationComponentId,
        IlluminationChannelId illuminationChannelId,
        std::vector<EquipmentComponentId> configuredOpticalComponentIds,
        double targetIrradianceMicrowattsPerSquareMillimeter);

    MicroscopeId microscopeId_;
    std::uint64_t equipmentDefinitionRevision_;
    OperationModeId operationModeId_;
    std::uint64_t operationModeRevision_;
    LightPathId lightPathId_;
    CalibrationRevisionId calibrationRevisionId_;
    EquipmentComponentId illuminationComponentId_;
    IlluminationChannelId illuminationChannelId_;
    std::vector<EquipmentComponentId> configuredOpticalComponentIds_;
    double targetIrradianceMicrowattsPerSquareMillimeter_;
};

} // namespace microscope::core
