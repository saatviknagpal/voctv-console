#include <QtTest>
#include "device/SimulatedVoctvDevice.h"
using namespace voctv;

static AcquisitionParams small() {
    AcquisitionParams p;
    p.depthPixels = 48; p.ascansPerBscan = 40; p.framesPerSecond = 60;
    p.volumeSlices = 3; p.sweepSteps = 5; p.selectedPixel = QPoint(20, 30);
    return p;
}

class TstDevice : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { registerMetaTypes(); }
    void startWithoutConnectEmitsError() {
        SimulatedVoctvDevice d; QSignalSpy err(&d, &IDevice::error);
        d.startAcquisition(Mode::Structural);
        QCOMPARE(err.count(), 1);
        QCOMPARE(d.state(), DeviceState::Disconnected);
    }
    void structuralEmitsFramesOfConfiguredSize() {
        SimulatedVoctvDevice d; d.connectDevice(); d.configure(small());
        QSignalSpy spy(&d, &IDevice::bscanReady);
        d.startAcquisition(Mode::Structural);
        QVERIFY(spy.wait(2000));
        const auto f = spy.first().first().value<BScanFrame>();
        QCOMPARE(f.width, 40); QCOMPARE(f.height, 48);
        d.stop();
        QCOMPARE(d.state(), DeviceState::Connected);
    }
    void volumeFinishesAfterSlices() {
        SimulatedVoctvDevice d; d.connectDevice(); d.configure(small());
        QSignalSpy frames(&d, &IDevice::bscanReady), done(&d, &IDevice::acquisitionFinished);
        d.startAcquisition(Mode::Volume);
        QVERIFY(done.wait(3000));
        QCOMPARE(frames.count(), 3);
        for (int i = 0; i < 3; ++i) QCOMPARE(frames.at(i).first().value<BScanFrame>().index, i);
        QCOMPARE(d.state(), DeviceState::Connected);
    }
    void tuningSweepEmitsAllSteps() {
        SimulatedVoctvDevice d; d.connectDevice(); d.configure(small());
        QSignalSpy pts(&d, &IDevice::tuningPointReady), done(&d, &IDevice::acquisitionFinished);
        d.startAcquisition(Mode::TuningSweep);
        QVERIFY(done.wait(3000));
        QCOMPARE(pts.count(), 5);
        QCOMPARE(pts.first().first().value<TuningPoint>().frequencyHz, 1000.0);
        QCOMPARE(pts.last().first().value<TuningPoint>().frequencyHz, 40000.0);
    }
    void vibrometryEmitsChunks() {
        SimulatedVoctvDevice d; d.connectDevice(); d.configure(small());
        QSignalSpy spy(&d, &IDevice::vibrationReady);
        d.startAcquisition(Mode::Vibrometry);
        QVERIFY(spy.wait(2000));
        QCOMPARE(spy.first().first().value<VibrationChunk>().displacementNm.size(), 400);
        d.stop();
    }
    void invalidConfigureEmitsErrorAndKeepsParams() {
        SimulatedVoctvDevice d; d.connectDevice(); d.configure(small());
        QSignalSpy err(&d, &IDevice::error);
        AcquisitionParams bad = small(); bad.depthPixels = 5;
        d.configure(bad);
        QCOMPARE(err.count(), 1);
        QSignalSpy spy(&d, &IDevice::bscanReady);
        d.startAcquisition(Mode::Structural);
        QVERIFY(spy.wait(2000));
        QCOMPARE(spy.first().first().value<BScanFrame>().height, 48);
        d.stop();
    }
    void doubleStartEmitsError() {
        SimulatedVoctvDevice d; d.connectDevice(); d.configure(small());
        QSignalSpy err(&d, &IDevice::error);
        d.startAcquisition(Mode::Structural);
        d.startAcquisition(Mode::Structural);
        QCOMPARE(err.count(), 1);
        QCOMPARE(d.state(), DeviceState::Acquiring);
        d.stop();
    }
    void simulateFaultStopsAndDisconnects() {
        SimulatedVoctvDevice d; d.connectDevice(); d.configure(small());
        QSignalSpy err(&d, &IDevice::error), frames(&d, &IDevice::bscanReady);
        d.startAcquisition(Mode::Structural);
        d.simulateFault();
        QCOMPARE(err.count(), 1);
        QCOMPARE(d.state(), DeviceState::Disconnected);
        QTest::qWait(200);
        QCOMPARE(frames.count(), 0);
    }
};
QTEST_GUILESS_MAIN(TstDevice)
#include "tst_device.moc"
