#include "amscope/QtAmcamCamera.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include <QTimer>

#include <cstdlib>
#include <iostream>
#include <string>
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
            if (autoWhiteBalanceCompleted && frameRateCompleted &&
                firstWriteCompleted && secondWriteCompleted)
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



    // Accessory reads/commands share the Qt queue and return typed errors
    // while closed; they must not reach the vendor SDK without an open handle.
    const auto runAccessoryRequest = [&camera](auto submitRequest) {
        QEventLoop accessoryLoop;
        QTimer accessoryTimeout;
        accessoryTimeout.setSingleShot(true);
        QObject::connect(&accessoryTimeout, &QTimer::timeout,
                         &accessoryLoop, &QEventLoop::quit);

        bool completed = false;
        bool validCallback = false;
        const auto requestId = submitRequest(
            [&](auto callbackRequestId, auto result) {
                completed = true;
                validCallback =
                    callbackRequestId != 0 &&
                    QThread::currentThread() == camera.thread() &&
                    std::holds_alternative<amscope::CameraError>(result);
                accessoryLoop.quit();
            });
        if (requestId == 0)
            return false;

        accessoryTimeout.start(5000);
        accessoryLoop.exec();
        return completed && validCallback;
    };

    if (!runAccessoryRequest([&](auto callback) {
            return camera.requestEmbeddedLightSource(std::move(callback));
        }) ||
        !runAccessoryRequest([&](auto callback) {
            return camera.requestSetEmbeddedLightSourceLevel(
                10, std::move(callback));
        }) ||
        !runAccessoryRequest([&](auto callback) {
            return camera.requestFilterWheelState(std::move(callback));
        }) ||
        !runAccessoryRequest([&](auto callback) {
            return camera.requestMoveFilterWheel(
                0, amscope::FilterWheelDirection::Clockwise,
                std::move(callback));
        }) ||
        !runAccessoryRequest([&](auto callback) {
            return camera.requestST4GuideState(std::move(callback));
        }) ||
        !runAccessoryRequest([&](auto callback) {
            return camera.requestST4Guide(
                amscope::ST4GuideDirection::North, 100,
                std::move(callback));
        }) ||
        !runAccessoryRequest([&](auto callback) {
            return camera.requestST4Guide(
                amscope::ST4GuideDirection::Stop, 0,
                std::move(callback));
        }) ||
        !runAccessoryRequest([&](auto callback) {
            return camera.requestAstroAutoFocuserState(std::move(callback));
        }) ||
        !runAccessoryRequest([&](auto callback) {
            return camera.requestMoveAstroAutoFocuser(0, std::move(callback));
        }) ||
        !runAccessoryRequest([&](auto callback) {
            return camera.requestHaltAstroAutoFocuser(std::move(callback));
        }) ||
        !runAccessoryRequest([&](auto callback) {
            return camera.requestLensFocusState(std::move(callback));
        }) ||
        !runAccessoryRequest([&](auto callback) {
            return camera.requestSetManualLensFocusPosition(0, std::move(callback));
        }) ||
        !runAccessoryRequest([&](auto callback) {
            return camera.requestSetLensAperturePosition(0, std::move(callback));
        }) ||
        !runAccessoryRequest([&](auto callback) {
            return camera.requestTriggerConfiguration(std::move(callback));
        }) ||
        !runAccessoryRequest([&](auto callback) {
            amscope::AmcamTriggerConfiguration configuration;
            configuration.mode = amscope::AmcamTriggerMode::SoftwareOrSimulated;
            return camera.requestConfigureTrigger(configuration, std::move(callback));
        }) ||
        !runAccessoryRequest([&](auto callback) {
            return camera.requestSoftwareTrigger(1, std::move(callback));
        })) {
        std::cerr << "Accessory requests lost their typed error or Qt affinity contract.\n";
        return EXIT_FAILURE;
    }

    // An accessory read is a feature snapshot. A later accessory mutation
    // invalidates that older read even though both requests share the session.
    QEventLoop accessorySupersessionLoop;
    QTimer accessorySupersessionTimeout;
    accessorySupersessionTimeout.setSingleShot(true);
    QObject::connect(&accessorySupersessionTimeout, &QTimer::timeout,
                     &accessorySupersessionLoop, &QEventLoop::quit);
    bool staleAccessoryReadDelivered = false;
    bool accessoryMutationCompleted = false;

    const auto staleAccessoryRead = camera.requestEmbeddedLightSource(
        [&](amscope::QtAmcamCamera::FeatureRequestId,
            amscope::QtAmcamCamera::EmbeddedLightSourceResult) {
            staleAccessoryReadDelivered = true;
        });
    const auto accessoryMutation = camera.requestSetEmbeddedLightSourceLevel(
        5,
        [&](amscope::QtAmcamCamera::FeatureRequestId requestId,
            amscope::QtAmcamCamera::AccessoryCommandResult result) {
            accessoryMutationCompleted = requestId != 0 &&
                std::holds_alternative<amscope::CameraError>(result);
            accessorySupersessionLoop.quit();
        });
    if (staleAccessoryRead == 0 || accessoryMutation == 0) {
        std::cerr << "A valid accessory supersession request was rejected.\n";
        return EXIT_FAILURE;
    }
    accessorySupersessionTimeout.start(5000);
    accessorySupersessionLoop.exec();
    if (!accessoryMutationCompleted || staleAccessoryReadDelivered) {
        std::cerr << "A later accessory mutation failed to invalidate an older accessory read.\n";
        return EXIT_FAILURE;
    }

    // Lifecycle requests share the same FIFO. Start on a closed camera
    // reports an error; later Stop and Close remain accepted and execute in order.
    QEventLoop lifecycleLoop;
    QTimer lifecycleTimeout;
    lifecycleTimeout.setSingleShot(true);
    QObject::connect(&lifecycleTimeout, &QTimer::timeout,
                     &lifecycleLoop, &QEventLoop::quit);

    std::vector<int> lifecycleCompletionOrder;
    bool lifecycleResultsTyped = true;
    bool lifecycleCallbacksOnAffinityThread = true;

    const auto startRequest = camera.requestStart(
        [&](amscope::QtAmcamCamera::CameraRequestId requestId,
            amscope::QtAmcamCamera::LifecycleResult result) {
            lifecycleCallbacksOnAffinityThread =
                lifecycleCallbacksOnAffinityThread &&
                QThread::currentThread() == camera.thread();
            lifecycleResultsTyped =
                lifecycleResultsTyped &&
                requestId != 0 &&
                std::holds_alternative<amscope::CameraError>(result);
            lifecycleCompletionOrder.push_back(1);
            if (lifecycleCompletionOrder.size() == 3)
                lifecycleLoop.quit();
        });
    const auto stopRequest = camera.requestStop(
        [&](amscope::QtAmcamCamera::CameraRequestId requestId,
            amscope::QtAmcamCamera::LifecycleResult result) {
            lifecycleCallbacksOnAffinityThread =
                lifecycleCallbacksOnAffinityThread &&
                QThread::currentThread() == camera.thread();
            lifecycleResultsTyped =
                lifecycleResultsTyped &&
                requestId != 0 &&
                std::holds_alternative<std::monostate>(result);
            lifecycleCompletionOrder.push_back(2);
            if (lifecycleCompletionOrder.size() == 3)
                lifecycleLoop.quit();
        });
    const auto closeRequest = camera.requestClose(
        [&](amscope::QtAmcamCamera::CameraRequestId requestId,
            amscope::QtAmcamCamera::LifecycleResult result) {
            lifecycleCallbacksOnAffinityThread =
                lifecycleCallbacksOnAffinityThread &&
                QThread::currentThread() == camera.thread();
            lifecycleResultsTyped =
                lifecycleResultsTyped &&
                requestId != 0 &&
                std::holds_alternative<std::monostate>(result);
            lifecycleCompletionOrder.push_back(3);
            if (lifecycleCompletionOrder.size() == 3)
                lifecycleLoop.quit();
        });

    if (startRequest == 0 || stopRequest == 0 || closeRequest == 0) {
        std::cerr << "A valid asynchronous lifecycle request was rejected.\n";
        return EXIT_FAILURE;
    }

    lifecycleTimeout.start(10000);
    lifecycleLoop.exec();

    if (lifecycleCompletionOrder != std::vector<int>{1, 2, 3}) {
        std::cerr << "Lifecycle operations did not execute/report in FIFO order.\n";
        return EXIT_FAILURE;
    }
    if (!lifecycleResultsTyped || !lifecycleCallbacksOnAffinityThread) {
        std::cerr << "Lifecycle callbacks lost typed errors or Qt affinity.\n";
        return EXIT_FAILURE;
    }
    // Run discovery after the lifecycle sequence in a stable Closed session.
    // A later lifecycle transition is supposed to invalidate an older discovery
    // result, so the test deliberately does not enqueue both at once.
    QEventLoop enumerationLoop;
    QTimer enumerationTimeout;
    enumerationTimeout.setSingleShot(true);
    QObject::connect(&enumerationTimeout, &QTimer::timeout,
                     &enumerationLoop, &QEventLoop::quit);

    bool enumerationCompleted = false;
    bool enumerationCallbackOnAffinityThread = true;
    const auto enumerationRequest = camera.requestEnumerateCameras(
        [&](amscope::QtAmcamCamera::CameraRequestId requestId,
            amscope::QtAmcamCamera::CameraListResult result) {
            enumerationCallbackOnAffinityThread =
                QThread::currentThread() == camera.thread();
            enumerationCompleted = requestId != 0 &&
                (std::holds_alternative<std::vector<amscope::CameraInfo>>(result) ||
                 std::holds_alternative<amscope::CameraError>(result));
            enumerationLoop.quit();
        });
    if (enumerationRequest == 0) {
        std::cerr << "Camera enumeration request could not be queued.\n";
        return EXIT_FAILURE;
    }

    enumerationTimeout.start(10000);
    enumerationLoop.exec();

    if (!enumerationCompleted || !enumerationCallbackOnAffinityThread) {
        std::cerr << "Camera enumeration did not complete on the Qt affinity thread.\n";
        return EXIT_FAILURE;
    }

    // A queued camera instance must reject the synchronous contract.
    const auto syncAfterQueue = camera.featureDescriptors();
    const auto* queuedModeError =
        std::get_if<camera::CameraFeatureAccessError>(&syncAfterQueue);
    if (!queuedModeError ||
        queuedModeError->message.find("committed to queued execution") == std::string::npos) {
        std::cerr << "Queued execution did not reject the synchronous feature API.\n";
        return EXIT_FAILURE;
    }

    // A synchronous operation commits a separate instance to sync mode and
    // prevents later queued operations from entering its execution stream.
    amscope::QtAmcamCamera synchronousCamera;
    amscope::CameraError synchronousError;
    (void)synchronousCamera.start(&synchronousError);
    const auto rejectedQueuedRequest = synchronousCamera.requestFeatureDescriptors(
        [](amscope::QtAmcamCamera::FeatureRequestId,
           amscope::QtAmcamCamera::FeatureDescriptorsResult) {});
    if (rejectedQueuedRequest != 0) {
        std::cerr << "Synchronous execution did not reject the queued request API.\n";
        return EXIT_FAILURE;
    }

    std::cout << "All Qt camera request tests passed.\n";
    return EXIT_SUCCESS;
}
