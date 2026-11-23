#include "camera/qt/FrameStream.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include <QTimer>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>
namespace {
camera::FramePtr makeFrame(std::uint32_t value) {
    auto frame = std::make_shared<camera::Frame>();
    frame->width=value; frame->height=1; frame->strideBytes=1;
    frame->pixelFormat=camera::PixelFormat::Raw8;
    frame->significantBitsPerPixel=8; frame->storageBitsPerPixel=8;
    frame->pixels.push_back(static_cast<std::uint8_t>(value));
    return frame;
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    camera::qt::FrameStream stream;
    QEventLoop loop; QTimer timeout; timeout.setSingleShot(true);
    QObject::connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit);
    int notifications=0; bool affinity=true, latestOk=false, rearmOk=false;
    camera::FramePtr latestPublished, rearmedPublished;
    QObject::connect(&stream,&camera::qt::FrameStream::frameAvailable,&loop,[&]() {
        ++notifications;
        affinity=affinity && QThread::currentThread()==stream.thread();
        const camera::FramePtr frame=stream.takeLatestFrame();
        if(notifications==1) {
            latestOk=frame && latestPublished && frame==latestPublished &&
                frame->width==200U && frame->pixels.size()==1U &&
                frame->pixels.front()==200U;
            rearmedPublished=makeFrame(201);
            stream.publishFrame(rearmedPublished);
        } else if(notifications==2) {
            rearmOk=frame && rearmedPublished && frame==rearmedPublished &&
                frame->pixels.front()==201U;
            loop.quit();
        }
    });
    std::thread producer([&]() {
        for(std::uint32_t value=1;value<=200;++value) {
            auto frame=makeFrame(value);
            if(value==200) latestPublished=frame;
            stream.publishFrame(std::move(frame));
        }
    });
    producer.join();
    timeout.start(3000); loop.exec();
    if(notifications!=2 || !affinity || !latestOk || !rearmOk) {
        std::cerr<<"FrameStream coalescing, lease, re-arm, or affinity failed.\n";
        return EXIT_FAILURE;
    }
    int calls=0;
    stream.publishFrame(makeFrame(300),[&calls]() noexcept {
        ++calls; return calls==1;
    });
    if(calls!=2 || stream.takeLatestFrame()) {
        std::cerr<<"Stale publication remained in the mailbox.\n";
        return EXIT_FAILURE;
    }
    stream.publishFrame(makeFrame(400)); stream.clear();
    if(stream.takeLatestFrame()) {
        std::cerr<<"Clear retained a frame lease.\n";
        return EXIT_FAILURE;
    }
    std::cout<<"All Qt frame stream tests passed.\n";
    return EXIT_SUCCESS;
}
