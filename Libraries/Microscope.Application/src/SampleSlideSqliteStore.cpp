#include "microscope/application/persistence/SampleSlideSqliteStore.h"

#include <sqlite3.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace microscope::application::persistence {
namespace {

using StoreError = SampleSlideStoreError;
using StoreErrorCode = SampleSlideStoreErrorCode;
using namespace core;

constexpr std::uint32_t kPayloadFormatVersion = 1;
constexpr std::uint32_t kMaximumCollectionCount = 100000;
constexpr std::uint32_t kMaximumStringBytes = 1024 * 1024;
constexpr std::uint64_t kMaximumSqliteInteger =
    static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max());

[[nodiscard]] StoreError makeError(
    StoreErrorCode code,
    sqlite3* database,
    std::string detail = {}) {
    const int nativeCode = database == nullptr ? 0 : sqlite3_errcode(database);
    if (detail.empty() && database != nullptr) {
        detail = sqlite3_errmsg(database);
    }
    return StoreError{code, nativeCode, std::move(detail)};
}

[[nodiscard]] Status<StoreError> failureStatus(StoreError error) {
    return Status<StoreError>::failure(std::move(error));
}

class Statement final {
public:
    Statement(sqlite3* database, const char* sql) noexcept
        : database_(database) {
        result_ = sqlite3_prepare_v2(database_, sql, -1, &statement_, nullptr);
    }

    ~Statement() {
        if (statement_ != nullptr) {
            sqlite3_finalize(statement_);
        }
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    [[nodiscard]] bool prepared() const noexcept {
        return result_ == SQLITE_OK && statement_ != nullptr;
    }

    [[nodiscard]] sqlite3_stmt* get() const noexcept {
        return statement_;
    }

    [[nodiscard]] bool bindText(int index, const std::string& value) noexcept {
        if (value.find('\0') != std::string::npos ||
            value.size() > static_cast<std::size_t>(
                               std::numeric_limits<int>::max())) {
            return false;
        }
        return sqlite3_bind_text(
            statement_, index, value.data(),
            static_cast<int>(value.size()), SQLITE_TRANSIENT) == SQLITE_OK;
    }

    [[nodiscard]] bool bindInt(int index, int value) noexcept {
        return sqlite3_bind_int(statement_, index, value) == SQLITE_OK;
    }

    [[nodiscard]] bool bindInt64(int index, std::uint64_t value) noexcept {
        if (value > kMaximumSqliteInteger) {
            return false;
        }
        return sqlite3_bind_int64(
            statement_, index, static_cast<sqlite3_int64>(value)) == SQLITE_OK;
    }

    [[nodiscard]] bool bindBlob(
        int index, const std::vector<std::uint8_t>& value) noexcept {
        if (value.empty() ||
            value.size() > static_cast<std::size_t>(
                               std::numeric_limits<int>::max())) {
            return false;
        }
        return sqlite3_bind_blob(
            statement_, index, value.data(),
            static_cast<int>(value.size()), SQLITE_TRANSIENT) == SQLITE_OK;
    }

    [[nodiscard]] bool bindOptionalDouble(
        int index, const std::optional<double>& value) noexcept {
        return value
            ? sqlite3_bind_double(statement_, index, *value) == SQLITE_OK
            : sqlite3_bind_null(statement_, index) == SQLITE_OK;
    }

    [[nodiscard]] bool bindOptionalText(
        int index, const std::optional<std::string>& value) noexcept {
        if (!value) {
            return sqlite3_bind_null(statement_, index) == SQLITE_OK;
        }
        return bindText(index, *value);
    }

    [[nodiscard]] int step() noexcept {
        return sqlite3_step(statement_);
    }

private:
    sqlite3* database_ = nullptr;
    sqlite3_stmt* statement_ = nullptr;
    int result_ = SQLITE_ERROR;
};

class Transaction final {
public:
    explicit Transaction(sqlite3* database) noexcept
        : database_(database),
          active_(sqlite3_exec(
              database_, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) ==
              SQLITE_OK) {}

    ~Transaction() {
        if (active_) {
            sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
        }
    }

    [[nodiscard]] bool active() const noexcept {
        return active_;
    }

    [[nodiscard]] bool commit() noexcept {
        if (!active_ ||
            sqlite3_exec(database_, "COMMIT", nullptr, nullptr, nullptr) !=
                SQLITE_OK) {
            return false;
        }
        active_ = false;
        return true;
    }

private:
    sqlite3* database_ = nullptr;
    bool active_ = false;
};

// Payloads are canonical little-endian values. Never persist native C++ object
// memory: padding, enum ABI, host endianness and compiler layout are not stable.
class BinaryWriter final {
public:
    void u8(std::uint8_t value) {
        bytes_.push_back(value);
    }

    void boolean(bool value) {
        u8(value ? 1U : 0U);
    }

    void u32(std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8) {
            u8(static_cast<std::uint8_t>((value >> shift) & 0xffU));
        }
    }

    void i32(std::int32_t value) {
        u32(static_cast<std::uint32_t>(value));
    }

