// src/core/TissueModel.cpp
#include "core/TissueModel.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace voctv {

static constexpr double kPi = 3.14159265358979323846;

TissueModel::TissueModel(quint32 seed) : seed_(seed) {}

const QVector<Layer> &TissueModel::layers() {
    static const QVector<Layer> kLayers = {
        {"Bone surface", 0.12, 0.95, 0.012},
        {"Reissner's membrane", 0.38, 0.45, 0.008},
        {"Tectorial membrane", 0.55, 0.35, 0.010},
        {"Basilar membrane", 0.68, 0.70, 0.010},
    };
    return kLayers;
}

int TissueModel::basilarMembraneLayer() { return 3; }

double TissueModel::layerDepthFrac(int layer, double xFrac, int slice) {
    return layers()[layer].depthFrac + 0.03 * std::sin(2.0 * kPi * xFrac + 0.08 * slice);
}

QVector<float> TissueModel::ascan(int depthPixels, double xFrac, int slice, int frame, double noise) const {
    QVector<float> out(depthPixels);
    std::mt19937 rng(seed_ ^ (quint32(xFrac * 100000.0) * 2654435761u) ^ (quint32(slice) * 40503u) ^
                     (quint32(frame) * 2246822519u));
    std::normal_distribution<double> gauss(0.0, 1.0);
    for (int z = 0; z < depthPixels; ++z) {
        const double d = (z + 0.5) / depthPixels;
        double v = 0.0;
        for (int l = 0; l < layers().size(); ++l) {
            const double s = (d - layerDepthFrac(l, xFrac, slice)) / layers()[l].widthFrac;
            v += layers()[l].reflectivity * std::exp(-0.5 * s * s);
        }
        v *= std::exp(-0.8 * d);  // light attenuation with depth
        if (noise > 0.0) {
            v *= std::max(0.0, 1.0 + noise * gauss(rng));      // multiplicative speckle
            v += noise * 0.04 * std::abs(gauss(rng));           // background noise floor
        }
        out[z] = float(std::clamp(v, 0.0, 1.0));
    }
    return out;
}

BScanFrame TissueModel::bscan(const AcquisitionParams &p, int slice, int frame) const {
    BScanFrame f;
    f.index = slice;
    f.width = p.ascansPerBscan;
    f.height = p.depthPixels;
    f.data.resize(f.width * f.height);
    for (int x = 0; x < f.width; ++x) {
        const auto column = ascan(f.height, (x + 0.5) / f.width, slice, frame, p.noiseLevel);
        for (int z = 0; z < f.height; ++z) f.data[z * f.width + x] = column[z];
    }
    return f;
}

double TissueModel::bestFrequencyHz(double xFrac) {
    return 40000.0 * std::pow(1000.0 / 40000.0, xFrac);  // base (x=0) high, apex (x=1) low
}

TuningPoint TissueModel::response(double frequencyHz, double levelDb, double xFrac, double depthFrac) {
    constexpr double kQ = 6.0;
    const double r = frequencyHz / bestFrequencyHz(xFrac);
    const double denom = std::sqrt((1 - r * r) * (1 - r * r) + (r / kQ) * (r / kQ));
    const double amp = 0.5 * std::pow(10.0, (levelDb - 60.0) / 20.0);
    const double bm = layerDepthFrac(basilarMembraneLayer(), xFrac, 0);
    const double s = (depthFrac - bm) / 0.02;
    const double weight = 0.1 + 0.9 * std::exp(-0.5 * s * s);
    TuningPoint t;
    t.frequencyHz = frequencyHz;
    t.magnitudeNm = amp * weight / denom;
    t.phaseRad = -std::atan2(r / kQ, 1 - r * r);
    return t;
}

VibrationChunk TissueModel::vibration(const AcquisitionParams &p, double startTimeS, int samples) const {
    const double xFrac = (p.selectedPixel.x() + 0.5) / p.ascansPerBscan;
    const double depthFrac = (p.selectedPixel.y() + 0.5) / p.depthPixels;
    const TuningPoint tp = response(p.toneFrequencyHz, p.toneLevelDb, xFrac, depthFrac);
    VibrationChunk c;
    c.startTimeS = startTimeS;
    c.sampleRateHz = 20.0 * p.toneFrequencyHz;
    c.displacementNm.resize(samples);
    std::mt19937 rng(seed_ ^ quint32(startTimeS * 1e6));
    std::normal_distribution<double> gauss(0.0, 1.0);
    for (int i = 0; i < samples; ++i) {
        const double t = startTimeS + i / c.sampleRateHz;
        const double v = tp.magnitudeNm * std::sin(2.0 * kPi * p.toneFrequencyHz * t + tp.phaseRad);
        c.displacementNm[i] = float(v + p.noiseLevel * 0.05 * tp.magnitudeNm * gauss(rng));
    }
    return c;
}

}  // namespace voctv
