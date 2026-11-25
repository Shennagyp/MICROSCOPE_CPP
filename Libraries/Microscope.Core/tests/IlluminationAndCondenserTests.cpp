#include "microscope/core/equipment/OpticalTopology.h"
#include "microscope/core/equipment/IlluminationSpecifications.h"

#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace microscope::core;

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

template <typename TValue, typename TError>
TValue take(Result<TValue, TError> result) {
    if (!result) {
        throw std::runtime_error("A test fixture factory rejected valid input.");
    }
    return std::move(result).value();
}

template <typename TId>
TId makeId(const char* value) {
    return take(TId::create(value));
}

PositiveMicrometers micrometers(double value) {
    return take(PositiveMicrometers::create(value));
}

PositiveNanometers nanometers(double value) {
    return take(PositiveNanometers::create(value));
}

NumericalAperture na(double value) {
    return take(NumericalAperture::create(value));
}

NonNegativeNumericalAperture nonNegativeNa(double value) {
    return take(NonNegativeNumericalAperture::create(value));
}

OpticalPort makePort(const char* id, OpticalPortDirection direction) {
    return take(OpticalPort::create(makeId<OpticalPortId>(id), direction));
}

EquipmentComponent makeOpticalComponent(const EquipmentComponentId& id) {
    return take(EquipmentComponent::create(
        id,
        EquipmentComponentKind::OpticalElement,
        {makePort("optical-input", OpticalPortDirection::Input),
         makePort("optical-output", OpticalPortDirection::Output)}));
}

EquipmentComponent makeIlluminationComponent(const EquipmentComponentId& id) {
    return take(EquipmentComponent::create(
        id,
        EquipmentComponentKind::IlluminationDevice,
        {makePort("illumination-output", OpticalPortDirection::Output)}));
}

EquipmentComponent makeImagingComponent(const EquipmentComponentId& id) {
    return take(EquipmentComponent::create(
        id,
        EquipmentComponentKind::ImagingDevice,
        {makePort("imaging-input", OpticalPortDirection::Input)}));
}

SpectralBand makeBand(double minimum, double maximum) {
    return take(SpectralBand::create(
        nanometers(minimum),
        nanometers(maximum)));
}

IlluminationIntensityRange makeIntensityRange(
    double minimum,
    double maximum,
    IlluminationIntensityUnit unit = IlluminationIntensityUnit::Percent) {
    return take(IlluminationIntensityRange::create(minimum, maximum, unit));
}

IlluminationChannel makeChannel(
    const char* id,
    std::optional<SpectralBand> band,
    IlluminationControlKind control,
    std::optional<IlluminationIntensityRange> range) {
    return take(IlluminationChannel::create(
        makeId<IlluminationChannelId>(id),
        std::move(band),
        control,
        std::move(range)));
}

IlluminationSpecification makeIlluminationSpecification(
    const EquipmentComponentId& componentId) {
    return take(IlluminationSpecification::create(
        componentId,
        IlluminationSourceTechnology::Led,
        {makeChannel(
             "blue-channel",
             makeBand(450.0, 490.0),
             IlluminationControlKind::Continuous,
             makeIntensityRange(0.0, 100.0)),
         makeChannel(
             "red-channel",
             makeBand(620.0, 660.0),
             IlluminationControlKind::DeviceDefined,
             std::nullopt)}));
}

