#pragma once

#include "microscope/core/diagnostics/Diagnostic.h"
#include "microscope/core/equipment/OpticalTopology.h"
#include "microscope/core/ids/Ids.h"
#include "microscope/core/result/Result.h"

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

enum class ConfigurationProfileError {
    InvalidContrastMethod,
    EmptyCompatibilitySet,
    DuplicateObjective,
    DuplicateCondenser,
    DuplicateOpticalPath,
    DuplicateCamera,
    DuplicateIllumination,
    DuplicateRequiredComponent
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
            std::vector<EquipmentComponentId> requiredPathComponentIds = {});

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

private:
    ConfigurationProfile(
        ConfigurationProfileId id,
        ContrastMethod contrastMethod,
        std::vector<EquipmentComponentId> compatibleObjectiveIds,
        std::vector<EquipmentComponentId> compatibleCondenserIds,
        std::vector<OpticalPathId> compatibleOpticalPathIds,
        std::vector<EquipmentComponentId> compatibleCameraIds,
        std::vector<EquipmentComponentId> compatibleIlluminationIds,
        std::vector<EquipmentComponentId> requiredPathComponentIds);

    ConfigurationProfileId id_;
    ContrastMethod contrastMethod_;
    std::vector<EquipmentComponentId> compatibleObjectiveIds_;
    std::vector<EquipmentComponentId> compatibleCondenserIds_;
    std::vector<OpticalPathId> compatibleOpticalPathIds_;
    std::vector<EquipmentComponentId> compatibleCameraIds_;
    std::vector<EquipmentComponentId> compatibleIlluminationIds_;
    std::vector<EquipmentComponentId> requiredPathComponentIds_;
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
    InvalidExposurePolicy
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
            OpticalPathId opticalPathId,
            EquipmentComponentId cameraComponentId,
            CameraModeId cameraModeId,
            EquipmentComponentId illuminationComponentId,
            IlluminationChannelId illuminationChannelId,
            ExposurePolicy exposurePolicy);

    [[nodiscard]] const ConfigurationProfileId& profileId() const noexcept;
    [[nodiscard]] ContrastMethod contrastMethod() const noexcept;
    [[nodiscard]] const EquipmentComponentId& objectiveComponentId() const noexcept;
    [[nodiscard]] const EquipmentComponentId& condenserComponentId() const noexcept;
    [[nodiscard]] const OpticalPathId& opticalPathId() const noexcept;
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
        OpticalPathId opticalPathId,
        EquipmentComponentId cameraComponentId,
        CameraModeId cameraModeId,
        EquipmentComponentId illuminationComponentId,
        IlluminationChannelId illuminationChannelId,
        ExposurePolicy exposurePolicy);

    ConfigurationProfileId profileId_;
    ContrastMethod contrastMethod_;
    EquipmentComponentId objectiveComponentId_;
    EquipmentComponentId condenserComponentId_;
    OpticalPathId opticalPathId_;
    EquipmentComponentId cameraComponentId_;
    CameraModeId cameraModeId_;
    EquipmentComponentId illuminationComponentId_;
    IlluminationChannelId illuminationChannelId_;
    ExposurePolicy exposurePolicy_;
};

class ConfigurationResolution final {
public:
    [[nodiscard]] const std::optional<ConfigurationRequest>&
    resolvedRequest() const noexcept;
    [[nodiscard]] const Diagnostics& diagnostics() const noexcept;
    [[nodiscard]] bool isResolved() const noexcept;

private:
    friend class ConfigurationResolver;

    ConfigurationResolution(
        std::optional<ConfigurationRequest> resolvedRequest,
        Diagnostics diagnostics);

    std::optional<ConfigurationRequest> resolvedRequest_;
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
