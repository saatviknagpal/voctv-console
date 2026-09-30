#pragma once
#include "core/TissueModel.h"
#include "device/IDevice.h"

class QTimer;

namespace voctv {

class SimulatedVoctvDevice : public IDevice {
    Q_OBJECT
public:
    explicit SimulatedVoctvDevice(QObject *parent = nullptr);
    DeviceState state() const { return state_; }

public slots:
    void connectDevice() override;
    void disconnectDevice() override;
    void configure(const voctv::AcquisitionParams &params) override;
    void startAcquisition(voctv::Mode mode) override;
    void stop() override;
    void simulateFault() override;

private slots:
    void tick();

private:
    void setState(DeviceState s);
    void finish();
    int intervalMs() const;

    QTimer *timer_;
    TissueModel model_;
    AcquisitionParams params_;
    Mode mode_ = Mode::Structural;
    DeviceState state_ = DeviceState::Disconnected;
    int slice_ = 0;
    int frame_ = 0;
    double timeS_ = 0.0;
    int sweepIndex_ = 0;
};

}  // namespace voctv
