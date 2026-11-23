#include "camera/qt/FrameStreamMailbox.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QMetaObject>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <thread>
namespace {
camera::FramePtr makeFrame(std::uint32_t value) {
    auto frame=std::make_shared<camera::Frame>();
    frame->width=value; frame->height=1; frame->strideBytes=1;
    frame->pixelFormat=camera::PixelFormat::Raw8;
    frame->significantBitsPerPixel=8; frame->storageBitsPerPixel=8;
    frame->pixels.push_back(static_cast<std::uint8_t>(value));
    return frame;
}
class NotificationContext final : public QObject {
public:
    std::function<void()> callback;
    int notifications=0;
    void queue() {
        (void)QMetaObject::invokeMethod(this,[this]() {
            ++notifications;
            if(callback) callback();
        },Qt::QueuedConnection);
    }
};
}
int main(int argc,char** argv) {
    QCoreApplication application(argc,argv);
    camera::qt::FrameStreamMailbox mailbox;
    NotificationContext notificationContext;
    QEventLoop loop; QTimer timeout; timeout.setSingleShot(true);
    QObject::connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit);
    bool affinity=true, latestOk=false, rearmOk=false;
    camera::FramePtr lastPublished,rearmedPublished;
    notificationContext.callback=[&]() {
        affinity=affinity && QThread::currentThread()==notificationContext.thread();
        const camera::FramePtr frame=mailbox.takeLatestFrame(
            [&]() noexcept { notificationContext.queue(); });
        if(notificationContext.notifications==1) {
            latestOk=frame && lastPublished && frame==lastPublished &&
                frame->width==200U && frame->pixels.size()==1U &&
                frame->pixels.front()==200U;
            rearmedPublished=makeFrame(201);
            auto rearmedLease=rearmedPublished;
            mailbox.publishFrame(std::move(rearmedLease),[]() noexcept {return true;},
                [&]() noexcept {notificationContext.queue();});
        } else if(notificationContext.notifications==2) {
            rearmOk=frame && rearmedPublished && frame==rearmedPublished &&
                frame->pixels.front()==201U;
            loop.quit();
        }
    };
    std::thread producer([&]() {
        for(std::uint32_t value=1;value<=200;++value) {
            auto frame=makeFrame(value);
            if(value==200) lastPublished=frame;
            mailbox.publishFrame(std::move(frame),[]() noexcept {return true;},
                [&]() noexcept {notificationContext.queue();});
        }
    });
    producer.join();
    timeout.start(3000); loop.exec();
    if(notificationContext.notifications!=2 || !affinity || !latestOk || !rearmOk) {
        std::cerr<<"Mailbox coalescing, lease identity, re-arm, or Qt affinity failed.\n";
        return EXIT_FAILURE;
    }
    int calls=0, queued=0;
    mailbox.publishFrame(makeFrame(300),[&calls]() noexcept {
        ++calls; return calls==1;
    },[&queued]() noexcept {++queued;});
    if(calls!=2 || queued!=0 || mailbox.takeLatestFrame([]() noexcept {})) {
        std::cerr<<"Stale publication remained in the mailbox.\n";
        return EXIT_FAILURE;
    }
    mailbox.publishFrame(makeFrame(400),[]() noexcept {return true;},
        [&queued]() noexcept {++queued;});
    mailbox.clear();
    if(mailbox.takeLatestFrame([]() noexcept {}) || queued!=1) {
        std::cerr<<"Clear did not empty the latest lease.\n";
        return EXIT_FAILURE;
    }
    std::cout<<"All Qt frame stream mailbox tests passed.\n";
    return EXIT_SUCCESS;
}
