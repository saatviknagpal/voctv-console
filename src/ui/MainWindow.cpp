#include "ui/MainWindow.h"
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include "app/AcquisitionController.h"
#include "core/TissueModel.h"
#include "device/SimulatedVoctvDevice.h"
#include "ui/BScanView.h"
#include "ui/PlotViews.h"
#include "ui/VolumeView.h"

using namespace voctv;

enum TabIndex { kTabVibration = 0, kTabTuning = 1, kTab3D = 2 };

static QSpinBox *spin(int lo, int hi, int value, const QString &suffix = {}) {
    auto *s = new QSpinBox; s->setRange(lo, hi); s->setValue(value); s->setSuffix(suffix); return s;
}
static QDoubleSpinBox *dspin(double lo, double hi, double value, int decimals, double step, const QString &suffix = {}) {
    auto *s = new QDoubleSpinBox; s->setRange(lo, hi); s->setDecimals(decimals); s->setSingleStep(step);
    s->setValue(value); s->setSuffix(suffix); return s;
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(tr("VOCTV Console — Volumetric OCT & Vibrometry (simulated)"));
    controller_ = new AcquisitionController(new SimulatedVoctvDevice, this);

    bscan_ = new BScanView;
    ascanPlot_ = new AScanPlot;
    vibPlot_ = new VibrationPlot;
    tuningPlot_ = new TuningCurvePlot;
    volume_ = new VolumeView;
    tabs_ = new QTabWidget;
    tabs_->insertTab(kTabVibration, vibPlot_, tr("Vibration trace"));
    tabs_->insertTab(kTabTuning, tuningPlot_, tr("Tuning curve"));
    tabs_->insertTab(kTab3D, volume_, tr("3D volume"));

    auto *top = new QSplitter(Qt::Horizontal);
    top->addWidget(bscan_);
    top->addWidget(ascanPlot_);
    top->setStretchFactor(0, 3);
    top->setStretchFactor(1, 2);
    auto *right = new QSplitter(Qt::Vertical);
    right->addWidget(top);
    right->addWidget(tabs_);
    right->setStretchFactor(0, 3);
    right->setStretchFactor(1, 2);

    auto *central = new QWidget;
    auto *layout = new QHBoxLayout(central);
    layout->addWidget(buildControls());
    layout->addWidget(right, 1);
    setCentralWidget(central);

    stateLabel_ = new QLabel; fpsLabel_ = new QLabel; dropLabel_ = new QLabel;
    statusBar()->addPermanentWidget(stateLabel_);
    statusBar()->addPermanentWidget(fpsLabel_);
    statusBar()->addPermanentWidget(dropLabel_);

    connect(controller_, &AcquisitionController::bscanReady, this, &MainWindow::onBscan);
    connect(controller_, &AcquisitionController::vibrationReady, this, &MainWindow::onVibration);
    connect(controller_, &AcquisitionController::tuningPointReady, this, &MainWindow::onTuningPoint);
    connect(controller_, &AcquisitionController::acquisitionFinished, this, &MainWindow::onFinished);
    connect(controller_, &AcquisitionController::stateChanged, this, &MainWindow::onState);
    connect(controller_, &AcquisitionController::error, this, &MainWindow::onError);
    connect(bscan_, &BScanView::pixelSelected, this, &MainWindow::onPixelSelected);

    store_.setParams(gatherParams());
    bscan_->setSelectedPixel(selected_);
    fpsClock_.start();
    onState(DeviceState::Disconnected);
    log(tr("Ready. Click Connect to attach the simulated VOCTV device."));
}

MainWindow::~MainWindow() = default;  // controller_ is a child; its destructor joins the worker thread

