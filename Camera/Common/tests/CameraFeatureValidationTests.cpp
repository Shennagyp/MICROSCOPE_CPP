#include "camera/CameraFeatureValidation.h"

#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* name) {
    if (!condition) {
        std::cerr << "FAILED: " << name << '\n';
        ++failures;
    }
}

bool accepted(const camera::CameraFeatureResult<std::monostate>& result) {
    return std::holds_alternative<std::monostate>(result);
}

const camera::CameraFeatureAccessError* rejected(
    const camera::CameraFeatureResult<std::monostate>& result) {
    return std::get_if<camera::CameraFeatureAccessError>(&result);
}

} // namespace

int main() {
    using namespace camera;

    CameraFeatureDescriptor fps{
        "FrameRate", "Frame Rate", CameraFeatureKind::FloatingPoint,
        std::string("fps"), true, true, true,
        CameraFeatureNumericRange{CameraFloatingPointRange{1.0, 120.0, 0.1}},
        std::nullopt, {}
    };

    check(accepted(validateFeatureWrite(fps, CameraFeatureValue{30.0})),
          "accepts aligned floating-point value");
    check(accepted(validateFeatureWrite(fps, CameraFeatureValue{30.1})),
          "accepts upper increment neighbor");

    auto fractional = validateFeatureWrite(fps, CameraFeatureValue{30.15});
    check(rejected(fractional) != nullptr &&
          rejected(fractional)->code == CameraFeatureAccessError::Code::InvalidValue,
          "rejects value not aligned to increment");

    auto outOfRange = validateFeatureWrite(fps, CameraFeatureValue{120.1});
    check(rejected(outOfRange) != nullptr &&
          rejected(outOfRange)->code == CameraFeatureAccessError::Code::InvalidValue,
          "rejects value above maximum");

    auto notFinite = validateFeatureWrite(
        fps, CameraFeatureValue{std::numeric_limits<double>::quiet_NaN()});
    check(rejected(notFinite) != nullptr &&
          rejected(notFinite)->code == CameraFeatureAccessError::Code::InvalidValue,
          "rejects NaN");

    CameraFeatureDescriptor exposure{
        "ExposureTime", "Exposure Time", CameraFeatureKind::Integer,
        std::string("us"), true, true, true,
        CameraFeatureNumericRange{CameraIntegerRange{10, 100000, std::nullopt}},
        std::nullopt, {}
    };
    check(accepted(validateFeatureWrite(exposure, CameraFeatureValue{std::int64_t{2500}})),
          "accepts integer within range");

    // Integer feature ranges must remain exact beyond IEEE-754's 53-bit
    // integer precision boundary. A double-backed range can collapse adjacent
    // int64 values and incorrectly accept an off-increment value.
    constexpr std::int64_t aboveDoubleIntegerPrecision = 9007199254740993LL;
    CameraFeatureDescriptor exactInteger{
        "ExactInteger", "Exact Integer", CameraFeatureKind::Integer,
        std::nullopt, true, true, true,
        CameraFeatureNumericRange{CameraIntegerRange{
            aboveDoubleIntegerPrecision,
            aboveDoubleIntegerPrecision + 10,
            std::int64_t{2}}},
        std::nullopt, {}
    };
    check(accepted(validateFeatureWrite(
              exactInteger,
              CameraFeatureValue{aboveDoubleIntegerPrecision + 2})),
          "accepts an exactly representable int64 value on the advertised increment");
    auto adjacentLargeInteger = validateFeatureWrite(
        exactInteger,
        CameraFeatureValue{aboveDoubleIntegerPrecision + 1});
    check(rejected(adjacentLargeInteger) != nullptr &&
          rejected(adjacentLargeInteger)->code ==
              CameraFeatureAccessError::Code::InvalidValue,
          "rejects an adjacent int64 value without rounding it through double");

    auto wrongIntegerRangeType = exactInteger;
    wrongIntegerRangeType.numericRange = CameraFeatureNumericRange{
        CameraFloatingPointRange{0.0, 10.0, std::nullopt}};
    auto wrongIntegerRangeResult = validateFeatureWrite(
        wrongIntegerRangeType,
        CameraFeatureValue{aboveDoubleIntegerPrecision + 2});
    check(rejected(wrongIntegerRangeResult) != nullptr &&
          rejected(wrongIntegerRangeResult)->code ==
              CameraFeatureAccessError::Code::BackendFailure,
          "rejects a floating-point range attached to an integer feature");

    auto invalidIntegerIncrement = exactInteger;
    invalidIntegerIncrement.numericRange = CameraFeatureNumericRange{
        CameraIntegerRange{0, 10, std::int64_t{0}}};
    auto invalidIntegerIncrementResult = validateFeatureWrite(
        invalidIntegerIncrement, CameraFeatureValue{std::int64_t{2}});
    check(rejected(invalidIntegerIncrementResult) != nullptr &&
          rejected(invalidIntegerIncrementResult)->code ==
              CameraFeatureAccessError::Code::BackendFailure,
          "rejects a non-positive integer increment as an invalid descriptor");

    CameraFeatureDescriptor fullDomainInteger{
        "FullDomainInteger", "Full Domain Integer", CameraFeatureKind::Integer,
        std::nullopt, true, true, true,
        CameraFeatureNumericRange{CameraIntegerRange{
            std::numeric_limits<std::int64_t>::min(),
            std::numeric_limits<std::int64_t>::max(),
            std::int64_t{1}}},
        std::nullopt, {}
    };
    check(accepted(validateFeatureWrite(
              fullDomainInteger,
              CameraFeatureValue{std::numeric_limits<std::int64_t>::max()})),
          "validates integer increments across the full signed 64-bit range without overflow");

    CameraFeatureDescriptor sensorBitDepth{
        "SensorBitDepth", "Sensor Bit Depth", CameraFeatureKind::Integer,
        std::string("significant bits"), true, true, true,
        std::nullopt, std::nullopt, {}, CameraFeatureGroup::ImageFormat,
        {8, 10, 12, 14, 16}
    };
    check(accepted(validateFeatureWrite(
              sensorBitDepth, CameraFeatureValue{std::int64_t{12}})),
          "accepts an advertised discrete integer value");
    auto unsupportedBitDepth = validateFeatureWrite(
        sensorBitDepth, CameraFeatureValue{std::int64_t{11}});
    check(rejected(unsupportedBitDepth) != nullptr &&
          rejected(unsupportedBitDepth)->code == CameraFeatureAccessError::Code::InvalidValue,
          "rejects an integer not present in the discrete capability set");

    auto invalidDiscreteDescriptor = sensorBitDepth;
    invalidDiscreteDescriptor.kind = CameraFeatureKind::Enumeration;
    auto invalidDiscreteResult = validateFeatureWrite(
        invalidDiscreteDescriptor,
        CameraFeatureValue{FeatureEnumeration{"12"}});
    check(rejected(invalidDiscreteResult) != nullptr &&
          rejected(invalidDiscreteResult)->code == CameraFeatureAccessError::Code::BackendFailure,
          "rejects discrete integer values attached to a non-integer feature");

    auto wrongType = validateFeatureWrite(exposure, CameraFeatureValue{2500.0});
    check(rejected(wrongType) != nullptr &&
          rejected(wrongType)->code == CameraFeatureAccessError::Code::ValueTypeMismatch &&
          rejected(wrongType)->expectedKind == CameraFeatureKind::Integer &&
          rejected(wrongType)->actualKind == CameraFeatureKind::FloatingPoint,
          "preserves expected and actual kinds");

    CameraFeatureDescriptor autoMode{
        "ExposureAuto", "Automatic Exposure", CameraFeatureKind::Enumeration,
        std::nullopt, true, true, true, std::nullopt, std::nullopt,
        {"Off", "Once", "Continuous"}
    };
    check(accepted(validateFeatureWrite(
              autoMode, CameraFeatureValue{FeatureEnumeration{"Once"}})),
          "accepts enumerated symbol");
    auto badSymbol = validateFeatureWrite(
        autoMode, CameraFeatureValue{FeatureEnumeration{"Always"}});
    check(rejected(badSymbol) != nullptr &&
          rejected(badSymbol)->code == CameraFeatureAccessError::Code::InvalidValue,
          "rejects unknown enumeration symbol");

    CameraFeatureDescriptor rgb{
        "WhiteBalanceRGB", "White Balance RGB", CameraFeatureKind::IntegerVector,
        std::nullopt, true, true, true, std::nullopt, std::size_t{3}, {}
    };
    check(accepted(validateFeatureWrite(
              rgb, CameraFeatureValue{std::vector<std::int64_t>{1, 2, 3}})),
          "accepts vector with required cardinality");
    auto shortVector = validateFeatureWrite(
        rgb, CameraFeatureValue{std::vector<std::int64_t>{1, 2}});
    check(rejected(shortVector) != nullptr &&
          rejected(shortVector)->code == CameraFeatureAccessError::Code::InvalidValue,
          "rejects vector with wrong cardinality");

    auto unavailable = exposure;
    unavailable.isAvailable = false;
    auto unavailableResult = validateFeatureWrite(unavailable, CameraFeatureValue{std::int64_t{20}});
    check(rejected(unavailableResult) != nullptr &&
          rejected(unavailableResult)->code == CameraFeatureAccessError::Code::FeatureUnavailable,
          "reports unavailable before writability");

    auto readOnly = exposure;
    readOnly.isWritable = false;
    auto readOnlyResult = validateFeatureWrite(readOnly, CameraFeatureValue{std::int64_t{20}});
    check(rejected(readOnlyResult) != nullptr &&
          rejected(readOnlyResult)->code == CameraFeatureAccessError::Code::FeatureNotWritable,
          "rejects read-only feature");

    auto invalidRange = exposure;
    invalidRange.numericRange = CameraFeatureNumericRange{CameraIntegerRange{100, 1, std::nullopt}};
    auto invalidRangeResult = validateFeatureWrite(
        invalidRange, CameraFeatureValue{std::int64_t{20}});
    check(rejected(invalidRangeResult) != nullptr &&
          rejected(invalidRangeResult)->code == CameraFeatureAccessError::Code::BackendFailure,
          "detects invalid backend range");

    auto invalidVectorDescriptor = rgb;
    invalidVectorDescriptor.elementCount.reset();
    invalidVectorDescriptor.numericRange = CameraFeatureNumericRange{CameraFloatingPointRange{0.0, 10.0, std::nullopt}};
    auto invalidDescriptorResult = validateFeatureWrite(
        invalidVectorDescriptor, CameraFeatureValue{std::vector<std::int64_t>{1, 2, 3}});
    check(rejected(invalidDescriptorResult) != nullptr &&
          rejected(invalidDescriptorResult)->code == CameraFeatureAccessError::Code::BackendFailure,
          "detects numeric range on non-scalar descriptor");

    if (failures != 0) {
        std::cerr << failures << " camera feature validation test(s) failed.\n";
        return 1;
    }

    std::cout << "All camera feature validation tests passed.\n";
    return 0;
}
