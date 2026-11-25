#include "microscope/core/runtime/BoundedFeedbackInbox.h"
#include "microscope/core/motion/StageMotionPort.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using namespace microscope::core;

template<class T,class E>
T take(Result<T,E> result) {
    if(!result)throw std::runtime_error("feedback inbox fixture rejected");
    return std::move(result).value();
}
template<class T>T id(const char* value){return take(T::create(value));}

class TestRuntimeSignal final : public IFeedbackRuntimeSignal {
public:
    void feedbackAvailable() noexcept override { available.fetch_add(1); }
    void feedbackLossDetected() noexcept override { loss.fetch_add(1); }
    void feedbackIngressClosed() noexcept override { closed.fetch_add(1); }
    std::atomic<int> available{0};
    std::atomic<int> loss{0};
    std::atomic<int> closed{0};
};

RuntimeDeviceCorrelation runtimeDevice(
    const char* microscope,const char* runtime,std::uint64_t generation,
    const char* device="stage") {
    return RuntimeDeviceCorrelation{
        id<MicroscopeId>(microscope),id<RuntimeId>(runtime),
        take(RuntimeGeneration::create(generation)),
        take(DeviceSessionGeneration::create(1)),
        id<DeviceId>(device)};
}
FeedbackEnvelope<StageMotionFeedbackPayload> feedbackFor(
    RuntimeDeviceCorrelation device,const char* axisId) {
    auto state=take(AxisMotionStateObservation::create(
        id<StageAxisId>(axisId),StageAxisMotionState::Moving));
    auto metadata=take(FeedbackMetadata::create(
        FeedbackCorrelation{std::move(device),std::nullopt},
        FeedbackKind::DeviceStateObserved,ObservationSource::DeviceEvent));
    return FeedbackEnvelope<StageMotionFeedbackPayload>::create(
        std::move(metadata),StageMotionFeedbackPayload{state});
}
}

int main() {
    int failures=0;
    const auto check=[&](bool value,const char* message) {
        if(!value){++failures;std::cerr<<"FAIL: "<<message<<'\n';}
    };

    TestRuntimeSignal zeroCapacitySignal;
    auto zeroCapacity=BoundedFeedbackInbox<StageMotionFeedbackPayload>::create(
        id<MicroscopeId>("scope"),id<RuntimeId>("runtime"),
        take(RuntimeGeneration::create(1)),0,zeroCapacitySignal);
    check(!zeroCapacity&&zeroCapacity.error()==
        BoundedFeedbackInboxCreationError::ZeroCapacity,
        "zero-capacity feedback inbox rejected");

    TestRuntimeSignal signal;
    auto inbox=take(BoundedFeedbackInbox<StageMotionFeedbackPayload>::create(
        id<MicroscopeId>("scope"),id<RuntimeId>("runtime"),
        take(RuntimeGeneration::create(1)),1,signal));

    auto first=feedbackFor(runtimeDevice("scope","runtime",1),"x");
    auto second=feedbackFor(runtimeDevice("scope","runtime",1),"y");
    check(inbox->submit(std::move(first))==FeedbackSubmitStatus::Enqueued,
          "first feedback is accepted");
    check(signal.available.load()==1&&signal.loss.load()==0,
          "successful enqueue wakes the runtime without reporting data loss");
    check(inbox->submit(std::move(second))==FeedbackSubmitStatus::QueueFull,
          "full queue rejects feedback without blocking the producer");
    check(inbox->feedbackLossLatched()&&signal.loss.load()==1,
          "queue overflow latches feedback loss and signals the independent control path");
    check(inbox->pendingCount()==1,
          "queue retains the already-enqueued item after overflow");

    auto receipt1=inbox->tryPop();
    check(receipt1&&receipt1->receipt().ingressSequence.value()==1,
          "first successful enqueue receives Core sequence one");
    inbox->acknowledgeFeedbackLoss();
    check(!inbox->feedbackLossLatched(),
          "runtime can acknowledge loss only after reconciliation");

    auto third=feedbackFor(runtimeDevice("scope","runtime",1),"y");
    check(inbox->submit(std::move(third))==FeedbackSubmitStatus::Enqueued,
          "inbox accepts feedback after the runtime acknowledges loss");
    auto receipt2=inbox->tryPop();
    check(receipt2&&receipt2->receipt().ingressSequence.value()==2,
          "rejected feedback does not consume a Core ingress sequence");

    auto foreign=feedbackFor(runtimeDevice("other-scope","runtime",1),"x");
    check(inbox->submit(std::move(foreign))==FeedbackSubmitStatus::WrongRuntimeScope,
          "feedback from another microscope scope is rejected");
    check(signal.available.load()==2&&signal.loss.load()==1&&!inbox->feedbackLossLatched(),
          "wrong-scope feedback does not masquerade as accepted data or queue overflow");

    inbox->close();
    check(inbox->isClosed()&&signal.closed.load()==1,
          "close disposes the feedback ingress and signals the runtime owner");
    auto afterClose=feedbackFor(runtimeDevice("scope","runtime",1),"x");
    check(inbox->submit(std::move(afterClose))==FeedbackSubmitStatus::Closed,
          "closed ingress rejects later driver feedback");
    check(!inbox->waitAndPop(),"closed and drained ingress wakes a waiting consumer");

    TestRuntimeSignal concurrentSignal;
    auto concurrentInbox=take(BoundedFeedbackInbox<StageMotionFeedbackPayload>::create(
        id<MicroscopeId>("scope-concurrent"),id<RuntimeId>("runtime-concurrent"),
        take(RuntimeGeneration::create(2)),100,concurrentSignal));
    std::atomic<int> enqueueFailures{0};
    std::vector<std::thread> producers;
    for(int producer=0;producer<4;++producer) {
        producers.emplace_back([&] {
            for(int j=0;j<25;++j) {
                auto event=feedbackFor(runtimeDevice("scope-concurrent","runtime-concurrent",2),"x");
                if(concurrentInbox->submit(std::move(event))!=FeedbackSubmitStatus::Enqueued)
                    enqueueFailures.fetch_add(1);
            }
        });
    }
    for(auto& producer:producers)
        producer.join();
    check(enqueueFailures.load()==0&&concurrentInbox->pendingCount()==100,
          "concurrent producers enqueue all feedback into fixed-capacity storage");
    std::uint64_t expectedSequence=1;
    while(auto receipt=concurrentInbox->tryPop()) {
        check(receipt->receipt().ingressSequence.value()==expectedSequence,
              "Core ingress sequence follows FIFO insertion order across producers");
        ++expectedSequence;
    }
    check(expectedSequence==101&&concurrentSignal.loss.load()==0&&
          concurrentSignal.available.load()==100,
          "concurrent enqueue sequence is gap-free and every accepted receipt wakes the runtime");

    if(failures!=0) {
        std::cerr<<failures<<" feedback inbox test(s) failed\n";
        return 1;
    }
    std::cout<<"All bounded feedback inbox tests passed\n";
    return 0;
}
