#include <QtTest>
#include "core/Types.h"
using namespace voctv;

class TstTypes : public QObject {
    Q_OBJECT
private slots:
    void defaultsAreValid() { QVERIFY(validate(AcquisitionParams{}).isEmpty()); }
    void rejectsDepthOutOfRange() {
        AcquisitionParams p; p.depthPixels = 8;
        QVERIFY(!validate(p).isEmpty());
    }
    void rejectsInvertedSweep() {
        AcquisitionParams p; p.sweepStartHz = 5000; p.sweepEndHz = 1000;
        QVERIFY(!validate(p).isEmpty());
    }
    void rejectsPixelOutsideFrame() {
        AcquisitionParams p; p.ascansPerBscan = 64; p.depthPixels = 64; p.selectedPixel = QPoint(100, 10);
        QVERIFY(!validate(p).isEmpty());
    }
    void frameIndexing() {
        BScanFrame f; f.width = 3; f.height = 2; f.data = {0, 1, 2, 3, 4, 5};
        QCOMPARE(f.at(2, 1), 5.0f);
    }
};
QTEST_GUILESS_MAIN(TstTypes)
#include "tst_types.moc"