    void u64(std::uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8) {
            u8(static_cast<std::uint8_t>((value >> shift) & 0xffU));
        }
    }

    void real(double value) {
        static_assert(
            sizeof(double) == sizeof(std::uint64_t),
            "The persistence codec requires 64-bit double.");
        static_assert(
            std::numeric_limits<double>::is_iec559,
            "The persistence codec requires IEEE-754 doubles.");
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        u64(bits);
    }

    void string(const std::string& value) {
        if (value.size() > kMaximumStringBytes ||
            value.size() > std::numeric_limits<std::uint32_t>::max() ||
            value.find('\0') != std::string::npos) {
            valid_ = false;
            return;
        }
        u32(static_cast<std::uint32_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    [[nodiscard]] bool count(std::size_t value) {
        if (value > kMaximumCollectionCount ||
            value > std::numeric_limits<std::uint32_t>::max()) {
            valid_ = false;
            return false;
        }
        u32(static_cast<std::uint32_t>(value));
        return true;
    }

    [[nodiscard]] bool valid() const noexcept {
        return valid_;
    }

    void optionalString(const std::optional<std::string>& value) {
        boolean(value.has_value());
        if (value) {
            string(*value);
        }
    }

    void optionalReal(const std::optional<double>& value) {
        boolean(value.has_value());
        if (value) {
            real(*value);
        }
    }

    template <typename TEnum>
    void enumeration(TEnum value) {
        i32(static_cast<std::int32_t>(value));
    }

    template <typename TEnum>
    void enumerations(const std::vector<TEnum>& values) {
        if (!count(values.size())) {
            return;
        }
        for (const auto value : values) {
            enumeration(value);
        }
    }

    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept {
        return bytes_;
    }

private:
    std::vector<std::uint8_t> bytes_;
    bool valid_ = true;
};

class BinaryReader final {
public:
    BinaryReader(const void* data, std::size_t size) noexcept
        : data_(static_cast<const std::uint8_t*>(data)), size_(size) {}

    [[nodiscard]] bool u8(std::uint8_t& value) noexcept {
        if (offset_ >= size_) {
            return false;
        }
        value = data_[offset_++];
        return true;
    }

    [[nodiscard]] bool boolean(bool& value) noexcept {
        std::uint8_t raw = 0;
        if (!u8(raw) || raw > 1U) {
            return false;
        }
        value = raw != 0U;
        return true;
    }

    [[nodiscard]] bool u32(std::uint32_t& value) noexcept {
        if (remaining() < 4) {
            return false;
        }
        value = 0;
        for (unsigned shift = 0; shift < 32; shift += 8) {
            value |= static_cast<std::uint32_t>(data_[offset_++]) << shift;
        }
        return true;
    }

    [[nodiscard]] bool i32(std::int32_t& value) noexcept {
        std::uint32_t raw = 0;
        if (!u32(raw)) {
            return false;
        }
        value = static_cast<std::int32_t>(raw);
        return true;
    }

    [[nodiscard]] bool u64(std::uint64_t& value) noexcept {
        if (remaining() < 8) {
            return false;
        }
        value = 0;
        for (unsigned shift = 0; shift < 64; shift += 8) {
            value |= static_cast<std::uint64_t>(data_[offset_++]) << shift;
        }
        return true;
    }

    [[nodiscard]] bool real(double& value) noexcept {
        std::uint64_t bits = 0;
        if (!u64(bits)) {
            return false;
        }
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    }

    [[nodiscard]] bool string(std::string& value) {
        std::uint32_t length = 0;
        if (!u32(length) || length > kMaximumStringBytes ||
            remaining() < length) {
            return false;
        }
        value.assign(
            reinterpret_cast<const char*>(data_ + offset_),
            static_cast<std::size_t>(length));
        offset_ += length;
        return value.find('\0') == std::string::npos;
    }

    [[nodiscard]] bool optionalString(
        std::optional<std::string>& value) {
        bool present = false;
        if (!boolean(present)) {
            return false;
        }
        if (!present) {
            value.reset();
            return true;
        }
        std::string decoded;
        if (!string(decoded)) {
            return false;
        }
        value = std::move(decoded);
        return true;
    }

    [[nodiscard]] bool optionalReal(std::optional<double>& value) {
        bool present = false;
        if (!boolean(present)) {
            return false;
        }
        if (!present) {
            value.reset();
            return true;
        }
        double decoded = 0.0;
        if (!real(decoded)) {
            return false;
        }
        value = decoded;
        return true;
    }

    template <typename TEnum>
    [[nodiscard]] bool enumeration(TEnum& value) noexcept {
        std::int32_t raw = 0;
        if (!i32(raw)) {
            return false;
        }
        value = static_cast<TEnum>(raw);
        return true;
    }

    template <typename TEnum>
    [[nodiscard]] bool enumerations(std::vector<TEnum>& values) {
        std::uint32_t count = 0;
        if (!u32(count) || count > kMaximumCollectionCount) {
            return false;
        }
        values.clear();
        values.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index) {
            TEnum value{};
            if (!enumeration(value)) {
                return false;
            }
            values.push_back(value);
        }
        return true;
    }

    [[nodiscard]] bool finished() const noexcept {
        return offset_ == size_;
    }

private:
    [[nodiscard]] std::size_t remaining() const noexcept {
        return offset_ <= size_ ? size_ - offset_ : 0;
    }

    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t offset_ = 0;
};

void writeBand(BinaryWriter& writer, const SpectralBand& band) {
    writer.real(band.minimumWavelengthNanometers());
    writer.real(band.maximumWavelengthNanometers());
}

void writeOptionalBand(
    BinaryWriter& writer, const std::optional<SpectralBand>& band) {
    writer.boolean(band.has_value());
    if (band) {
        writeBand(writer, *band);
    }
}

[[nodiscard]] bool readOptionalBand(
    BinaryReader& reader, std::optional<SpectralBand>& band) {
    bool present = false;
    if (!reader.boolean(present)) {
        return false;
    }
    if (!present) {
        band.reset();
        return true;
    }

    double minimum = 0.0;
    double maximum = 0.0;
    if (!reader.real(minimum) || !reader.real(maximum)) {
        return false;
    }
    auto minimumValue = PositiveNanometers::create(minimum);
    auto maximumValue = PositiveNanometers::create(maximum);
    if (!minimumValue || !maximumValue) {
        return false;
    }
    auto decoded = SpectralBand::create(
        std::move(minimumValue).value(),
        std::move(maximumValue).value());
    if (!decoded) {
        return false;
    }
    band = std::move(decoded).value();
    return true;
}

