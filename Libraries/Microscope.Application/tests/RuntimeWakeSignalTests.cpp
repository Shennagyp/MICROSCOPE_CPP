#include "microscope/application/runtime/RuntimeWakeSignal.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using namespace microscope::application::runtime;

int main() {
    int failures=0;
    const auto check=[&](bool value,const char* message) {
        if(!value){++failures;std::cerr<<"FAIL: "<<message<<'\n';}
    };

    RuntimeWakeSignal signal;
    const auto initial=signal.snapshot();
    check(initial.generation==0&&!initial.feedbackIngressClosed&&!initial.wakeFault,
          "new runtime wake signal starts in a clean state");

    // Signal before wait: the generation predicate must make this observable
    // rather than losing a notification in the condition-variable handoff.
    signal.feedbackAvailable();
    auto immediate=signal.waitForChange(initial.generation,std::chrono::milliseconds(5));
    check(immediate&&immediate->generation==1&&
          immediate->lastReason==RuntimeWakeReason::FeedbackAvailable,
          "pre-wait feedback notification cannot be lost");

    const auto beforeLoss=signal.snapshot();
    std::atomic<bool> waiterEntered{false};
    std::atomic<bool> waiterWoke{false};
    std::thread waiter([&] {
        waiterEntered.store(true,std::memory_order_release);
        auto wake=signal.waitForChange(
            beforeLoss.generation,std::chrono::milliseconds(1000));
        waiterWoke.store(bool(wake),std::memory_order_release);
    });
    while(!waiterEntered.load(std::memory_order_acquire))
        std::this_thread::yield();
    signal.feedbackLossDetected();
    waiter.join();
    auto afterLoss=signal.snapshot();
    check(waiterWoke.load(std::memory_order_acquire),
          "feedback-loss notification wakes a blocked owner thread");
    check(afterLoss.lastReason==RuntimeWakeReason::FeedbackLoss&&
          afterLoss.feedbackLossNotifications==1,
          "feedback loss is distinguishable from ordinary feedback availability");

    const auto beforeControl=afterLoss.generation;
    signal.requestControlWake();
    auto control=signal.waitForChange(beforeControl,std::chrono::milliseconds(5));
    check(control&&control->lastReason==RuntimeWakeReason::ControlRequest,
          "application control requests share the runtime wake primitive");

    signal.feedbackIngressClosed();
    const auto closed=signal.snapshot();
    check(closed.feedbackIngressClosed&&
          closed.lastReason==RuntimeWakeReason::FeedbackIngressClosed,
          "ingress closure is separately observable by the worker");
    auto afterClose=signal.waitForChange(closed.generation,std::chrono::milliseconds(5));
    check(afterClose&&afterClose->feedbackIngressClosed,
          "closed ingress keeps the worker wake predicate satisfied for shutdown");

    if(failures) {
        std::cerr<<failures<<" runtime wake signal test(s) failed\n";
        return 1;
    }
    std::cout<<"All runtime wake signal tests passed\n";
    return 0;
}
