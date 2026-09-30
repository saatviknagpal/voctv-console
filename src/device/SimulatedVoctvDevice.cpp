#include "device/SimulatedVoctvDevice.h"
#include <QTimer>
#include <cmath>

namespace voctv {

SimulatedVoctvDevice::SimulatedVoctvDevice(QObject *parent)
    : IDevice(parent), timer_(new QTimer(this)), model_(params_.seed) {
    connect(timer_, &QTimer::timeout, this, &SimulatedVoctvDevice::tick);
}

void SimulatedVoctvDevice::setState(DeviceState s) {
    if (state_ == s) return;
    state_ = s;
    emit stateChanged(s);
}

int SimulatedVoctvDevice::intervalMs() const {
    return mode_ == Mode::Vibrometry ? 50 : qMax(1, 1000 / params_.framesPerSecond);
}

void SimulatedVoctvDevice::connectDevice() {
    if (state_ == DeviceState::Disconnected || state_ == DeviceState::Error) setState(DeviceState::Connected);
}

void SimulatedVoctvDevice::disconnectDevice() {
    timer_->stop();
    setState(DeviceState::Disconnected);
}

void SimulatedVoctvDevice::configure(const AcquisitionParams &params) {
    const QString problem = validate(params);
    if (!problem.isEmpty()) {
        emit error(problem);
        return;
    }
    params_ = params;
    model_ = TissueModel(params.seed);
    if (timer_->isActive()) timer_->setInterval(intervalMs());
}

void SimulatedVoctvDevice::startAcquisition(Mode mode) {
    if (state_ == DeviceState::Acquiring) {
        emit error(QStringLiteral("Acquisition is already running."));
        return;
    }
    if (state_ != DeviceState::Connected) {
        emit error(QStringLiteral("Device is not connected."));
        return;
    }
    mode_ = mode;
    slice_ = 0;
    frame_ = 0;
    timeS_ = 0.0;
    sweepIndex_ = 0;
    setState(DeviceState::Acquiring);
    timer_->start(intervalMs());
}

void SimulatedVoctvDevice::stop() {
    timer_->stop();
    if (state_ == DeviceState::Acquiring) setState(DeviceState::Connected);
}

void SimulatedVoctvDevice::simulateFault() {
    timer_->stop();
    setState(DeviceState::Error);
    emit error(QStringLiteral("Device connection lost (simulated fault)."));
    setState(DeviceState::Disconnected);
}

void SimulatedVoctvDevice::finish() {
    timer_->stop();
    setState(DeviceState::Connected);
    emit acquisitionFinished(mode_);
}

void SimulatedVoctvDevice::tick() {
    switch (mode_) {
    case Mode::Structural:
        emit bscanReady(model_.bscan(params_, 0, frame_++));
        break;
    case Mode::Volume:
        emit bscanReady(model_.bscan(params_, slice_, frame_++));
        if (++slice_ >= params_.volumeSlices) finish();
        break;
    case Mode::Vibrometry: {
        constexpr int kSamples = 400;
        const VibrationChunk c = model_.vibration(params_, timeS_, kSamples);
        timeS_ += kSamples / c.sampleRateHz;
        emit vibrationReady(c);
        break;
    }
    case Mode::TuningSweep: {
        const double f = params_.sweepStartHz *
                         std::pow(params_.sweepEndHz / params_.sweepStartHz, double(sweepIndex_) / (params_.sweepSteps - 1));
        const double xFrac = (params_.selectedPixel.x() + 0.5) / params_.ascansPerBscan;
        const double depthFrac = (params_.selectedPixel.y() + 0.5) / params_.depthPixels;
        emit tuningPointReady(TissueModel::response(f, params_.toneLevelDb, xFrac, depthFrac));
        if (++sweepIndex_ >= params_.sweepSteps) finish();
        break;
    }
    }
}

}  // namespace voctv
