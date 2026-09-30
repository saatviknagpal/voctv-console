#pragma once
#include <QObject>
#include "core/Types.h"

namespace voctv {

// Contract every acquisition device implements. Real hardware = a new subclass.
class IDevice : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~IDevice() override = default;

public slots:
    virtual void connectDevice() = 0;
    virtual void disconnectDevice() = 0;
    virtual void configure(const voctv::AcquisitionParams &params) = 0;
    virtual void startAcquisition(voctv::Mode mode) = 0;
    virtual void stop() = 0;
    virtual void simulateFault() {}

signals:
    void bscanReady(const voctv::BScanFrame &frame);
    void vibrationReady(const voctv::VibrationChunk &chunk);
    void tuningPointReady(const voctv::TuningPoint &point);
    void acquisitionFinished(voctv::Mode mode);
    void stateChanged(voctv::DeviceState state);
    void error(const QString &message);
};

}  // namespace voctv
