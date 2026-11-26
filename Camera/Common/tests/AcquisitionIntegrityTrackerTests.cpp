#include "camera/AcquisitionIntegrityTracker.h"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    {
        camera::AcquisitionIntegrityTracker tracker;
        tracker.recordBuffer(100, camera::FrameDeliveryStatus::CompleteImage);
        tracker.recordBuffer(101, camera::FrameDeliveryStatus::CompleteImage);
        tracker.recordBuffer(104, camera::FrameDeliveryStatus::CompleteImage);
        tracker.recordBuffer(std::nullopt,
                             camera::FrameDeliveryStatus::NonImagePayload);
        tracker.recordBuffer(105, camera::FrameDeliveryStatus::Incomplete);
        tracker.recordFrameWaitTimeout();

        const auto stats = tracker.snapshot();
        require(stats.observedBuffers == 5, "all delivered buffers are observed");
        require(stats.completeImages == 3, "complete images are counted");
        require(stats.incompleteBuffers == 1, "incomplete buffers are separate");
        require(stats.nonImagePayloads == 1, "non-image payloads are separate");
        require(stats.frameWaitTimeouts == 1, "wait timeouts are separate");
        require(stats.sequenceGaps == 1, "one sequence gap is detected");
        require(stats.missingFrames == 2, "the exact missing-frame count is retained");
        tracker.recordBufferStarvationEvents(3);
        require(tracker.snapshot().bufferStarvationEvents == 3,
                "provider-reported buffer starvation is kept as a separate signal");
        require(stats.buffersWithoutSequence == 1,
                "unsequenced buffers are counted without inventing an ID");
    }

    {
        camera::AcquisitionIntegrityTracker tracker;
        tracker.recordBuffer(7, camera::FrameDeliveryStatus::CompleteImage);
        tracker.recordBuffer(7, camera::FrameDeliveryStatus::CompleteImage);
        tracker.recordBuffer(6, camera::FrameDeliveryStatus::CompleteImage);
        tracker.recordBuffer(7, camera::FrameDeliveryStatus::CompleteImage);
        const auto stats = tracker.snapshot();
        require(stats.duplicateFrameIds == 2,
                "duplicate IDs include a repeated high-water ID after ambiguity");
        require(stats.ambiguousSequenceTransitions == 1,
                "an unknown backwards transition is not misreported as a gap");
        require(stats.missingFrames == 0, "ambiguous transitions do not invent losses");
        tracker.recordBuffer(8, camera::FrameDeliveryStatus::CompleteImage);
        const auto afterAmbiguous = tracker.snapshot();
        require(afterAmbiguous.sequenceGaps == 0 &&
                    afterAmbiguous.missingFrames == 0,
                "a later expected ID must not create a false gap after ambiguity");
    }

    {
        // GigE Vision frame IDs are one-based 16-bit values and wrap to one.
        camera::AcquisitionIntegrityTracker tracker({65535, 1});
        tracker.recordBuffer(65534, camera::FrameDeliveryStatus::CompleteImage);
        tracker.recordBuffer(65535, camera::FrameDeliveryStatus::CompleteImage);
        tracker.recordBuffer(1, camera::FrameDeliveryStatus::CompleteImage);
        tracker.recordBuffer(2, camera::FrameDeliveryStatus::CompleteImage);
        tracker.recordBuffer(5, camera::FrameDeliveryStatus::CompleteImage);
        const auto stats = tracker.snapshot();
        require(stats.sequenceCounterWraps == 1, "declared wrap is recognized");
        require(stats.sequenceGaps == 1 && stats.missingFrames == 2,
                "gaps across and after wrap are counted precisely");
    }

    {
        camera::AcquisitionIntegrityTracker tracker({65535, 1});
        tracker.recordBuffer(100, camera::FrameDeliveryStatus::CompleteImage);
        tracker.recordBuffer(99, camera::FrameDeliveryStatus::CompleteImage);
        tracker.recordBuffer(101, camera::FrameDeliveryStatus::CompleteImage);
        const auto stats = tracker.snapshot();
        require(stats.outOfOrderFrameIds == 1,
                "a backwards jump under a known wrap model is classified as out of order");
        require(stats.missingFrames == 0,
                "out-of-order delivery does not inflate missing-frame totals");
    }

    {
        camera::AcquisitionIntegrityTracker tracker;
        tracker.recordBuffer(10, camera::FrameDeliveryStatus::CompleteImage);
        tracker.recordBuffer(14, camera::FrameDeliveryStatus::CompleteImage);
        tracker.reset();
        tracker.recordBuffer(200, camera::FrameDeliveryStatus::CompleteImage);
        tracker.recordBuffer(201, camera::FrameDeliveryStatus::CompleteImage);
        const auto stats = tracker.snapshot();
        require(stats.observedBuffers == 2 && stats.sequenceGaps == 0 &&
                    stats.missingFrames == 0 && stats.bufferStarvationEvents == 0,
                "reset starts a clean accounting interval");
    }

    {
        camera::AcquisitionIntegrityTracker tracker;
        tracker.configureSequenceCounter({15, 1});
        tracker.recordBuffer(14, camera::FrameDeliveryStatus::CompleteImage);
        tracker.recordBuffer(1, camera::FrameDeliveryStatus::CompleteImage);
        const auto stats = tracker.snapshot();
        require(stats.sequenceCounterWraps == 1,
                "a reconfigured counter uses its declared minimum and modulus");
    }

    std::cout << "Acquisition integrity tracker tests passed.\n";
    return EXIT_SUCCESS;
}