[[nodiscard]] bool encodeSampleTypePayload(
    const SampleType& sampleType,
    std::vector<std::uint8_t>& payload) {
    BinaryWriter writer;
    writer.string(sampleType.name());
    writer.enumeration(sampleType.materialClass());
    writer.optionalString(sampleType.description());

    const auto& requirements = sampleType.microscopyRequirements();
    writer.count(requirements.techniques().size());
    for (const auto& technique : requirements.techniques()) {
        writer.enumeration(technique.method);
        writer.enumeration(technique.kind);
    }
    writer.boolean(requirements.objective().has_value());
    if (requirements.objective()) {
        const auto& objective = *requirements.objective();
        writer.optionalReal(objective.minimumMagnification());
        writer.optionalReal(objective.maximumMagnification());
        writer.optionalReal(objective.minimumNumericalAperture());
        writer.optionalReal(
            objective.maximumCoverglassDesignMismatchMicrometers());
        writer.enumerations(objective.allowedImmersionMedia());
    }

    writer.boolean(requirements.camera().has_value());
    if (requirements.camera()) {
        const auto& camera = *requirements.camera();
        writer.optionalReal(camera.minimumFrameRateFramesPerSecond());
        writer.optionalReal(camera.maximumReadNoiseElectronsRms());
        writer.optionalReal(
            camera.maximumDarkCurrentElectronsPerPixelPerSecond());
        writer.optionalReal(camera.minimumFullWellCapacityElectrons());
        writer.optionalReal(camera.minimumMeanQuantumEfficiency());
        writer.boolean(camera.preferMonochrome());
        writer.boolean(camera.preferGlobalShutter());
    }

    writer.boolean(requirements.condenser().has_value());
    if (requirements.condenser()) {
        const auto& condenser = *requirements.condenser();
        writer.optionalReal(condenser.minimumApertureNA());
        writer.optionalReal(condenser.maximumApertureNA());
        writer.enumerations(condenser.allowedDesigns());
        writer.enumerations(condenser.allowedImmersionMedia());
    }

    writer.boolean(requirements.illumination().has_value());
    if (requirements.illumination()) {
        const auto& illumination = *requirements.illumination();
        writer.optionalReal(
            illumination.minimumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter());
        writer.optionalReal(
            illumination.maximumSpecimenPlaneIrradianceMicrowattsPerSquareMillimeter());
        writeOptionalBand(writer, illumination.requiredSpectralBand());
        writer.boolean(illumination.requireContinuouslyAdjustableSource());
    }
    writer.boolean(requirements.requireFluorescentLabelMetadata());

    writer.boolean(sampleType.preparationRequirements().has_value());
    if (sampleType.preparationRequirements()) {
        const auto& preparation = *sampleType.preparationRequirements();
        writer.enumerations(preparation.allowedMethods());
        writer.enumerations(preparation.allowedFixationStates());
        writer.optionalReal(preparation.minimumSectionThicknessMicrometers());
        writer.optionalReal(preparation.maximumSectionThicknessMicrometers());
        writer.optionalReal(preparation.minimumCoverglassThicknessMicrometers());
        writer.optionalReal(preparation.maximumCoverglassThicknessMicrometers());
        writer.boolean(preparation.requireMountingMedium());
        writer.enumerations(preparation.requiredLabelKinds());
    }
    if (!writer.valid() ||
        writer.bytes().size() > 64U * 1024U * 1024U) {
        return false;
    }
    payload = writer.bytes();
    return true;
}

[[nodiscard]] std::optional<SampleType> decodeSampleTypePayload(
    const SampleTypeId& id,
    std::uint64_t revision,
    const void* data,
    std::size_t size) {
    BinaryReader reader(data, size);
    std::string name;
    SampleMaterialClass materialClass = SampleMaterialClass::Unknown;
    std::optional<std::string> description;
    if (!reader.string(name) ||
        !reader.enumeration(materialClass) ||
        !reader.optionalString(description)) {
        return std::nullopt;
    }

    std::uint32_t techniqueCount = 0;
    if (!reader.u32(techniqueCount) ||
        techniqueCount > kMaximumCollectionCount) {
        return std::nullopt;
    }
    std::vector<MicroscopyTechniqueConstraint> techniques;
    techniques.reserve(techniqueCount);
    for (std::uint32_t index = 0; index < techniqueCount; ++index) {
        MicroscopyTechniqueConstraint value;
        if (!reader.enumeration(value.method) ||
            !reader.enumeration(value.kind)) {
            return std::nullopt;
        }
        techniques.push_back(value);
    }

    bool present = false;
    std::optional<ObjectiveMicroscopyRequirements> objective;
    if (!reader.boolean(present)) {
        return std::nullopt;
    }
    if (present) {
        std::optional<double> minimumMagnification;
        std::optional<double> maximumMagnification;
        std::optional<double> minimumNA;
        std::optional<double> maximumCoverglassMismatch;
        std::vector<ObjectiveImmersionMedium> media;
        if (!reader.optionalReal(minimumMagnification) ||
            !reader.optionalReal(maximumMagnification) ||
            !reader.optionalReal(minimumNA) ||
            !reader.optionalReal(maximumCoverglassMismatch) ||
            !reader.enumerations(media)) {
            return std::nullopt;
        }
        auto value = ObjectiveMicroscopyRequirements::create(
            minimumMagnification,
            maximumMagnification,
            minimumNA,
            std::move(media),
            maximumCoverglassMismatch);
        if (!value) {
            return std::nullopt;
        }
        objective = std::move(value).value();
    }

    std::optional<CameraPerformanceRequirements> camera;
    if (!reader.boolean(present)) {
        return std::nullopt;
    }
    if (present) {
        std::optional<double> minimumFrameRate;
        std::optional<double> maximumReadNoise;
        std::optional<double> maximumDarkCurrent;
        std::optional<double> minimumFullWell;
        std::optional<double> minimumQuantumEfficiency;
        bool preferMonochrome = false;
        bool preferGlobalShutter = false;
        if (!reader.optionalReal(minimumFrameRate) ||
            !reader.optionalReal(maximumReadNoise) ||
            !reader.optionalReal(maximumDarkCurrent) ||
            !reader.optionalReal(minimumFullWell) ||
            !reader.optionalReal(minimumQuantumEfficiency) ||
            !reader.boolean(preferMonochrome) ||
            !reader.boolean(preferGlobalShutter)) {
            return std::nullopt;
        }
        auto value = CameraPerformanceRequirements::create(
            minimumFrameRate,
            maximumReadNoise,
            maximumDarkCurrent,
            minimumFullWell,
            minimumQuantumEfficiency,
            preferMonochrome,
            preferGlobalShutter);
        if (!value) {
            return std::nullopt;
        }
        camera = std::move(value).value();
    }

    std::optional<CondenserMicroscopyRequirements> condenser;
    if (!reader.boolean(present)) {
        return std::nullopt;
    }
    if (present) {
        std::optional<double> minimumNA;
        std::optional<double> maximumNA;
        std::vector<CondenserDesign> designs;
        std::vector<CondenserImmersionMedium> media;
        if (!reader.optionalReal(minimumNA) ||
            !reader.optionalReal(maximumNA) ||
            !reader.enumerations(designs) ||
            !reader.enumerations(media)) {
            return std::nullopt;
        }
        auto value = CondenserMicroscopyRequirements::create(
            minimumNA, maximumNA, std::move(designs), std::move(media));
        if (!value) {
            return std::nullopt;
        }
        condenser = std::move(value).value();
    }

    std::optional<IlluminationMicroscopyRequirements> illumination;
    if (!reader.boolean(present)) {
        return std::nullopt;
    }
    if (present) {
        std::optional<double> minimumIrradiance;
        std::optional<double> maximumIrradiance;
        std::optional<SpectralBand> spectralBand;
        bool requireContinuous = false;
        if (!reader.optionalReal(minimumIrradiance) ||
            !reader.optionalReal(maximumIrradiance) ||
            !readOptionalBand(reader, spectralBand) ||
            !reader.boolean(requireContinuous)) {
            return std::nullopt;
        }
        auto value = IlluminationMicroscopyRequirements::create(
            minimumIrradiance,
            maximumIrradiance,
            std::move(spectralBand),
            requireContinuous);
        if (!value) {
            return std::nullopt;
        }
        illumination = std::move(value).value();
    }

    bool requireFluorescentLabels = false;
    if (!reader.boolean(requireFluorescentLabels)) {
        return std::nullopt;
    }
    auto microscopyRequirements = MicroscopyRequirements::create(
        std::move(techniques),
        std::move(objective),
        std::move(camera),
        requireFluorescentLabels,
        std::move(condenser),
        std::move(illumination));
    if (!microscopyRequirements) {
        return std::nullopt;
    }

    std::optional<SlidePreparationRequirements> preparationRequirements;
    if (!reader.boolean(present)) {
        return std::nullopt;
    }
    if (present) {
        std::vector<SpecimenPreparationMethod> methods;
        std::vector<SpecimenFixationState> fixationStates;
        std::optional<double> minimumSection;
        std::optional<double> maximumSection;
        std::optional<double> minimumCoverglass;
        std::optional<double> maximumCoverglass;
        bool requireMountingMedium = false;
        std::vector<SpecimenLabelKind> requiredLabels;
        if (!reader.enumerations(methods) ||
            !reader.enumerations(fixationStates) ||
            !reader.optionalReal(minimumSection) ||
            !reader.optionalReal(maximumSection) ||
            !reader.optionalReal(minimumCoverglass) ||
            !reader.optionalReal(maximumCoverglass) ||
            !reader.boolean(requireMountingMedium) ||
            !reader.enumerations(requiredLabels)) {
            return std::nullopt;
        }
        auto value = SlidePreparationRequirements::create(
            std::move(methods),
            std::move(fixationStates),
            minimumSection,
            maximumSection,
            minimumCoverglass,
            maximumCoverglass,
            requireMountingMedium,
            std::move(requiredLabels));
        if (!value) {
            return std::nullopt;
        }
        preparationRequirements = std::move(value).value();
    }

    if (!reader.finished()) {
        return std::nullopt;
    }
    auto result = SampleType::create(
        id,
        revision,
        std::move(name),
        materialClass,
        std::move(microscopyRequirements).value(),
        std::move(description),
        std::move(preparationRequirements));
    if (!result) {
        return std::nullopt;
    }
    return std::move(result).value();
}

