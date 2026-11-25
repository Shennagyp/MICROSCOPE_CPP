#pragma once

#include "microscope/core/diagnostics/Diagnostic.h"
#include "microscope/core/equipment/OpticalTopology.h"
#include "microscope/core/ids/Ids.h"
#include "microscope/core/result/Result.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace microscope::core {

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

enum class ExposurePolicy {
    Manual,
    Automatic,
    ConstrainedAutomatic,
    Unknown
};

enum class OpticalFunctionRequirementError {
    InvalidFunction,
    ZeroMinimumComponentCount
};

// A profile can require one or more installed optical functions. A compatibility
// identity binds mating components (e.g. phase plate and matching phase annulus).
class OpticalFunctionRequirement final {
public:
    [[nodiscard]] static Result<
        OpticalFunctionRequirement,
        OpticalFunctionRequirementError> create(
            OpticalElementFunction function,
            std::optional<OpticalCompatibilityId> compatibilityId = std::nullopt,
            std::uint32_t minimumComponentCount = 1);

    [[nodiscard]] OpticalElementFunction function() const noexcept;
    [[nodiscard]] const std::optional<OpticalCompatibilityId>&
    compatibilityId() const noexcept;
    [[nodiscard]] std::uint32_t minimumComponentCount() const noexcept;

private:
    OpticalFunctionRequirement(
        OpticalElementFunction function,
        std::optional<OpticalCompatibilityId> compatibilityId,
        std::uint32_t minimumComponentCount);

    OpticalElementFunction function_;
    std::optional<OpticalCompatibilityId> compatibilityId_;
    std::uint32_t minimumComponentCount_;
};

enum class IlluminationIntensitySettingError {
    InvalidUnit,
    InvalidValue
};

// A target intensity in the unit declared by the selected illumination channel.
class IlluminationIntensitySetting final {
public:
    [[nodiscard]] static Result<
        IlluminationIntensitySetting,
        IlluminationIntensitySettingError> create(
            double value,
            IlluminationIntensityUnit unit);

    [[nodiscard]] double value() const noexcept;
    [[nodiscard]] IlluminationIntensityUnit unit() const noexcept;

private:
    IlluminationIntensitySetting(
        double value,
        IlluminationIntensityUnit unit);

    double value_;
    IlluminationIntensityUnit unit_;
};

// An initial exposure target. ExposurePolicy remains a separate request choice.
class ExposurePreset final {
public:
    [[nodiscard]] static ExposurePreset create(PositiveMicroseconds exposureTime);

    [[nodiscard]] double exposureTimeMicroseconds() const noexcept;

private:
    explicit ExposurePreset(PositiveMicroseconds exposureTime);

    PositiveMicroseconds exposureTime_;
};

enum class ConfigurationProfileError {
    InvalidContrastMethod,
    EmptyCompatibilitySet,
    DuplicateObjective,
    DuplicateCondenser,
    DuplicateOpticalPath,
    DuplicateCamera,
    DuplicateIllumination,
    DuplicateRequiredComponent,
    DuplicateOpticalFunctionRequirement
};

// A profile is declarative compatibility data for one instrument definition.
// It references installed identities; it does not contain executable behavior.
class ConfigurationProfile final {
public:
    [[nodiscard]] static Result<
        ConfigurationProfile,
        ConfigurationProfileError> create(
            ConfigurationProfileId id,
            ContrastMethod contrastMethod,
            std::vector<EquipmentComponentId> compatibleObjectiveIds,
            std::vector<EquipmentComponentId> compatibleCondenserIds,
            std::vector<OpticalPathId> compatibleOpticalPathIds,
            std::vector<EquipmentComponentId> compatibleCameraIds,
            std::vector<EquipmentComponentId> compatibleIlluminationIds,
            std::vector<EquipmentComponentId> requiredPathComponentIds = {},
            std::vector<OpticalFunctionRequirement> requiredOpticalFunctions = {},
            std::optional<NonNegativeNumericalAperture> condenserApertureTarget = std::nullopt,
            std::optional<IlluminationIntensitySetting> illuminationIntensityTarget = std::nullopt,
            std::optional<ExposurePreset> exposurePreset = std::nullopt);

