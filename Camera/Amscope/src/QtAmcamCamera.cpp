#include "amscope/QtAmcamCamera.h"
#include <QByteArray>
#include <QMetaObject>
#include <QMetaType>
#include <utility>

namespace amscope {
QtAmcamCamera::QtAmcamCamera(QObject* parent)
    : QtAmcamCamera(AmcamCameraProfileRegistry{}, parent) {}

QtAmcamCamera::QtAmcamCamera(
    AmcamCameraProfileRegistry profiles,
    QObject* parent)
    : QObject(parent), camera_(std::move(profiles)) {
    qRegisterMetaType<amscope::CameraState>("amscope::CameraState");
    qRegisterMetaType<amscope::FramePtr>("amscope::FramePtr");
}

QtAmcamCamera::~QtAmcamCamera() {
    CameraError ignored;
    camera_.close(&ignored);
    clearFrameMailbox();
}

std::vector<CameraInfo> QtAmcamCamera::enumerate(CameraError* error) {
    return AmcamCamera::enumerate(error);
}

bool QtAmcamCamera::open(const QString& deviceId, CameraError* error) {
    const QByteArray id = deviceId.toUtf8();
    CameraError local;
    if (!camera_.open(std::string(id.constData(), static_cast<std::size_t>(id.size())),
                      &local)) {
        if (error) *error = local;
        emitError(local);
        return false;
    }
    clearFrameMailbox();
    emit stateChanged(camera_.state());
    return true;
}

bool QtAmcamCamera::close(CameraError* error) {
    CameraError local;
    const bool ok = camera_.close(&local);
    clearFrameMailbox();
    if (!ok) {
        if (error) *error = local;
        emitError(local);
    }
    emit stateChanged(camera_.state());
    return ok;
}

bool QtAmcamCamera::start(CameraError* error) {
    CameraError local;
    const bool ok = camera_.start(
        [this](FramePtr frame) {
            publishLatestFrame(std::move(frame));
        },
        [this](const CameraError& acquisitionError) {
            // Marshal notifications to this QObject's affinity thread. The
            // worker never calls Qt Widgets or a renderer directly.
            const CameraError copy = acquisitionError;
            QMetaObject::invokeMethod(this, [this, copy]() {
                emitError(copy);
                emit stateChanged(camera_.state());
            }, Qt::QueuedConnection);
        },
        &local);

    if (!ok) {
        if (error) *error = local;
        emitError(local);
    }
    emit stateChanged(camera_.state());
    return ok;
}

bool QtAmcamCamera::stop(CameraError* error) {
    CameraError local;
    const bool ok = camera_.stop(&local);
    clearFrameMailbox();
    if (!ok) {
        if (error) *error = local;
        emitError(local);
    }
    emit stateChanged(camera_.state());
    return ok;
}

CameraState QtAmcamCamera::state() const noexcept { return camera_.state(); }
std::uint64_t QtAmcamCamera::droppedFrames() const noexcept { return camera_.droppedFrames(); }

FramePtr QtAmcamCamera::takeLatestFrame() {
    std::lock_guard<std::mutex> lock(mailboxMutex_);
    FramePtr frame = std::move(latestFrame_);
    notificationPending_ = false;
    return frame;
}


bool QtAmcamCamera::autoWhiteBalance(CameraError* error) {
    CameraError local;
    const bool ok = camera_.autoWhiteBalance(&local);
    if (!ok) { if (error) *error = local; emitError(local); }
    return ok;
}
bool QtAmcamCamera::readFrameRate(FrameRateStatistics& stats, CameraError* error) {
    CameraError local;
    const bool ok = camera_.readFrameRate(stats, &local);
    if (!ok) { if (error) *error = local; emitError(local); }
    return ok;
}

void QtAmcamCamera::publishLatestFrame(FramePtr frame) {
    bool notify = false;
    {
        std::lock_guard<std::mutex> lock(mailboxMutex_);
        latestFrame_ = std::move(frame);
        if (!notificationPending_) {
            notificationPending_ = true;
            notify = true;
        }
    }

    if (notify) {
        // Queue the signal emission onto the QObject's thread. A connected
        // renderer on another thread should use Qt::QueuedConnection too.
        QMetaObject::invokeMethod(this, [this]() {
            emit frameAvailable();
        }, Qt::QueuedConnection);
    }
}

void QtAmcamCamera::clearFrameMailbox() {
    std::lock_guard<std::mutex> lock(mailboxMutex_);
    latestFrame_.reset();
    notificationPending_ = false;
}

void QtAmcamCamera::emitError(const CameraError& error) {
    emit errorOccurred(QString::fromStdString(error.message),
                       static_cast<qint32>(error.vendorCode));
}

camera::CameraFeatureResult<std::vector<camera::CameraFeatureDescriptor>>
QtAmcamCamera::featureDescriptors() {
    return camera_.featureDescriptors();
}

camera::CameraFeatureResult<camera::CameraFeatureValue>
QtAmcamCamera::readFeature(std::string_view name) {
    return camera_.readFeature(name);
}

camera::CameraFeatureResult<std::monostate>
QtAmcamCamera::writeFeature(
    std::string_view name,
    const camera::CameraFeatureValue& value) {
    return camera_.writeFeature(name, value);
}

} // namespace amscope
