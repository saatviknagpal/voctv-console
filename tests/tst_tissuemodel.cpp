#include <QtTest>
#include <cmath>
#include "core/TissueModel.h"
using namespace voctv;

class TstTissueModel : public QObject {
    Q_OBJECT
private slots:
    void layersPeakAtConfiguredDepths() {
        TissueModel m(7);
        const int n = 256;
        const auto a = m.ascan(n, 0.5, 0, 0, 0.0);  // xFrac 0.5, slice 0 => no curvature offset
        for (int l = 0; l < TissueModel::layers().size(); ++l) {
            const int expected = int(std::round(TissueModel::layers()[l].depthFrac * n - 0.5));
            int best = expected;
            for (int z = qMax(0, expected - 5); z <= qMin(n - 1, expected + 5); ++z)
                if (a[z] > a[best]) best = z;
            QVERIFY2(std::abs(best - expected) <= 2, qPrintable(QString("layer %1 peak at %2, expected %3").arg(l).arg(best).arg(expected)));
            QVERIFY(a[best] > 0.15f);
        }
    }
    void sameSeedSameOutput() {
        TissueModel a(3), b(3);
        QCOMPARE(a.ascan(128, 0.3, 2, 5, 0.3), b.ascan(128, 0.3, 2, 5, 0.3));
    }
    void differentFramesDifferWithNoise() {
        TissueModel m(3);
        QVERIFY(m.ascan(128, 0.3, 2, 5, 0.3) != m.ascan(128, 0.3, 2, 6, 0.3));
    }
    void bscanHasRequestedShape() {
        TissueModel m;
        AcquisitionParams p; p.depthPixels = 64; p.ascansPerBscan = 48;
        const auto f = m.bscan(p, 3, 0);
        QCOMPARE(f.width, 48); QCOMPARE(f.height, 64); QCOMPARE(f.index, 3);
        QCOMPARE(f.data.size(), 48 * 64);
    }
    void tuningPeaksNearBestFrequency() {
        const double xFrac = 0.5, start = 1000, end = 40000; const int steps = 40;
        const double bmDepth = TissueModel::layerDepthFrac(TissueModel::basilarMembraneLayer(), xFrac, 0);
        double bestF = start, bestMag = -1;
        for (int i = 0; i < steps; ++i) {
            const double f = start * std::pow(end / start, double(i) / (steps - 1));
            const auto r = TissueModel::response(f, 60, xFrac, bmDepth);
            if (r.magnitudeNm > bestMag) { bestMag = r.magnitudeNm; bestF = f; }
        }
        const double bf = TissueModel::bestFrequencyHz(xFrac);
        const double stepRatio = std::pow(end / start, 1.0 / (steps - 1));
        QVERIFY(qMax(bestF / bf, bf / bestF) <= stepRatio * 1.001);
    }
    void vibrationChunkShape() {
        TissueModel m;
        AcquisitionParams p; p.toneFrequencyHz = 5000;
        const auto c = m.vibration(p, 0.0, 400);
        QCOMPARE(c.displacementNm.size(), 400);
        QCOMPARE(c.sampleRateHz, 100000.0);
    }
};
QTEST_GUILESS_MAIN(TstTissueModel)
#include "tst_tissuemodel.moc"