[[nodiscard]] bool encodeSlidePayload(
    const Slide& slide, std::vector<std::uint8_t>& payload) {
    BinaryWriter writer;
    writer.string(slide.name());
    writer.boolean(slide.originLabId().has_value());
    if (slide.originLabId()) {
        writer.string(slide.originLabId()->value());
    }
    writer.real(slide.geometry().widthMicrometers());
    writer.real(slide.geometry().heightMicrometers());

    const auto& preparation = slide.preparation();
    writer.enumeration(preparation.method());
    writer.enumeration(preparation.fixationState());
    writer.optionalReal(preparation.sectionThicknessMicrometers());
    writer.optionalReal(preparation.coverglassThicknessMicrometers());
    writer.optionalString(preparation.mountingMediumName());
    writer.optionalReal(preparation.mountingMediumRefractiveIndex());

    writer.count(preparation.labels().size());
    for (const auto& label : preparation.labels()) {
        writer.string(label.name());
        writer.enumeration(label.kind());
        writeOptionalBand(writer, label.excitationBand());
        writeOptionalBand(writer, label.emissionBand());

        writer.boolean(label.emissionSpectrum().has_value());
        if (label.emissionSpectrum()) {
            const auto& samples = label.emissionSpectrum()->samples();
            writer.count(samples.size());
            for (const auto& sample : samples) {
                writer.real(sample.wavelengthNanometers);
                writer.real(sample.relativePower);
            }
        }

        writer.boolean(label.filterSetCompatibilityId().has_value());
        if (label.filterSetCompatibilityId()) {
            writer.string(label.filterSetCompatibilityId()->value());
        }
    }
    if (!writer.valid() ||
        writer.bytes().size() > 64U * 1024U * 1024U) {
        return false;
    }
    payload = writer.bytes();
    return true;
}

