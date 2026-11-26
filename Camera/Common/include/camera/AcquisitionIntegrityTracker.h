#pragma once

#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>

namespace camera {

// Describes what happened to one buffer delivered by a backend. This is
// deliberately independent of GenTL payload types or vendor error codes.
enum class FrameDeliveryStatus : std::uint8_t {
    CompleteImage,
    Incomplete,
    NonImagePayload,
    InvalidPayload
};

// A sequence counter may wrap. A modulus of zero means that no wrap rule is
// known; backwards transitions are then reported as ambiguous, never converted
// into a guessed number of missing frames. minimumValue supports one-based
// counters such as the GenTL GigE Vision frame ID.
struct SequenceCounterModel {
    std::uint64_t modulus = 0;
    std::uint64_t minimumValue = 0;
};

struct AcquisitionIntegritySnapshot {
    std::uint64_t observedBuffers = 0;
    std::uint64_t completeImages = 0;
    std::uint64_t incompleteBuffers = 0;
    std::uint64_t nonImagePayloads = 0;
    std::uint64_t invalidPayloads = 0;
    std::uint64_t frameWaitTimeouts = 0;
    std::uint64_t bufferStarvationEvents = 0;
    std::uint64_t buffersWithoutSequence = 0;
    std::uint64_t sequenceGaps = 0;
    std::uint64_t missingFrames = 0;
    std::uint64_t duplicateFrameIds = 0;
    std::uint64_t outOfOrderFrameIds = 0;
    std::uint64_t ambiguousSequenceTransitions = 0;
    std::uint64_t sequenceCounterWraps = 0;
};

// Thread-safe, bounded-memory accounting for one acquisition session.
// It records observations only: it never fabricates frames, retries capture,
// or decides whether a scan tile should be accepted.
class AcquisitionIntegrityTracker final {
public:
    explicit AcquisitionIntegrityTracker(
        SequenceCounterModel sequenceModel = {}) noexcept
        : sequenceModel_(sequenceModel) {}

    AcquisitionIntegrityTracker(const AcquisitionIntegrityTracker&) = delete;
    AcquisitionIntegrityTracker& operator=(
        const AcquisitionIntegrityTracker&) = delete;

    // Configure the counter's known wrap rule. Changing the model discards
    // only the sequence baseline; accumulated session counters are retained.
    void configureSequenceCounter(SequenceCounterModel model) noexcept {
        std::lock_guard<std::mutex> guard(mutex_);
        sequenceModel_ = model;
        previousSequence_.reset();
    }

    void recordBuffer(
        std::optional<std::uint64_t> sequence,
        FrameDeliveryStatus status) noexcept {
        std::lock_guard<std::mutex> guard(mutex_);
        increment(snapshot_.observedBuffers);

        switch (status) {
        case FrameDeliveryStatus::CompleteImage:
            increment(snapshot_.completeImages);
            break;
        case FrameDeliveryStatus::Incomplete:
            increment(snapshot_.incompleteBuffers);
            break;
        case FrameDeliveryStatus::NonImagePayload:
            increment(snapshot_.nonImagePayloads);
            break;
        case FrameDeliveryStatus::InvalidPayload:
            increment(snapshot_.invalidPayloads);
            break;
        }

        if (!sequence) {
            increment(snapshot_.buffersWithoutSequence);
            return;
        }
        observeSequence(*sequence);
    }

    // A host-side wait timeout is not itself proof that the camera dropped a
    // frame. Keep it separate from sequence gaps and producer underruns.
    void recordFrameWaitTimeout() noexcept {
        std::lock_guard<std::mutex> guard(mutex_);
        increment(snapshot_.frameWaitTimeouts);
    }

    // Adds provider-reported events such as receive-buffer starvation. The
    // source counter and its reset semantics remain the adapter's responsibility.
    void recordBufferStarvationEvents(std::uint64_t count = 1) noexcept {
        std::lock_guard<std::mutex> guard(mutex_);
        add(snapshot_.bufferStarvationEvents, count);
    }

    [[nodiscard]] AcquisitionIntegritySnapshot snapshot() const noexcept {
        std::lock_guard<std::mutex> guard(mutex_);
        return snapshot_;
    }

    // Starts a new accounting interval while retaining the configured
    // sequence-counter model.
    void reset() noexcept {
        std::lock_guard<std::mutex> guard(mutex_);
        snapshot_ = {};
        previousSequence_.reset();
    }

private:
    static void increment(std::uint64_t& value) noexcept {
        if (value != std::numeric_limits<std::uint64_t>::max())
            ++value;
    }

    static void add(std::uint64_t& value, std::uint64_t amount) noexcept {
        const auto maximum = std::numeric_limits<std::uint64_t>::max();
        value = amount > maximum - value ? maximum : value + amount;
    }

    void observeSequence(std::uint64_t value) noexcept {
        const auto model = sequenceModel_;

        if (model.modulus != 0) {
            if (value < model.minimumValue ||
                value - model.minimumValue >= model.modulus) {
                increment(snapshot_.ambiguousSequenceTransitions);
                previousSequence_ = value;
                return;
            }
            if (previousSequence_ &&
                (*previousSequence_ < model.minimumValue ||
                 *previousSequence_ - model.minimumValue >= model.modulus)) {
                increment(snapshot_.ambiguousSequenceTransitions);
                previousSequence_ = value;
                return;
            }
        }

        if (!previousSequence_) {
            previousSequence_ = value;
            return;
        }

        const auto previous = *previousSequence_;
        if (value == previous) {
            increment(snapshot_.duplicateFrameIds);
            return;
        }

        if (model.modulus == 0) {
            if (value < previous) {
                // Without a declared wrap policy this could be a reset, wrap,
                // or reordered delivery. Do not guess which or count a gap.
                increment(snapshot_.ambiguousSequenceTransitions);
                previousSequence_ = value;
                return;
            }
            recordForwardDistance(value - previous);
            previousSequence_ = value;
            return;
        }

        const auto previousIndex = previous - model.minimumValue;
        const auto currentIndex = value - model.minimumValue;
        const auto distance = currentIndex >= previousIndex
            ? currentIndex - previousIndex
            : model.modulus - previousIndex + currentIndex;

        if (distance > model.modulus / 2) {
            increment(snapshot_.outOfOrderFrameIds);
            return;
        }

        if (value < previous)
            increment(snapshot_.sequenceCounterWraps);
        recordForwardDistance(distance);
        previousSequence_ = value;
    }

    void recordForwardDistance(std::uint64_t distance) noexcept {
        if (distance <= 1) return;
        increment(snapshot_.sequenceGaps);
        add(snapshot_.missingFrames, distance - 1);
    }

    mutable std::mutex mutex_;
    SequenceCounterModel sequenceModel_;
    std::optional<std::uint64_t> previousSequence_;
    AcquisitionIntegritySnapshot snapshot_;
};

} // namespace camera
