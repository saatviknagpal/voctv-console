// src/core/Types.cpp
#include "core/Types.h"

namespace voctv {

static bool inRange(double v, double lo, double hi) { return v >= lo && v <= hi; }

QString validate(const AcquisitionParams &p) {
    if (!inRange(p.depthPixels, 32, 1024)) return QStringLiteral("Depth pixels must be between 32 and 1024.");
    if (!inRange(p.ascansPerBscan, 32, 1024)) return QStringLiteral("A-scans per B-scan must be between 32 and 1024.");
    if (!inRange(p.framesPerSecond, 1, 60)) return QStringLiteral("Frame rate must be between 1 and 60 fps.");
    if (!inRange(p.noiseLevel, 0.0, 1.0)) return QStringLiteral("Noise level must be between 0 and 1.");
    if (!inRange(p.volumeSlices, 2, 256)) return QStringLiteral("Volume slices must be between 2 and 256.");
    if (!inRange(p.toneFrequencyHz, 100, 80000)) return QStringLiteral("Tone frequency must be between 100 and 80000 Hz.");
    if (!inRange(p.toneLevelDb, 0, 120)) return QStringLiteral("Tone level must be between 0 and 120 dB.");
    if (!inRange(p.sweepStartHz, 100, 80000) || !inRange(p.sweepEndHz, 100, 80000) || p.sweepStartHz >= p.sweepEndHz)
        return QStringLiteral("Sweep range must satisfy 100 <= start < end <= 80000 Hz.");
    if (!inRange(p.sweepSteps, 2, 200)) return QStringLiteral("Sweep steps must be between 2 and 200.");
    if (p.selectedPixel.x() < 0 || p.selectedPixel.x() >= p.ascansPerBscan ||
        p.selectedPixel.y() < 0 || p.selectedPixel.y() >= p.depthPixels)
        return QStringLiteral("Selected pixel is outside the B-scan.");
    return {};
}

void registerMetaTypes() {
    qRegisterMetaType<Mode>();
    qRegisterMetaType<DeviceState>();
    qRegisterMetaType<AcquisitionParams>();
    qRegisterMetaType<BScanFrame>();
    qRegisterMetaType<VibrationChunk>();
    qRegisterMetaType<TuningPoint>();
}

}  // namespace voctv
