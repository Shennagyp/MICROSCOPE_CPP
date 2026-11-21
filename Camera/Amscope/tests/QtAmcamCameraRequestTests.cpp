#include "amscope/QtAmcamCamera.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include <QTimer>

#include <cstdlib>
#include <iostream>
#include <variant>
#include <vector>

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    amscope::QtAmcamCamera camera;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);

    bool staleDescriptorsDelivered = false;
    bool autoWhiteBalanceCompleted = false;
    bool frameRateCompleted = false;
    bool firstWriteCompleted = false;
    bool secondWriteCompleted = false;
    std::vector<int> writeCompletionOrder;
    bool callbacksOnAffinityThread = true;
    bool resultsAreTypedErrors = true;

    QObject::connect(&timeout, &QTimer::timeout,
                     &loop, &QEventLoop::quit);

    const auto staleRequest = camera.requestFeatureDescriptors(
        [&staleDescriptorsDelivered](
            amscope::QtAmcamCamera::FeatureRequestId,
            amscope::QtAmcamCamera::FeatureDescriptorsResult) {
            staleDescriptorsDelivered = true;
        });

    const auto whiteBalanceRequest = camera.requestAutoWhiteBalance(
        [&](amscope::QtAmcamCamera::FeatureRequestId requestId,
            amscope::QtAmcamCamera::AutoWhiteBalanceResult result) {
            callbacksOnAffinityThread =
                callbacksOnAffinityThread &&
                QThread::currentThread() == camera.thread();
            resultsAreTypedErrors =
                resultsAreTypedErrors &&
                requestId != 0 &&
                std::holds_alternative<amscope::CameraError>(result);
            autoWhiteBalanceCompleted = true;
            if (autoWhiteBalanceCompleted && frameRateCompleted &&
                firstWriteCompleted && secondWriteCompleted)
                loop.quit();
        });

    const auto firstWriteRequest = camera.requestWriteFeature(
        "ExposureTime",
        camera::CameraFeatureValue{std::int64_t{12000}},
        [&](amscope::QtAmcamCamera::FeatureRequestId requestId,
            amscope::QtAmcamCamera::FeatureWriteResult result) {
            callbacksOnAffinityThread =
                callbacksOnAffinityThread &&
                QThread::currentThread() == camera.thread();
            resultsAreTypedErrors =
                resultsAreTypedErrors &&
                requestId != 0 &&
                std::holds_alternative<camera::CameraFeatureAccessError>(result);
            writeCompletionOrder.push_back(1);
            firstWriteCompleted = true;
            if (autoWhiteBalanceCompleted && frameRateCompleted &&
                firstWriteCompleted && secondWriteCompleted)
                loop.quit();
        });

    const auto secondWriteRequest = camera.requestWriteFeature(
        "AmcamWhiteBalanceTint",
        camera::CameraFeatureValue{std::int64_t{120}},
        [&](amscope::QtAmcamCamera::FeatureRequestId requestId,
            amscope::QtAmcamCamera::FeatureWriteResult result) {
            callbacksOnAffinityThread =
                callbacksOnAffinityThread &&
                QThread::currentThread() == camera.thread();
            resultsAreTypedErrors =
                resultsAreTypedErrors &&
                requestId != 0 &&
                std::holds_alternative<camera::CameraFeatureAccessError>(result);
            writeCompletionOrder.push_back(2);
            secondWriteCompleted = true;
            if (autoWhiteBalanceCompleted && frameRateCompleted &&
                firstWriteCompleted && secondWriteCompleted)
                loop.quit();
        });

    const auto frameRateRequest = camera.requestFrameRate(
        [&](amscope::QtAmcamCamera::FeatureRequestId requestId,
            amscope::QtAmcamCamera::FrameRateResult result) {
            callbacksOnAffinityThread =
                callbacksOnAffinityThread &&
                QThread::currentThread() == camera.thread();
            resultsAreTypedErrors =
                resultsAreTypedErrors &&
                requestId != 0 &&
                std::holds_alternative<amscope::CameraError>(result);
            frameRateCompleted = true;
            if (autoWhiteBalanceCompleted && frameRateCompleted)
                loop.quit();
        });

    if (staleRequest == 0 || whiteBalanceRequest == 0 ||
        firstWriteRequest == 0 || secondWriteRequest == 0 ||
        frameRateRequest == 0) {
        std::cerr << "A valid asynchronous camera request was rejected.\n";
        return EXIT_FAILURE;
    }

    timeout.start(5000);
    loop.exec();

    if (!autoWhiteBalanceCompleted || !frameRateCompleted ||
        !firstWriteCompleted || !secondWriteCompleted) {
        std::cerr << "Asynchronous command/telemetry/write completion timed out.\n";
        return EXIT_FAILURE;
    }
    if (staleDescriptorsDelivered) {
        std::cerr << "A descriptor callback from a superseded context was delivered.\n";
        return EXIT_FAILURE;
    }
    if (!callbacksOnAffinityThread) {
        std::cerr << "A camera request callback ran outside the QObject affinity thread.\n";
        return EXIT_FAILURE;
    }
    if (!resultsAreTypedErrors) {
        std::cerr << "A closed-camera request did not return its typed error.\n";
        return EXIT_FAILURE;
    }
    if (writeCompletionOrder != std::vector<int>{1, 2}) {
        std::cerr << "Accepted feature writes did not complete in FIFO order.\n";
        return EXIT_FAILURE;
    }

    std::cout << "All Qt camera request tests passed.\n";
    return EXIT_SUCCESS;
}
