#pragma once
#include "amscope/AmcamCamera.h"
#include <QMetaType>
#include <QObject>
#include <QString>
#include <mutex>

Q_DECLARE_METATYPE(amscope::CameraState)
Q_DECLARE_METATYPE(amscope::FramePtr)

namespace amscope {

// Qt Core bridge for Widgets, Qt Quick, or a dedicated render-thread object.
// frameAvailable is a coalesced notification; retrieve the current immutable
// frame lease by calling takeLatestFrame() on the consumer thread.
class QtAmcamCamera final : public QObject {
    Q_OBJECT
public:
    explicit QtAmcamCamera(QObject* parent = nullptr);
    ~QtAmcamCamera() override;

    QtAmcamCamera(const QtAmcamCamera&) = delete;
    QtAmcamCamera& operator=(const QtAmcamCamera&) = delete;

    [[nodiscard]] static std::vector<CameraInfo> enumerate(CameraError* error = nullptr);
    bool open(const QString& deviceId, CameraError* error = nullptr);
    bool close(CameraError* error = nullptr);
    bool start(CameraError* error = nullptr);
    bool stop(CameraError* error = nullptr);

    [[nodiscard]] CameraState state() const noexcept;
    [[nodiscard]] std::uint64_t droppedFrames() const noexcept;
    [[nodiscard]] FramePtr takeLatestFrame();

    bool setResolution(std::uint32_t index, CameraError* error = nullptr);
    bool setAutoExposure(bool enabled, CameraError* error = nullptr);
    bool setExposureTime(std::uint32_t microseconds, CameraError* error = nullptr);
    bool setExposureGain(std::uint16_t gainPercent, CameraError* error = nullptr);
    bool readExposure(ExposureSettings& settings, CameraError* error = nullptr);
    bool setWhiteBalance(int temperature, int tint, CameraError* error = nullptr);
    bool readWhiteBalance(WhiteBalanceSettings& settings, CameraError* error = nullptr);
    bool autoWhiteBalance(CameraError* error = nullptr);
    bool readFrameRate(FrameRateStatistics& stats, CameraError* error = nullptr);

signals:
    void frameAvailable();
    void stateChanged(amscope::CameraState state);
    void errorOccurred(const QString& message, qint32 vendorCode);

private:
    void publishLatestFrame(FramePtr frame);
    void clearFrameMailbox();
    void emitError(const CameraError& error);

    AmcamCamera camera_;
    std::mutex mailboxMutex_;
    FramePtr latestFrame_;
    bool notificationPending_ = false;
};
} // namespace amscope
