#include "microscope/core/configuration/LightPathCalibration.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace microscope::core {
namespace {

[[nodiscard]] bool isKnownDriveUnit(IlluminationIntensityUnit unit) noexcept {
    switch (unit) {
    case IlluminationIntensityUnit::Percent:
    case IlluminationIntensityUnit::Milliamperes:
    case IlluminationIntensityUnit::Milliwatts:
    case IlluminationIntensityUnit::Relative:
    case IlluminationIntensityUnit::DeviceDefined:
        return true;
    }
    return false;
}

[[nodiscard]] bool hasDuplicateIds(
    const std::vector<EquipmentComponentId>& ids) {
    std::set<EquipmentComponentId> unique;
    for (const auto& id : ids) {
        if (!unique.insert(id).second) {
            return true;
        }
    }
    return false;
}

} // namespace

LightPathCalibration::LightPathCalibration(
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
    std::vector<LightPathCalibrationPoint> points)
    : revisionId_(std::move(revisionId)),
      microscopeId_(std::move(microscopeId)),
      equipmentDefinitionRevision_(equipmentDefinitionRevision),
      operationModeId_(std::move(operationModeId)),
      operationModeRevision_(operationModeRevision),
      lightPathId_(std::move(lightPathId)),
      illuminationComponentId_(std::move(illuminationComponentId)),
      illuminationChannelId_(std::move(illuminationChannelId)),
      configuredOpticalComponentIds_(std::move(configuredOpticalComponentIds)),
      driveUnit_(driveUnit),
      points_(std::move(points)) {}

Result<LightPathCalibration, LightPathCalibrationError>
LightPathCalibration::create(
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
    std::vector<LightPathCalibrationPoint> points) {
    if (equipmentDefinitionRevision == 0) {
        return Result<LightPathCalibration, LightPathCalibrationError>::failure(
            LightPathCalibrationError::ZeroEquipmentRevision);
    }
    if (operationModeRevision == 0) {
        return Result<LightPathCalibration, LightPathCalibrationError>::failure(
            LightPathCalibrationError::ZeroOperationModeRevision);
    }
    if (!isKnownDriveUnit(driveUnit)) {
        return Result<LightPathCalibration, LightPathCalibrationError>::failure(
            LightPathCalibrationError::InvalidDriveUnit);
    }
    if (points.size() < 2) {
        return Result<LightPathCalibration, LightPathCalibrationError>::failure(
            LightPathCalibrationError::TooFewPoints);
    }
    if (hasDuplicateIds(configuredOpticalComponentIds)) {
        return Result<LightPathCalibration, LightPathCalibrationError>::failure(
            LightPathCalibrationError::DuplicateOpticalComponent);
    }

    double previousDrive = -1.0;
    double previousIrradiance = -1.0;
    for (const auto& point : points) {
        if (!std::isfinite(point.driveValue) ||
            point.driveValue < 0.0 ||
            !std::isfinite(point.irradianceMicrowattsPerSquareMillimeter) ||
            point.irradianceMicrowattsPerSquareMillimeter < 0.0 ||
            !std::isfinite(point.relativeUncertaintyPercent) ||
            point.relativeUncertaintyPercent < 0.0) {
            return Result<LightPathCalibration, LightPathCalibrationError>::failure(
                LightPathCalibrationError::InvalidPoint);
        }
        if (point.driveValue <= previousDrive) {
            return Result<LightPathCalibration, LightPathCalibrationError>::failure(
                LightPathCalibrationError::DriveValuesNotIncreasing);
        }
        if (point.irradianceMicrowattsPerSquareMillimeter <= previousIrradiance) {
            return Result<LightPathCalibration, LightPathCalibrationError>::failure(
                LightPathCalibrationError::IrradianceValuesNotIncreasing);
        }
        previousDrive = point.driveValue;
        previousIrradiance = point.irradianceMicrowattsPerSquareMillimeter;
    }

    return Result<LightPathCalibration, LightPathCalibrationError>::success(
        LightPathCalibration(
            std::move(revisionId),
            std::move(microscopeId),
            equipmentDefinitionRevision,
            std::move(operationModeId),
            operationModeRevision,
            std::move(lightPathId),
            std::move(illuminationComponentId),
            std::move(illuminationChannelId),
            std::move(configuredOpticalComponentIds),
            driveUnit,
            std::move(points)));
}

const CalibrationRevisionId& LightPathCalibration::revisionId() const noexcept {
    return revisionId_;
}

const MicroscopeId& LightPathCalibration::microscopeId() const noexcept {
    return microscopeId_;
}

std::uint64_t LightPathCalibration::equipmentDefinitionRevision() const noexcept {
    return equipmentDefinitionRevision_;
}

const OperationModeId& LightPathCalibration::operationModeId() const noexcept {
    return operationModeId_;
}

std::uint64_t LightPathCalibration::operationModeRevision() const noexcept {
    return operationModeRevision_;
}

const LightPathId& LightPathCalibration::lightPathId() const noexcept {
    return lightPathId_;
}

const EquipmentComponentId&
LightPathCalibration::illuminationComponentId() const noexcept {
    return illuminationComponentId_;
}

const IlluminationChannelId&
LightPathCalibration::illuminationChannelId() const noexcept {
    return illuminationChannelId_;
}

const std::vector<EquipmentComponentId>&
LightPathCalibration::configuredOpticalComponentIds() const noexcept {
    return configuredOpticalComponentIds_;
}

IlluminationIntensityUnit LightPathCalibration::driveUnit() const noexcept {
    return driveUnit_;
}

const std::vector<LightPathCalibrationPoint>&
LightPathCalibration::points() const noexcept {
    return points_;
}