QWidget *MainWindow::buildControls() {
    auto *panel = new QWidget;
    panel->setFixedWidth(340);
    auto *v = new QVBoxLayout(panel);

    auto *devBox = new QGroupBox(tr("Device"));
    auto *devLay = new QVBoxLayout(devBox);
    connectBtn_ = new QPushButton(tr("Connect"));
    faultBtn_ = new QPushButton(tr("Simulate disconnect"));
    devLay->addWidget(connectBtn_);
    devLay->addWidget(faultBtn_);
    v->addWidget(devBox);

    auto *acqBox = new QGroupBox(tr("Acquisition"));
    auto *form = new QFormLayout(acqBox);
    mode_ = new QComboBox;
    mode_->addItems({tr("Structural (live B-scan)"), tr("Volume"), tr("Vibrometry"), tr("Tuning sweep")});
    depth_ = spin(32, 1024, 256, " px");
    ascans_ = spin(32, 1024, 256);
    fps_ = spin(1, 60, 20, " fps");
    noise_ = dspin(0, 1, 0.15, 2, 0.05);
    slices_ = spin(2, 256, 64);
    form->addRow(tr("Mode"), mode_);
    form->addRow(tr("Depth"), depth_);
    form->addRow(tr("A-scans / B-scan"), ascans_);
    form->addRow(tr("Frame rate"), fps_);
    form->addRow(tr("Speckle noise"), noise_);
    form->addRow(tr("Volume slices"), slices_);
    v->addWidget(acqBox);

    auto *stimBox = new QGroupBox(tr("Stimulus"));
    auto *sform = new QFormLayout(stimBox);
    toneFreq_ = dspin(100, 80000, 8000, 0, 500, " Hz");
    toneLevel_ = dspin(0, 120, 60, 0, 5, " dB");
    sweepStart_ = dspin(100, 80000, 1000, 0, 500, " Hz");
    sweepEnd_ = dspin(100, 80000, 40000, 0, 500, " Hz");
    sweepSteps_ = spin(2, 200, 40);
    sform->addRow(tr("Tone"), toneFreq_);
    sform->addRow(tr("Level"), toneLevel_);
    sform->addRow(tr("Sweep start"), sweepStart_);
    sform->addRow(tr("Sweep end"), sweepEnd_);
    sform->addRow(tr("Sweep steps"), sweepSteps_);
    v->addWidget(stimBox);

    auto *runRow = new QHBoxLayout;
    startBtn_ = new QPushButton(tr("Start")); startBtn_->setObjectName("start");
    stopBtn_ = new QPushButton(tr("Stop")); stopBtn_->setObjectName("stop");
    runRow->addWidget(startBtn_);
    runRow->addWidget(stopBtn_);
    v->addLayout(runRow);

    auto *fileRow = new QHBoxLayout;
    saveBtn_ = new QPushButton(tr("Save session"));
    loadBtn_ = new QPushButton(tr("Load session"));
    fileRow->addWidget(saveBtn_);
    fileRow->addWidget(loadBtn_);
    v->addLayout(fileRow);

    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(200);
    v->addWidget(new QLabel(tr("Log")));
    v->addWidget(log_, 1);

    connect(connectBtn_, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(startBtn_, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    connect(stopBtn_, &QPushButton::clicked, this, &MainWindow::onStopClicked);
    connect(faultBtn_, &QPushButton::clicked, this, &MainWindow::onFaultClicked);
    connect(saveBtn_, &QPushButton::clicked, this, &MainWindow::onSaveClicked);
    connect(loadBtn_, &QPushButton::clicked, this, &MainWindow::onLoadClicked);
    for (QSpinBox *s : {depth_, ascans_, fps_, slices_, sweepSteps_})
        connect(s, &QSpinBox::valueChanged, this, &MainWindow::onParamsEdited);
    for (QDoubleSpinBox *s : {noise_, toneFreq_, toneLevel_, sweepStart_, sweepEnd_})
        connect(s, &QDoubleSpinBox::valueChanged, this, &MainWindow::onParamsEdited);
    return panel;
}

Mode MainWindow::currentMode() const { return Mode(mode_->currentIndex()); }

AcquisitionParams MainWindow::gatherParams() {
    AcquisitionParams p;
    p.depthPixels = depth_->value();
    p.ascansPerBscan = ascans_->value();
    p.framesPerSecond = fps_->value();
    p.noiseLevel = noise_->value();
    p.volumeSlices = slices_->value();
    p.toneFrequencyHz = toneFreq_->value();
    p.toneLevelDb = toneLevel_->value();
    p.sweepStartHz = sweepStart_->value();
    p.sweepEndHz = sweepEnd_->value();
    p.sweepSteps = sweepSteps_->value();
    selected_ = QPoint(qBound(0, selected_.x(), p.ascansPerBscan - 1), qBound(0, selected_.y(), p.depthPixels - 1));
    p.selectedPixel = selected_;
    return p;
}

void MainWindow::applyParamsToWidgets(const AcquisitionParams &p) {
    loadingWidgets_ = true;
    depth_->setValue(p.depthPixels); ascans_->setValue(p.ascansPerBscan); fps_->setValue(p.framesPerSecond);
    noise_->setValue(p.noiseLevel); slices_->setValue(p.volumeSlices); toneFreq_->setValue(p.toneFrequencyHz);
    toneLevel_->setValue(p.toneLevelDb); sweepStart_->setValue(p.sweepStartHz); sweepEnd_->setValue(p.sweepEndHz);
    sweepSteps_->setValue(p.sweepSteps);
    selected_ = p.selectedPixel;
    bscan_->setSelectedPixel(selected_);
    loadingWidgets_ = false;
}

void MainWindow::onConnectClicked() {
    if (state_ == DeviceState::Disconnected || state_ == DeviceState::Error) controller_->connectDevice();
    else controller_->disconnectDevice();
}

void MainWindow::onStartClicked() {
    const AcquisitionParams p = gatherParams();
    const QString problem = validate(p);
    if (!problem.isEmpty()) { onError(problem); return; }
    if (state_ == DeviceState::Acquiring) controller_->stop();
    activeMode_ = currentMode();
    store_.setParams(p);
    switch (activeMode_) {
    case Mode::Volume: store_.clearVolume(); volumeFramesSinceUpdate_ = 0; tabs_->setCurrentIndex(kTab3D); break;
    case Mode::Vibrometry: store_.clearVibration(); tabs_->setCurrentIndex(kTabVibration); break;
    case Mode::TuningSweep: store_.clearTuning(); tuningPlot_->setPoints({}); tabs_->setCurrentIndex(kTabTuning); break;
    case Mode::Structural: break;
    }
    controller_->configure(p);
    controller_->start(activeMode_);
    log(tr("Started %1").arg(mode_->currentText()));
}

void MainWindow::onStopClicked() { controller_->stop(); log(tr("Stopped")); }
void MainWindow::onFaultClicked() { controller_->simulateFault(); }

void MainWindow::onParamsEdited() {
    if (loadingWidgets_) return;
    const AcquisitionParams p = gatherParams();
    if (!validate(p).isEmpty()) return;  // e.g., sweep start temporarily above end while typing
    store_.setParams(p);
    if (state_ == DeviceState::Acquiring) controller_->configure(p);
}

void MainWindow::onPixelSelected(QPoint pixel) {
    selected_ = pixel;
    updateAScan();
    const double xFrac = (pixel.x() + 0.5) / store_.params().ascansPerBscan;
    log(tr("Selected point (%1, %2), best frequency ≈ %3 Hz")
            .arg(pixel.x()).arg(pixel.y()).arg(TissueModel::bestFrequencyHz(xFrac), 0, 'f', 0));
    onParamsEdited();
}

void MainWindow::updateAScan() {
    const BScanFrame &f = store_.latestBscan();
    if (f.data.isEmpty() || selected_.x() >= f.width) return;
    QVector<float> column(f.height);
    for (int z = 0; z < f.height; ++z) column[z] = f.at(selected_.x(), z);
    ascanPlot_->setProfile(column);
}

void MainWindow::updateVolumeView() {
    const int depth = store_.params().depthPixels;
    volume_->setSurface(store_.layerSurface(int(0.58 * depth), int(0.78 * depth)), depth);
}

void MainWindow::onBscan(const BScanFrame &frame) {
    store_.recordBscan(frame, activeMode_ == Mode::Volume);
    bscan_->setFrame(frame);
    updateAScan();
    if (activeMode_ == Mode::Volume && ++volumeFramesSinceUpdate_ % 8 == 0) updateVolumeView();
    ++framesInWindow_;
    if (fpsClock_.elapsed() >= 1000) {
        measuredFps_ = framesInWindow_ * 1000.0 / fpsClock_.restart();
        framesInWindow_ = 0;
    }
    updateStatus();
}

void MainWindow::onVibration(const VibrationChunk &chunk) {
    store_.appendVibration(chunk);
    vibPlot_->setSamples(store_.vibrationSamples(), store_.vibrationSampleRate());
}

void MainWindow::onTuningPoint(const TuningPoint &point) {
    store_.appendTuning(point);
    tuningPlot_->setPoints(store_.tuning());
}

void MainWindow::onFinished(Mode mode) {
    if (mode == Mode::Volume) {
        updateVolumeView();
        log(tr("Volume complete: %1 slices").arg(store_.filledVolumeSlices()));
    } else if (mode == Mode::TuningSweep) {
        log(tr("Tuning sweep complete: %1 points").arg(store_.tuning().size()));
    }
}

void MainWindow::onState(DeviceState state) {
    state_ = state;
    const bool connected = state == DeviceState::Connected || state == DeviceState::Acquiring;
    connectBtn_->setText(connected ? tr("Disconnect") : tr("Connect"));
    startBtn_->setEnabled(connected);
    stopBtn_->setEnabled(state == DeviceState::Acquiring);
    faultBtn_->setEnabled(connected);
    if (state != DeviceState::Acquiring) measuredFps_ = 0.0;
    updateStatus();
}

void MainWindow::onError(const QString &message) {
    log(tr("Error: %1").arg(message));
    statusBar()->showMessage(message, 6000);
    if (!headless_) {
        auto *box = new QMessageBox(QMessageBox::Warning, tr("VOCTV device"), message, QMessageBox::Ok, this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
    }
}

void MainWindow::onSaveClicked() {
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose a folder for the session"));
    if (dir.isEmpty()) return;
    const QString err = store_.save(dir);
    err.isEmpty() ? log(tr("Session saved to %1").arg(dir)) : onError(err);
}

void MainWindow::onLoadClicked() {
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose a saved session folder"));
    if (dir.isEmpty()) return;
    const QString err = store_.load(dir);
    if (!err.isEmpty()) { onError(err); return; }
    refreshFromStore();
    log(tr("Session loaded from %1").arg(dir));
}

void MainWindow::refreshFromStore() {
    applyParamsToWidgets(store_.params());
    if (!store_.latestBscan().data.isEmpty()) bscan_->setFrame(store_.latestBscan());
    updateAScan();
    updateVolumeView();
    tuningPlot_->setPoints(store_.tuning());
    vibPlot_->setSamples(store_.vibrationSamples(), store_.vibrationSampleRate());
}

void MainWindow::updateStatus() {
    static const char *names[] = {"Disconnected", "Connected", "Acquiring", "Error"};
    stateLabel_->setText(tr("State: %1").arg(names[int(state_)]));
    fpsLabel_->setText(tr("%1 fps").arg(measuredFps_, 0, 'f', 1));
    dropLabel_->setText(tr("dropped frames: %1").arg(controller_->droppedFrames()));
}

void MainWindow::log(const QString &message) {
    log_->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss  ") + message);
}

void MainWindow::runDemo(std::function<void()> done) {
    struct Step { int atMs; std::function<void()> action; };
    const int depth = depth_->value(), ascans = ascans_->value();
    const QVector<Step> steps = {
        {0, [this] { onConnectClicked(); }},
        {400, [this] { mode_->setCurrentIndex(int(Mode::Volume)); fps_->setValue(60); slices_->setValue(48); onStartClicked(); }},
        {1800, [this] { mode_->setCurrentIndex(int(Mode::TuningSweep)); fps_->setValue(30); onStartClicked(); }},
        {3400, [this, depth, ascans] {
             onPixelSelected(QPoint(ascans / 2, int(0.68 * depth)));
             toneFreq_->setValue(TissueModel::bestFrequencyHz(0.5));
             mode_->setCurrentIndex(int(Mode::Vibrometry)); onStartClicked(); }},
        {5000, [this] { mode_->setCurrentIndex(int(Mode::Structural)); fps_->setValue(20); onStartClicked(); }},
        {6200, [this] { tabs_->setCurrentIndex(kTab3D); }},
        {7200, [done] { done(); }},
    };
    for (const auto &s : steps) QTimer::singleShot(s.atMs, this, s.action);
}