    [[nodiscard]] const ConfigurationProfileId& id() const noexcept;
    [[nodiscard]] ContrastMethod contrastMethod() const noexcept;
    [[nodiscard]] const std::vector<EquipmentComponentId>&
    compatibleObjectiveIds() const noexcept;
    [[nodiscard]] const std::vector<EquipmentComponentId>&
    compatibleCondenserIds() const noexcept;
    [[nodiscard]] const std::vector<OpticalPathId>&
    compatibleOpticalPathIds() const noexcept;
    [[nodiscard]] const std::vector<EquipmentComponentId>&
    compatibleCameraIds() const noexcept;
    [[nodiscard]] const std::vector<EquipmentComponentId>&
    compatibleIlluminationIds() const noexcept;
    [[nodiscard]] const std::vector<EquipmentComponentId>&
    requiredPathComponentIds() const noexcept;
    [[nodiscard]] const std::vector<OpticalFunctionRequirement>&
    requiredOpticalFunctions() const noexcept;
    [[nodiscard]] const std::optional<NonNegativeNumericalAperture>&
    condenserApertureTarget() const noexcept;
    [[nodiscard]] const std::optional<IlluminationIntensitySetting>&
    illuminationIntensityTarget() const noexcept;
    [[nodiscard]] const std::optional<ExposurePreset>&
    exposurePreset() const noexcept;

private:
    ConfigurationProfile(
        ConfigurationProfileId id,
        ContrastMethod contrastMethod,
        std::vector<EquipmentComponentId> compatibleObjectiveIds,
        std::vector<EquipmentComponentId> compatibleCondenserIds,
        std::vector<OpticalPathId> compatibleOpticalPathIds,
        std::vector<EquipmentComponentId> compatibleCameraIds,
        std::vector<EquipmentComponentId> compatibleIlluminationIds,
        std::vector<EquipmentComponentId> requiredPathComponentIds,
        std::vector<OpticalFunctionRequirement> requiredOpticalFunctions,
        std::optional<NonNegativeNumericalAperture> condenserApertureTarget,
        std::optional<IlluminationIntensitySetting> illuminationIntensityTarget,
        std::optional<ExposurePreset> exposurePreset);

    ConfigurationProfileId id_;
    ContrastMethod contrastMethod_;
    std::vector<EquipmentComponentId> compatibleObjectiveIds_;
    std::vector<EquipmentComponentId> compatibleCondenserIds_;
    std::vector<OpticalPathId> compatibleOpticalPathIds_;
    std::vector<EquipmentComponentId> compatibleCameraIds_;
    std::vector<EquipmentComponentId> compatibleIlluminationIds_;
    std::vector<EquipmentComponentId> requiredPathComponentIds_;
    std::vector<OpticalFunctionRequirement> requiredOpticalFunctions_;
    std::optional<NonNegativeNumericalAperture> condenserApertureTarget_;
    std::optional<IlluminationIntensitySetting> illuminationIntensityTarget_;
    std::optional<ExposurePreset> exposurePreset_;
};

enum class ConfigurationProfileCatalogError {
    EmptyCatalog,
    DuplicateProfileId
};

class ConfigurationProfileCatalog final {
public:
    [[nodiscard]] static Result<
        ConfigurationProfileCatalog,
        ConfigurationProfileCatalogError> create(
            std::vector<ConfigurationProfile> profiles);

    [[nodiscard]] const std::vector<ConfigurationProfile>& profiles() const noexcept;

private:
    explicit ConfigurationProfileCatalog(std::vector<ConfigurationProfile> profiles);

    std::vector<ConfigurationProfile> profiles_;
};

enum class ConfigurationRequestError {
    InvalidContrastMethod,
    InvalidExposurePolicy,
    EmptyOpticalPathSet,
    DuplicateOpticalPath,
    DuplicateAdditionalOpticalComponent
};

// An immutable selection request. It names concrete installed components and
// device-supported modes; it does not claim that any selection is active yet.
class ConfigurationRequest final {
public:
    [[nodiscard]] static Result<
        ConfigurationRequest,
        ConfigurationRequestError> create(
            ConfigurationProfileId profileId,
            ContrastMethod contrastMethod,
            EquipmentComponentId objectiveComponentId,
            EquipmentComponentId condenserComponentId,
            std::vector<OpticalPathId> opticalPathIds,
            EquipmentComponentId cameraComponentId,
            CameraModeId cameraModeId,
            EquipmentComponentId illuminationComponentId,
            IlluminationChannelId illuminationChannelId,
            ExposurePolicy exposurePolicy,
            std::vector<EquipmentComponentId> selectedAdditionalOpticalComponentIds = {});