std::optional<double> LightPathCalibration::driveSettingForTargetIrradiance(
    double targetIrradiance) const noexcept {
    if (!std::isfinite(targetIrradiance) ||
        targetIrradiance < points_.front().irradianceMicrowattsPerSquareMillimeter ||
        targetIrradiance > points_.back().irradianceMicrowattsPerSquareMillimeter) {
        return std::nullopt;
    }

    for (std::size_t index = 0; index + 1 < points_.size(); ++index) {
        const auto& lower = points_[index];
        const auto& upper = points_[index + 1];
        if (targetIrradiance <= upper.irradianceMicrowattsPerSquareMillimeter) {
            const double fraction =
                (targetIrradiance - lower.irradianceMicrowattsPerSquareMillimeter) /
                (upper.irradianceMicrowattsPerSquareMillimeter -
                 lower.irradianceMicrowattsPerSquareMillimeter);
            return lower.driveValue + fraction * (upper.driveValue - lower.driveValue);
        }
    }

    return points_.back().driveValue;
}

LightPathModeSetting::LightPathModeSetting(
    OperationModeId operationModeId,
    std::uint64_t operationModeRevision,
    LightPathId lightPathId,
    CalibrationRevisionId calibrationRevisionId,
    EquipmentComponentId illuminationComponentId,
    IlluminationChannelId illuminationChannelId,
    std::vector<EquipmentComponentId> configuredOpticalComponentIds,
    double targetIrradianceMicrowattsPerSquareMillimeter)
    : operationModeId_(std::move(operationModeId)),
      operationModeRevision_(operationModeRevision),
      lightPathId_(std::move(lightPathId)),
      calibrationRevisionId_(std::move(calibrationRevisionId)),
      illuminationComponentId_(std::move(illuminationComponentId)),
      illuminationChannelId_(std::move(illuminationChannelId)),
      configuredOpticalComponentIds_(std::move(configuredOpticalComponentIds)),
      targetIrradianceMicrowattsPerSquareMillimeter_(
          targetIrradianceMicrowattsPerSquareMillimeter) {}

Result<LightPathModeSetting, LightPathModeSettingError>
LightPathModeSetting::create(
    OperationModeId operationModeId,
    std::uint64_t operationModeRevision,
    LightPathId lightPathId,
    CalibrationRevisionId calibrationRevisionId,
    EquipmentComponentId illuminationComponentId,
    IlluminationChannelId illuminationChannelId,
    std::vector<EquipmentComponentId> configuredOpticalComponentIds,
    double targetIrradianceMicrowattsPerSquareMillimeter) {
    if (operationModeRevision == 0) {
        return Result<LightPathModeSetting, LightPathModeSettingError>::failure(
            LightPathModeSettingError::ZeroOperationModeRevision);
    }
    if (!std::isfinite(targetIrradianceMicrowattsPerSquareMillimeter) ||
        targetIrradianceMicrowattsPerSquareMillimeter <= 0.0) {
        return Result<LightPathModeSetting, LightPathModeSettingError>::failure(
            LightPathModeSettingError::InvalidTargetIrradiance);
    }
    if (hasDuplicateIds(configuredOpticalComponentIds)) {
        return Result<LightPathModeSetting, LightPathModeSettingError>::failure(
            LightPathModeSettingError::DuplicateOpticalComponent);
    }

    return Result<LightPathModeSetting, LightPathModeSettingError>::success(
        LightPathModeSetting(
            std::move(operationModeId),
            operationModeRevision,
            std::move(lightPathId),
            std::move(calibrationRevisionId),
            std::move(illuminationComponentId),
            std::move(illuminationChannelId),
            std::move(configuredOpticalComponentIds),
            targetIrradianceMicrowattsPerSquareMillimeter));
}

const OperationModeId& LightPathModeSetting::operationModeId() const noexcept {
    return operationModeId_;
}

std::uint64_t LightPathModeSetting::operationModeRevision() const noexcept {
    return operationModeRevision_;
}

const LightPathId& LightPathModeSetting::lightPathId() const noexcept {
    return lightPathId_;
}

const CalibrationRevisionId&
LightPathModeSetting::calibrationRevisionId() const noexcept {
    return calibrationRevisionId_;
}

const EquipmentComponentId&
LightPathModeSetting::illuminationComponentId() const noexcept {
    return illuminationComponentId_;
}

const IlluminationChannelId&
LightPathModeSetting::illuminationChannelId() const noexcept {
    return illuminationChannelId_;
}

const std::vector<EquipmentComponentId>&
LightPathModeSetting::configuredOpticalComponentIds() const noexcept {
    return configuredOpticalComponentIds_;
}

double LightPathModeSetting::targetIrradianceMicrowattsPerSquareMillimeter()
    const noexcept {
    return targetIrradianceMicrowattsPerSquareMillimeter_;
}

std::optional<double> LightPathModeSetting::resolveDriveSetting(
    const LightPathCalibration& calibration) const noexcept {
    if (calibration.revisionId() != calibrationRevisionId_ ||
        calibration.operationModeId() != operationModeId_ ||
        calibration.operationModeRevision() != operationModeRevision_ ||
        calibration.lightPathId() != lightPathId_ ||
        calibration.illuminationComponentId() != illuminationComponentId_ ||
        calibration.illuminationChannelId() != illuminationChannelId_ ||
        calibration.configuredOpticalComponentIds() !=
            configuredOpticalComponentIds_) {
        return std::nullopt;
    }

    return calibration.driveSettingForTargetIrradiance(
        targetIrradianceMicrowattsPerSquareMillimeter_);
}

} // namespace microscope::core
