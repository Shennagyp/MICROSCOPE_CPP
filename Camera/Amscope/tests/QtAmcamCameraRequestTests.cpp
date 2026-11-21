#include "amscope/QtAmcamCamera.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include <QTimer>

#include <cstdlib>
#include <iostream>
#include <variant>

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    amscope::QtAmcamCamera camera;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);

    bool staleDescriptorsDelivered = false;
    bool autoWhiteBalanceCompleted = false;
    bool frameRateCompleted = false;
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
            if (autoWhiteBalanceCompleted && frameRateCompleted)
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

    if (staleRequest == 0 || whiteBalanceRequest == 0 || frameRateRequest == 0) {
        std::cerr << "A valid asynchronous camera request was rejected.\n";
        return EXIT_FAILURE;
    }

    timeout.start(5000);
    loop.exec();

    if (!autoWhiteBalanceCompleted || !frameRateCompleted) {
        std::cerr << "Asynchronous command/telemetry completion timed out.\n";
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
        std::cerr << "A closed-camera request did not return a typed CameraError.\n";
        return EXIT_FAILURE;
    }

    std::cout << "All Qt camera request tests passed.\n";
    return EXIT_SUCCESS;
}
