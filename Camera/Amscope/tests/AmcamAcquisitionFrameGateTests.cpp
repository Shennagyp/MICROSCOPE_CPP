#include "amscope/detail/AcquisitionFrameGate.h"

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
    amscope::detail::AcquisitionFrameGate gate{10};

    require(gate.accepts(10),
            "a newly started acquisition accepts frames in its start session");
    require(!gate.accepts(11),
            "a lifecycle transition suppresses frames while Stop is in progress");

    // A Stop failure restores the old running acquisition at the new session
    // generation instead of destroying the callback path or starting a new one.
    gate.activateFor(11);
    require(gate.accepts(11),
            "a failed Stop reactivates the same acquisition for frame delivery");
    require(!gate.accepts(10),
            "reactivation never re-enables callbacks against the old session");

    require(gate.invalidateIfCurrent(11),
            "a fatal callback invalidates its current acquisition generation");
    require(!gate.accepts(11),
            "fatal acquisition failure suppresses subsequent frames");
    require(!gate.invalidateIfCurrent(11),
            "a duplicate fatal callback cannot invalidate the gate twice");
    require(!gate.invalidateIfCurrent(12),
            "a stale fatal callback cannot invalidate a different generation");

    // A later successful Stop or Close advances the session and does not
    // reactivate the gate. It remains stale until a new acquisition gets a new gate.
    require(!gate.accepts(12),
            "a successful Stop/Close keeps the previous acquisition invalid");
    amscope::detail::AcquisitionFrameGate nextAcquisition{12};
    require(nextAcquisition.accepts(12),
            "a new acquisition receives a fresh frame-delivery gate");
    require(!gate.accepts(12),
            "the previous acquisition cannot publish into the new acquisition");

    std::cout << "All acquisition frame-gate tests passed.\n";
    return EXIT_SUCCESS;
}
