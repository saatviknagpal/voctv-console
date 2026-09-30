#pragma once
#include "core/Types.h"

namespace voctv {

class DataStore {
public:
    static constexpr int kMaxVibrationSamples = 20000;

    void setParams(const AcquisitionParams &p) { params_ = p; }
    const AcquisitionParams &params() const { return params_; }

    void recordBscan(const BScanFrame &f, bool intoVolume);
    const BScanFrame &latestBscan() const { return latest_; }
    void clearVolume();
    const QVector<BScanFrame> &volume() const { return volume_; }
    int filledVolumeSlices() const;

    void appendVibration(const VibrationChunk &c);
    void clearVibration() { vibration_.clear(); }
    const QVector<float> &vibrationSamples() const { return vibration_; }
    double vibrationSampleRate() const { return sampleRateHz_; }

    void appendTuning(const TuningPoint &p) { tuning_.append(p); }
    void clearTuning() { tuning_.clear(); }
    const QVector<TuningPoint> &tuning() const { return tuning_; }

    // For each captured slice: depth index of the brightest pixel within [depthFrom, depthTo) per A-scan.
    QVector<QVector<float>> layerSurface(int depthFrom, int depthTo) const;

    QString save(const QString &dir) const;  // empty string on success
    QString load(const QString &dir);        // empty string on success; state unchanged on failure

private:
    AcquisitionParams params_;
    BScanFrame latest_;
    QVector<BScanFrame> volume_;
    QVector<float> vibration_;
    double sampleRateHz_ = 0.0;
    QVector<TuningPoint> tuning_;
};

}  // namespace voctv
