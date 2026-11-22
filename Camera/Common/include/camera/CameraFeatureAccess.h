#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace camera {

// Backend-neutral feature identifier for selecting RAW output precision.
// The feature is an Integer with discreteIntegerValues containing exact
// significant-bit counts (for example 8, 10, 12). Each backend publishes only
// the discrete values it can actually configure. A relative mode such as
// "maximum supported" is not a feature value; storage-container width is not
// significant sensor/output precision.
inline constexpr std::string_view RawOutputBitDepthFeature =
    "RawOutputBitDepth";


enum class CameraFeatureKind : std::uint8_t {
    Boolean,
    Integer,
    IntegerVector,
    FloatingPoint,
    String,
    Enumeration
};

// An enumeration symbol is not an ordinary string value.
struct FeatureEnumeration {
    std::string symbol;
};

using CameraFeatureValue = std::variant<
    bool,
    std::int64_t,
    std::vector<std::int64_t>,
    double,
    std::string,
    FeatureEnumeration>;

struct CameraFeatureNumericRange {
    double minimum = 0.0;
    double maximum = 0.0;
    std::optional<double> increment;
};

enum class CameraFeatureGroup : std::uint8_t {
    General,
    Exposure,
    ImageFormat,
    ImageProcessing,
    Color,
    Trigger,
    Sensor,
    Transport,
    Hardware
};

struct CameraFeatureDescriptor {
    std::string name;
    std::string displayName;
    CameraFeatureKind kind = CameraFeatureKind::String;
    std::optional<std::string> unit;
    bool isAvailable = false;
    bool isReadable = false;
    bool isWritable = false;
    std::optional<CameraFeatureNumericRange> numericRange;
    std::optional<std::size_t> elementCount;
    std::vector<std::string> enumerationValues;
    CameraFeatureGroup group = CameraFeatureGroup::General;
    // Non-empty only for Integer features whose values are a discrete set.
    // An editor should present these values as choices, not a free-form field.
    std::vector<std::int64_t> discreteIntegerValues;
};

struct CameraFeatureAccessError {
    enum class Code : std::uint8_t {
        Unsupported,
        FeatureNotFound,
        FeatureUnavailable,
        FeatureNotReadable,
        FeatureNotWritable,
        ValueTypeMismatch,
        InvalidValue,
        BackendFailure
    };

    Code code = Code::BackendFailure;
    std::string feature;
    std::string message;
    std::int32_t nativeCode = 0;
    std::optional<CameraFeatureKind> expectedKind;
    std::optional<CameraFeatureKind> actualKind;
};

template <typename T>
using CameraFeatureResult = std::variant<T, CameraFeatureAccessError>;

class ICameraFeatureAccess {
public:
    virtual ~ICameraFeatureAccess() = default;

    virtual CameraFeatureResult<std::vector<CameraFeatureDescriptor>>
    featureDescriptors() = 0;

    // name is consumed synchronously. Implementations must copy it before
    // queueing any asynchronous work.
    virtual CameraFeatureResult<CameraFeatureValue>
    readFeature(std::string_view name) = 0;

    virtual CameraFeatureResult<std::monostate>
    writeFeature(std::string_view name, const CameraFeatureValue& value) = 0;
};

} // namespace camera