CondenserSpecification makeCondenserSpecification(
    const EquipmentComponentId& componentId) {
    return take(CondenserSpecification::create(
        componentId,
        nonNegativeNa(0.0),
        na(1.40),
        CondenserDesign::AchromatAplanatic,
        CondenserImmersionMedium::Oil,
        micrometers(1'000.0)));
}

void testSpectralBandAndIntensityRangeValidation() {
    static_assert(!std::is_convertible<
        PositiveNanometers, PositiveMicrometers>::value,
        "wavelength and distance types must not be interchangeable");

    const auto band = makeBand(450.0, 490.0);
    check(band.minimumWavelengthNanometers() == 450.0 &&
          band.maximumWavelengthNanometers() == 490.0,
          "preserves the channel wavelength band in nanometers");

    auto invertedBand = SpectralBand::create(nanometers(500.0), nanometers(400.0));
    check(!invertedBand &&
          invertedBand.error() == SpectralBandError::InvertedRange,
          "rejects an inverted spectral range");

    auto invalidIntensityUnit = IlluminationIntensityRange::create(
        0.0, 100.0, static_cast<IlluminationIntensityUnit>(255));
    check(!invalidIntensityUnit &&
          invalidIntensityUnit.error() ==
              IlluminationIntensityRangeError::InvalidUnit,
          "rejects an unknown intensity unit");

    auto negativeIntensity = IlluminationIntensityRange::create(
        -1.0, 100.0, IlluminationIntensityUnit::Percent);
    check(!negativeIntensity &&
          negativeIntensity.error() ==
              IlluminationIntensityRangeError::InvalidValue,
          "rejects negative intensity values");

    auto nonFiniteIntensity = IlluminationIntensityRange::create(
        0.0,
        std::numeric_limits<double>::infinity(),
        IlluminationIntensityUnit::Milliwatts);
    check(!nonFiniteIntensity &&
          nonFiniteIntensity.error() ==
              IlluminationIntensityRangeError::InvalidValue,
          "rejects non-finite intensity values");

    auto fixedRange = IlluminationIntensityRange::create(
        100.0, 100.0, IlluminationIntensityUnit::Percent);
    check(!fixedRange &&
          fixedRange.error() ==
              IlluminationIntensityRangeError::NonIncreasingRange,
          "requires continuous control ranges to have increasing bounds");
}

void testIlluminationChannelControlSemantics() {
    auto continuousWithoutRange = IlluminationChannel::create(
        makeId<IlluminationChannelId>("continuous-without-range"),
        std::nullopt,
        IlluminationControlKind::Continuous,
        std::nullopt);
    check(!continuousWithoutRange &&
          continuousWithoutRange.error() ==
              IlluminationChannelError::MissingContinuousRange,
          "requires a declared range for continuous intensity control");

    auto fixedWithRange = IlluminationChannel::create(
        makeId<IlluminationChannelId>("fixed-with-range"),
        std::nullopt,
        IlluminationControlKind::Fixed,
        makeIntensityRange(10.0, 90.0));
    check(!fixedWithRange &&
          fixedWithRange.error() ==
              IlluminationChannelError::UnexpectedContinuousRange,
          "does not attach a continuous range to a fixed channel");

    auto invalidControl = IlluminationChannel::create(
        makeId<IlluminationChannelId>("invalid-control"),
        std::nullopt,
        static_cast<IlluminationControlKind>(255),
        std::nullopt);
    check(!invalidControl &&
          invalidControl.error() == IlluminationChannelError::InvalidControlKind,
          "rejects an unknown illumination control kind");

    const auto fixedChannel = makeChannel(
        "fixed-channel",
        std::nullopt,
        IlluminationControlKind::Fixed,
        std::nullopt);
    check(fixedChannel.controlKind() == IlluminationControlKind::Fixed &&
          !fixedChannel.spectralBand(),
          "allows fixed output and an unspecified spectral band");
}

void testMultiChannelIlluminationSpecification() {
    const auto componentId =
        makeId<EquipmentComponentId>("illumination-led-array");
    const auto specification = makeIlluminationSpecification(componentId);

    check(specification.componentId() == componentId,
          "illumination specification retains installed component identity");
    check(specification.technology() == IlluminationSourceTechnology::Led,
          "preserves source technology");
    check(specification.channels().size() == 2,
          "supports multiple independently identified illumination channels");
    check(specification.channels()[0].id() != specification.channels()[1].id(),
          "each channel has its own stable identity");
    check(specification.channels()[0].intensityRange()->unit() ==
              IlluminationIntensityUnit::Percent &&
          specification.channels()[1].controlKind() ==
              IlluminationControlKind::DeviceDefined,
          "preserves per-channel intensity control semantics and units");

    auto noChannels = IlluminationSpecification::create(
        componentId, IlluminationSourceTechnology::Led, {});
    check(!noChannels &&
          noChannels.error() == IlluminationSpecificationError::NoChannels,
          "requires at least one illumination channel");

    auto invalidTechnology = IlluminationSpecification::create(
        componentId,
        static_cast<IlluminationSourceTechnology>(255),
        {makeChannel(
            "one-channel",
            std::nullopt,
            IlluminationControlKind::Unknown,
            std::nullopt)});
    check(!invalidTechnology &&
          invalidTechnology.error() ==
              IlluminationSpecificationError::InvalidTechnology,
          "rejects an unknown illumination source technology");

    auto duplicateChannels = IlluminationSpecification::create(
        componentId,
        IlluminationSourceTechnology::Led,
        {makeChannel(
             "duplicate-channel",
             std::nullopt,
             IlluminationControlKind::Unknown,
             std::nullopt),
         makeChannel(
             "duplicate-channel",
             std::nullopt,
             IlluminationControlKind::Fixed,
             std::nullopt)});
    check(!duplicateChannels &&
          duplicateChannels.error() ==
              IlluminationSpecificationError::DuplicateChannelId,
          "rejects duplicate channel identities within one source");
}

void testCondenserSpecificationValidation() {
    const auto componentId =
        makeId<EquipmentComponentId>("condenser-achromat-aplanatic");
    const auto specification = makeCondenserSpecification(componentId);

    check(specification.minimumApertureNA() == 0.0 &&
          specification.maximumApertureNA() == 1.40,
          "allows a fully closed minimum aperture and a positive maximum NA");
    check(specification.design() == CondenserDesign::AchromatAplanatic &&
          specification.immersionMedium() == CondenserImmersionMedium::Oil &&
          specification.workingDistance()->value() == 1'000.0,
          "preserves condenser design, immersion medium and working distance");

    auto invertedRange = CondenserSpecification::create(
        componentId,
        nonNegativeNa(1.20),
        na(1.00),
        CondenserDesign::Abbe,
        CondenserImmersionMedium::Air);
    check(!invertedRange &&
          invertedRange.error() ==
              CondenserSpecificationError::InvertedApertureRange,
          "rejects a condenser aperture range with minimum above maximum");

    auto invalidDesign = CondenserSpecification::create(
        componentId,
        nonNegativeNa(0.0),
        na(1.00),
        static_cast<CondenserDesign>(255),
        CondenserImmersionMedium::Air);
    check(!invalidDesign &&
          invalidDesign.error() == CondenserSpecificationError::InvalidDesign,
          "rejects an unknown condenser design");

    auto invalidImmersion = CondenserSpecification::create(
        componentId,
        nonNegativeNa(0.0),
        na(1.00),
        CondenserDesign::Abbe,
        static_cast<CondenserImmersionMedium>(255));
    check(!invalidImmersion &&
          invalidImmersion.error() ==
              CondenserSpecificationError::InvalidImmersionMedium,
          "rejects an unknown condenser immersion medium");
}

void testEquipmentAggregateValidatesCondenserAndIlluminationBindings() {
    const auto microscopeId = makeId<MicroscopeId>("microscope-illumination-condenser");
    const auto condenserId = makeId<EquipmentComponentId>("condenser-component");
    const auto illuminationId = makeId<EquipmentComponentId>("illumination-component");

    const std::vector<EquipmentComponent> components{
        makeOpticalComponent(condenserId),
        makeIlluminationComponent(illuminationId)};

    const auto condenser = makeCondenserSpecification(condenserId);
    const auto illumination = makeIlluminationSpecification(illuminationId);

    auto validDefinition = MicroscopeEquipmentDefinition::create(
        microscopeId,
        components,
        {},
        {},
        {},
        {},
        {condenser},
        {illumination});
    check(static_cast<bool>(validDefinition),
          "accepts condenser and illumination specs attached to matching installed components");
    if (validDefinition) {
        check(validDefinition.value().condenserSpecifications().size() == 1 &&
              validDefinition.value().illuminationSpecifications().size() == 1,
              "equipment definition retains condenser/illumination specifications");
    }

    auto illuminationOnOptic = MicroscopeEquipmentDefinition::create(
        microscopeId,
        components,
        {},
        {},
        {},
        {},
        {},
        {makeIlluminationSpecification(condenserId)});
    check(!illuminationOnOptic &&
          illuminationOnOptic.error() ==
              EquipmentDefinitionError::IlluminationComponentWrongKind,
          "requires illumination specifications to reference an illumination device");

    auto condenserOnSource = MicroscopeEquipmentDefinition::create(
        microscopeId,
        components,
        {},
        {},
        {},
        {},
        {makeCondenserSpecification(illuminationId)},
        {});
    check(!condenserOnSource &&
          condenserOnSource.error() ==
              EquipmentDefinitionError::CondenserComponentWrongKind,
          "requires condenser specifications to reference an optical element");

    auto missingIllumination = MicroscopeEquipmentDefinition::create(
        microscopeId,
        components,
        {},
        {},
        {},
        {},
        {},
        {makeIlluminationSpecification(
            makeId<EquipmentComponentId>("uninstalled-illumination"))});
    check(!missingIllumination &&
          missingIllumination.error() ==
              EquipmentDefinitionError::IlluminationReferencesUnknownComponent,
          "rejects an illumination specification for an uninstalled component");

    auto duplicateCondenser = MicroscopeEquipmentDefinition::create(
        microscopeId,
        components,
        {},
        {},
        {},
        {},
        {condenser, condenser},
        {});
    check(!duplicateCondenser &&
          duplicateCondenser.error() ==
              EquipmentDefinitionError::DuplicateCondenserSpecification,
          "rejects duplicate condenser specifications for one component");

    auto duplicateIllumination = MicroscopeEquipmentDefinition::create(
        microscopeId,
        components,
        {},
        {},
        {},
        {},
        {},
        {illumination, illumination});
    check(!duplicateIllumination &&
          duplicateIllumination.error() ==
              EquipmentDefinitionError::DuplicateIlluminationSpecification,
          "rejects duplicate illumination specifications for one component");
}

} // namespace

int main() {
    testSpectralBandAndIntensityRangeValidation();
    testIlluminationChannelControlSemantics();
    testMultiChannelIlluminationSpecification();
    testCondenserSpecificationValidation();
    testEquipmentAggregateValidatesCondenserAndIlluminationBindings();

    if (failures != 0) {
        std::cerr << failures << " illumination/condenser contract test(s) failed.\n";
        return 1;
    }

    std::cout << "All illumination/condenser contract tests passed.\n";
    return 0;
}
