#include "app/AcquisitionController.h"

namespace voctv {

AcquisitionController::AcquisitionController(IDevice *device, QObject *parent)
    : QObject(parent), device_(device) {
    registerMetaTypes();
    thread_.setObjectName(QStringLiteral("voctv-device"));
    device_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, device_, &QObject::deleteLater);

    connect(device_, &IDevice::bscanReady, this, &AcquisitionController::onDeviceBscan);
    connect(device_, &IDevice::vibrationReady, this, &AcquisitionController::vibrationReady);
    connect(device_, &IDevice::tuningPointReady, this, &AcquisitionController::tuningPointReady);
    connect(device_, &IDevice::acquisitionFinished, this, &AcquisitionController::onDeviceFinished);
    connect(device_, &IDevice::stateChanged, this, &AcquisitionController::stateChanged);
    connect(device_, &IDevice::error, this, &AcquisitionController::error);
    thread_.start();
}

AcquisitionController::~AcquisitionController() {
    QMetaObject::invokeMethod(device_, [d = device_] { d->stop(); }, Qt::BlockingQueuedConnection);
    thread_.quit();
    thread_.wait();  // device is deleted on its own thread via deleteLater
}

template <typename F> void AcquisitionController::onDevice(F &&fn) {
    QMetaObject::invokeMethod(device_, std::forward<F>(fn), Qt::QueuedConnection);
}

void AcquisitionController::connectDevice() { onDevice([d = device_] { d->connectDevice(); }); }
void AcquisitionController::disconnectDevice() { onDevice([d = device_] { d->disconnectDevice(); }); }
void AcquisitionController::configure(const AcquisitionParams &p) { onDevice([d = device_, p] { d->configure(p); }); }
void AcquisitionController::start(Mode mode) {
    pending_.reset();
    lossless_ = mode == Mode::Volume;
    onDevice([d = device_, mode] { d->startAcquisition(mode); });
}
void AcquisitionController::stop() { onDevice([d = device_] { d->stop(); }); }
void AcquisitionController::simulateFault() { onDevice([d = device_] { d->simulateFault(); }); }

void AcquisitionController::onDeviceBscan(const BScanFrame &frame) {
    if (lossless_) {  // every volume slice matters: deliver in order, never coalesce
        deliverPending();
        emit bscanReady(frame);
        return;
    }
    if (pending_) ++dropped_;  // an undelivered frame is being replaced: latest wins
    pending_ = frame;
    if (!deliveryScheduled_) {
        deliveryScheduled_ = true;
        QMetaObject::invokeMethod(this, &AcquisitionController::deliverPending, Qt::QueuedConnection);
    }
}

void AcquisitionController::deliverPending() {
    deliveryScheduled_ = false;
    if (!pending_) return;
    BScanFrame frame = std::move(*pending_);
    pending_.reset();
    emit bscanReady(frame);
}

void AcquisitionController::onDeviceFinished(Mode mode) {
    deliverPending();  // the final frame must reach the GUI before "finished"
    emit acquisitionFinished(mode);
}

}  // namespace voctv
