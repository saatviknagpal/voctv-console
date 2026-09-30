#include <QtTest>
#include <QTemporaryDir>
#include "core/TissueModel.h"
#include "data/DataStore.h"
using namespace voctv;

static DataStore populated() {
    DataStore s;
    AcquisitionParams p; p.depthPixels = 40; p.ascansPerBscan = 32; p.volumeSlices = 4; p.selectedPixel = QPoint(5, 6);
    s.setParams(p);
    TissueModel m(1);
    for (int i = 0; i < 3; ++i) s.recordBscan(m.bscan(p, i, i), true);
    VibrationChunk c; c.sampleRateHz = 1000; c.displacementNm = {1, 2, 3, 4};
    s.appendVibration(c);
    s.appendTuning({1000, 0.5, -0.1});
    s.appendTuning({2000, 0.9, -0.4});
    return s;
}

class TstDataStore : public QObject {
    Q_OBJECT
private slots:
    void resizedVolumeRestartsAndStaysLoadable() {
        DataStore s = populated();  // 3 slices of 32x40
        AcquisitionParams p = s.params(); p.depthPixels = 64;  // depth changed mid-volume
        s.setParams(p);
        s.recordBscan(TissueModel(1).bscan(p, 3, 3), true);
        QCOMPARE(s.filledVolumeSlices(), 1);  // stale-size slices were discarded
        QTemporaryDir dir;
        QVERIFY(s.save(dir.path()).isEmpty());
        DataStore b;
        const QString err = b.load(dir.path());
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(b.filledVolumeSlices(), 1);
    }
    void roundTrip() {
        const DataStore a = populated();
        QTemporaryDir dir;
        QVERIFY(a.save(dir.path()).isEmpty());
        DataStore b;
        QVERIFY(b.load(dir.path()).isEmpty());
        QCOMPARE(b.params().depthPixels, 40);
        QCOMPARE(b.params().selectedPixel, QPoint(5, 6));
        QCOMPARE(b.filledVolumeSlices(), 3);
        QCOMPARE(b.volume()[2].data, a.volume()[2].data);
        QCOMPARE(b.vibrationSamples(), a.vibrationSamples());
        QCOMPARE(b.vibrationSampleRate(), 1000.0);
        QCOMPARE(b.tuning().size(), 2);
        QCOMPARE(b.tuning()[1].magnitudeNm, 0.9);
    }
    void truncatedVolumeFailsAndKeepsState() {
        const DataStore a = populated();
        QTemporaryDir dir;
        QVERIFY(a.save(dir.path()).isEmpty());
        QFile f(dir.path() + "/volume.bin");
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.resize(f.size() - 8));
        f.close();
        DataStore b = populated();
        b.clearTuning();
        const QString err = b.load(dir.path());
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains("volume.bin"));
        QCOMPARE(b.tuning().size(), 0);  // unchanged
    }
    void badJsonFails() {
        QTemporaryDir dir;
        QFile f(dir.path() + "/session.json");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{not json");
        f.close();
        DataStore s;
        QVERIFY(!s.load(dir.path()).isEmpty());
    }
    void missingDirectoryFails() {
        DataStore s;
        QVERIFY(!s.load("Z:/definitely/not/here").isEmpty());
    }
    void vibrationIsCapped() {
        DataStore s;
        VibrationChunk c; c.sampleRateHz = 1000; c.displacementNm = QVector<float>(15000, 1.0f);
        s.appendVibration(c); s.appendVibration(c);
        QCOMPARE(s.vibrationSamples().size(), DataStore::kMaxVibrationSamples);
    }
    void layerSurfaceFindsBasilarMembrane() {
        DataStore s;
        AcquisitionParams p; p.depthPixels = 200; p.ascansPerBscan = 64; p.volumeSlices = 2; p.noiseLevel = 0.0;
        s.setParams(p);
        TissueModel m;
        s.recordBscan(m.bscan(p, 0, 0), true);
        s.recordBscan(m.bscan(p, 1, 0), true);
        const auto surf = s.layerSurface(int(0.58 * 200), int(0.78 * 200));
        QCOMPARE(surf.size(), 2);
        QCOMPARE(surf[0].size(), 64);
        const double x = (32 + 0.5) / 64;
        const double expected = TissueModel::layerDepthFrac(TissueModel::basilarMembraneLayer(), x, 0) * 200 - 0.5;
        QVERIFY(std::abs(surf[0][32] - expected) <= 2.0);
    }
};
QTEST_GUILESS_MAIN(TstDataStore)
#include "tst_datastore.moc"
