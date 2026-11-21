#include "amscope/QtAmcamCamera.h"
#include <QByteArray>
#include <QMetaObject>
#include <QMetaType>
#include <utility>

namespace amscope {
QtAmcamCamera::QtAmcamCamera(QObject* parent) : QObject(parent) {
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

bool QtAmcamCamera::setResolution(std::uint32_t index, CameraError* error) {
    CameraError local;
    const bool ok = camera_.setResolution(index, &local);
    if (!ok) { if (error) *error = local; emitError(local); }
    return ok;
}
bool QtAmcamCamera::setAutoExposure(bool enabled, CameraError* error) {
    CameraError local;
    const bool ok = camera_.setAutoExposure(enabled, &local);
    if (!ok) { if (error) *error = local; emitError(local); }
    return ok;
}
bool QtAmcamCamera::setExposureTime(std::uint32_t time, CameraError* error) {
    CameraError local;
    const bool ok = camera_.setExposureTime(time, &local);
    if (!ok) { if (error) *error = local; emitError(local); }
    return ok;
}
bool QtAmcamCamera::setExposureGain(std::uint16_t gain, CameraError* error) {
    CameraError local;
    const bool ok = camera_.setExposureGain(gain, &local);
    if (!ok) { if (error) *error = local; emitError(local); }
    return ok;
}
bool QtAmcamCamera::readExposure(ExposureSettings& settings, CameraError* error) {
    CameraError local;
    const bool ok = camera_.readExposure(settings, &local);
    if (!ok) { if (error) *error = local; emitError(local); }
    return ok;
}
bool QtAmcamCamera::setWhiteBalance(int temperature, int tint, CameraError* error) {
    CameraError local;
    const bool ok = camera_.setWhiteBalance(temperature, tint, &local);
    if (!ok) { if (error) *error = local; emitError(local); }
    return ok;
}
bool QtAmcamCamera::readWhiteBalance(WhiteBalanceSettings& settings, CameraError* error) {
    CameraError local;
    const bool ok = camera_.readWhiteBalance(settings, &local);
    if (!ok) { if (error) *error = local; emitError(local); }
    return ok;
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
} // namespace amscope