    [[nodiscard]] const ConfigurationProfileId& profileId() const noexcept;
    [[nodiscard]] ContrastMethod contrastMethod() const noexcept;
    [[nodiscard]] const EquipmentComponentId& objectiveComponentId() const noexcept;
    [[nodiscard]] const EquipmentComponentId& condenserComponentId() const noexcept;
    [[nodiscard]] const std::vector<OpticalPathId>& opticalPathIds() const noexcept;
    [[nodiscard]] const std::vector<EquipmentComponentId>&
    selectedAdditionalOpticalComponentIds() const noexcept;
    [[nodiscard]] const EquipmentComponentId& cameraComponentId() const noexcept;
    [[nodiscard]] const CameraModeId& cameraModeId() const noexcept;
    [[nodiscard]] const EquipmentComponentId& illuminationComponentId() const noexcept;
    [[nodiscard]] const IlluminationChannelId& illuminationChannelId() const noexcept;
    [[nodiscard]] ExposurePolicy exposurePolicy() const noexcept;

private:
    ConfigurationRequest(
        ConfigurationProfileId profileId,
        ContrastMethod contrastMethod,
        EquipmentComponentId objectiveComponentId,
        EquipmentComponentId condenserComponentId,
        std::vector<OpticalPathId> opticalPathIds,
        EquipmentComponentId cameraComponentId,
        CameraModeId cameraModeId,
        EquipmentComponentId illuminationComponentId,
        IlluminationChannelId illuminationChannelId,
        ExposurePolicy exposurePolicy,
        std::vector<EquipmentComponentId> selectedAdditionalOpticalComponentIds);

    ConfigurationProfileId profileId_;
    ContrastMethod contrastMethod_;
    EquipmentComponentId objectiveComponentId_;
    EquipmentComponentId condenserComponentId_;
    std::vector<OpticalPathId> opticalPathIds_;
    EquipmentComponentId cameraComponentId_;
    CameraModeId cameraModeId_;
    EquipmentComponentId illuminationComponentId_;
    IlluminationChannelId illuminationChannelId_;
    ExposurePolicy exposurePolicy_;
    std::vector<EquipmentComponentId> selectedAdditionalOpticalComponentIds_;
};

// A validated desired microscope state, scoped to the exact installed
// equipment definition against which the request was resolved.
class ResolvedConfiguration final {
public:
    [[nodiscard]] const MicroscopeId& microscopeId() const noexcept;
    [[nodiscard]] const ConfigurationRequest& request() const noexcept;
    [[nodiscard]] const std::optional<NonNegativeNumericalAperture>&
    condenserApertureTarget() const noexcept;
    [[nodiscard]] const std::optional<IlluminationIntensitySetting>&
    illuminationIntensityTarget() const noexcept;
    [[nodiscard]] const std::optional<ExposurePreset>&
    exposurePreset() const noexcept;

private:
    friend class ConfigurationResolver;

    ResolvedConfiguration(
        MicroscopeId microscopeId,
        ConfigurationRequest request,
        std::optional<NonNegativeNumericalAperture> condenserApertureTarget,
        std::optional<IlluminationIntensitySetting> illuminationIntensityTarget,
        std::optional<ExposurePreset> exposurePreset);

    MicroscopeId microscopeId_;
    ConfigurationRequest request_;
    std::optional<NonNegativeNumericalAperture> condenserApertureTarget_;
    std::optional<IlluminationIntensitySetting> illuminationIntensityTarget_;
    std::optional<ExposurePreset> exposurePreset_;
};

class ConfigurationResolution final {
public:
    [[nodiscard]] const std::optional<ResolvedConfiguration>&
    resolvedConfiguration() const noexcept;
    [[nodiscard]] const Diagnostics& diagnostics() const noexcept;
    [[nodiscard]] bool isResolved() const noexcept;

private:
    friend class ConfigurationResolver;

    ConfigurationResolution(
        std::optional<ResolvedConfiguration> resolvedConfiguration,
        Diagnostics diagnostics);

    std::optional<ResolvedConfiguration> resolvedConfiguration_;
    Diagnostics diagnostics_;
};

// Pure resolver: no hardware, storage, clocks, UI or mutable runtime state.
// A successful result means the selected identities are compatible with the
// installed equipment and profile; it does not mean the configuration is active.
class ConfigurationResolver final {
public:
    [[nodiscard]] static ConfigurationResolution resolve(
        const MicroscopeEquipmentDefinition& equipment,
        const ConfigurationProfileCatalog& catalog,
        const ConfigurationRequest& request);
};

} // namespace microscope::core