[[nodiscard]] std::optional<Slide> decodeSlidePayload(
    const SlideId& id,
    std::uint64_t revision,
    const SampleTypeId& sampleTypeId,
    std::uint64_t sampleTypeRevision,
    const void* data,
    std::size_t size) {
    BinaryReader reader(data, size);
    std::string name;
    bool hasOriginLabId = false;
    std::optional<OriginLabId> originLabId;
    double width = 0.0;
    double height = 0.0;
    SpecimenPreparationMethod method = SpecimenPreparationMethod::Unknown;
    SpecimenFixationState fixation = SpecimenFixationState::Unknown;
    std::optional<double> sectionThickness;
    std::optional<double> coverglassThickness;
    std::optional<std::string> mountingMedium;
    std::optional<double> refractiveIndex;
    if (!reader.string(name) || !reader.boolean(hasOriginLabId)) {
        return std::nullopt;
    }
    if (hasOriginLabId) {
        std::string rawOriginLabId;
        if (!reader.string(rawOriginLabId)) {
            return std::nullopt;
        }
        auto createdOriginLabId = OriginLabId::create(rawOriginLabId);
        if (!createdOriginLabId) {
            return std::nullopt;
        }
        originLabId = std::move(createdOriginLabId).value();
    }
    if (!reader.real(width) ||
        !reader.real(height) ||
        !reader.enumeration(method) ||
        !reader.enumeration(fixation) ||
        !reader.optionalReal(sectionThickness) ||
        !reader.optionalReal(coverglassThickness) ||
        !reader.optionalString(mountingMedium) ||
        !reader.optionalReal(refractiveIndex)) {
        return std::nullopt;
    }

    std::uint32_t labelCount = 0;
    if (!reader.u32(labelCount) || labelCount > kMaximumCollectionCount) {
        return std::nullopt;
    }
    std::vector<SpecimenLabel> labels;
    labels.reserve(labelCount);
    for (std::uint32_t index = 0; index < labelCount; ++index) {
        std::string labelName;
        SpecimenLabelKind kind = SpecimenLabelKind::Other;
        std::optional<SpectralBand> excitation;
        std::optional<SpectralBand> emission;
        bool hasSpectrum = false;
        if (!reader.string(labelName) ||
            !reader.enumeration(kind) ||
            !readOptionalBand(reader, excitation) ||
            !readOptionalBand(reader, emission) ||
            !reader.boolean(hasSpectrum)) {
            return std::nullopt;
        }

        std::optional<SpectralPowerDistribution> spectrum;
        if (hasSpectrum) {
            std::uint32_t sampleCount = 0;
            if (!reader.u32(sampleCount) ||
                sampleCount > kMaximumCollectionCount) {
                return std::nullopt;
            }
            std::vector<SpectralPowerSample> samples;
            samples.reserve(sampleCount);
            for (std::uint32_t sampleIndex = 0;
                 sampleIndex < sampleCount;
                 ++sampleIndex) {
                SpectralPowerSample sample;
                if (!reader.real(sample.wavelengthNanometers) ||
                    !reader.real(sample.relativePower)) {
                    return std::nullopt;
                }
                samples.push_back(sample);
            }
            auto created = SpectralPowerDistribution::create(std::move(samples));
            if (!created) {
                return std::nullopt;
            }
            spectrum = std::move(created).value();
        }

        bool hasFilterSet = false;
        if (!reader.boolean(hasFilterSet)) {
            return std::nullopt;
        }
        std::optional<OpticalCompatibilityId> filterSet;
        if (hasFilterSet) {
            std::string raw;
            if (!reader.string(raw)) {
                return std::nullopt;
            }
            auto created = OpticalCompatibilityId::create(raw);
            if (!created) {
                return std::nullopt;
            }
            filterSet = std::move(created).value();
        }

        auto label = SpecimenLabel::create(
            std::move(labelName),
            kind,
            std::move(excitation),
            std::move(emission),
            std::move(spectrum),
            std::move(filterSet));
        if (!label) {
            return std::nullopt;
        }
        labels.push_back(std::move(label).value());
    }

    if (!reader.finished()) {
        return std::nullopt;
    }
    auto geometry = SlideGeometry::create(width, height);
    if (!geometry) {
        return std::nullopt;
    }
    auto preparation = SlidePreparation::create(
        method,
        fixation,
        sectionThickness,
        coverglassThickness,
        std::move(mountingMedium),
        refractiveIndex,
        std::move(labels));
    if (!preparation) {
        return std::nullopt;
    }
    auto slide = Slide::create(
        id,
        revision,
        sampleTypeId,
        sampleTypeRevision,
        std::move(name),
        std::move(geometry).value(),
        std::move(preparation).value(),
        std::move(originLabId));
    if (!slide) {
        return std::nullopt;
    }
    return std::move(slide).value();
}

[[nodiscard]] bool revisionFitsSqlite(std::uint64_t revision) noexcept {
    return revision > 0 && revision <= kMaximumSqliteInteger;
}

[[nodiscard]] bool isConstraintViolation(sqlite3* database) noexcept {
    return (sqlite3_extended_errcode(database) & 0xff) == SQLITE_CONSTRAINT;
}

[[nodiscard]] bool readCurrentRevision(
    sqlite3* database,
    const char* table,
    const char* idColumn,
    const std::string& id,
    std::optional<std::uint64_t>& revision) {
    const std::string sql =
        std::string("SELECT revision FROM ") + table +
        " WHERE " + idColumn + " = ?";
    Statement statement(database, sql.c_str());
    if (!statement.prepared() || !statement.bindText(1, id)) {
        return false;
    }
    const int step = statement.step();
    if (step == SQLITE_DONE) {
        revision.reset();
        return true;
    }
    if (step != SQLITE_ROW) {
        return false;
    }
    const sqlite3_int64 stored = sqlite3_column_int64(statement.get(), 0);
    if (stored <= 0) {
        return false;
    }
    revision = static_cast<std::uint64_t>(stored);
    return true;
}

[[nodiscard]] bool rowExists(
    sqlite3* database,
    const char* sql,
    const std::string& firstId,
    std::uint64_t revision,
    bool& exists) {
    Statement statement(database, sql);
    if (!statement.prepared() ||
        !statement.bindText(1, firstId) ||
        !statement.bindInt64(2, revision)) {
        return false;
    }
    const int step = statement.step();
    if (step == SQLITE_ROW) {
        exists = true;
        return true;
    }
    if (step == SQLITE_DONE) {
        exists = false;
        return true;
    }
    return false;
}

} // namespace

SampleSlideSqliteStore::SampleSlideSqliteStore(sqlite3* database) noexcept
    : database_(database) {}

core::Result<
    std::unique_ptr<SampleSlideSqliteStore>,
    SampleSlideStoreError> SampleSlideSqliteStore::open(
        const std::string& databasePath) {
    if (databasePath.empty()) {
        return core::Result<
            std::unique_ptr<SampleSlideSqliteStore>,
            SampleSlideStoreError>::failure(
                makeError(StoreErrorCode::EmptyDatabasePath, nullptr));
    }

    sqlite3* database = nullptr;
    const int result = sqlite3_open_v2(
        databasePath.c_str(),
        &database,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr);
    if (result != SQLITE_OK) {
        auto error = makeError(
            StoreErrorCode::DatabaseOpenFailed,
            database,
            database == nullptr
                ? "SQLite could not allocate a database handle."
                : sqlite3_errmsg(database));
        if (database != nullptr) {
            sqlite3_close_v2(database);
        }
        return core::Result<
            std::unique_ptr<SampleSlideSqliteStore>,
            SampleSlideStoreError>::failure(std::move(error));
    }

    sqlite3_busy_timeout(database, 5000);
    if (sqlite3_exec(
            database, "PRAGMA foreign_keys = ON", nullptr, nullptr, nullptr) !=
        SQLITE_OK) {
        auto error = makeError(StoreErrorCode::DatabaseOpenFailed, database);
        sqlite3_close_v2(database);
        return core::Result<
            std::unique_ptr<SampleSlideSqliteStore>,
            SampleSlideStoreError>::failure(std::move(error));
    }

    auto store = std::unique_ptr<SampleSlideSqliteStore>(
        new SampleSlideSqliteStore(database));
    const auto initialized = store->initializeSchema();
    if (!initialized) {
        return core::Result<
            std::unique_ptr<SampleSlideSqliteStore>,
            SampleSlideStoreError>::failure(initialized.error());
    }
    return core::Result<
        std::unique_ptr<SampleSlideSqliteStore>,
        SampleSlideStoreError>::success(std::move(store));
}

