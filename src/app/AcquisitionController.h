// src/app/AcquisitionController.h
#pragma once
#include <QObject>
#include <QThread>
#include <optional>
#include "device/IDevice.h"

namespace voctv {

// Runs a device on a worker thread; the GUI only ever talks to this object.
class AcquisitionController : public QObject {
    Q_OBJECT
public:
    explicit AcquisitionController(IDevice *device, QObject *parent = nullptr);  // takes ownership
    ~AcquisitionController() override;
    int droppedFrames() const { return dropped_; }

public slots:
    void connectDevice();
    void disconnectDevice();
    void configure(const voctv::AcquisitionParams &params);
    void start(voctv::Mode mode);
    void stop();
    void simulateFault();

signals:
    void bscanReady(const voctv::BScanFrame &frame);
    void vibrationReady(const voctv::VibrationChunk &chunk);
    void tuningPointReady(const voctv::TuningPoint &point);
    void acquisitionFinished(voctv::Mode mode);
    void stateChanged(voctv::DeviceState state);
    void error(const QString &message);

private slots:
    void onDeviceBscan(const voctv::BScanFrame &frame);
    void deliverPending();
    void onDeviceFinished(voctv::Mode mode);

private:
    template <typename F> void onDevice(F &&fn);

    QThread thread_;
    IDevice *device_;
    std::optional<BScanFrame> pending_;
    bool deliveryScheduled_ = false;
    bool lossless_ = false;  // volume slices are all kept; live views use latest-wins
    int dropped_ = 0;
};

}  // namespace voctv
