#pragma once
#include <QMetaType>
#include <QPoint>
#include <QString>
#include <QVector>

namespace voctv {

enum class Mode { Structural, Volume, Vibrometry, TuningSweep };
enum class DeviceState { Disconnected, Connected, Acquiring, Error };

struct AcquisitionParams {
    int depthPixels = 256;
    int ascansPerBscan = 256;
    int framesPerSecond = 20;
    double noiseLevel = 0.15;
    int volumeSlices = 64;
    double toneFrequencyHz = 8000.0;
    double toneLevelDb = 60.0;
    double sweepStartHz = 1000.0;
    double sweepEndHz = 40000.0;
    int sweepSteps = 40;
    QPoint selectedPixel{128, 128};  // x = A-scan index, y = depth index
    quint32 seed = 42;
};

struct BScanFrame {
    int index = 0;
    int width = 0;
    int height = 0;
    QVector<float> data;  // row-major: data[y * width + x], values 0..1
    float at(int x, int y) const { return data[y * width + x]; }
};

struct VibrationChunk {
    double startTimeS = 0.0;
    double sampleRateHz = 0.0;
    QVector<float> displacementNm;
};

struct TuningPoint {
    double frequencyHz = 0.0;
    double magnitudeNm = 0.0;
    double phaseRad = 0.0;
};

QString validate(const AcquisitionParams &p);
void registerMetaTypes();

}  // namespace voctv

Q_DECLARE_METATYPE(voctv::Mode)
Q_DECLARE_METATYPE(voctv::DeviceState)
Q_DECLARE_METATYPE(voctv::AcquisitionParams)
Q_DECLARE_METATYPE(voctv::BScanFrame)
Q_DECLARE_METATYPE(voctv::VibrationChunk)
Q_DECLARE_METATYPE(voctv::TuningPoint)