SampleSlideSqliteStore::~SampleSlideSqliteStore() {
    if (database_ != nullptr) {
        sqlite3_close_v2(database_);
        database_ = nullptr;
    }
}

core::Status<SampleSlideStoreError>
SampleSlideSqliteStore::initializeSchema() {
    std::lock_guard<std::mutex> lock(mutex_);
    Transaction transaction(database_);
    if (!transaction.active()) {
        return failureStatus(makeError(
            StoreErrorCode::SchemaMigrationFailed, database_));
    }

    constexpr const char* schemaSql = R"SQL(
        CREATE TABLE IF NOT EXISTS sample_slide_store_schema (
            id INTEGER PRIMARY KEY CHECK (id = 1),
            schema_version INTEGER NOT NULL
        );

        INSERT OR IGNORE INTO sample_slide_store_schema (id, schema_version)
        VALUES (1, 1);

        CREATE TABLE IF NOT EXISTS sample_type_revision (
            sample_type_id TEXT NOT NULL,
            revision INTEGER NOT NULL CHECK (revision > 0),
            payload_format_version INTEGER NOT NULL CHECK (payload_format_version > 0),
            payload BLOB NOT NULL,
            PRIMARY KEY (sample_type_id, revision)
        );

        CREATE TABLE IF NOT EXISTS sample_type_current (
            sample_type_id TEXT PRIMARY KEY,
            revision INTEGER NOT NULL CHECK (revision > 0),
            FOREIGN KEY (sample_type_id, revision)
                REFERENCES sample_type_revision (sample_type_id, revision)
        );

        CREATE TABLE IF NOT EXISTS slide_revision (
            slide_id TEXT NOT NULL,
            revision INTEGER NOT NULL CHECK (revision > 0),
            sample_type_id TEXT NOT NULL,
            sample_type_revision INTEGER NOT NULL CHECK (sample_type_revision > 0),
            payload_format_version INTEGER NOT NULL CHECK (payload_format_version > 0),
            payload BLOB NOT NULL,
            PRIMARY KEY (slide_id, revision),
            FOREIGN KEY (sample_type_id, sample_type_revision)
                REFERENCES sample_type_revision (sample_type_id, revision)
        );

        CREATE TABLE IF NOT EXISTS slide_current (
            slide_id TEXT PRIMARY KEY,
            revision INTEGER NOT NULL CHECK (revision > 0),
            FOREIGN KEY (slide_id, revision)
                REFERENCES slide_revision (slide_id, revision)
        );

        CREATE TRIGGER IF NOT EXISTS sample_type_revision_no_update
        BEFORE UPDATE ON sample_type_revision
        BEGIN SELECT RAISE(ABORT, 'SampleType revisions are immutable'); END;

        CREATE TRIGGER IF NOT EXISTS sample_type_revision_no_delete
        BEFORE DELETE ON sample_type_revision
        BEGIN SELECT RAISE(ABORT, 'SampleType revisions are immutable'); END;

        CREATE TRIGGER IF NOT EXISTS slide_revision_no_update
        BEFORE UPDATE ON slide_revision
        BEGIN SELECT RAISE(ABORT, 'Slide revisions are immutable'); END;

        CREATE TRIGGER IF NOT EXISTS slide_revision_no_delete
        BEFORE DELETE ON slide_revision
        BEGIN SELECT RAISE(ABORT, 'Slide revisions are immutable'); END;

        CREATE INDEX IF NOT EXISTS idx_slide_revision_sample_type
        ON slide_revision (sample_type_id, sample_type_revision);
    )SQL";

    char* errorMessage = nullptr;
    const int schemaResult = sqlite3_exec(
        database_, schemaSql, nullptr, nullptr, &errorMessage);
    if (schemaResult != SQLITE_OK) {
        std::string detail = errorMessage == nullptr
            ? sqlite3_errmsg(database_)
            : errorMessage;
        sqlite3_free(errorMessage);
        return failureStatus(makeError(
            StoreErrorCode::SchemaMigrationFailed,
            database_,
            std::move(detail)));
    }

    Statement version(
        database_,
        "SELECT schema_version FROM sample_slide_store_schema WHERE id = 1");
    if (!version.prepared() || version.step() != SQLITE_ROW) {
        return failureStatus(makeError(
            StoreErrorCode::SchemaMigrationFailed, database_));
    }
    if (sqlite3_column_int(version.get(), 0) != 1) {
        return failureStatus(makeError(
            StoreErrorCode::UnsupportedSchemaVersion,
            database_,
            "Unsupported Sample/Slide persistence schema version."));
    }

    if (!transaction.commit()) {
        return failureStatus(makeError(
            StoreErrorCode::SchemaMigrationFailed, database_));
    }
    return core::Status<SampleSlideStoreError>::success();
}

core::Status<SampleSlideStoreError>
SampleSlideSqliteStore::saveSampleTypeRevision(
    const core::SampleType& sampleType,
    std::uint64_t expectedCurrentRevision) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!revisionFitsSqlite(sampleType.revision()) ||
        expectedCurrentRevision > kMaximumSqliteInteger ||
        expectedCurrentRevision == kMaximumSqliteInteger ||
        sampleType.revision() != expectedCurrentRevision + 1) {
        return failureStatus(makeError(
            StoreErrorCode::InvalidRevision,
            database_,
            "SampleType revision must be the next positive SQLite-safe revision."));
    }

    Transaction transaction(database_);
    if (!transaction.active()) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }

    std::optional<std::uint64_t> current;
    if (!readCurrentRevision(
            database_,
            "sample_type_current",
            "sample_type_id",
            sampleType.id().value(),
            current)) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }
    const std::uint64_t currentValue = current.value_or(0);
    if (currentValue != expectedCurrentRevision) {
        return failureStatus(makeError(
            StoreErrorCode::RevisionConflict,
            database_,
            "The current SampleType revision changed before this write."));
    }

    std::vector<std::uint8_t> payload;
    if (!encodeSampleTypePayload(sampleType, payload)) {
        return failureStatus(makeError(
            StoreErrorCode::PayloadTooLarge,
            database_,
            "SampleType payload exceeds the persistence codec limits."));
    }
    Statement insert(
        database_,
        "INSERT INTO sample_type_revision "
        "(sample_type_id, revision, payload_format_version, payload) "
        "VALUES (?, ?, ?, ?)");
    if (!insert.prepared() ||
        !insert.bindText(1, sampleType.id().value()) ||
        !insert.bindInt64(2, sampleType.revision()) ||
        !insert.bindInt(3, static_cast<int>(kPayloadFormatVersion)) ||
        !insert.bindBlob(4, payload)) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }
    if (insert.step() != SQLITE_DONE) {
        const auto code = isConstraintViolation(database_)
            ? StoreErrorCode::DuplicateSampleTypeRevision
            : StoreErrorCode::SqliteOperationFailed;
        return failureStatus(makeError(
            code,
            database_,
            "Could not append the immutable SampleType revision."));
    }

    Statement currentWrite(
        database_,
        "INSERT INTO sample_type_current (sample_type_id, revision) "
        "VALUES (?, ?) ON CONFLICT(sample_type_id) DO UPDATE SET "
        "revision = excluded.revision");
    if (!currentWrite.prepared() ||
        !currentWrite.bindText(1, sampleType.id().value()) ||
        !currentWrite.bindInt64(2, sampleType.revision()) ||
        currentWrite.step() != SQLITE_DONE) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }

    if (!transaction.commit()) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }
    return core::Status<SampleSlideStoreError>::success();
}

