#include <QtTest>
#include "app/AcquisitionController.h"
#include "device/SimulatedVoctvDevice.h"
using namespace voctv;

static AcquisitionParams small() {
    AcquisitionParams p;
    p.depthPixels = 64; p.ascansPerBscan = 64; p.framesPerSecond = 60; p.selectedPixel = QPoint(10, 10);
    return p;
}

class TstController : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { registerMetaTypes(); }
    void deliversFramesOnGuiThread() {
        AcquisitionController c(new SimulatedVoctvDevice);
        bool onGui = true;
        connect(&c, &AcquisitionController::bscanReady, this, [&] {
            if (QThread::currentThread() != QCoreApplication::instance()->thread()) onGui = false;
        });
        QSignalSpy spy(&c, &AcquisitionController::bscanReady);
        c.connectDevice(); c.configure(small()); c.start(Mode::Structural);
        QVERIFY(spy.wait(3000));
        c.stop();
        QVERIFY(onGui);
    }
    void startStopCycles() {
        AcquisitionController c(new SimulatedVoctvDevice);
        c.connectDevice(); c.configure(small());
        for (int i = 0; i < 3; ++i) {
            QSignalSpy spy(&c, &AcquisitionController::bscanReady);
            c.start(Mode::Structural);
            QVERIFY(spy.wait(3000));
            QSignalSpy st(&c, &AcquisitionController::stateChanged);
            c.stop();
            QTRY_VERIFY_WITH_TIMEOUT(!st.isEmpty() && st.last().first().value<DeviceState>() == DeviceState::Connected, 3000);
        }
    }
    void destroyWhileAcquiring() {
        auto *c = new AcquisitionController(new SimulatedVoctvDevice);
        QSignalSpy spy(c, &AcquisitionController::bscanReady);
        c->connectDevice(); c->configure(small()); c->start(Mode::Structural);
        QVERIFY(spy.wait(3000));
        delete c;  // must not hang or crash
    }
    void coalescesFramesWhenGuiIsBusy() {
        AcquisitionController c(new SimulatedVoctvDevice);
        QSignalSpy spy(&c, &AcquisitionController::bscanReady);
        c.connectDevice(); c.configure(small()); c.start(Mode::Structural);
        QVERIFY(spy.wait(3000));
        QThread::msleep(600);  // simulate a blocked GUI thread
        QTest::qWait(150);
        c.stop();
        QVERIFY(c.droppedFrames() > 0);
    }
    void configureDuringAcquisitionChangesFrameSize() {
        AcquisitionController c(new SimulatedVoctvDevice);
        int w = 0, h = 0;
        connect(&c, &AcquisitionController::bscanReady, this, [&](const BScanFrame &f) { w = f.width; h = f.height; });
        c.connectDevice(); c.configure(small()); c.start(Mode::Structural);
        QTRY_VERIFY_WITH_TIMEOUT(w == 64, 3000);
        AcquisitionParams bigger = small(); bigger.ascansPerBscan = 96; bigger.depthPixels = 80;
        c.configure(bigger);
        QTRY_VERIFY_WITH_TIMEOUT(w == 96 && h == 80, 3000);
        c.stop();
    }
    void faultReportsError() {
        AcquisitionController c(new SimulatedVoctvDevice);
        QSignalSpy err(&c, &AcquisitionController::error);
        c.connectDevice(); c.configure(small()); c.start(Mode::Structural);
        c.simulateFault();
        QVERIFY(err.wait(3000));
    }
};
QTEST_GUILESS_MAIN(TstController)
#include "tst_controller.moc"
