#pragma once
#include <QElapsedTimer>
#include <QMainWindow>
#include <functional>
#include "core/Types.h"
#include "data/DataStore.h"

class QComboBox; class QSpinBox; class QDoubleSpinBox; class QPushButton; class QTabWidget; class QLabel; class QPlainTextEdit;
class BScanView; class AScanPlot; class VibrationPlot; class TuningCurvePlot; class VolumeView;
namespace voctv { class AcquisitionController; }

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    void setHeadless(bool headless) { headless_ = headless; }
    void runDemo(std::function<void()> done);

private slots:
    void onConnectClicked();
    void onStartClicked();
    void onStopClicked();
    void onFaultClicked();
    void onSaveClicked();
    void onLoadClicked();
    void onParamsEdited();
    void onPixelSelected(QPoint pixel);
    void onBscan(const voctv::BScanFrame &frame);
    void onVibration(const voctv::VibrationChunk &chunk);
    void onTuningPoint(const voctv::TuningPoint &point);
    void onFinished(voctv::Mode mode);
    void onState(voctv::DeviceState state);
    void onError(const QString &message);

private:
    QWidget *buildControls();
    voctv::AcquisitionParams gatherParams();
    voctv::Mode currentMode() const;
    void applyParamsToWidgets(const voctv::AcquisitionParams &p);
    void updateAScan();
    void updateVolumeView();
    void refreshFromStore();
    void updateStatus();
    void log(const QString &message);

    voctv::AcquisitionController *controller_ = nullptr;
    voctv::DataStore store_;
    voctv::DeviceState state_ = voctv::DeviceState::Disconnected;
    voctv::Mode activeMode_ = voctv::Mode::Structural;
    QPoint selected_{128, 174};
    bool headless_ = false;
    bool loadingWidgets_ = false;

    QComboBox *mode_;
    QSpinBox *depth_, *ascans_, *fps_, *slices_, *sweepSteps_;
    QDoubleSpinBox *noise_, *toneFreq_, *toneLevel_, *sweepStart_, *sweepEnd_;
    QPushButton *connectBtn_, *startBtn_, *stopBtn_, *faultBtn_, *saveBtn_, *loadBtn_;
    BScanView *bscan_;
    AScanPlot *ascanPlot_;
    VibrationPlot *vibPlot_;
    TuningCurvePlot *tuningPlot_;
    VolumeView *volume_;
    QTabWidget *tabs_;
    QLabel *stateLabel_, *fpsLabel_, *dropLabel_;
    QPlainTextEdit *log_;
    QElapsedTimer fpsClock_;
    int framesInWindow_ = 0;
    double measuredFps_ = 0.0;
    int volumeFramesSinceUpdate_ = 0;
};
