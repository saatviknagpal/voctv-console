// src/core/TissueModel.h
#pragma once
#include "core/Types.h"

namespace voctv {

struct Layer {
    const char *name;
    double depthFrac;     // 0 = top of image, 1 = bottom
    double reflectivity;  // peak brightness before attenuation
    double widthFrac;     // gaussian width as a fraction of depth
};

class TissueModel {
public:
    explicit TissueModel(quint32 seed = 42);
    static const QVector<Layer> &layers();
    static int basilarMembraneLayer();
    static double layerDepthFrac(int layer, double xFrac, int slice);
    QVector<float> ascan(int depthPixels, double xFrac, int slice, int frame, double noise) const;
    BScanFrame bscan(const AcquisitionParams &p, int slice, int frame) const;
    static double bestFrequencyHz(double xFrac);
    static TuningPoint response(double frequencyHz, double levelDb, double xFrac, double depthFrac);
    VibrationChunk vibration(const AcquisitionParams &p, double startTimeS, int samples) const;

private:
    quint32 seed_;
};

}  // namespace voctv