core::Result<
    std::optional<core::SampleType>,
    SampleSlideStoreError> SampleSlideSqliteStore::loadSampleTypeRevision(
        const core::SampleTypeId& sampleTypeId,
        std::uint64_t revision) {
    std::lock_guard<std::mutex> lock(mutex_);
    return loadSampleTypeRevisionUnlocked(sampleTypeId, revision);
}

core::Result<
    std::optional<core::SampleType>,
    SampleSlideStoreError> SampleSlideSqliteStore::loadCurrentSampleType(
        const core::SampleTypeId& sampleTypeId) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::optional<std::uint64_t> current;
    if (!readCurrentRevision(
            database_,
            "sample_type_current",
            "sample_type_id",
            sampleTypeId.value(),
            current)) {
        return core::Result<
            std::optional<core::SampleType>,
            SampleSlideStoreError>::failure(makeError(
                StoreErrorCode::SqliteOperationFailed, database_));
    }
    if (!current) {
        return core::Result<
            std::optional<core::SampleType>,
            SampleSlideStoreError>::success(std::nullopt);
    }
    return loadSampleTypeRevisionUnlocked(sampleTypeId, *current);
}

core::Result<
    std::optional<core::SampleType>,
    SampleSlideStoreError>
SampleSlideSqliteStore::loadSampleTypeRevisionUnlocked(
    const core::SampleTypeId& sampleTypeId,
    std::uint64_t revision) {
    if (!revisionFitsSqlite(revision)) {
        return core::Result<
            std::optional<core::SampleType>,
            SampleSlideStoreError>::failure(makeError(
                StoreErrorCode::InvalidRevision, database_));
    }

    Statement statement(
        database_,
        "SELECT payload_format_version, payload FROM sample_type_revision "
        "WHERE sample_type_id = ? AND revision = ?");
    if (!statement.prepared() ||
        !statement.bindText(1, sampleTypeId.value()) ||
        !statement.bindInt64(2, revision)) {
        return core::Result<
            std::optional<core::SampleType>,
            SampleSlideStoreError>::failure(makeError(
                StoreErrorCode::SqliteOperationFailed, database_));
    }

    const int step = statement.step();
    if (step == SQLITE_DONE) {
        return core::Result<
            std::optional<core::SampleType>,
            SampleSlideStoreError>::success(std::nullopt);
    }
    if (step != SQLITE_ROW) {
        return core::Result<
            std::optional<core::SampleType>,
            SampleSlideStoreError>::failure(makeError(
                StoreErrorCode::SqliteOperationFailed, database_));
    }

    const int formatVersion = sqlite3_column_int(statement.get(), 0);
    const auto* payload = sqlite3_column_blob(statement.get(), 1);
    const int payloadBytes = sqlite3_column_bytes(statement.get(), 1);
    if (formatVersion != static_cast<int>(kPayloadFormatVersion) ||
        payload == nullptr || payloadBytes <= 0 ||
        payloadBytes > 64 * 1024 * 1024) {
        return core::Result<
            std::optional<core::SampleType>,
            SampleSlideStoreError>::failure(makeError(
                StoreErrorCode::InvalidPersistedRecord,
                database_,
                "SampleType payload format or length is invalid."));
    }

    auto decoded = decodeSampleTypePayload(
        sampleTypeId,
        revision,
        payload,
        static_cast<std::size_t>(payloadBytes));
    if (!decoded) {
        return core::Result<
            std::optional<core::SampleType>,
            SampleSlideStoreError>::failure(makeError(
                StoreErrorCode::InvalidPersistedRecord,
                database_,
                "The stored SampleType payload violates the current domain contract."));
    }
    return core::Result<
        std::optional<core::SampleType>,
        SampleSlideStoreError>::success(std::move(decoded));
}

