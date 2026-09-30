#include "data/DataStore.h"
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace voctv {

void DataStore::recordBscan(const BScanFrame &f, bool intoVolume) {
    latest_ = f;
    if (!intoVolume) return;
    // A volume must have one frame size; a resize mid-acquisition restarts it.
    for (const auto &v : volume_)
        if (!v.data.isEmpty() && (v.width != f.width || v.height != f.height)) {
            volume_.clear();
            break;
        }
    if (volume_.size() != params_.volumeSlices) volume_.resize(params_.volumeSlices);
    if (f.index >= 0 && f.index < volume_.size()) volume_[f.index] = f;
}

void DataStore::clearVolume() {
    volume_.clear();
    volume_.resize(params_.volumeSlices);
}

int DataStore::filledVolumeSlices() const {
    int n = 0;
    for (const auto &f : volume_) n += f.data.isEmpty() ? 0 : 1;
    return n;
}

void DataStore::appendVibration(const VibrationChunk &c) {
    if (c.sampleRateHz != sampleRateHz_) {
        vibration_.clear();
        sampleRateHz_ = c.sampleRateHz;
    }
    vibration_ += c.displacementNm;
    if (vibration_.size() > kMaxVibrationSamples)
        vibration_.remove(0, vibration_.size() - kMaxVibrationSamples);
}

QVector<QVector<float>> DataStore::layerSurface(int depthFrom, int depthTo) const {
    QVector<QVector<float>> rows;
    for (const auto &f : volume_) {
        if (f.data.isEmpty()) continue;
        const int lo = qBound(0, depthFrom, f.height - 1);
        const int hi = qBound(lo + 1, depthTo, f.height);
        QVector<float> row(f.width);
        for (int x = 0; x < f.width; ++x) {
            int best = lo;
            for (int z = lo; z < hi; ++z)
                if (f.at(x, z) > f.at(x, best)) best = z;
            row[x] = float(best);
        }
        rows.append(row);
    }
    return rows;
}

static QJsonObject paramsToJson(const AcquisitionParams &p) {
    return QJsonObject{
        {"depthPixels", p.depthPixels}, {"ascansPerBscan", p.ascansPerBscan},
        {"framesPerSecond", p.framesPerSecond}, {"noiseLevel", p.noiseLevel},
        {"volumeSlices", p.volumeSlices}, {"toneFrequencyHz", p.toneFrequencyHz},
        {"toneLevelDb", p.toneLevelDb}, {"sweepStartHz", p.sweepStartHz},
        {"sweepEndHz", p.sweepEndHz}, {"sweepSteps", p.sweepSteps},
        {"selectedX", p.selectedPixel.x()}, {"selectedY", p.selectedPixel.y()},
        {"seed", qint64(p.seed)},
    };
}

static AcquisitionParams paramsFromJson(const QJsonObject &o) {
    AcquisitionParams p;
    p.depthPixels = o["depthPixels"].toInt(p.depthPixels);
    p.ascansPerBscan = o["ascansPerBscan"].toInt(p.ascansPerBscan);
    p.framesPerSecond = o["framesPerSecond"].toInt(p.framesPerSecond);
    p.noiseLevel = o["noiseLevel"].toDouble(p.noiseLevel);
    p.volumeSlices = o["volumeSlices"].toInt(p.volumeSlices);
    p.toneFrequencyHz = o["toneFrequencyHz"].toDouble(p.toneFrequencyHz);
    p.toneLevelDb = o["toneLevelDb"].toDouble(p.toneLevelDb);
    p.sweepStartHz = o["sweepStartHz"].toDouble(p.sweepStartHz);
    p.sweepEndHz = o["sweepEndHz"].toDouble(p.sweepEndHz);
    p.sweepSteps = o["sweepSteps"].toInt(p.sweepSteps);
    p.selectedPixel = QPoint(o["selectedX"].toInt(), o["selectedY"].toInt());
    p.seed = quint32(o["seed"].toInteger(p.seed));
    return p;
}

static void prepare(QDataStream &s) {
    s.setByteOrder(QDataStream::LittleEndian);
    s.setFloatingPointPrecision(QDataStream::SinglePrecision);
}

QString DataStore::save(const QString &dir) const {
    if (!QDir().mkpath(dir)) return QStringLiteral("Cannot create folder: %1").arg(dir);
    QJsonArray sliceIndices;
    int width = 0, height = 0;
    QFile vol(dir + "/volume.bin");
    if (!vol.open(QIODevice::WriteOnly)) return QStringLiteral("Cannot write volume.bin");
    QDataStream vs(&vol);
    prepare(vs);
    for (int i = 0; i < volume_.size(); ++i) {
        if (volume_[i].data.isEmpty()) continue;
        sliceIndices.append(i);
        width = volume_[i].width;
        height = volume_[i].height;
        for (float v : volume_[i].data) vs << v;
    }
    vol.close();

    QFile vib(dir + "/vibration.bin");
    if (!vib.open(QIODevice::WriteOnly)) return QStringLiteral("Cannot write vibration.bin");
    QDataStream bs(&vib);
    prepare(bs);
    for (float v : vibration_) bs << v;
    vib.close();

    QJsonArray tuning;
    for (const auto &t : tuning_)
        tuning.append(QJsonObject{{"f", t.frequencyHz}, {"m", t.magnitudeNm}, {"p", t.phaseRad}});

    const QJsonObject root{
        {"version", 1},
        {"params", paramsToJson(params_)},
        {"volume", QJsonObject{{"width", width}, {"height", height}, {"sliceIndices", sliceIndices}}},
        {"vibration", QJsonObject{{"sampleRateHz", sampleRateHz_}, {"count", int(vibration_.size())}}},
        {"tuning", tuning},
    };
    QFile js(dir + "/session.json");
    if (!js.open(QIODevice::WriteOnly)) return QStringLiteral("Cannot write session.json");
    js.write(QJsonDocument(root).toJson());
    return {};
}

QString DataStore::load(const QString &dir) {
    QFile js(dir + "/session.json");
    if (!js.open(QIODevice::ReadOnly)) return QStringLiteral("Cannot open session.json in %1").arg(dir);
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(js.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject())
        return QStringLiteral("session.json is not valid JSON: %1").arg(perr.errorString());
    const QJsonObject root = doc.object();
    if (root["version"].toInt() != 1) return QStringLiteral("Unsupported session version.");

    DataStore next;
    next.params_ = paramsFromJson(root["params"].toObject());
    const QString problem = validate(next.params_);
    if (!problem.isEmpty()) return QStringLiteral("Invalid parameters in session.json: %1").arg(problem);

    const QJsonObject volObj = root["volume"].toObject();
    const int width = volObj["width"].toInt(), height = volObj["height"].toInt();
    const QJsonArray indices = volObj["sliceIndices"].toArray();
    QFile vol(dir + "/volume.bin");
    if (!vol.open(QIODevice::ReadOnly)) return QStringLiteral("Cannot open volume.bin");
    const qint64 expectedVol = qint64(indices.size()) * width * height * 4;
    if (vol.size() != expectedVol)
        return QStringLiteral("volume.bin size mismatch: expected %1 bytes, found %2").arg(expectedVol).arg(vol.size());
    next.volume_.resize(next.params_.volumeSlices);
    QDataStream vs(&vol);
    prepare(vs);
    for (const auto &idx : indices) {
        const int i = idx.toInt();
        if (i < 0 || i >= next.volume_.size()) return QStringLiteral("volume slice index %1 out of range").arg(i);
        BScanFrame f;
        f.index = i; f.width = width; f.height = height;
        f.data.resize(width * height);
        for (float &v : f.data) vs >> v;
        next.volume_[i] = f;
        next.latest_ = f;
    }

    const QJsonObject vibObj = root["vibration"].toObject();
    const int count = vibObj["count"].toInt();
    QFile vib(dir + "/vibration.bin");
    if (!vib.open(QIODevice::ReadOnly)) return QStringLiteral("Cannot open vibration.bin");
    if (vib.size() != qint64(count) * 4)
        return QStringLiteral("vibration.bin size mismatch: expected %1 bytes, found %2").arg(qint64(count) * 4).arg(vib.size());
    QDataStream bs(&vib);
    prepare(bs);
    next.vibration_.resize(count);
    for (float &v : next.vibration_) bs >> v;
    next.sampleRateHz_ = vibObj["sampleRateHz"].toDouble();

    for (const auto &t : root["tuning"].toArray()) {
        const QJsonObject o = t.toObject();
        next.tuning_.append({o["f"].toDouble(), o["m"].toDouble(), o["p"].toDouble()});
    }

    *this = next;
    return {};
}

}  // namespace voctv
