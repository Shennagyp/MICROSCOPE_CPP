#include "microscope/application/configuration/EquipmentDefinitionJsonLoader.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <iomanip>
#include <locale>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace microscope::application::configuration {
namespace {

using Error = EquipmentDefinitionJsonError;
using ErrorCode = EquipmentDefinitionJsonErrorCode;

constexpr std::size_t kMaximumJsonDepth = 64;
constexpr std::size_t kMaximumJsonNodes = 500000;
constexpr std::size_t kMaximumArrayElements = 50000;
constexpr std::size_t kMaximumObjectMembers = 128;
constexpr std::size_t kMaximumStringBytes = 1024U * 1024U;

struct JsonValue final {
    enum class Kind { Null, Boolean, Number, String, Array, Object };

    Kind kind = Kind::Null;
    bool boolean = false;
    std::string text;
    std::vector<JsonValue> array;
    std::map<std::string, JsonValue, std::less<>> object;
};

void setError(
    Error& error,
    ErrorCode code,
    std::string path,
    std::string detail) {
    error.code = code;
    error.offset = 0;
    error.line = 1;
    error.column = 1;
    error.path = std::move(path);
    error.detail = std::move(detail);
}

bool validUtf8(std::string_view value) noexcept {
    std::size_t i = 0;
    while (i < value.size()) {
        const auto lead = static_cast<unsigned char>(value[i]);
        if (lead <= 0x7fU) {
            ++i;
            continue;
        }
        if (lead >= 0xc2U && lead <= 0xdfU) {
            if (i + 1 >= value.size()) return false;
            const auto b1 = static_cast<unsigned char>(value[i + 1]);
            if ((b1 & 0xc0U) != 0x80U) return false;
            i += 2;
            continue;
        }
        if (lead >= 0xe0U && lead <= 0xefU) {
            if (i + 2 >= value.size()) return false;
            const auto b1 = static_cast<unsigned char>(value[i + 1]);
            const auto b2 = static_cast<unsigned char>(value[i + 2]);
            if ((b1 & 0xc0U) != 0x80U || (b2 & 0xc0U) != 0x80U) return false;
            if (lead == 0xe0U && b1 < 0xa0U) return false;
            if (lead == 0xedU && b1 >= 0xa0U) return false;
            i += 3;
            continue;
        }
        if (lead >= 0xf0U && lead <= 0xf4U) {
            if (i + 3 >= value.size()) return false;
            const auto b1 = static_cast<unsigned char>(value[i + 1]);
            const auto b2 = static_cast<unsigned char>(value[i + 2]);
            const auto b3 = static_cast<unsigned char>(value[i + 3]);
            if ((b1 & 0xc0U) != 0x80U ||
                (b2 & 0xc0U) != 0x80U ||
                (b3 & 0xc0U) != 0x80U) return false;
            if (lead == 0xf0U && b1 < 0x90U) return false;
            if (lead == 0xf4U && b1 >= 0x90U) return false;
            i += 4;
            continue;
        }
        return false;
    }
    return true;
}

void appendUtf8(std::uint32_t codePoint, std::string& output) {
    if (codePoint <= 0x7fU) {
        output.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7ffU) {
        output.push_back(static_cast<char>(0xc0U | (codePoint >> 6U)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    } else if (codePoint <= 0xffffU) {
        output.push_back(static_cast<char>(0xe0U | (codePoint >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    } else {
        output.push_back(static_cast<char>(0xf0U | (codePoint >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    }
}

class JsonParser final {
public:
    JsonParser(std::string_view input, Error& error)
        : input_(input), error_(error) {}

    bool parse(JsonValue& output) {
        skipWhitespace();
        if (position_ == input_.size()) return syntax("The JSON document is empty.");
        if (!parseValue(output, 0)) return false;
        skipWhitespace();
        if (position_ != input_.size()) return syntax("Unexpected characters after the JSON value.");
        return true;
    }

private:
    bool syntax(std::string detail) {
        error_.code = ErrorCode::JsonSyntaxError;
        error_.offset = position_;
        error_.path = "$";
        error_.detail = std::move(detail);
        error_.line = 1;
        error_.column = 1;
        for (std::size_t i = 0; i < position_ && i < input_.size(); ++i) {
            if (input_[i] == '\n') {
                ++error_.line;
                error_.column = 1;
            } else {
                ++error_.column;
            }
        }
        return false;
    }

    void skipWhitespace() noexcept {
        while (position_ < input_.size()) {
            const char c = input_[position_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++position_;
            } else {
                break;
            }
        }
    }

    bool parseValue(JsonValue& output, std::size_t depth) {
        if (depth > kMaximumJsonDepth) return syntax("The JSON nesting depth limit was exceeded.");
        if (++nodeCount_ > kMaximumJsonNodes) return syntax("The JSON node-count limit was exceeded.");
        skipWhitespace();
        if (position_ >= input_.size()) return syntax("Unexpected end of JSON document.");

        const char c = input_[position_];
        if (c == 'n') return parseLiteral("null", JsonValue::Kind::Null, output);
        if (c == 't') return parseLiteral("true", JsonValue::Kind::Boolean, output, true);
        if (c == 'f') return parseLiteral("false", JsonValue::Kind::Boolean, output, false);
        if (c == '"') {
            output = JsonValue{};
            output.kind = JsonValue::Kind::String;
            return parseString(output.text);
        }
        if (c == '[') return parseArray(output, depth + 1);
        if (c == '{') return parseObject(output, depth + 1);
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber(output);
        return syntax("Expected a JSON value.");
    }

    bool parseLiteral(
        std::string_view literal,
        JsonValue::Kind kind,
        JsonValue& output,
        bool booleanValue = false) {
        if (input_.substr(position_, literal.size()) != literal) {
            return syntax("Invalid JSON literal.");
        }
        position_ += literal.size();
        output = JsonValue{};
        output.kind = kind;
        output.boolean = booleanValue;
        return true;
    }

    bool parseArray(JsonValue& output, std::size_t depth) {
        ++position_; // [
        output = JsonValue{};
        output.kind = JsonValue::Kind::Array;
        skipWhitespace();
        if (position_ < input_.size() && input_[position_] == ']') {
            ++position_;
            return true;
        }

        for (;;) {
            if (output.array.size() >= kMaximumArrayElements) {
                return syntax("A JSON array exceeds the configured element limit.");
            }
            JsonValue item;
            if (!parseValue(item, depth)) return false;
            output.array.push_back(std::move(item));
            skipWhitespace();
            if (position_ >= input_.size()) return syntax("Unterminated JSON array.");
            if (input_[position_] == ']') {
                ++position_;
                return true;
            }
            if (input_[position_] != ',') return syntax("Expected ',' or ']' in JSON array.");
            ++position_;
            skipWhitespace();
        }
    }

    bool parseObject(JsonValue& output, std::size_t depth) {
        ++position_; // {
        output = JsonValue{};
        output.kind = JsonValue::Kind::Object;
        skipWhitespace();
        if (position_ < input_.size() && input_[position_] == '}') {
            ++position_;
            return true;
        }

        for (;;) {
            if (output.object.size() >= kMaximumObjectMembers) {
                return syntax("A JSON object exceeds the configured member limit.");
            }
            if (position_ >= input_.size() || input_[position_] != '"') {
                return syntax("Expected a quoted JSON object key.");
            }
            std::string key;
            if (!parseString(key)) return false;
            skipWhitespace();
            if (position_ >= input_.size() || input_[position_] != ':') {
                return syntax("Expected ':' after JSON object key.");
            }
            ++position_;
            JsonValue value;
            if (!parseValue(value, depth)) return false;
            if (!output.object.emplace(std::move(key), std::move(value)).second) {
                return syntax("Duplicate JSON object keys are not permitted.");
            }
            skipWhitespace();
            if (position_ >= input_.size()) return syntax("Unterminated JSON object.");
            if (input_[position_] == '}') {
                ++position_;
                return true;
            }
            if (input_[position_] != ',') return syntax("Expected ',' or '}' in JSON object.");
            ++position_;
            skipWhitespace();
        }
    }

    bool parseHexQuad(std::uint32_t& value) {
        if (input_.size() - position_ < 4) return syntax("Incomplete JSON Unicode escape.");
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = input_[position_++];
            value <<= 4U;
            if (c >= '0' && c <= '9') value |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<std::uint32_t>(c - 'A' + 10);
            else return syntax("Invalid hexadecimal digit in JSON Unicode escape.");
        }
        return true;
    }

    bool parseString(std::string& output) {
        if (position_ >= input_.size() || input_[position_] != '"') {
            return syntax("Expected a JSON string.");
        }
        ++position_;
        output.clear();
        while (position_ < input_.size()) {
            const unsigned char c = static_cast<unsigned char>(input_[position_++]);
            if (c == '"') {
                if (output.size() > kMaximumStringBytes || !validUtf8(output)) {
                    return syntax("A JSON string is oversized or contains invalid UTF-8.");
                }
                return true;
            }
            if (c < 0x20U) return syntax("Unescaped control character in JSON string.");
            if (c != '\\') {
                output.push_back(static_cast<char>(c));
            } else {
                if (position_ >= input_.size()) return syntax("Incomplete JSON string escape.");
                const char escaped = input_[position_++];
                switch (escaped) {
                case '"': output.push_back('"'); break;
                case '\\': output.push_back('\\'); break;
                case '/': output.push_back('/'); break;
                case 'b': output.push_back('\b'); break;
                case 'f': output.push_back('\f'); break;
                case 'n': output.push_back('\n'); break;
                case 'r': output.push_back('\r'); break;
                case 't': output.push_back('\t'); break;
                case 'u': {
                    std::uint32_t first = 0;
                    if (!parseHexQuad(first)) return false;
                    std::uint32_t codePoint = first;
                    if (first >= 0xd800U && first <= 0xdbffU) {
                        if (input_.size() - position_ < 6 ||
                            input_[position_] != '\\' || input_[position_ + 1] != 'u') {
                            return syntax("A high surrogate must be followed by a low surrogate.");
                        }
                        position_ += 2;
                        std::uint32_t second = 0;
                        if (!parseHexQuad(second)) return false;
                        if (second < 0xdc00U || second > 0xdfffU) {
                            return syntax("Invalid low surrogate in JSON Unicode escape.");
                        }
                        codePoint = 0x10000U +
                            ((first - 0xd800U) << 10U) + (second - 0xdc00U);
                    } else if (first >= 0xdc00U && first <= 0xdfffU) {
                        return syntax("A low surrogate cannot appear without a high surrogate.");
                    }
                    appendUtf8(codePoint, output);
                    break;
                }
                default: return syntax("Unknown JSON string escape.");
                }
            }
            if (output.size() > kMaximumStringBytes) {
                return syntax("A JSON string exceeds the configured byte limit.");
            }
        }
        return syntax("Unterminated JSON string.");
    }

    bool parseNumber(JsonValue& output) {
        const std::size_t start = position_;
        if (input_[position_] == '-') ++position_;
        if (position_ >= input_.size()) return syntax("Incomplete JSON number.");
        if (input_[position_] == '0') {
            ++position_;
            if (position_ < input_.size() &&
                input_[position_] >= '0' && input_[position_] <= '9') {
                return syntax("Leading zeroes are not allowed in JSON numbers.");
            }
        } else if (input_[position_] >= '1' && input_[position_] <= '9') {
            while (position_ < input_.size() &&
                input_[position_] >= '0' && input_[position_] <= '9') ++position_;
        } else {
            return syntax("Invalid integer part in JSON number.");
        }

        if (position_ < input_.size() && input_[position_] == '.') {
            ++position_;
            const auto fractionStart = position_;
            while (position_ < input_.size() &&
                input_[position_] >= '0' && input_[position_] <= '9') ++position_;
            if (fractionStart == position_) return syntax("JSON fraction requires at least one digit.");
        }

        if (position_ < input_.size() &&
            (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() &&
                (input_[position_] == '+' || input_[position_] == '-')) ++position_;
            const auto exponentStart = position_;
            while (position_ < input_.size() &&
                input_[position_] >= '0' && input_[position_] <= '9') ++position_;
            if (exponentStart == position_) return syntax("JSON exponent requires at least one digit.");
        }

        output = JsonValue{};
        output.kind = JsonValue::Kind::Number;
        output.text.assign(input_.substr(start, position_ - start));
        return true;
    }

    std::string_view input_;
    Error& error_;
    std::size_t position_ = 0;
    std::size_t nodeCount_ = 0;
};

using JsonObject = std::map<std::string, JsonValue, std::less<>>;

bool fail(
    Error& error,
    ErrorCode code,
    const std::string& path,
    const std::string& detail) {
    setError(error, code, path, detail);
    return false;
}

bool objectValue(
    const JsonValue& value,
    const std::string& path,
    const JsonObject*& output,
    Error& error) {
    if (value.kind != JsonValue::Kind::Object) {
        return fail(error, ErrorCode::WrongType, path, "Expected a JSON object.");
    }
    output = &value.object;
    return true;
}

bool allowedKeys(
    const JsonObject& object,
    std::initializer_list<std::string_view> allowed,
    const std::string& path,
    Error& error) {
    for (const auto& item : object) {
        const bool known = std::find(allowed.begin(), allowed.end(), item.first) != allowed.end();
        if (!known) return fail(error, ErrorCode::UnknownField,
            path + "." + item.first, "Unknown field for the declared schema version.");
    }
    return true;
}

const JsonValue* find(
    const JsonObject& object,
    std::string_view key) {
    const auto it = object.find(key);
    return it == object.end() ? nullptr : &it->second;
}

bool required(
    const JsonObject& object,
    const char* key,
    const std::string& path,
    const JsonValue*& output,
    Error& error) {
    output = find(object, key);
    if (output == nullptr) {
        return fail(error, ErrorCode::MissingField,
            path + "." + key, "Required field is missing.");
    }
    return true;
}

bool stringValue(
    const JsonValue& value,
    const std::string& path,
    std::string& output,
    Error& error) {
    if (value.kind != JsonValue::Kind::String) {
        return fail(error, ErrorCode::WrongType, path, "Expected a JSON string.");
    }
    output = value.text;
    return true;
}

bool numberValue(
    const JsonValue& value,
    const std::string& path,
    double& output,
    Error& error) {
    if (value.kind != JsonValue::Kind::Number) {
        return fail(error, ErrorCode::WrongType, path, "Expected a JSON number.");
    }
    // Floating std::from_chars is not consistently available for older macOS
    // deployment targets. A classic-locale stream preserves JSON's decimal
    // syntax without consulting or mutating the process-global C locale.
    std::istringstream numberStream(value.text);
    numberStream.imbue(std::locale::classic());
    numberStream >> std::noskipws >> output;
    if (numberStream.fail() || !numberStream.eof() || !std::isfinite(output)) {
        return fail(error, ErrorCode::InvalidValue, path,
            "Number is not representable as a finite double.");
    }
    return true;
}

bool unsignedValue(
    const JsonValue& value,
    const std::string& path,
    std::uint64_t& output,
    Error& error) {
    if (value.kind != JsonValue::Kind::Number) {
        return fail(error, ErrorCode::WrongType, path, "Expected an unsigned integer.");
    }
    if (value.text.empty() || value.text.front() == '-' ||
        value.text.find_first_of(".eE") != std::string::npos) {
        return fail(error, ErrorCode::InvalidValue, path,
            "Expected a non-negative integer without a fraction or exponent.");
    }
    const auto result = std::from_chars(
        value.text.data(), value.text.data() + value.text.size(), output);
    if (result.ec != std::errc{} ||
        result.ptr != value.text.data() + value.text.size()) {
        return fail(error, ErrorCode::InvalidValue, path,
            "Integer is outside the supported unsigned range.");
    }
    return true;
}

bool booleanValue(
    const JsonValue& value,
    const std::string& path,
    bool& output,
    Error& error) {
    if (value.kind != JsonValue::Kind::Boolean) {
        return fail(error, ErrorCode::WrongType, path, "Expected a JSON boolean.");
    }
    output = value.boolean;
    return true;
}

template <typename Enum>
bool enumValue(
    const JsonValue& value,
    const std::string& path,
    std::initializer_list<std::pair<std::string_view, Enum>> choices,
    Enum& output,
    Error& error) {
    if (value.kind != JsonValue::Kind::String) {
        return fail(error, ErrorCode::WrongType, path, "Expected an enum name string.");
    }
    for (const auto& choice : choices) {
        if (value.text == choice.first) {
            output = choice.second;
            return true;
        }
    }
    return fail(error, ErrorCode::InvalidValue, path,
        "Unknown enum name for schema version 1: '" + value.text + "'.");
}

template <typename Id>
bool idValue(
    const JsonValue& value,
    const std::string& path,
    std::optional<Id>& output,
    Error& error) {
    std::string raw;
    if (!stringValue(value, path, raw, error)) return false;
    auto created = Id::create(std::move(raw));
    if (!created) {
        return fail(error, ErrorCode::InvalidValue, path,
            "Identifier is empty, whitespace-only or contains an embedded null.");
    }
    output = std::move(created).value();
    return true;
}

template <typename Id>
bool requiredId(
    const JsonObject& object,
    const char* key,
    const std::string& path,
    std::optional<Id>& output,
    Error& error) {
    const JsonValue* value = nullptr;
    if (!required(object, key, path, value, error)) return false;
    return idValue(*value, path + "." + key, output, error);
}

template <typename Id>
bool optionalId(
    const JsonObject& object,
    const char* key,
    const std::string& path,
    std::optional<Id>& output,
    Error& error) {
    const JsonValue* value = find(object, key);
    if (value == nullptr || value->kind == JsonValue::Kind::Null) {
        output.reset();
        return true;
    }
    return idValue(*value, path + "." + key, output, error);
}

bool optionalString(
    const JsonObject& object,
    const char* key,
    const std::string& path,
    std::optional<std::string>& output,
    Error& error) {
    const JsonValue* value = find(object, key);
    if (value == nullptr || value->kind == JsonValue::Kind::Null) {
        output.reset();
        return true;
    }
    std::string parsed;
    if (!stringValue(*value, path + "." + key, parsed, error)) return false;
    output = std::move(parsed);
    return true;
}

bool optionalNumber(
    const JsonObject& object,
    const char* key,
    const std::string& path,
    std::optional<double>& output,
    Error& error) {
    const JsonValue* value = find(object, key);
    if (value == nullptr || value->kind == JsonValue::Kind::Null) {
        output.reset();
        return true;
    }
    double parsed = 0.0;
    if (!numberValue(*value, path + "." + key, parsed, error)) return false;
    output = parsed;
    return true;
}

template <typename Tag>
bool positiveNumber(
    const JsonValue& value,
    const std::string& path,
    std::optional<core::PositiveFiniteValue<Tag>>& output,
    Error& error) {
    double number = 0.0;
    if (!numberValue(value, path, number, error)) return false;
    auto created = core::PositiveFiniteValue<Tag>::create(number);
    if (!created) return fail(error, ErrorCode::InvalidValue, path,
        "Value must be finite and greater than zero.");
    output = std::move(created).value();
    return true;
}

template <typename Tag>
bool nonNegativeNumber(
    const JsonValue& value,
    const std::string& path,
    std::optional<core::NonNegativeFiniteValue<Tag>>& output,
    Error& error) {
    double number = 0.0;
    if (!numberValue(value, path, number, error)) return false;
    auto created = core::NonNegativeFiniteValue<Tag>::create(number);
    if (!created) return fail(error, ErrorCode::InvalidValue, path,
        "Value must be finite and non-negative.");
    output = std::move(created).value();
    return true;
}

bool requiredNumber(
    const JsonObject& object,
    const char* key,
    const std::string& path,
    double& output,
    Error& error) {
    const JsonValue* value = nullptr;
    return required(object, key, path, value, error) &&
        numberValue(*value, path + "." + key, output, error);
}

bool requiredUInt32(
    const JsonObject& object,
    const char* key,
    const std::string& path,
    std::uint32_t& output,
    Error& error) {
    const JsonValue* value = nullptr;
    std::uint64_t parsed = 0;
    if (!required(object, key, path, value, error) ||
        !unsignedValue(*value, path + "." + key, parsed, error)) return false;
    if (parsed > std::numeric_limits<std::uint32_t>::max()) {
        return fail(error, ErrorCode::InvalidValue, path + "." + key,
            "Integer exceeds the 32-bit unsigned range.");
    }
    output = static_cast<std::uint32_t>(parsed);
    return true;
}

bool optionalObject(
    const JsonObject& object,
    const char* key,
    const std::string& path,
    const JsonObject*& output,
    Error& error,
    bool& present) {
    const JsonValue* value = find(object, key);
    present = value != nullptr && value->kind != JsonValue::Kind::Null;
    if (!present) {
        output = nullptr;
        return true;
    }
    return objectValue(*value, path + "." + key, output, error);
}

bool optionalArray(
    const JsonObject& object,
    const char* key,
    const std::string& path,
    const JsonValue*& output,
    Error& error) {
    output = find(object, key);
    if (output == nullptr || output->kind == JsonValue::Kind::Null) return true;
    if (output->kind != JsonValue::Kind::Array) {
        return fail(error, ErrorCode::WrongType, path + "." + key,
            "Expected a JSON array or null.");
    }
    if (output->array.size() > kMaximumArrayElements) {
        return fail(error, ErrorCode::InvalidValue, path + "." + key,
            "Array exceeds the configured element limit.");
    }
    return true;
}

bool requiredArray(
    const JsonObject& object,
    const char* key,
    const std::string& path,
    const JsonValue*& output,
    Error& error) {
    if (!required(object, key, path, output, error)) return false;
    if (output->kind != JsonValue::Kind::Array) {
        return fail(error, ErrorCode::WrongType, path + "." + key,
            "Expected a JSON array.");
    }
    if (output->array.size() > kMaximumArrayElements) {
        return fail(error, ErrorCode::InvalidValue, path + "." + key,
            "Array exceeds the configured element limit.");
    }
    return true;
}

template <typename T, typename Parser>
bool parseArray(
    const JsonValue& array,
    const std::string& path,
    std::vector<T>& output,
    Error& error,
    Parser&& parseItem) {
    if (array.kind != JsonValue::Kind::Array) {
        return fail(error, ErrorCode::WrongType, path, "Expected a JSON array.");
    }
    if (array.array.size() > kMaximumArrayElements) {
        return fail(error, ErrorCode::InvalidValue, path,
            "Array exceeds the configured element limit.");
    }
    output.clear();
    output.reserve(array.array.size());
    for (std::size_t i = 0; i < array.array.size(); ++i) {
        std::optional<T> item;
        if (!parseItem(array.array[i],
                path + "[" + std::to_string(i) + "]", item, error) || !item) {
            return false;
        }
        output.push_back(std::move(*item));
    }
    return true;
}



bool requiredObject(
    const JsonObject& object,
    const char* key,
    const std::string& path,
    const JsonObject*& output,
    Error& error) {
    const JsonValue* value = nullptr;
    if (!required(object, key, path, value, error)) return false;
    return objectValue(*value, path + "." + key, output, error);
}

template <typename T>
bool positiveValue(
    const JsonValue& value,
    const std::string& path,
    std::optional<T>& output,
    Error& error) {
    double number = 0.0;
    if (!numberValue(value, path, number, error)) return false;
    auto created = T::create(number);
    if (!created) return fail(error, ErrorCode::InvalidValue, path,
        "Value must be finite and greater than zero.");
    output = std::move(created).value();
    return true;
}

template <typename T>
bool nonNegativeValue(
    const JsonValue& value,
    const std::string& path,
    std::optional<T>& output,
    Error& error) {
    double number = 0.0;
    if (!numberValue(value, path, number, error)) return false;
    auto created = T::create(number);
    if (!created) return fail(error, ErrorCode::InvalidValue, path,
        "Value must be finite and non-negative.");
    output = std::move(created).value();
    return true;
}

template <typename T>
bool requiredPositiveField(
    const JsonObject& object,
    const char* key,
    const std::string& path,
    std::optional<T>& output,
    Error& error) {
    const JsonValue* value = nullptr;
    return required(object, key, path, value, error) &&
        positiveValue(*value, path + "." + key, output, error);
}

template <typename T>
bool requiredNonNegativeField(
    const JsonObject& object,
    const char* key,
    const std::string& path,
    std::optional<T>& output,
    Error& error) {
    const JsonValue* value = nullptr;
    return required(object, key, path, value, error) &&
        nonNegativeValue(*value, path + "." + key, output, error);
}

template <typename T>
bool optionalPositiveField(
    const JsonObject& object,
    const char* key,
    const std::string& path,
    std::optional<T>& output,
    Error& error) {
    const JsonValue* value = find(object, key);
    if (value == nullptr || value->kind == JsonValue::Kind::Null) {
        output.reset();
        return true;
    }
    return positiveValue(*value, path + "." + key, output, error);
}

template <typename T>
bool optionalNonNegativeField(
    const JsonObject& object,
    const char* key,
    const std::string& path,
    std::optional<T>& output,
    Error& error) {
    const JsonValue* value = find(object, key);
    if (value == nullptr || value->kind == JsonValue::Kind::Null) {
        output.reset();
        return true;
    }
    return nonNegativeValue(*value, path + "." + key, output, error);
}

template <typename Enum>
bool requiredEnumField(
    const JsonObject& object,
    const char* key,
    const std::string& path,
    std::initializer_list<std::pair<std::string_view, Enum>> choices,
    Enum& output,
    Error& error) {
    const JsonValue* value = nullptr;
    return required(object, key, path, value, error) &&
        enumValue(*value, path + "." + key, choices, output, error);
}

template <typename Enum>
bool optionalEnumField(
    const JsonObject& object,
    const char* key,
    const std::string& path,
    std::initializer_list<std::pair<std::string_view, Enum>> choices,
    std::optional<Enum>& output,
    Error& error) {
    const JsonValue* value = find(object, key);
    if (value == nullptr || value->kind == JsonValue::Kind::Null) {
        output.reset();
        return true;
    }
    Enum parsed{};
    if (!enumValue(*value, path + "." + key, choices, parsed, error)) return false;
    output = parsed;
    return true;
}

template <typename Id>
bool parseIdArrayValue(
    const JsonValue& value,
    const std::string& path,
    std::vector<Id>& output,
    Error& error) {
    if (value.kind != JsonValue::Kind::Array) {
        return fail(error, ErrorCode::WrongType, path, "Expected an array of identifiers.");
    }
    if (value.array.size() > kMaximumArrayElements) {
        return fail(error, ErrorCode::InvalidValue, path,
            "Array exceeds the configured element limit.");
    }
    output.clear();
    output.reserve(value.array.size());
    for (std::size_t i = 0; i < value.array.size(); ++i) {
        std::optional<Id> parsed;
        if (!idValue(value.array[i], path + "[" + std::to_string(i) + "]",
                parsed, error) || !parsed) return false;
        output.push_back(std::move(*parsed));
    }
    return true;
}

template <typename Id>
bool optionalIdArrayField(
    const JsonObject& object,
    const char* key,
    const std::string& path,
    std::vector<Id>& output,
    Error& error) {
    const JsonValue* value = nullptr;
    if (!optionalArray(object, key, path, value, error)) return false;
    if (value == nullptr || value->kind == JsonValue::Kind::Null) {
        output.clear();
        return true;
    }
    return parseIdArrayValue(*value, path + "." + key, output, error);
}

bool parseComponent(
    const JsonValue& value,
    const std::string& path,
    std::optional<core::EquipmentComponent>& output,
    Error& error) {
    const JsonObject* object = nullptr;
    if (!objectValue(value, path, object, error) ||
        !allowedKeys(*object, {"id", "kind", "ports", "opticalFunctions"}, path, error)) {
        return false;
    }

    std::optional<core::EquipmentComponentId> componentId;
    const JsonValue* idNode = nullptr;
    if (!required(*object, "id", path, idNode, error) ||
        !idValue(*idNode, path + ".id", componentId, error) || !componentId) return false;

    core::EquipmentComponentKind kind{};
    if (!requiredEnumField(*object, "kind", path,
            {{"optical-element", core::EquipmentComponentKind::OpticalElement},
             {"imaging-device", core::EquipmentComponentKind::ImagingDevice},
             {"illumination-device", core::EquipmentComponentKind::IlluminationDevice},
             {"mechanical-assembly", core::EquipmentComponentKind::MechanicalAssembly},
             {"other", core::EquipmentComponentKind::Other}},
            kind, error)) return false;

    std::vector<core::OpticalPort> ports;
    const JsonValue* portsNode = nullptr;
    if (!optionalArray(*object, "ports", path, portsNode, error)) return false;
    if (portsNode != nullptr && portsNode->kind != JsonValue::Kind::Null) {
        if (!parseArray(*portsNode, path + ".ports", ports, error,
            [](const JsonValue& item, const std::string& itemPath,
                std::optional<core::OpticalPort>& parsed, Error& itemError) {
                const JsonObject* port = nullptr;
                if (!objectValue(item, itemPath, port, itemError) ||
                    !allowedKeys(*port, {"id", "direction"}, itemPath, itemError)) return false;
                std::optional<core::OpticalPortId> portId;
                const JsonValue* idValueNode = nullptr;
                if (!required(*port, "id", itemPath, idValueNode, itemError) ||
                    !idValue(*idValueNode, itemPath + ".id", portId, itemError) || !portId) return false;
                core::OpticalPortDirection direction{};
                if (!requiredEnumField(*port, "direction", itemPath,
                    {{"input", core::OpticalPortDirection::Input},
                     {"output", core::OpticalPortDirection::Output},
                     {"bidirectional", core::OpticalPortDirection::Bidirectional}},
                    direction, itemError)) return false;
                auto created = core::OpticalPort::create(
                    std::move(*portId), direction);
                if (!created) return fail(itemError, ErrorCode::DomainValidationFailed,
                    itemPath, "Core rejected the optical-port declaration.");
                parsed = std::move(created).value();
                return true;
            })) return false;
    }

    std::vector<core::OpticalFunctionCapability> functions;
    const JsonValue* functionsNode = nullptr;
    if (!optionalArray(*object, "opticalFunctions", path, functionsNode, error)) return false;
    if (functionsNode != nullptr && functionsNode->kind != JsonValue::Kind::Null) {
        if (!parseArray(*functionsNode, path + ".opticalFunctions", functions, error,
            [](const JsonValue& item, const std::string& itemPath,
                std::optional<core::OpticalFunctionCapability>& parsed, Error& itemError) {
                const JsonObject* capability = nullptr;
                if (!objectValue(item, itemPath, capability, itemError) ||
                    !allowedKeys(*capability, {"function", "compatibilityId"},
                        itemPath, itemError)) return false;
                core::OpticalElementFunction function{};
                if (!requiredEnumField(*capability, "function", itemPath,
                    {{"beam-splitter", core::OpticalElementFunction::BeamSplitter},
                     {"phase-plate", core::OpticalElementFunction::PhasePlate},
                     {"phase-annulus", core::OpticalElementFunction::PhaseAnnulus},
                     {"polarizer", core::OpticalElementFunction::Polarizer},
                     {"analyzer", core::OpticalElementFunction::Analyzer},
                     {"differential-interference-prism", core::OpticalElementFunction::DifferentialInterferencePrism},
                     {"excitation-filter", core::OpticalElementFunction::ExcitationFilter},
                     {"emission-filter", core::OpticalElementFunction::EmissionFilter},
                     {"dichroic-mirror", core::OpticalElementFunction::DichroicMirror},
                     {"neutral-density-filter", core::OpticalElementFunction::NeutralDensityFilter},
                     {"darkfield-stop", core::OpticalElementFunction::DarkfieldStop},
                     {"other", core::OpticalElementFunction::Other}},
                    function, itemError)) return false;
                std::optional<core::OpticalCompatibilityId> compatibility;
                if (!optionalId(*capability, "compatibilityId", itemPath,
                    compatibility, itemError)) return false;
                auto created = core::OpticalFunctionCapability::create(
                    function, std::move(compatibility));
                if (!created) return fail(itemError, ErrorCode::DomainValidationFailed,
                    itemPath, "Core rejected the optical-function capability.");
                parsed = std::move(created).value();
                return true;
            })) return false;
    }

    auto created = core::EquipmentComponent::create(
        std::move(*componentId), kind, std::move(ports), std::move(functions));
    if (!created) return fail(error, ErrorCode::DomainValidationFailed,
        path, "Core rejected the equipment component, its ports or optical capabilities.");
    output = std::move(created).value();
    return true;
}

bool parseConnection(
    const JsonValue& value,
    const std::string& path,
    std::optional<core::OpticalConnection>& output,
    Error& error) {
    const JsonObject* object = nullptr;
    if (!objectValue(value, path, object, error) ||
        !allowedKeys(*object, {"id", "firstPortId", "secondPortId"}, path, error)) return false;

    std::optional<core::OpticalConnectionId> id;
    std::optional<core::OpticalPortId> first;
    std::optional<core::OpticalPortId> second;
    if (!requiredId(*object, "id", path, id, error) ||
        !requiredId(*object, "firstPortId", path, first, error) ||
        !requiredId(*object, "secondPortId", path, second, error)) return false;
    auto created = core::OpticalConnection::create(
        std::move(*id), std::move(*first), std::move(*second));
    if (!created) return fail(error, ErrorCode::DomainValidationFailed,
        path, "Core rejected the optical connection.");
    output = std::move(created).value();
    return true;
}

bool parseOpticalPath(
    const JsonValue& value,
    const std::string& path,
    std::optional<core::OpticalPath>& output,
    Error& error) {
    const JsonObject* object = nullptr;
    if (!objectValue(value, path, object, error) ||
        !allowedKeys(*object, {"id", "role", "steps"}, path, error)) return false;

    std::optional<core::OpticalPathId> id;
    if (!requiredId(*object, "id", path, id, error)) return false;
    core::OpticalPathRole role{};
    if (!requiredEnumField(*object, "role", path,
        {{"imaging", core::OpticalPathRole::Imaging},
         {"illumination", core::OpticalPathRole::Illumination},
         {"other", core::OpticalPathRole::Other}}, role, error)) return false;

    const JsonValue* stepsNode = nullptr;
    if (!requiredArray(*object, "steps", path, stepsNode, error)) return false;
    std::vector<core::OpticalPathStep> steps;
    if (!parseArray(*stepsNode, path + ".steps", steps, error,
        [](const JsonValue& item, const std::string& itemPath,
            std::optional<core::OpticalPathStep>& parsed, Error& itemError) {
            const JsonObject* step = nullptr;
            if (!objectValue(item, itemPath, step, itemError) ||
                !allowedKeys(*step, {"componentId", "entryPortId", "exitPortId"},
                    itemPath, itemError)) return false;
            std::optional<core::EquipmentComponentId> component;
            if (!requiredId(*step, "componentId", itemPath, component, itemError)) return false;
            std::optional<core::OpticalPortId> entry, exit;
            if (!optionalId(*step, "entryPortId", itemPath, entry, itemError) ||
                !optionalId(*step, "exitPortId", itemPath, exit, itemError)) return false;
            auto created = core::OpticalPathStep::create(
                std::move(*component), std::move(entry), std::move(exit));
            if (!created) return fail(itemError, ErrorCode::DomainValidationFailed,
                itemPath, "Core rejected the optical-path step.");
            parsed = std::move(created).value();
            return true;
        })) return false;

    auto created = core::OpticalPath::create(
        std::move(*id), std::move(steps), role);
    if (!created) return fail(error, ErrorCode::DomainValidationFailed,
        path, "Core rejected the ordered optical path.");
    output = std::move(created).value();
    return true;
}

bool parseLightPath(
    const JsonValue& value,
    const std::string& path,
    std::optional<core::LightPath>& output,
    Error& error) {
    const JsonObject* object = nullptr;
    if (!objectValue(value, path, object, error) ||
        !allowedKeys(*object, {"id", "name", "opticalPathIds"}, path, error)) return false;
    std::optional<core::LightPathId> id;
    std::string name;
    const JsonValue* nameNode = nullptr;
    if (!requiredId(*object, "id", path, id, error) ||
        !required(*object, "name", path, nameNode, error) ||
        !stringValue(*nameNode, path + ".name", name, error)) return false;
    const JsonValue* pathsNode = nullptr;
    if (!requiredArray(*object, "opticalPathIds", path, pathsNode, error)) return false;
    std::vector<core::OpticalPathId> paths;
    if (!parseIdArrayValue(*pathsNode, path + ".opticalPathIds", paths, error)) return false;
    auto created = core::LightPath::create(
        std::move(*id), std::move(name), std::move(paths));
    if (!created) return fail(error, ErrorCode::DomainValidationFailed,
        path, "Core rejected the selectable LightPath.");
    output = std::move(created).value();
    return true;
}



bool parseObjective(
    const JsonValue& value,
    const std::string& path,
    std::optional<core::ObjectiveSpecification>& output,
    Error& error) {
    const JsonObject* object = nullptr;
    if (!objectValue(value, path, object, error) ||
        !allowedKeys(*object,
            {"componentId", "nominalMagnification", "numericalAperture",
             "workingDistanceMicrometers", "chromaticCorrection", "fieldCorrection",
             "immersionMedium", "designCoverglassThicknessMicrometers"},
            path, error)) return false;

    std::optional<core::EquipmentComponentId> component;
    std::optional<core::NominalMagnification> magnification;
    std::optional<core::NumericalAperture> aperture;
    std::optional<core::PositiveMicrometers> workingDistance;
    std::optional<core::PositiveMicrometers> coverglass;
    if (!requiredId(*object, "componentId", path, component, error) ||
        !requiredPositiveField(*object, "nominalMagnification", path, magnification, error) ||
        !requiredPositiveField(*object, "numericalAperture", path, aperture, error) ||
        !requiredPositiveField(*object, "workingDistanceMicrometers", path,
            workingDistance, error) ||
        !optionalPositiveField(*object, "designCoverglassThicknessMicrometers",
            path, coverglass, error)) return false;

    core::ObjectiveChromaticCorrection chromatic{};
    core::ObjectiveFieldCorrection field{};
    core::ObjectiveImmersionMedium immersion{};
    if (!requiredEnumField(*object, "chromaticCorrection", path,
        {{"achromatic", core::ObjectiveChromaticCorrection::Achromatic},
         {"semi-apochromatic", core::ObjectiveChromaticCorrection::SemiApochromatic},
         {"apochromatic", core::ObjectiveChromaticCorrection::Apochromatic},
         {"other", core::ObjectiveChromaticCorrection::Other},
         {"unknown", core::ObjectiveChromaticCorrection::Unknown}},
        chromatic, error) ||
        !requiredEnumField(*object, "fieldCorrection", path,
        {{"plan", core::ObjectiveFieldCorrection::Plan},
         {"non-plan", core::ObjectiveFieldCorrection::NonPlan},
         {"unknown", core::ObjectiveFieldCorrection::Unknown}},
        field, error) ||
        !requiredEnumField(*object, "immersionMedium", path,
        {{"air", core::ObjectiveImmersionMedium::Air},
         {"water", core::ObjectiveImmersionMedium::Water},
         {"oil", core::ObjectiveImmersionMedium::Oil},
         {"glycerol", core::ObjectiveImmersionMedium::Glycerol},
         {"silicone", core::ObjectiveImmersionMedium::Silicone},
         {"other", core::ObjectiveImmersionMedium::Other},
         {"unknown", core::ObjectiveImmersionMedium::Unknown}},
        immersion, error)) return false;

    auto created = core::ObjectiveSpecification::create(
        std::move(*component), std::move(*magnification), std::move(*aperture),
        std::move(*workingDistance), chromatic, field, immersion,
        std::move(coverglass));
    if (!created) return fail(error, ErrorCode::DomainValidationFailed,
        path, "Core rejected the objective specification.");
    output = std::move(created).value();
    return true;
}

bool parseCondenser(
    const JsonValue& value,
    const std::string& path,
    std::optional<core::CondenserSpecification>& output,
    Error& error) {
    const JsonObject* object = nullptr;
    if (!objectValue(value, path, object, error) ||
        !allowedKeys(*object,
            {"componentId", "minimumApertureNA", "maximumApertureNA",
             "design", "immersionMedium", "workingDistanceMicrometers"},
            path, error)) return false;

    std::optional<core::EquipmentComponentId> component;
    std::optional<core::NonNegativeNumericalAperture> minimum;
    std::optional<core::NumericalAperture> maximum;
    std::optional<core::PositiveMicrometers> workingDistance;
    if (!requiredId(*object, "componentId", path, component, error) ||
        !requiredNonNegativeField(*object, "minimumApertureNA", path, minimum, error) ||
        !requiredPositiveField(*object, "maximumApertureNA", path, maximum, error) ||
        !optionalPositiveField(*object, "workingDistanceMicrometers",
            path, workingDistance, error)) return false;

    core::CondenserDesign design{};
    core::CondenserImmersionMedium immersion{};
    if (!requiredEnumField(*object, "design", path,
        {{"abbe", core::CondenserDesign::Abbe},
         {"achromatic", core::CondenserDesign::Achromatic},
         {"achromat-aplanatic", core::CondenserDesign::AchromatAplanatic},
         {"other", core::CondenserDesign::Other},
         {"unknown", core::CondenserDesign::Unknown}},
        design, error) ||
        !requiredEnumField(*object, "immersionMedium", path,
        {{"air", core::CondenserImmersionMedium::Air},
         {"water", core::CondenserImmersionMedium::Water},
         {"oil", core::CondenserImmersionMedium::Oil},
         {"other", core::CondenserImmersionMedium::Other},
         {"unknown", core::CondenserImmersionMedium::Unknown}},
        immersion, error)) return false;

    auto created = core::CondenserSpecification::create(
        std::move(*component), std::move(*minimum), std::move(*maximum),
        design, immersion, std::move(workingDistance));
    if (!created) return fail(error, ErrorCode::DomainValidationFailed,
        path, "Core rejected the condenser specification.");
    output = std::move(created).value();
    return true;
}

bool parseOpticalRelay(
    const JsonValue& value,
    const std::string& path,
    std::optional<core::OpticalRelaySpecification>& output,
    Error& error) {
    const JsonObject* object = nullptr;
    if (!objectValue(value, path, object, error) ||
        !allowedKeys(*object, {"componentId", "role", "magnificationFactor"},
            path, error)) return false;

    std::optional<core::EquipmentComponentId> component;
    std::optional<double> factor;
    if (!requiredId(*object, "componentId", path, component, error) ||
        !optionalNumber(*object, "magnificationFactor", path, factor, error)) return false;

    core::OpticalRelayRole role{};
    if (!requiredEnumField(*object, "role", path,
        {{"tube-lens", core::OpticalRelayRole::TubeLens},
         {"intermediate-relay", core::OpticalRelayRole::IntermediateRelay},
         {"camera-adapter", core::OpticalRelayRole::CameraAdapter},
         {"reduction-adapter", core::OpticalRelayRole::ReductionAdapter},
         {"magnification-changer", core::OpticalRelayRole::MagnificationChanger},
         {"other", core::OpticalRelayRole::Other}},
        role, error)) return false;

    auto created = core::OpticalRelaySpecification::create(
        std::move(*component), role, factor);
    if (!created) return fail(error, ErrorCode::DomainValidationFailed,
        path, "Core rejected the optical-relay specification.");
    output = std::move(created).value();
    return true;
}

bool parseFilter(
    const JsonValue& value,
    const std::string& path,
    std::optional<core::OpticalFilterSpecification>& output,
    Error& error) {
    const JsonObject* object = nullptr;
    if (!objectValue(value, path, object, error) ||
        !allowedKeys(*object,
            {"componentId", "role", "compatibilityId", "spectralResponse"},
            path, error)) return false;

    std::optional<core::EquipmentComponentId> component;
    std::optional<core::OpticalCompatibilityId> compatibility;
    if (!requiredId(*object, "componentId", path, component, error) ||
        !optionalId(*object, "compatibilityId", path, compatibility, error)) return false;

    core::OpticalFilterRole role{};
    if (!requiredEnumField(*object, "role", path,
        {{"excitation", core::OpticalFilterRole::Excitation},
         {"emission", core::OpticalFilterRole::Emission},
         {"dichroic", core::OpticalFilterRole::Dichroic},
         {"neutral-density", core::OpticalFilterRole::NeutralDensity},
         {"other", core::OpticalFilterRole::Other}},
        role, error)) return false;

    const JsonValue* responseNode = nullptr;
    if (!requiredArray(*object, "spectralResponse", path, responseNode, error)) return false;
    std::vector<core::OpticalFilterSpectralSample> samples;
    if (!parseArray(*responseNode, path + ".spectralResponse", samples, error,
        [](const JsonValue& item, const std::string& itemPath,
            std::optional<core::OpticalFilterSpectralSample>& parsed, Error& itemError) {
            const JsonObject* sample = nullptr;
            if (!objectValue(item, itemPath, sample, itemError) ||
                !allowedKeys(*sample,
                    {"wavelengthNanometers", "transmissionFraction", "reflectionFraction"},
                    itemPath, itemError)) return false;
            std::optional<core::PositiveNanometers> wavelength;
            double transmission = 0.0;
            std::optional<double> reflection;
            if (!requiredPositiveField(*sample, "wavelengthNanometers",
                    itemPath, wavelength, itemError) ||
                !requiredNumber(*sample, "transmissionFraction",
                    itemPath, transmission, itemError) ||
                !optionalNumber(*sample, "reflectionFraction",
                    itemPath, reflection, itemError)) return false;
            parsed = core::OpticalFilterSpectralSample{
                std::move(*wavelength), transmission, reflection};
            return true;
        })) return false;

    auto created = core::OpticalFilterSpecification::create(
        std::move(*component), role, std::move(samples), std::move(compatibility));
    if (!created) return fail(error, ErrorCode::DomainValidationFailed,
        path, "Core rejected the optical-filter spectral response.");
    output = std::move(created).value();
    return true;
}



bool parsePixelDimensions(
    const JsonValue& value,
    const std::string& path,
    std::optional<core::PixelDimensions>& output,
    Error& error) {
    const JsonObject* object = nullptr;
    if (!objectValue(value, path, object, error) ||
        !allowedKeys(*object, {"width", "height"}, path, error)) return false;
    std::uint32_t width = 0, height = 0;
    if (!requiredUInt32(*object, "width", path, width, error) ||
        !requiredUInt32(*object, "height", path, height, error)) return false;
    auto created = core::PixelDimensions::create(width, height);
    if (!created) return fail(error, ErrorCode::InvalidValue,
        path, "Pixel dimensions must both be greater than zero.");
    output = std::move(created).value();
    return true;
}

bool parseSensorRegion(
    const JsonValue& value,
    const std::string& path,
    std::optional<core::SensorRegionOfInterest>& output,
    Error& error) {
    const JsonObject* object = nullptr;
    if (!objectValue(value, path, object, error) ||
        !allowedKeys(*object, {"x", "y", "width", "height"}, path, error)) return false;
    std::uint32_t x = 0, y = 0, width = 0, height = 0;
    if (!requiredUInt32(*object, "x", path, x, error) ||
        !requiredUInt32(*object, "y", path, y, error) ||
        !requiredUInt32(*object, "width", path, width, error) ||
        !requiredUInt32(*object, "height", path, height, error)) return false;
    auto created = core::SensorRegionOfInterest::create(x, y, width, height);
    if (!created) return fail(error, ErrorCode::InvalidValue,
        path, "Sensor region width and height must be greater than zero.");
    output = std::move(created).value();
    return true;
}

bool parsePixelBinning(
    const JsonValue& value,
    const std::string& path,
    std::optional<core::PixelBinning>& output,
    Error& error) {
    const JsonObject* object = nullptr;
    if (!objectValue(value, path, object, error) ||
        !allowedKeys(*object, {"horizontalFactor", "verticalFactor"},
            path, error)) return false;
    std::uint32_t horizontal = 0, vertical = 0;
    if (!requiredUInt32(*object, "horizontalFactor", path, horizontal, error) ||
        !requiredUInt32(*object, "verticalFactor", path, vertical, error)) return false;
    auto created = core::PixelBinning::create(horizontal, vertical);
    if (!created) return fail(error, ErrorCode::InvalidValue,
        path, "Pixel-binning factors must both be greater than zero.");
    output = std::move(created).value();
    return true;
}

bool parseCameraOutputMode(
    const JsonValue& value,
    const std::string& path,
    std::optional<core::CameraOutputMode>& output,
    Error& error) {
    const JsonObject* object = nullptr;
    if (!objectValue(value, path, object, error) ||
        !allowedKeys(*object,
            {"id", "sensorRegion", "binning", "deliveredDimensions",
             "pixelLayout", "pixelFormatCode"},
            path, error)) return false;

    std::optional<core::CameraModeId> id;
    if (!requiredId(*object, "id", path, id, error)) return false;

    const JsonValue* regionNode = nullptr;
    const JsonValue* binningNode = nullptr;
    const JsonValue* dimensionsNode = nullptr;
    if (!required(*object, "sensorRegion", path, regionNode, error) ||
        !required(*object, "binning", path, binningNode, error) ||
        !required(*object, "deliveredDimensions", path, dimensionsNode, error)) return false;

    std::optional<core::SensorRegionOfInterest> region;
    std::optional<core::PixelBinning> binning;
    std::optional<core::PixelDimensions> dimensions;
    if (!parseSensorRegion(*regionNode, path + ".sensorRegion", region, error) ||
        !parsePixelBinning(*binningNode, path + ".binning", binning, error) ||
        !parsePixelDimensions(*dimensionsNode, path + ".deliveredDimensions",
            dimensions, error)) return false;

    core::CameraPixelLayout layout{};
    std::string pixelFormat;
    const JsonValue* formatNode = nullptr;
    if (!requiredEnumField(*object, "pixelLayout", path,
        {{"monochrome", core::CameraPixelLayout::Monochrome},
         {"bayer", core::CameraPixelLayout::Bayer},
         {"interleaved-color", core::CameraPixelLayout::InterleavedColor},
         {"planar-color", core::CameraPixelLayout::PlanarColor},
         {"other", core::CameraPixelLayout::Other}},
        layout, error) ||
        !required(*object, "pixelFormatCode", path, formatNode, error) ||
        !stringValue(*formatNode, path + ".pixelFormatCode", pixelFormat, error)) return false;

    auto created = core::CameraOutputMode::create(
        std::move(*id), std::move(*region), std::move(*binning),
        std::move(*dimensions), layout, std::move(pixelFormat));
    if (!created) return fail(error, ErrorCode::DomainValidationFailed,
        path, "Core rejected the camera output mode or its sensor-pixel mapping.");
    output = std::move(created).value();
    return true;
}

bool parseExposureTimeRange(
    const JsonValue& value,
    const std::string& path,
    std::optional<core::ExposureTimeRange>& output,
    Error& error) {
    const JsonObject* object = nullptr;
    if (!objectValue(value, path, object, error) ||
        !allowedKeys(*object, {"minimumMicroseconds", "maximumMicroseconds"},
            path, error)) return false;
    std::optional<core::PositiveMicroseconds> minimum, maximum;
    if (!requiredPositiveField(*object, "minimumMicroseconds", path, minimum, error) ||
        !requiredPositiveField(*object, "maximumMicroseconds", path, maximum, error)) return false;
    auto created = core::ExposureTimeRange::create(
        std::move(*minimum), std::move(*maximum));
    if (!created) return fail(error, ErrorCode::InvalidValue,
        path, "Camera exposure-time range is inverted.");
    output = std::move(created).value();
    return true;
}

bool parseCameraSensorCharacteristics(
    const JsonValue& value,
    const std::string& path,
    std::optional<core::CameraSensorCharacteristics>& output,
    Error& error) {
    const JsonObject* object = nullptr;
    if (!objectValue(value, path, object, error) ||
        !allowedKeys(*object,
            {"technology", "shutterType", "colorMode", "readNoiseElectronsRms",
             "darkCurrentElectronsPerPixelPerSecond", "fullWellCapacityElectrons",
             "maximumFrameRateFramesPerSecond", "quantumEfficiency",
             "provenance", "provenanceReference"},
            path, error)) return false;

    core::CameraSensorCharacteristics characteristics;
    std::optional<core::CameraSpecificationProvenance> provenance;
    if (!optionalEnumField(*object, "technology", path,
        {{"ccd", core::CameraSensorTechnology::CCD},
         {"cmos", core::CameraSensorTechnology::CMOS},
         {"scientific-cmos", core::CameraSensorTechnology::ScientificCMOS},
         {"unknown", core::CameraSensorTechnology::Unknown}},
        characteristics.technology, error) ||
        !optionalEnumField(*object, "shutterType", path,
        {{"global", core::CameraShutterType::Global},
         {"rolling", core::CameraShutterType::Rolling},
         {"frame-transfer", core::CameraShutterType::FrameTransfer},
         {"global-reset", core::CameraShutterType::GlobalReset},
         {"unknown", core::CameraShutterType::Unknown}},
        characteristics.shutterType, error) ||
        !optionalEnumField(*object, "colorMode", path,
        {{"monochrome", core::CameraSensorColorMode::Monochrome},
         {"color", core::CameraSensorColorMode::Color},
         {"unknown", core::CameraSensorColorMode::Unknown}},
        characteristics.colorMode, error) ||
        !optionalNumber(*object, "readNoiseElectronsRms", path,
            characteristics.readNoiseElectronsRms, error) ||
        !optionalNumber(*object, "darkCurrentElectronsPerPixelPerSecond", path,
            characteristics.darkCurrentElectronsPerPixelPerSecond, error) ||
        !optionalNumber(*object, "fullWellCapacityElectrons", path,
            characteristics.fullWellCapacityElectrons, error) ||
        !optionalNumber(*object, "maximumFrameRateFramesPerSecond", path,
            characteristics.maximumFrameRateFramesPerSecond, error) ||
        !optionalString(*object, "provenanceReference", path,
            characteristics.provenanceReference, error)) return false;

    if (!optionalEnumField(*object, "provenance", path,
        {{"manufacturer-datasheet", core::CameraSpecificationProvenance::ManufacturerDatasheet},
         {"vendor-model-descriptor", core::CameraSpecificationProvenance::VendorModelDescriptor},
         {"user-verified", core::CameraSpecificationProvenance::UserVerified},
         {"measured", core::CameraSpecificationProvenance::Measured},
         {"unknown", core::CameraSpecificationProvenance::Unknown}},
        provenance, error)) return false;

    if (provenance) characteristics.provenance = *provenance;
    const JsonValue* qeNode = nullptr;
    if (!optionalArray(*object, "quantumEfficiency", path, qeNode, error)) return false;
    if (qeNode != nullptr && qeNode->kind != JsonValue::Kind::Null) {
        if (!parseArray(*qeNode, path + ".quantumEfficiency",
            characteristics.quantumEfficiency, error,
            [](const JsonValue& item, const std::string& itemPath,
                std::optional<core::CameraQuantumEfficiencySample>& parsed, Error& itemError) {
                const JsonObject* sample = nullptr;
                if (!objectValue(item, itemPath, sample, itemError) ||
                    !allowedKeys(*sample, {"wavelengthNanometers", "quantumEfficiency"},
                        itemPath, itemError)) return false;
                double wavelength = 0.0, efficiency = 0.0;
                if (!requiredNumber(*sample, "wavelengthNanometers", itemPath,
                        wavelength, itemError) ||
                    !requiredNumber(*sample, "quantumEfficiency", itemPath,
                        efficiency, itemError)) return false;
                parsed = core::CameraQuantumEfficiencySample{wavelength, efficiency};
                return true;
            })) return false;
    }

    output = std::move(characteristics);
    return true;
}

bool parseCameraSpecification(
    const JsonValue& value,
    const std::string& path,
    std::optional<core::CameraSpecification>& output,
    Error& error) {
    const JsonObject* object = nullptr;
    if (!objectValue(value, path, object, error) ||
        !allowedKeys(*object,
            {"componentId", "activeSensorDimensions", "pixelPitchXMicrometers",
             "pixelPitchYMicrometers", "outputModes", "exposureTimeRange",
             "sensorCharacteristics"},
            path, error)) return false;

    std::optional<core::EquipmentComponentId> component;
    if (!requiredId(*object, "componentId", path, component, error)) return false;

    const JsonValue* dimensionsNode = nullptr;
    if (!required(*object, "activeSensorDimensions", path, dimensionsNode, error)) return false;
    std::optional<core::PixelDimensions> dimensions;
    if (!parsePixelDimensions(*dimensionsNode, path + ".activeSensorDimensions",
        dimensions, error)) return false;

    std::optional<core::PositiveMicrometers> pitchX, pitchY;
    if (!requiredPositiveField(*object, "pixelPitchXMicrometers", path, pitchX, error) ||
        !requiredPositiveField(*object, "pixelPitchYMicrometers", path, pitchY, error)) return false;

    const JsonValue* modesNode = nullptr;
    if (!requiredArray(*object, "outputModes", path, modesNode, error)) return false;
    std::vector<core::CameraOutputMode> modes;
    if (!parseArray(*modesNode, path + ".outputModes", modes, error,
        parseCameraOutputMode)) return false;

    std::optional<core::ExposureTimeRange> exposure;
    const JsonValue* exposureNode = find(*object, "exposureTimeRange");
    if (exposureNode != nullptr && exposureNode->kind != JsonValue::Kind::Null) {
        if (!parseExposureTimeRange(*exposureNode, path + ".exposureTimeRange",
            exposure, error)) return false;
    }

    std::optional<core::CameraSensorCharacteristics> sensor;
    const JsonValue* sensorNode = find(*object, "sensorCharacteristics");
    if (sensorNode != nullptr && sensorNode->kind != JsonValue::Kind::Null) {
        if (!parseCameraSensorCharacteristics(*sensorNode,
            path + ".sensorCharacteristics", sensor, error)) return false;
    }

    auto created = core::CameraSpecification::create(
        std::move(*component), std::move(*dimensions),
        std::move(*pitchX), std::move(*pitchY), std::move(modes),
        std::move(exposure), std::move(sensor));
    if (!created) return fail(error, ErrorCode::DomainValidationFailed,
        path, "Core rejected the camera sensor, output modes or sensor characteristics.");
    output = std::move(created).value();
    return true;
}



bool parseSpectralBand(
    const JsonValue& value,
    const std::string& path,
    std::optional<core::SpectralBand>& output,
    Error& error) {
    const JsonObject* object = nullptr;
    if (!objectValue(value, path, object, error) ||
        !allowedKeys(*object,
            {"minimumWavelengthNanometers", "maximumWavelengthNanometers"},
            path, error)) return false;
    std::optional<core::PositiveNanometers> minimum, maximum;
    if (!requiredPositiveField(*object, "minimumWavelengthNanometers",
            path, minimum, error) ||
        !requiredPositiveField(*object, "maximumWavelengthNanometers",
            path, maximum, error)) return false;
    auto created = core::SpectralBand::create(
        std::move(*minimum), std::move(*maximum));
    if (!created) return fail(error, ErrorCode::DomainValidationFailed,
        path, "Core rejected the spectral band.");
    output = std::move(created).value();
    return true;
}

bool parseIlluminationIntensityRange(
    const JsonValue& value,
    const std::string& path,
    std::optional<core::IlluminationIntensityRange>& output,
    Error& error) {
    const JsonObject* object = nullptr;
    if (!objectValue(value, path, object, error) ||
        !allowedKeys(*object, {"minimum", "maximum", "unit"}, path, error)) return false;
    double minimum = 0.0, maximum = 0.0;
    core::IlluminationIntensityUnit unit{};
    if (!requiredNumber(*object, "minimum", path, minimum, error) ||
        !requiredNumber(*object, "maximum", path, maximum, error) ||
        !requiredEnumField(*object, "unit", path,
            {{"percent", core::IlluminationIntensityUnit::Percent},
             {"milliamperes", core::IlluminationIntensityUnit::Milliamperes},
             {"milliwatts", core::IlluminationIntensityUnit::Milliwatts},
             {"relative", core::IlluminationIntensityUnit::Relative},
             {"device-defined", core::IlluminationIntensityUnit::DeviceDefined}},
            unit, error)) return false;
    auto created = core::IlluminationIntensityRange::create(minimum, maximum, unit);
    if (!created) return fail(error, ErrorCode::DomainValidationFailed,
        path, "Core rejected the illumination intensity range.");
    output = std::move(created).value();
    return true;
}

bool parseSpectralPowerDistribution(
    const JsonValue& value,
    const std::string& path,
    std::optional<core::SpectralPowerDistribution>& output,
    Error& error) {
    if (value.kind != JsonValue::Kind::Array) {
        return fail(error, ErrorCode::WrongType, path,
            "Expected an array of spectral-power samples.");
    }
    std::vector<core::SpectralPowerSample> samples;
    if (!parseArray(value, path, samples, error,
        [](const JsonValue& item, const std::string& itemPath,
            std::optional<core::SpectralPowerSample>& parsed, Error& itemError) {
            const JsonObject* sample = nullptr;
            if (!objectValue(item, itemPath, sample, itemError) ||
                !allowedKeys(*sample, {"wavelengthNanometers", "relativePower"},
                    itemPath, itemError)) return false;
            double wavelength = 0.0, power = 0.0;
            if (!requiredNumber(*sample, "wavelengthNanometers",
                    itemPath, wavelength, itemError) ||
                !requiredNumber(*sample, "relativePower",
                    itemPath, power, itemError)) return false;
            parsed = core::SpectralPowerSample{wavelength, power};
            return true;
        })) return false;

    auto created = core::SpectralPowerDistribution::create(std::move(samples));
    if (!created) return fail(error, ErrorCode::DomainValidationFailed,
        path, "Core rejected the spectral-power distribution.");
    output = std::move(created).value();
    return true;
}

bool parseIlluminationChannel(
    const JsonValue& value,
    const std::string& path,
    std::optional<core::IlluminationChannel>& output,
    Error& error) {
    const JsonObject* object = nullptr;
    if (!objectValue(value, path, object, error) ||
        !allowedKeys(*object,
            {"id", "spectralBand", "controlKind", "intensityRange",
             "spectralPowerDistribution"},
            path, error)) return false;

    std::optional<core::IlluminationChannelId> id;
    if (!requiredId(*object, "id", path, id, error)) return false;

    std::optional<core::SpectralBand> band;
    const JsonValue* bandNode = find(*object, "spectralBand");
    if (bandNode != nullptr && bandNode->kind != JsonValue::Kind::Null) {
        if (!parseSpectralBand(*bandNode, path + ".spectralBand", band, error)) return false;
    }

    core::IlluminationControlKind control{};
    if (!requiredEnumField(*object, "controlKind", path,
        {{"fixed", core::IlluminationControlKind::Fixed},
         {"continuous", core::IlluminationControlKind::Continuous},
         {"device-defined", core::IlluminationControlKind::DeviceDefined},
         {"unknown", core::IlluminationControlKind::Unknown}},
        control, error)) return false;

    std::optional<core::IlluminationIntensityRange> range;
    const JsonValue* rangeNode = find(*object, "intensityRange");
    if (rangeNode != nullptr && rangeNode->kind != JsonValue::Kind::Null) {
        if (!parseIlluminationIntensityRange(*rangeNode,
            path + ".intensityRange", range, error)) return false;
    }

    std::optional<core::SpectralPowerDistribution> distribution;
    const JsonValue* distributionNode = find(*object, "spectralPowerDistribution");
    if (distributionNode != nullptr && distributionNode->kind != JsonValue::Kind::Null) {
        if (!parseSpectralPowerDistribution(*distributionNode,
            path + ".spectralPowerDistribution", distribution, error)) return false;
    }

    auto created = core::IlluminationChannel::create(
        std::move(*id), std::move(band), control,
        std::move(range), std::move(distribution));
    if (!created) return fail(error, ErrorCode::DomainValidationFailed,
        path, "Core rejected the illumination channel.");
    output = std::move(created).value();
    return true;
}

bool parseIlluminationSpecification(
    const JsonValue& value,
    const std::string& path,
    std::optional<core::IlluminationSpecification>& output,
    Error& error) {
    const JsonObject* object = nullptr;
    if (!objectValue(value, path, object, error) ||
        !allowedKeys(*object, {"componentId", "technology", "channels"},
            path, error)) return false;

    std::optional<core::EquipmentComponentId> component;
    if (!requiredId(*object, "componentId", path, component, error)) return false;
    core::IlluminationSourceTechnology technology{};
    if (!requiredEnumField(*object, "technology", path,
        {{"led", core::IlluminationSourceTechnology::Led},
         {"halogen", core::IlluminationSourceTechnology::Halogen},
         {"xenon-arc", core::IlluminationSourceTechnology::XenonArc},
         {"mercury-arc", core::IlluminationSourceTechnology::MercuryArc},
         {"metal-halide-arc", core::IlluminationSourceTechnology::MetalHalideArc},
         {"laser", core::IlluminationSourceTechnology::Laser},
         {"other", core::IlluminationSourceTechnology::Other},
         {"unknown", core::IlluminationSourceTechnology::Unknown}},
        technology, error)) return false;

    const JsonValue* channelsNode = nullptr;
    if (!requiredArray(*object, "channels", path, channelsNode, error)) return false;
    std::vector<core::IlluminationChannel> channels;
    if (!parseArray(*channelsNode, path + ".channels", channels, error,
        parseIlluminationChannel)) return false;

    auto created = core::IlluminationSpecification::create(
        std::move(*component), technology, std::move(channels));
    if (!created) return fail(error, ErrorCode::DomainValidationFailed,
        path, "Core rejected the illumination-source specification.");
    output = std::move(created).value();
    return true;
}




bool signedIntegerValue(
    const JsonValue& value,const std::string& path,
    std::int64_t& output,Error& error) {
    if(value.kind!=JsonValue::Kind::Number)
        return fail(error,ErrorCode::WrongType,path,"Expected a signed integer.");
    if(value.text.empty()||value.text.find_first_of(".eE")!=std::string::npos)
        return fail(error,ErrorCode::InvalidValue,path,
            "Expected an integer without a fraction or exponent.");
    const auto parsed=std::from_chars(
        value.text.data(),value.text.data()+value.text.size(),output);
    if(parsed.ec!=std::errc{}||parsed.ptr!=value.text.data()+value.text.size())
        return fail(error,ErrorCode::InvalidValue,path,
            "Integer is outside the signed 64-bit range.");
    return true;
}

bool requiredSignedInteger(
    const JsonObject& object,const char* key,const std::string& path,
    std::int64_t& output,Error& error) {
    const JsonValue* value=nullptr;
    return required(object,key,path,value,error)&&
        signedIntegerValue(*value,path+"."+key,output,error);
}

bool parseStageNanometreRange(
    const JsonValue& value,const std::string& path,
    std::optional<core::StageNanometreRange>& output,Error& error) {
    const JsonObject* object=nullptr;
    if(!objectValue(value,path,object,error)||
       !allowedKeys(*object,{"minimum","maximum"},path,error)) return false;
    std::int64_t minimum=0,maximum=0;
    if(!requiredSignedInteger(*object,"minimum",path,minimum,error)||
       !requiredSignedInteger(*object,"maximum",path,maximum,error)) return false;
    auto created=core::StageNanometreRange::create(minimum,maximum);
    if(!created) return fail(error,ErrorCode::DomainValidationFailed,path,
        "Core rejected the nanometre range.");
    output=std::move(created).value();
    return true;
}

bool parseStageRadianRange(
    const JsonValue& value,const std::string& path,
    std::optional<core::StageRadianRange>& output,Error& error) {
    const JsonObject* object=nullptr;
    if(!objectValue(value,path,object,error)||
       !allowedKeys(*object,{"minimum","maximum"},path,error)) return false;
    double minimum=0.0,maximum=0.0;
    if(!requiredNumber(*object,"minimum",path,minimum,error)||
       !requiredNumber(*object,"maximum",path,maximum,error)) return false;
    auto created=core::StageRadianRange::create(minimum,maximum);
    if(!created) return fail(error,ErrorCode::DomainValidationFailed,path,
        "Core rejected the radian range.");
    output=std::move(created).value();
    return true;
}

bool parseStagePosition2D(
    const JsonValue& value,const std::string& path,
    core::StagePosition2DNanometres& output,Error& error) {
    const JsonObject* object=nullptr;
    if(!objectValue(value,path,object,error)||
       !allowedKeys(*object,{"x","y"},path,error)) return false;
    return requiredSignedInteger(*object,"x",path,output.x,error)&&
        requiredSignedInteger(*object,"y",path,output.y,error);
}

bool parseStagePosition3D(
    const JsonValue& value,const std::string& path,
    core::StagePosition3DNanometres& output,Error& error) {
    const JsonObject* object=nullptr;
    if(!objectValue(value,path,object,error)||
       !allowedKeys(*object,{"x","y","z"},path,error)) return false;
    return requiredSignedInteger(*object,"x",path,output.x,error)&&
        requiredSignedInteger(*object,"y",path,output.y,error)&&
        requiredSignedInteger(*object,"z",path,output.z,error);
}

bool parseStageSlotSpecification(
    const JsonValue& value,const std::string& path,
    std::optional<core::StageSlotSpecification>& output,Error& error) {
    const JsonObject* object=nullptr;
    if(!objectValue(value,path,object,error)||
       !allowedKeys(*object,{"id","originInStageNanometres",
           "xRangeNanometres","yRangeNanometres"},path,error)) return false;
    std::optional<core::StageSlotId> id;
    const JsonValue *originNode=nullptr,*xNode=nullptr,*yNode=nullptr;
    if(!requiredId(*object,"id",path,id,error)||
       !required(*object,"originInStageNanometres",path,originNode,error)||
       !required(*object,"xRangeNanometres",path,xNode,error)||
       !required(*object,"yRangeNanometres",path,yNode,error)) return false;
    core::StagePosition2DNanometres origin;
    std::optional<core::StageNanometreRange> xRange,yRange;
    if(!parseStagePosition2D(*originNode,path+".originInStageNanometres",origin,error)||
       !parseStageNanometreRange(*xNode,path+".xRangeNanometres",xRange,error)||
       !parseStageNanometreRange(*yNode,path+".yRangeNanometres",yRange,error)) return false;
    auto created=core::StageSlotSpecification::create(
        std::move(*id),origin,std::move(*xRange),std::move(*yRange));
    if(!created) return fail(error,ErrorCode::DomainValidationFailed,path,
        "Core rejected the StageSlot specification.");
    output=std::move(created).value();
    return true;
}

bool parseStageWorkingLinearAxis(
    const JsonValue& value,const std::string& path,
    std::optional<core::StageWorkingLinearAxisSpecification>& output,Error& error) {
    const JsonObject* object=nullptr;
    if(!objectValue(value,path,object,error)||
       !allowedKeys(*object,{"axisId","effectiveRangeNanometres"},path,error)) return false;
    std::optional<core::StageAxisId> axisId;
    const JsonValue* rangeNode=nullptr;
    if(!requiredId(*object,"axisId",path,axisId,error)||
       !required(*object,"effectiveRangeNanometres",path,rangeNode,error)) return false;
    std::optional<core::StageNanometreRange> range;
    if(!parseStageNanometreRange(*rangeNode,path+".effectiveRangeNanometres",range,error))
        return false;
    output=core::StageWorkingLinearAxisSpecification{std::move(*axisId),std::move(*range)};
    return true;
}

bool parseStageWorkingRotationAxis(
    const JsonValue& value,const std::string& path,
    std::optional<core::StageWorkingRotationAxisSpecification>& output,Error& error) {
    const JsonObject* object=nullptr;
    if(!objectValue(value,path,object,error)||
       !allowedKeys(*object,{"axisId","effectiveRangeRadians"},path,error)) return false;
    std::optional<core::StageAxisId> axisId;
    const JsonValue* rangeNode=nullptr;
    if(!requiredId(*object,"axisId",path,axisId,error)||
       !required(*object,"effectiveRangeRadians",path,rangeNode,error)) return false;
    std::optional<core::StageRadianRange> range;
    if(!parseStageRadianRange(*rangeNode,path+".effectiveRangeRadians",range,error))
        return false;
    output=core::StageWorkingRotationAxisSpecification{std::move(*axisId),std::move(*range)};
    return true;
}

bool parseStageConfigurationSpecification(
    const JsonValue& value,const std::string& path,
    std::optional<core::StageConfigurationSpecification>& output,Error& error) {
    const JsonObject* object=nullptr;
    if(!objectValue(value,path,object,error)||
       !allowedKeys(*object,{"id","workingLinearAxes","workingRotationAxes","slots"},
           path,error)) return false;
    std::optional<core::StageConfigurationId> id;
    const JsonValue *linearNode=nullptr,*rotationNode=nullptr,*slotsNode=nullptr;
    if(!requiredId(*object,"id",path,id,error)||
       !requiredArray(*object,"workingLinearAxes",path,linearNode,error)||
       !requiredArray(*object,"workingRotationAxes",path,rotationNode,error)||
       !requiredArray(*object,"slots",path,slotsNode,error)) return false;
    std::vector<core::StageWorkingLinearAxisSpecification> linear;
    std::vector<core::StageWorkingRotationAxisSpecification> rotary;
    std::vector<core::StageSlotSpecification> slots;
    if(!parseArray(*linearNode,path+".workingLinearAxes",linear,error,
           parseStageWorkingLinearAxis)||
       !parseArray(*rotationNode,path+".workingRotationAxes",rotary,error,
           parseStageWorkingRotationAxis)||
       !parseArray(*slotsNode,path+".slots",slots,error,parseStageSlotSpecification))
        return false;
    auto created=core::StageConfigurationSpecification::create(
        std::move(*id),std::move(linear),std::move(rotary),std::move(slots));
    if(!created) return fail(error,ErrorCode::DomainValidationFailed,path,
        "Core rejected the StageConfiguration specification.");
    output=std::move(created).value();
    return true;
}

bool parseStageAxisSpecification(
    const JsonValue& value,
    const std::string& path,
    std::optional<core::AxisSpecification>& output,
    Error& error,std::uint64_t schemaVersion) {
    const JsonObject* object=nullptr;
    if(!objectValue(value,path,object,error)) return false;
    if(schemaVersion==EquipmentDefinitionJsonLoader::kLegacySchemaVersion) {
        if(!allowedKeys(*object,
            {"id","motionType","coordinateUnit","operatingRange","nominalResolution",
             "accuracy","repeatability","measurementUncertainty","positionFeedback",
             "measurementLocation"},path,error)) return false;
    } else if(!allowedKeys(*object,
        {"id","motionType","coordinateUnit","operatingRange","nominalResolution",
         "accuracy","repeatability","measurementUncertainty","positionFeedback",
         "measurementLocation","linearRole","rotationGeometry"},path,error)) return false;

    std::optional<core::StageAxisId> id;
    if (!requiredId(*object, "id", path, id, error)) return false;

    core::AxisMotionType motionType{};
    core::AxisCoordinateUnit coordinateUnit{};
    if (!requiredEnumField(*object, "motionType", path,
        {{"linear", core::AxisMotionType::Linear},
         {"rotary", core::AxisMotionType::Rotary},
         {"other", core::AxisMotionType::Other},
         {"unknown", core::AxisMotionType::Unknown}},
        motionType, error) ||
        !requiredEnumField(*object, "coordinateUnit", path,
        {{"nanometres", core::AxisCoordinateUnit::Nanometres},
         {"micrometers", core::AxisCoordinateUnit::Micrometers},
         {"millimeters", core::AxisCoordinateUnit::Millimeters},
         {"degrees", core::AxisCoordinateUnit::Degrees},
         {"radians", core::AxisCoordinateUnit::Radians},
         {"device-defined", core::AxisCoordinateUnit::DeviceDefined},
         {"unknown", core::AxisCoordinateUnit::Unknown}},
        coordinateUnit, error)) return false;

    if(schemaVersion>=2 && motionType==core::AxisMotionType::Linear &&
       coordinateUnit!=core::AxisCoordinateUnit::Nanometres)
        return fail(error,ErrorCode::InvalidValue,path+".coordinateUnit",
            "Schema-v2 LinearAxis coordinates, travel, resolution and tolerances must use nanometres.");

    const JsonObject* rangeObject = nullptr;
    if (!requiredObject(*object, "operatingRange", path, rangeObject, error) ||
        !allowedKeys(*rangeObject, {"minimum", "maximum", "unit"},
            path + ".operatingRange", error)) return false;
    double minimum = 0.0, maximum = 0.0;
    core::AxisCoordinateUnit rangeUnit{};
    if (!requiredNumber(*rangeObject, "minimum", path + ".operatingRange",
            minimum, error) ||
        !requiredNumber(*rangeObject, "maximum", path + ".operatingRange",
            maximum, error) ||
        !requiredEnumField(*rangeObject, "unit", path + ".operatingRange",
            {{"nanometres", core::AxisCoordinateUnit::Nanometres},
             {"micrometers", core::AxisCoordinateUnit::Micrometers},
             {"millimeters", core::AxisCoordinateUnit::Millimeters},
             {"degrees", core::AxisCoordinateUnit::Degrees},
             {"radians", core::AxisCoordinateUnit::Radians},
             {"device-defined", core::AxisCoordinateUnit::DeviceDefined},
             {"unknown", core::AxisCoordinateUnit::Unknown}},
            rangeUnit, error)) return false;
    auto range = core::AxisOperatingRange::create(minimum, maximum, rangeUnit);
    if (!range) return fail(error, ErrorCode::DomainValidationFailed,
        path + ".operatingRange", "Core rejected the axis operating range.");

    std::optional<core::PositiveAxisResolution> resolution;
    if (!requiredPositiveField(*object, "nominalResolution", path,
        resolution, error)) return false;

    std::optional<core::AxisAccuracySpecification> accuracy;
    const JsonValue* accuracyNode = find(*object, "accuracy");
    if (accuracyNode != nullptr && accuracyNode->kind != JsonValue::Kind::Null) {
        const JsonObject* accuracyObject = nullptr;
        if (!objectValue(*accuracyNode, path + ".accuracy",
                accuracyObject, error) ||
            !allowedKeys(*accuracyObject,
                {"maximumAbsoluteError", "testConditions"},
                path + ".accuracy", error)) return false;
        std::optional<core::NonNegativeAxisAccuracy> value;
        std::string conditions;
        const JsonValue* conditionsNode = nullptr;
        if (!requiredNonNegativeField(*accuracyObject, "maximumAbsoluteError",
                path + ".accuracy", value, error) ||
            !required(*accuracyObject, "testConditions",
                path + ".accuracy", conditionsNode, error) ||
            !stringValue(*conditionsNode, path + ".accuracy.testConditions",
                conditions, error)) return false;
        auto created = core::AxisAccuracySpecification::create(
            std::move(*value), std::move(conditions));
        if (!created) return fail(error, ErrorCode::DomainValidationFailed,
            path + ".accuracy", "Core rejected axis accuracy metadata.");
        accuracy = std::move(created).value();
    }

    std::optional<core::AxisRepeatabilitySpecification> repeatability;
    const JsonValue* repeatabilityNode = find(*object, "repeatability");
    if (repeatabilityNode != nullptr && repeatabilityNode->kind != JsonValue::Kind::Null) {
        const JsonObject* repeatabilityObject = nullptr;
        if (!objectValue(*repeatabilityNode, path + ".repeatability",
                repeatabilityObject, error) ||
            !allowedKeys(*repeatabilityObject,
                {"value", "metric", "testConditions"},
                path + ".repeatability", error)) return false;
        std::optional<core::NonNegativeAxisRepeatability> value;
        core::AxisRepeatabilityMetric metric{};
        std::string conditions;
        const JsonValue* conditionsNode = nullptr;
        if (!requiredNonNegativeField(*repeatabilityObject, "value",
                path + ".repeatability", value, error) ||
            !requiredEnumField(*repeatabilityObject, "metric",
                path + ".repeatability",
                {{"maximum-deviation", core::AxisRepeatabilityMetric::MaximumDeviation},
                 {"standard-deviation", core::AxisRepeatabilityMetric::StandardDeviation},
                 {"root-mean-square", core::AxisRepeatabilityMetric::RootMeanSquare},
                 {"unknown", core::AxisRepeatabilityMetric::Unknown}},
                metric, error) ||
            !required(*repeatabilityObject, "testConditions",
                path + ".repeatability", conditionsNode, error) ||
            !stringValue(*conditionsNode, path + ".repeatability.testConditions",
                conditions, error)) return false;
        auto created = core::AxisRepeatabilitySpecification::create(
            std::move(*value), metric, std::move(conditions));
        if (!created) return fail(error, ErrorCode::DomainValidationFailed,
            path + ".repeatability", "Core rejected axis repeatability metadata.");
        repeatability = std::move(created).value();
    }

    std::optional<core::AxisMeasurementUncertaintySpecification> uncertainty;
    const JsonValue* uncertaintyNode = find(*object, "measurementUncertainty");
    if (uncertaintyNode != nullptr && uncertaintyNode->kind != JsonValue::Kind::Null) {
        const JsonObject* uncertaintyObject = nullptr;
        if (!objectValue(*uncertaintyNode, path + ".measurementUncertainty",
                uncertaintyObject, error) ||
            !allowedKeys(*uncertaintyObject, {"value", "testConditions"},
                path + ".measurementUncertainty", error)) return false;
        std::optional<core::NonNegativeAxisUncertainty> value;
        std::string conditions;
        const JsonValue* conditionsNode = nullptr;
        if (!requiredNonNegativeField(*uncertaintyObject, "value",
                path + ".measurementUncertainty", value, error) ||
            !required(*uncertaintyObject, "testConditions",
                path + ".measurementUncertainty", conditionsNode, error) ||
            !stringValue(*conditionsNode, path + ".measurementUncertainty.testConditions",
                conditions, error)) return false;
        auto created = core::AxisMeasurementUncertaintySpecification::create(
            std::move(*value), std::move(conditions));
        if (!created) return fail(error, ErrorCode::DomainValidationFailed,
            path + ".measurementUncertainty", "Core rejected axis measurement uncertainty metadata.");
        uncertainty = std::move(created).value();
    }

    core::AxisPositionFeedbackCapability feedback{};
    std::optional<core::AxisMeasurementLocation> location;
    if (!requiredEnumField(*object, "positionFeedback", path,
        {{"none", core::AxisPositionFeedbackCapability::None},
         {"encoder", core::AxisPositionFeedbackCapability::Encoder},
         {"linear-scale", core::AxisPositionFeedbackCapability::LinearScale},
         {"device-reported", core::AxisPositionFeedbackCapability::DeviceReported},
         {"external-sensor", core::AxisPositionFeedbackCapability::ExternalSensor},
         {"other", core::AxisPositionFeedbackCapability::Other},
         {"unknown", core::AxisPositionFeedbackCapability::Unknown}},
        feedback, error) ||
        !optionalEnumField(*object, "measurementLocation", path,
        {{"motor", core::AxisMeasurementLocation::Motor},
         {"axis", core::AxisMeasurementLocation::Axis},
         {"specimen", core::AxisMeasurementLocation::Specimen},
         {"external", core::AxisMeasurementLocation::External},
         {"other", core::AxisMeasurementLocation::Other},
         {"unknown", core::AxisMeasurementLocation::Unknown}},
        location, error)) return false;

    std::optional<core::StageLinearAxisRole> linearRole;
    std::optional<core::StageRotationAxisGeometry> rotationGeometry;
    if(schemaVersion>=2) {
        if(!optionalEnumField(*object,"linearRole",path,
            {{"x",core::StageLinearAxisRole::X},
             {"y",core::StageLinearAxisRole::Y},
             {"z",core::StageLinearAxisRole::Z}},linearRole,error)) return false;
        const JsonValue* geometryNode=find(*object,"rotationGeometry");
        if(geometryNode!=nullptr&&geometryNode->kind!=JsonValue::Kind::Null) {
            const JsonObject* geometryObject=nullptr;
            if(!objectValue(*geometryNode,path+".rotationGeometry",geometryObject,error)||
               !allowedKeys(*geometryObject,{"pointOnAxisNanometres","direction"},
                   path+".rotationGeometry",error)) return false;
            const JsonValue* pointNode=nullptr;
            core::StagePosition3DNanometres point;
            core::StageRotationAxisDirection direction{};
            if(!required(*geometryObject,"pointOnAxisNanometres",
                    path+".rotationGeometry",pointNode,error)||
               !parseStagePosition3D(*pointNode,
                    path+".rotationGeometry.pointOnAxisNanometres",point,error)||
               !requiredEnumField(*geometryObject,"direction",path+".rotationGeometry",
                    {{"x",core::StageRotationAxisDirection::X},
                     {"y",core::StageRotationAxisDirection::Y},
                     {"z",core::StageRotationAxisDirection::Z}},direction,error))
                return false;
            rotationGeometry=core::StageRotationAxisGeometry{point,direction};
        }
        if(motionType==core::AxisMotionType::Linear&&!linearRole)
            return fail(error,ErrorCode::MissingField,path+".linearRole",
                "Schema version 2 requires a linear X/Y/Z role.");
        if(motionType==core::AxisMotionType::Rotary&&!rotationGeometry)
            return fail(error,ErrorCode::MissingField,path+".rotationGeometry",
                "Schema version 2 requires rotary-axis geometry.");
    }

    auto created = core::AxisSpecification::create(
        std::move(*id), motionType, coordinateUnit, std::move(range).value(),
        std::move(*resolution), std::move(accuracy), std::move(repeatability),
        std::move(uncertainty), feedback, location,linearRole,rotationGeometry);
    if (!created) return fail(error, ErrorCode::DomainValidationFailed,
        path, "Core rejected the stage-axis specification.");
    output = std::move(created).value();
    return true;
}

bool parseStageSpecification(
    const JsonValue& value,
    const std::string& path,
    std::optional<core::StageSpecification>& output,
    Error& error,std::uint64_t schemaVersion) {
    const JsonObject* object=nullptr;
    if(!objectValue(value,path,object,error)) return false;
    if(schemaVersion==EquipmentDefinitionJsonLoader::kLegacySchemaVersion) {
        if(!allowedKeys(*object,{"componentId","axes"},path,error)) return false;
    } else if(!allowedKeys(*object,{"componentId","axes","configurations"},path,error))
        return false;
    std::optional<core::EquipmentComponentId> component;
    if (!requiredId(*object, "componentId", path, component, error)) return false;
    const JsonValue* axesNode = nullptr;
    if (!requiredArray(*object, "axes", path, axesNode, error)) return false;
    std::vector<core::AxisSpecification> axes;
    if(!parseArray(*axesNode,path+".axes",axes,error,
        [&schemaVersion](const JsonValue& item,const std::string& itemPath,
            std::optional<core::AxisSpecification>& parsed,Error& itemError) {
            return parseStageAxisSpecification(item,itemPath,parsed,itemError,schemaVersion);
        })) return false;

    std::vector<core::StageConfigurationSpecification> configurations;
    if(schemaVersion>=2) {
        const JsonValue* configurationsNode=nullptr;
        if(!requiredArray(*object,"configurations",path,configurationsNode,error)||
           !parseArray(*configurationsNode,path+".configurations",configurations,error,
               parseStageConfigurationSpecification)) return false;
    }
    auto created = core::StageSpecification::create(
        std::move(*component),std::move(axes),std::move(configurations));
    if (!created) return fail(error, ErrorCode::DomainValidationFailed,
        path, "Core rejected the stage or its axes.");
    output = std::move(created).value();
    return true;
}

template <typename T, typename Parser>
bool parseOptionalArrayField(
    const JsonObject& object,
    const char* key,
    const std::string& path,
    std::vector<T>& output,
    Error& error,
    Parser&& parser) {
    const JsonValue* node = nullptr;
    if (!optionalArray(object, key, path, node, error)) return false;
    if (node == nullptr || node->kind == JsonValue::Kind::Null) {
        output.clear();
        return true;
    }
    return parseArray(*node, path + "." + key, output, error,
        std::forward<Parser>(parser));
}

bool decodeEquipmentDefinition(
    const JsonValue& root,
    std::optional<core::MicroscopeEquipmentDefinition>& output,
    Error& error) {
    const JsonObject* object = nullptr;
    if(!objectValue(root,"$",object,error)) return false;

    const JsonValue* schemaNode=nullptr;
    std::uint64_t schemaVersion=0;
    if(!required(*object,"schemaVersion","$",schemaNode,error)||
       !unsignedValue(*schemaNode,"$.schemaVersion",schemaVersion,error)) return false;
    if(schemaVersion!=EquipmentDefinitionJsonLoader::kLegacySchemaVersion&&
       schemaVersion!=EquipmentDefinitionJsonLoader::kSupportedSchemaVersion) {
        return fail(error,ErrorCode::UnsupportedSchemaVersion,"$.schemaVersion",
            "Supported equipment-definition JSON schema versions are 1 and 2.");
    }
    if(schemaVersion==EquipmentDefinitionJsonLoader::kLegacySchemaVersion) {
        if(!allowedKeys(*object,
            {"schemaVersion","microscopeId","revision",
             "components","connections","opticalPaths","lightPaths",
             "objectiveSpecifications","cameraSpecifications",
             "condenserSpecifications","illuminationSpecifications",
             "stageSpecifications",
             "filterSpecifications","opticalRelaySpecifications"},
            "$",error)) return false;
    } else {
        if(!allowedKeys(*object,
            {"schemaVersion","microscopeId","revision",
             "components","connections","opticalPaths","lightPaths",
             "objectiveSpecifications","cameraSpecifications",
             "condenserSpecifications","illuminationSpecifications",
             "stageSpecifications","filterSpecifications","opticalRelaySpecifications"},
            "$",error)) return false;
    }

    std::optional<core::MicroscopeId> microscopeId;
    std::uint64_t revision = 0;
    if (!requiredId(*object, "microscopeId", "$", microscopeId, error) ||
        !required(*object, "revision", "$", schemaNode, error) ||
        !unsignedValue(*schemaNode, "$.revision", revision, error)) return false;
    if (revision == 0 ||
        revision > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return fail(error, ErrorCode::InvalidValue, "$.revision",
            "Equipment revision must be positive and representable by the persistence layer.");
    }

    const JsonValue* componentsNode = nullptr;
    const JsonValue* connectionsNode = nullptr;
    const JsonValue* opticalPathsNode = nullptr;
    if (!requiredArray(*object, "components", "$", componentsNode, error) ||
        !requiredArray(*object, "connections", "$", connectionsNode, error) ||
        !requiredArray(*object, "opticalPaths", "$", opticalPathsNode, error)) return false;

    std::vector<core::EquipmentComponent> components;
    if (!parseArray(*componentsNode, "$.components", components, error,
        parseComponent)) return false;

    std::vector<core::OpticalConnection> connections;
    if (!parseArray(*connectionsNode, "$.connections", connections, error,
        parseConnection)) return false;

    std::vector<core::OpticalPath> opticalPaths;
    if (!parseArray(*opticalPathsNode, "$.opticalPaths", opticalPaths, error,
        parseOpticalPath)) return false;

    std::vector<core::LightPath> lightPaths;
    if (!parseOptionalArrayField(*object, "lightPaths", "$",
        lightPaths, error, parseLightPath)) return false;

    std::vector<core::ObjectiveSpecification> objectives;
    if (!parseOptionalArrayField(*object, "objectiveSpecifications", "$",
        objectives, error, parseObjective)) return false;

    std::vector<core::CameraSpecification> cameras;
    if (!parseOptionalArrayField(*object, "cameraSpecifications", "$",
        cameras, error, parseCameraSpecification)) return false;

    std::vector<core::CondenserSpecification> condensers;
    if (!parseOptionalArrayField(*object, "condenserSpecifications", "$",
        condensers, error, parseCondenser)) return false;

    std::vector<core::IlluminationSpecification> illuminations;
    if (!parseOptionalArrayField(*object, "illuminationSpecifications", "$",
        illuminations, error, parseIlluminationSpecification)) return false;

    std::vector<core::StageSpecification> stages;
    if(!parseOptionalArrayField(*object,"stageSpecifications","$",
        stages,error,
        [schemaVersion](const JsonValue& item,const std::string& itemPath,
            std::optional<core::StageSpecification>& parsed,Error& itemError) {
            return parseStageSpecification(item,itemPath,parsed,itemError,schemaVersion);
        })) return false;


    std::vector<core::OpticalFilterSpecification> filters;
    if (!parseOptionalArrayField(*object, "filterSpecifications", "$",
        filters, error, parseFilter)) return false;

    std::vector<core::OpticalRelaySpecification> relays;
    if (!parseOptionalArrayField(*object, "opticalRelaySpecifications", "$",
        relays, error, parseOpticalRelay)) return false;

    auto created = core::MicroscopeEquipmentDefinition::create(
        std::move(*microscopeId),
        std::move(components),
        std::move(connections),
        std::move(opticalPaths),
        std::move(objectives),
        std::move(cameras),
        std::move(condensers),
        std::move(illuminations),
        std::move(stages),
        std::move(lightPaths),
        std::move(filters),
        revision,
        std::move(relays));
    if (!created) {
        return fail(error, ErrorCode::DomainValidationFailed, "$",
            "Core rejected the equipment graph. Check component/port identities, connection ownership, path direction and adjacency, LightPath camera endpoints, Stage axis/configuration references, and specification component kinds.");
    }
    output = std::move(created).value();
    return true;
}

} // namespace

core::Result<core::MicroscopeEquipmentDefinition, EquipmentDefinitionJsonError>
EquipmentDefinitionJsonLoader::parse(std::string_view document) {
    Error error{};
    if (document.empty()) {
        setError(error, ErrorCode::EmptyDocument, "$",
            "Equipment-definition JSON document is empty.");
        return core::Result<core::MicroscopeEquipmentDefinition, Error>::failure(
            std::move(error));
    }
    if (document.size() > kMaximumDocumentBytes) {
        setError(error, ErrorCode::DocumentTooLarge, "$",
            "Equipment-definition JSON document exceeds the configured size limit.");
        return core::Result<core::MicroscopeEquipmentDefinition, Error>::failure(
            std::move(error));
    }

    JsonValue root;
    JsonParser parser(document, error);
    if (!parser.parse(root)) {
        return core::Result<core::MicroscopeEquipmentDefinition, Error>::failure(
            std::move(error));
    }

    std::optional<core::MicroscopeEquipmentDefinition> equipment;
    if (!decodeEquipmentDefinition(root, equipment, error) || !equipment) {
        if (error.detail.empty()) {
            setError(error, ErrorCode::DomainValidationFailed, "$",
                "Equipment-definition document could not be decoded.");
        }
        return core::Result<core::MicroscopeEquipmentDefinition, Error>::failure(
            std::move(error));
    }

    return core::Result<core::MicroscopeEquipmentDefinition, Error>::success(
        std::move(*equipment));
}

core::Result<core::MicroscopeEquipmentDefinition, EquipmentDefinitionJsonError>
EquipmentDefinitionJsonLoader::loadFile(const std::string& path) {
    Error error{};
    if (path.empty()) {
        setError(error, ErrorCode::FileOpenFailed, "$",
            "Equipment-definition file path is empty.");
        return core::Result<core::MicroscopeEquipmentDefinition, Error>::failure(
            std::move(error));
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        setError(error, ErrorCode::FileOpenFailed, "$",
            "Could not open the equipment-definition JSON file.");
        return core::Result<core::MicroscopeEquipmentDefinition, Error>::failure(
            std::move(error));
    }

    stream.seekg(0, std::ios::end);
    const std::streampos end = stream.tellg();
    if (end < std::streampos(0)) {
        setError(error, ErrorCode::FileReadFailed, "$",
            "Could not determine the equipment-definition file size.");
        return core::Result<core::MicroscopeEquipmentDefinition, Error>::failure(
            std::move(error));
    }
    const auto size = static_cast<std::uint64_t>(end);
    if (size > kMaximumDocumentBytes) {
        setError(error, ErrorCode::DocumentTooLarge, "$",
            "Equipment-definition JSON file exceeds the configured size limit.");
        return core::Result<core::MicroscopeEquipmentDefinition, Error>::failure(
            std::move(error));
    }
    if (size == 0) return parse({});

    std::string document(static_cast<std::size_t>(size), '\0');
    stream.seekg(0, std::ios::beg);
    stream.read(document.data(), static_cast<std::streamsize>(document.size()));
    if (stream.gcount() != static_cast<std::streamsize>(document.size())) {
        setError(error, ErrorCode::FileReadFailed, "$",
            "Equipment-definition JSON file could not be read completely.");
        return core::Result<core::MicroscopeEquipmentDefinition, Error>::failure(
            std::move(error));
    }
    return parse(document);
}

} // namespace microscope::application::configuration