core::Status<SampleSlideStoreError>
SampleSlideSqliteStore::saveSlideRevision(
    const core::Slide& slide,
    std::uint64_t expectedCurrentRevision) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!revisionFitsSqlite(slide.revision()) ||
        !revisionFitsSqlite(slide.sampleTypeRevision()) ||
        expectedCurrentRevision > kMaximumSqliteInteger ||
        expectedCurrentRevision == kMaximumSqliteInteger ||
        slide.revision() != expectedCurrentRevision + 1) {
        return failureStatus(makeError(
            StoreErrorCode::InvalidRevision,
            database_,
            "Slide revision must be the next positive SQLite-safe revision."));
    }

    Transaction transaction(database_);
    if (!transaction.active()) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }

    bool sampleTypeExists = false;
    if (!rowExists(
            database_,
            "SELECT 1 FROM sample_type_revision "
            "WHERE sample_type_id = ? AND revision = ?",
            slide.sampleTypeId().value(),
            slide.sampleTypeRevision(),
            sampleTypeExists)) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }
    if (!sampleTypeExists) {
        return failureStatus(makeError(
            StoreErrorCode::SlideSampleTypeRevisionNotFound,
            database_,
            "The exact SampleType revision pinned by this Slide is not persisted."));
    }

    std::optional<std::uint64_t> current;
    if (!readCurrentRevision(
            database_,
            "slide_current",
            "slide_id",
            slide.id().value(),
            current)) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }
    if (current.value_or(0) != expectedCurrentRevision) {
        return failureStatus(makeError(
            StoreErrorCode::RevisionConflict,
            database_,
            "The current Slide revision changed before this write."));
    }

    std::vector<std::uint8_t> payload;
    if (!encodeSlidePayload(slide, payload)) {
        return failureStatus(makeError(
            StoreErrorCode::PayloadTooLarge,
            database_,
            "Slide payload exceeds the persistence codec limits."));
    }
    Statement insert(
        database_,
        "INSERT INTO slide_revision "
        "(slide_id, revision, sample_type_id, sample_type_revision, "
        "payload_format_version, payload) VALUES (?, ?, ?, ?, ?, ?)");
    if (!insert.prepared() ||
        !insert.bindText(1, slide.id().value()) ||
        !insert.bindInt64(2, slide.revision()) ||
        !insert.bindText(3, slide.sampleTypeId().value()) ||
        !insert.bindInt64(4, slide.sampleTypeRevision()) ||
        !insert.bindInt(5, static_cast<int>(kPayloadFormatVersion)) ||
        !insert.bindBlob(6, payload)) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }
    if (insert.step() != SQLITE_DONE) {
        const auto code = isConstraintViolation(database_)
            ? StoreErrorCode::DuplicateSlideRevision
            : StoreErrorCode::SqliteOperationFailed;
        return failureStatus(makeError(
            code,
            database_,
            "Could not append the immutable Slide revision."));
    }

    Statement currentWrite(
        database_,
        "INSERT INTO slide_current (slide_id, revision) VALUES (?, ?) "
        "ON CONFLICT(slide_id) DO UPDATE SET revision = excluded.revision");
    if (!currentWrite.prepared() ||
        !currentWrite.bindText(1, slide.id().value()) ||
        !currentWrite.bindInt64(2, slide.revision()) ||
        currentWrite.step() != SQLITE_DONE) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }

    if (!transaction.commit()) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }
    return core::Status<SampleSlideStoreError>::success();
}

core::Result<
    std::optional<core::Slide>,
    SampleSlideStoreError> SampleSlideSqliteStore::loadSlideRevision(
        const core::SlideId& slideId,
        std::uint64_t revision) {
    std::lock_guard<std::mutex> lock(mutex_);
    return loadSlideRevisionUnlocked(slideId, revision);
}

core::Result<
    std::optional<core::Slide>,
    SampleSlideStoreError> SampleSlideSqliteStore::loadCurrentSlide(
        const core::SlideId& slideId) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::optional<std::uint64_t> current;
    if (!readCurrentRevision(
            database_,
            "slide_current",
            "slide_id",
            slideId.value(),
            current)) {
        return core::Result<
            std::optional<core::Slide>,
            SampleSlideStoreError>::failure(makeError(
                StoreErrorCode::SqliteOperationFailed, database_));
    }
    if (!current) {
        return core::Result<
            std::optional<core::Slide>,
            SampleSlideStoreError>::success(std::nullopt);
    }
    return loadSlideRevisionUnlocked(slideId, *current);
}

core::Result<
    std::optional<core::Slide>,
    SampleSlideStoreError>
SampleSlideSqliteStore::loadSlideRevisionUnlocked(
    const core::SlideId& slideId,
    std::uint64_t revision) {
    if (!revisionFitsSqlite(revision)) {
        return core::Result<
            std::optional<core::Slide>,
            SampleSlideStoreError>::failure(makeError(
                StoreErrorCode::InvalidRevision, database_));
    }

    Statement statement(
        database_,
        "SELECT sample_type_id, sample_type_revision, payload_format_version, "
        "payload FROM slide_revision WHERE slide_id = ? AND revision = ?");
    if (!statement.prepared() ||
        !statement.bindText(1, slideId.value()) ||
        !statement.bindInt64(2, revision)) {
        return core::Result<
            std::optional<core::Slide>,
            SampleSlideStoreError>::failure(makeError(
                StoreErrorCode::SqliteOperationFailed, database_));
    }

    const int step = statement.step();
    if (step == SQLITE_DONE) {
        return core::Result<
            std::optional<core::Slide>,
            SampleSlideStoreError>::success(std::nullopt);
    }
    if (step != SQLITE_ROW) {
        return core::Result<
            std::optional<core::Slide>,
            SampleSlideStoreError>::failure(makeError(
                StoreErrorCode::SqliteOperationFailed, database_));
    }

    const auto* sampleTypeText = sqlite3_column_text(statement.get(), 0);
    const int sampleTypeTextBytes = sqlite3_column_bytes(statement.get(), 0);
    const sqlite3_int64 sampleTypeRevisionRaw =
        sqlite3_column_int64(statement.get(), 1);
    const int formatVersion = sqlite3_column_int(statement.get(), 2);
    const auto* payload = sqlite3_column_blob(statement.get(), 3);
    const int payloadBytes = sqlite3_column_bytes(statement.get(), 3);
    if (sampleTypeText == nullptr || sampleTypeTextBytes <= 0 ||
        sampleTypeRevisionRaw <= 0 ||
        formatVersion != static_cast<int>(kPayloadFormatVersion) ||
        payload == nullptr || payloadBytes <= 0 ||
        payloadBytes > 64 * 1024 * 1024) {
        return core::Result<
            std::optional<core::Slide>,
            SampleSlideStoreError>::failure(makeError(
                StoreErrorCode::InvalidPersistedRecord,
                database_,
                "Slide identity binding or payload is invalid."));
    }

    const std::string sampleTypeIdRaw(
        reinterpret_cast<const char*>(sampleTypeText),
        static_cast<std::size_t>(sampleTypeTextBytes));
    auto sampleTypeId = SampleTypeId::create(sampleTypeIdRaw);
    if (!sampleTypeId) {
        return core::Result<
            std::optional<core::Slide>,
            SampleSlideStoreError>::failure(makeError(
                StoreErrorCode::InvalidPersistedRecord, database_));
    }

    auto decoded = decodeSlidePayload(
        slideId,
        revision,
        std::move(sampleTypeId).value(),
        static_cast<std::uint64_t>(sampleTypeRevisionRaw),
        payload,
        static_cast<std::size_t>(payloadBytes));
    if (!decoded) {
        return core::Result<
            std::optional<core::Slide>,
            SampleSlideStoreError>::failure(makeError(
                StoreErrorCode::InvalidPersistedRecord,
                database_,
                "The stored Slide payload violates the current domain contract."));
    }
    return core::Result<
        std::optional<core::Slide>,
        SampleSlideStoreError>::success(std::move(decoded));
}

} // namespace microscope::application::persistence
