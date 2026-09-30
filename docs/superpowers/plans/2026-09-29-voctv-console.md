# VOCTV Console Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a Qt 6 C++ desktop app that simulates a Volumetric OCT + Vibrometry acquisition device, streams data on a worker thread, and visualizes B-scans, A-scans, vibration, tuning curves and a 3D layer surface.

**Architecture:** A pure `TissueModel` generates data; `SimulatedVoctvDevice` implements the `IDevice` interface and emits frames from a `QTimer`; `AcquisitionController` runs the device on a `QThread` and coalesces B-scans (latest wins); `DataStore` holds and persists the session; `MainWindow` and small view widgets render everything. Core logic is a static library shared by the app and Qt Test executables.

**Tech Stack:** C++17, CMake + Ninja, GCC (MSYS2 UCRT64), Qt 6 (Core, Gui, Widgets, Charts, DataVisualization, Test), ffmpeg for the demo GIF.

**Spec:** `docs/superpowers/specs/2026-09-29-voctv-console-design.md`

## Global Constraints

- C++17; Qt 6; CMake >= 3.21; Ninja; GCC from MSYS2 UCRT64.
- UI must never block: all device work runs on the worker thread.
- Real hardware = one new `IDevice` subclass; no other file changes.
- Parameter limits (identical in UI and `validate()`): depth 32–1024 px, A-scans 32–1024, fps 1–60, noise 0–1, volume slices 2–256, tone 100–80000 Hz, level 0–120 dB, sweep 100 <= start < end <= 80000 Hz, sweep steps 2–200, selected pixel inside the B-scan.
- Session format: `session.json` + `volume.bin` + `vibration.bin` (float32 little-endian).
- Deviation from spec (recorded): 3D uses **Qt DataVisualization `Q3DSurface`** (C++ widget API) instead of Qt Graphs, whose 3D API is QML-first. If `qt6-datavis3d` is unavailable in MSYS2, stop and report before substituting.
- All shell commands below run from the repo root in Git Bash through the UCRT64 shell:
  `UCRT() { MSYSTEM=UCRT64 CHERE_INVOKING=1 /c/msys64/usr/bin/bash -lc "$*"; }`

## Review Focus

- Changing parameters (e.g., depth size) during acquisition → next frames use the new size, no crash. Pinned in Task 4 `configureDuringAcquisitionChangesFrameSize`.
- Selected pixel outside the B-scan after shrinking dimensions → `validate()` rejects it and the UI clamps it. Pinned in Task 1 `rejectsPixelOutsideFrame` and the Task 6 `gatherParams()` clamp.
- Loading a session whose binary size does not match its JSON → descriptive error, current session unchanged. Pinned in Task 5 `truncatedVolumeFailsAndKeepsState`.
- Closing the app mid-acquisition → no hang or crash. Pinned in Task 4 `destroyWhileAcquiring`.
- Pressing Start twice, or Start before Connect → error message, not a crash. Pinned in Task 3 `doubleStartEmitsError` and `startWithoutConnectEmitsError`.

---

### Task 1: Toolchain, project skeleton, shared types

**Files:**
- Create: `CMakeLists.txt`, `.gitignore`, `scripts/build.sh`
- Create: `src/core/Types.h`, `src/core/Types.cpp`
- Test: `tests/tst_types.cpp`

**Interfaces:**
- Produces: namespace `voctv` with `enum class Mode { Structural, Volume, Vibrometry, TuningSweep }`, `enum class DeviceState { Disconnected, Connected, Acquiring, Error }`, structs `AcquisitionParams`, `BScanFrame` (`index,width,height,QVector<float> data`, `float at(int x,int y) const`), `VibrationChunk` (`startTimeS, sampleRateHz, QVector<float> displacementNm`), `TuningPoint` (`frequencyHz, magnitudeNm, phaseRad`), `QString validate(const AcquisitionParams&)` (empty = valid), `void registerMetaTypes()`.

- [ ] **Step 1: Install toolchain**

```bash
winget install -e --id MSYS2.MSYS2 --accept-source-agreements --accept-package-agreements
/c/msys64/usr/bin/bash -lc "pacman -Syu --noconfirm"   # may need to run twice
/c/msys64/usr/bin/bash -lc "pacman -Ss mingw-w64-ucrt-x86_64-qt6-datavis3d"  # must list the package
/c/msys64/usr/bin/bash -lc "pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-charts mingw-w64-ucrt-x86_64-qt6-datavis3d mingw-w64-ucrt-x86_64-qt6-tools"
UCRT "g++ --version && cmake --version && qmake6 --version"
```
Expected: versions print; Qt >= 6.5.

- [ ] **Step 2: Create `.gitignore` and `scripts/build.sh`**

```gitignore
build/
dist/
*.zip
frames/
```

```bash
#!/usr/bin/env bash
set -euo pipefail
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

- [ ] **Step 3: Write the failing test `tests/tst_types.cpp`**

```cpp
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
```

- [ ] **Step 4: Create `CMakeLists.txt` (core lib + tests only for now)**

```cmake
cmake_minimum_required(VERSION 3.21)
project(VoctvConsole VERSION 1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Charts DataVisualization Test)
qt_standard_project_setup()

add_library(voctv_core STATIC
    src/core/Types.h src/core/Types.cpp
)
target_include_directories(voctv_core PUBLIC src)
target_link_libraries(voctv_core PUBLIC Qt6::Core Qt6::Gui)

enable_testing()
function(voctv_test name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE voctv_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()
voctv_test(tst_types)
```

- [ ] **Step 5: Run to verify it fails**

Run: `UCRT ./scripts/build.sh`
Expected: FAIL — `core/Types.h: No such file or directory`.

- [ ] **Step 6: Implement `src/core/Types.h` and `src/core/Types.cpp`**

```cpp
// src/core/Types.h
#pragma once
#include <QMetaType>
#include <QPoint>
#include <QString>
#include <QVector>

namespace voctv {

enum class Mode { Structural, Volume, Vibrometry, TuningSweep };
enum class DeviceState { Disconnected, Connected, Acquiring, Error };

struct AcquisitionParams {
    int depthPixels = 256;
    int ascansPerBscan = 256;
    int framesPerSecond = 20;
    double noiseLevel = 0.15;
    int volumeSlices = 64;
    double toneFrequencyHz = 8000.0;
    double toneLevelDb = 60.0;
    double sweepStartHz = 1000.0;
    double sweepEndHz = 40000.0;
    int sweepSteps = 40;
    QPoint selectedPixel{128, 128};  // x = A-scan index, y = depth index
    quint32 seed = 42;
};

struct BScanFrame {
    int index = 0;
    int width = 0;
    int height = 0;
    QVector<float> data;  // row-major: data[y * width + x], values 0..1
    float at(int x, int y) const { return data[y * width + x]; }
};

struct VibrationChunk {
    double startTimeS = 0.0;
    double sampleRateHz = 0.0;
    QVector<float> displacementNm;
};

struct TuningPoint {
    double frequencyHz = 0.0;
    double magnitudeNm = 0.0;
    double phaseRad = 0.0;
};

QString validate(const AcquisitionParams &p);
void registerMetaTypes();

}  // namespace voctv

Q_DECLARE_METATYPE(voctv::Mode)
Q_DECLARE_METATYPE(voctv::DeviceState)
Q_DECLARE_METATYPE(voctv::AcquisitionParams)
Q_DECLARE_METATYPE(voctv::BScanFrame)
Q_DECLARE_METATYPE(voctv::VibrationChunk)
Q_DECLARE_METATYPE(voctv::TuningPoint)
```

```cpp
// src/core/Types.cpp
#include "core/Types.h"

namespace voctv {

static bool inRange(double v, double lo, double hi) { return v >= lo && v <= hi; }

QString validate(const AcquisitionParams &p) {
    if (!inRange(p.depthPixels, 32, 1024)) return QStringLiteral("Depth pixels must be between 32 and 1024.");
    if (!inRange(p.ascansPerBscan, 32, 1024)) return QStringLiteral("A-scans per B-scan must be between 32 and 1024.");
    if (!inRange(p.framesPerSecond, 1, 60)) return QStringLiteral("Frame rate must be between 1 and 60 fps.");
    if (!inRange(p.noiseLevel, 0.0, 1.0)) return QStringLiteral("Noise level must be between 0 and 1.");
    if (!inRange(p.volumeSlices, 2, 256)) return QStringLiteral("Volume slices must be between 2 and 256.");
    if (!inRange(p.toneFrequencyHz, 100, 80000)) return QStringLiteral("Tone frequency must be between 100 and 80000 Hz.");
    if (!inRange(p.toneLevelDb, 0, 120)) return QStringLiteral("Tone level must be between 0 and 120 dB.");
    if (!inRange(p.sweepStartHz, 100, 80000) || !inRange(p.sweepEndHz, 100, 80000) || p.sweepStartHz >= p.sweepEndHz)
        return QStringLiteral("Sweep range must satisfy 100 <= start < end <= 80000 Hz.");
    if (!inRange(p.sweepSteps, 2, 200)) return QStringLiteral("Sweep steps must be between 2 and 200.");
    if (p.selectedPixel.x() < 0 || p.selectedPixel.x() >= p.ascansPerBscan ||
        p.selectedPixel.y() < 0 || p.selectedPixel.y() >= p.depthPixels)
        return QStringLiteral("Selected pixel is outside the B-scan.");
    return {};
}

void registerMetaTypes() {
    qRegisterMetaType<Mode>();
    qRegisterMetaType<DeviceState>();
    qRegisterMetaType<AcquisitionParams>();
    qRegisterMetaType<BScanFrame>();
    qRegisterMetaType<VibrationChunk>();
    qRegisterMetaType<TuningPoint>();
}

}  // namespace voctv
```

- [ ] **Step 7: Run tests to verify pass**

Run: `UCRT ./scripts/build.sh`
Expected: `100% tests passed, 0 tests failed out of 1`.

- [ ] **Step 8: Commit**

```bash
git add .gitignore CMakeLists.txt scripts src tests
git commit -m "feat: project skeleton, shared types and parameter validation"
```

---

### Task 2: TissueModel (simulated cochlea physics)

**Files:**
- Create: `src/core/TissueModel.h`, `src/core/TissueModel.cpp`
- Modify: `CMakeLists.txt` (add sources, add `voctv_test(tst_tissuemodel)`)
- Test: `tests/tst_tissuemodel.cpp`

**Interfaces:**
- Consumes: `AcquisitionParams`, `BScanFrame`, `VibrationChunk`, `TuningPoint` (Task 1).
- Produces: `struct Layer { const char *name; double depthFrac, reflectivity, widthFrac; }`; `class TissueModel { explicit TissueModel(quint32 seed = 42); static const QVector<Layer> &layers(); static int basilarMembraneLayer(); static double layerDepthFrac(int layer, double xFrac, int slice); QVector<float> ascan(int depthPixels, double xFrac, int slice, int frame, double noise) const; BScanFrame bscan(const AcquisitionParams &p, int slice, int frame) const; static double bestFrequencyHz(double xFrac); static TuningPoint response(double frequencyHz, double levelDb, double xFrac, double depthFrac); VibrationChunk vibration(const AcquisitionParams &p, double startTimeS, int samples) const; }`.

- [ ] **Step 1: Write the failing test `tests/tst_tissuemodel.cpp`**

```cpp
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
```

- [ ] **Step 2: Register the test and sources in `CMakeLists.txt`**

Change the library source list to:
```cmake
add_library(voctv_core STATIC
    src/core/Types.h src/core/Types.cpp
    src/core/TissueModel.h src/core/TissueModel.cpp
)
```
Append after `voctv_test(tst_types)`:
```cmake
voctv_test(tst_tissuemodel)
```

- [ ] **Step 3: Run to verify it fails**

Run: `UCRT ./scripts/build.sh`
Expected: FAIL — `core/TissueModel.h: No such file or directory`.

- [ ] **Step 4: Implement `src/core/TissueModel.h` and `.cpp`**

```cpp
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
```

```cpp
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
```

- [ ] **Step 5: Run tests to verify pass**

Run: `UCRT ./scripts/build.sh`
Expected: `100% tests passed, 0 tests failed out of 2`.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/core tests/tst_tissuemodel.cpp
git commit -m "feat: tissue model with layered OCT reflectivity and tonotopic vibration"
```

---

### Task 3: IDevice interface and SimulatedVoctvDevice

**Files:**
- Create: `src/device/IDevice.h`, `src/device/SimulatedVoctvDevice.h`, `src/device/SimulatedVoctvDevice.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/tst_device.cpp`

**Interfaces:**
- Consumes: `TissueModel` (Task 2), types (Task 1).
- Produces: `class IDevice : public QObject` with public slots `connectDevice()`, `disconnectDevice()`, `configure(const AcquisitionParams&)`, `startAcquisition(voctv::Mode)`, `stop()`, virtual `simulateFault()`; signals `bscanReady(const voctv::BScanFrame&)`, `vibrationReady(const voctv::VibrationChunk&)`, `tuningPointReady(const voctv::TuningPoint&)`, `acquisitionFinished(voctv::Mode)`, `stateChanged(voctv::DeviceState)`, `error(const QString&)`. `class SimulatedVoctvDevice : public IDevice` adds `DeviceState state() const`.

- [ ] **Step 1: Write the failing test `tests/tst_device.cpp`**

```cpp
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
```

- [ ] **Step 2: Register sources and test in `CMakeLists.txt`**

Library sources become:
```cmake
add_library(voctv_core STATIC
    src/core/Types.h src/core/Types.cpp
    src/core/TissueModel.h src/core/TissueModel.cpp
    src/device/IDevice.h
    src/device/SimulatedVoctvDevice.h src/device/SimulatedVoctvDevice.cpp
)
```
Append: `voctv_test(tst_device)`

- [ ] **Step 3: Run to verify it fails**

Run: `UCRT ./scripts/build.sh`
Expected: FAIL — `device/SimulatedVoctvDevice.h: No such file or directory`.

- [ ] **Step 4: Implement `IDevice.h`**

```cpp
// src/device/IDevice.h
#pragma once
#include <QObject>
#include "core/Types.h"

namespace voctv {

// Contract every acquisition device implements. Real hardware = a new subclass.
class IDevice : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~IDevice() override = default;

public slots:
    virtual void connectDevice() = 0;
    virtual void disconnectDevice() = 0;
    virtual void configure(const voctv::AcquisitionParams &params) = 0;
    virtual void startAcquisition(voctv::Mode mode) = 0;
    virtual void stop() = 0;
    virtual void simulateFault() {}

signals:
    void bscanReady(const voctv::BScanFrame &frame);
    void vibrationReady(const voctv::VibrationChunk &chunk);
    void tuningPointReady(const voctv::TuningPoint &point);
    void acquisitionFinished(voctv::Mode mode);
    void stateChanged(voctv::DeviceState state);
    void error(const QString &message);
};

}  // namespace voctv
```

- [ ] **Step 5: Implement `SimulatedVoctvDevice`**

```cpp
// src/device/SimulatedVoctvDevice.h
#pragma once
#include "core/TissueModel.h"
#include "device/IDevice.h"

class QTimer;

namespace voctv {

class SimulatedVoctvDevice : public IDevice {
    Q_OBJECT
public:
    explicit SimulatedVoctvDevice(QObject *parent = nullptr);
    DeviceState state() const { return state_; }

public slots:
    void connectDevice() override;
    void disconnectDevice() override;
    void configure(const voctv::AcquisitionParams &params) override;
    void startAcquisition(voctv::Mode mode) override;
    void stop() override;
    void simulateFault() override;

private slots:
    void tick();

private:
    void setState(DeviceState s);
    void finish();
    int intervalMs() const;

    QTimer *timer_;
    TissueModel model_;
    AcquisitionParams params_;
    Mode mode_ = Mode::Structural;
    DeviceState state_ = DeviceState::Disconnected;
    int slice_ = 0;
    int frame_ = 0;
    double timeS_ = 0.0;
    int sweepIndex_ = 0;
};

}  // namespace voctv
```

```cpp
// src/device/SimulatedVoctvDevice.cpp
#include "device/SimulatedVoctvDevice.h"
#include <QTimer>
#include <cmath>

namespace voctv {

SimulatedVoctvDevice::SimulatedVoctvDevice(QObject *parent)
    : IDevice(parent), timer_(new QTimer(this)), model_(params_.seed) {
    connect(timer_, &QTimer::timeout, this, &SimulatedVoctvDevice::tick);
}

void SimulatedVoctvDevice::setState(DeviceState s) {
    if (state_ == s) return;
    state_ = s;
    emit stateChanged(s);
}

int SimulatedVoctvDevice::intervalMs() const {
    return mode_ == Mode::Vibrometry ? 50 : qMax(1, 1000 / params_.framesPerSecond);
}

void SimulatedVoctvDevice::connectDevice() {
    if (state_ == DeviceState::Disconnected || state_ == DeviceState::Error) setState(DeviceState::Connected);
}

void SimulatedVoctvDevice::disconnectDevice() {
    timer_->stop();
    setState(DeviceState::Disconnected);
}

void SimulatedVoctvDevice::configure(const AcquisitionParams &params) {
    const QString problem = validate(params);
    if (!problem.isEmpty()) {
        emit error(problem);
        return;
    }
    params_ = params;
    model_ = TissueModel(params.seed);
    if (timer_->isActive()) timer_->setInterval(intervalMs());
}

void SimulatedVoctvDevice::startAcquisition(Mode mode) {
    if (state_ == DeviceState::Acquiring) {
        emit error(QStringLiteral("Acquisition is already running."));
        return;
    }
    if (state_ != DeviceState::Connected) {
        emit error(QStringLiteral("Device is not connected."));
        return;
    }
    mode_ = mode;
    slice_ = 0;
    frame_ = 0;
    timeS_ = 0.0;
    sweepIndex_ = 0;
    setState(DeviceState::Acquiring);
    timer_->start(intervalMs());
}

void SimulatedVoctvDevice::stop() {
    timer_->stop();
    if (state_ == DeviceState::Acquiring) setState(DeviceState::Connected);
}

void SimulatedVoctvDevice::simulateFault() {
    timer_->stop();
    setState(DeviceState::Error);
    emit error(QStringLiteral("Device connection lost (simulated fault)."));
    setState(DeviceState::Disconnected);
}

void SimulatedVoctvDevice::finish() {
    timer_->stop();
    setState(DeviceState::Connected);
    emit acquisitionFinished(mode_);
}

void SimulatedVoctvDevice::tick() {
    switch (mode_) {
    case Mode::Structural:
        emit bscanReady(model_.bscan(params_, 0, frame_++));
        break;
    case Mode::Volume:
        emit bscanReady(model_.bscan(params_, slice_, frame_++));
        if (++slice_ >= params_.volumeSlices) finish();
        break;
    case Mode::Vibrometry: {
        constexpr int kSamples = 400;
        const VibrationChunk c = model_.vibration(params_, timeS_, kSamples);
        timeS_ += kSamples / c.sampleRateHz;
        emit vibrationReady(c);
        break;
    }
    case Mode::TuningSweep: {
        const double f = params_.sweepStartHz *
                         std::pow(params_.sweepEndHz / params_.sweepStartHz, double(sweepIndex_) / (params_.sweepSteps - 1));
        const double xFrac = (params_.selectedPixel.x() + 0.5) / params_.ascansPerBscan;
        const double depthFrac = (params_.selectedPixel.y() + 0.5) / params_.depthPixels;
        emit tuningPointReady(TissueModel::response(f, params_.toneLevelDb, xFrac, depthFrac));
        if (++sweepIndex_ >= params_.sweepSteps) finish();
        break;
    }
    }
}

}  // namespace voctv
```

- [ ] **Step 6: Run tests to verify pass**

Run: `UCRT ./scripts/build.sh`
Expected: `100% tests passed, 0 tests failed out of 3`.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt src/device tests/tst_device.cpp
git commit -m "feat: IDevice interface and simulated VOCTV device"
```

---

### Task 4: AcquisitionController (worker thread + latest-frame-wins)

**Files:**
- Create: `src/app/AcquisitionController.h`, `src/app/AcquisitionController.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/tst_controller.cpp`

**Interfaces:**
- Consumes: `IDevice`, `SimulatedVoctvDevice` (Task 3).
- Produces: `class AcquisitionController : public QObject { explicit AcquisitionController(IDevice *device, QObject *parent = nullptr); int droppedFrames() const; slots connectDevice(), disconnectDevice(), configure(const AcquisitionParams&), start(voctv::Mode), stop(), simulateFault(); signals identical to IDevice's }`. Takes ownership of `device`.

- [ ] **Step 1: Write the failing test `tests/tst_controller.cpp`**

```cpp
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
```

- [ ] **Step 2: Register sources and test in `CMakeLists.txt`**

Add to library sources: `src/app/AcquisitionController.h src/app/AcquisitionController.cpp`
Append: `voctv_test(tst_controller)`

- [ ] **Step 3: Run to verify it fails**

Run: `UCRT ./scripts/build.sh`
Expected: FAIL — `app/AcquisitionController.h: No such file or directory`.

- [ ] **Step 4: Implement the controller**

```cpp
// src/app/AcquisitionController.h
#pragma once
#include <QObject>
#include <QThread>
#include <optional>
#include "device/IDevice.h"

namespace voctv {

// Runs a device on a worker thread; the GUI only ever talks to this object.
class AcquisitionController : public QObject {
    Q_OBJECT
public:
    explicit AcquisitionController(IDevice *device, QObject *parent = nullptr);  // takes ownership
    ~AcquisitionController() override;
    int droppedFrames() const { return dropped_; }

public slots:
    void connectDevice();
    void disconnectDevice();
    void configure(const voctv::AcquisitionParams &params);
    void start(voctv::Mode mode);
    void stop();
    void simulateFault();

signals:
    void bscanReady(const voctv::BScanFrame &frame);
    void vibrationReady(const voctv::VibrationChunk &chunk);
    void tuningPointReady(const voctv::TuningPoint &point);
    void acquisitionFinished(voctv::Mode mode);
    void stateChanged(voctv::DeviceState state);
    void error(const QString &message);

private slots:
    void onDeviceBscan(const voctv::BScanFrame &frame);
    void deliverPending();

private:
    template <typename F> void onDevice(F &&fn);

    QThread thread_;
    IDevice *device_;
    std::optional<BScanFrame> pending_;
    bool deliveryScheduled_ = false;
    int dropped_ = 0;
};

}  // namespace voctv
```

```cpp
// src/app/AcquisitionController.cpp
#include "app/AcquisitionController.h"

namespace voctv {

AcquisitionController::AcquisitionController(IDevice *device, QObject *parent)
    : QObject(parent), device_(device) {
    registerMetaTypes();
    thread_.setObjectName(QStringLiteral("voctv-device"));
    device_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, device_, &QObject::deleteLater);

    connect(device_, &IDevice::bscanReady, this, &AcquisitionController::onDeviceBscan);
    connect(device_, &IDevice::vibrationReady, this, &AcquisitionController::vibrationReady);
    connect(device_, &IDevice::tuningPointReady, this, &AcquisitionController::tuningPointReady);
    connect(device_, &IDevice::acquisitionFinished, this, &AcquisitionController::acquisitionFinished);
    connect(device_, &IDevice::stateChanged, this, &AcquisitionController::stateChanged);
    connect(device_, &IDevice::error, this, &AcquisitionController::error);
    thread_.start();
}

AcquisitionController::~AcquisitionController() {
    QMetaObject::invokeMethod(device_, [d = device_] { d->stop(); }, Qt::BlockingQueuedConnection);
    thread_.quit();
    thread_.wait();  // device is deleted on its own thread via deleteLater
}

template <typename F> void AcquisitionController::onDevice(F &&fn) {
    QMetaObject::invokeMethod(device_, std::forward<F>(fn), Qt::QueuedConnection);
}

void AcquisitionController::connectDevice() { onDevice([d = device_] { d->connectDevice(); }); }
void AcquisitionController::disconnectDevice() { onDevice([d = device_] { d->disconnectDevice(); }); }
void AcquisitionController::configure(const AcquisitionParams &p) { onDevice([d = device_, p] { d->configure(p); }); }
void AcquisitionController::start(Mode mode) {
    pending_.reset();
    onDevice([d = device_, mode] { d->startAcquisition(mode); });
}
void AcquisitionController::stop() { onDevice([d = device_] { d->stop(); }); }
void AcquisitionController::simulateFault() { onDevice([d = device_] { d->simulateFault(); }); }

void AcquisitionController::onDeviceBscan(const BScanFrame &frame) {
    if (pending_) ++dropped_;  // an undelivered frame is being replaced: latest wins
    pending_ = frame;
    if (!deliveryScheduled_) {
        deliveryScheduled_ = true;
        QMetaObject::invokeMethod(this, &AcquisitionController::deliverPending, Qt::QueuedConnection);
    }
}

void AcquisitionController::deliverPending() {
    deliveryScheduled_ = false;
    if (!pending_) return;
    BScanFrame frame = std::move(*pending_);
    pending_.reset();
    emit bscanReady(frame);
}

}  // namespace voctv
```

- [ ] **Step 5: Run tests to verify pass**

Run: `UCRT ./scripts/build.sh`
Expected: `100% tests passed, 0 tests failed out of 4`.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/app tests/tst_controller.cpp
git commit -m "feat: acquisition controller with worker thread and frame coalescing"
```

---

### Task 5: DataStore (session state, 3D layer surface, save/load)

**Files:**
- Create: `src/data/DataStore.h`, `src/data/DataStore.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/tst_datastore.cpp`

**Interfaces:**
- Consumes: types (Task 1), `TissueModel` (tests only).
- Produces: `class DataStore { void setParams(const AcquisitionParams&); const AcquisitionParams &params() const; void recordBscan(const BScanFrame&, bool intoVolume); const BScanFrame &latestBscan() const; void clearVolume(); const QVector<BScanFrame> &volume() const; int filledVolumeSlices() const; void appendVibration(const VibrationChunk&); void clearVibration(); const QVector<float> &vibrationSamples() const; double vibrationSampleRate() const; void appendTuning(const TuningPoint&); void clearTuning(); const QVector<TuningPoint> &tuning() const; QVector<QVector<float>> layerSurface(int depthFrom, int depthTo) const; QString save(const QString &dir) const; QString load(const QString &dir); static constexpr int kMaxVibrationSamples = 20000; }`.

- [ ] **Step 1: Write the failing test `tests/tst_datastore.cpp`**

```cpp
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
```

- [ ] **Step 2: Register sources and test in `CMakeLists.txt`**

Add to library sources: `src/data/DataStore.h src/data/DataStore.cpp`
Append: `voctv_test(tst_datastore)`

- [ ] **Step 3: Run to verify it fails**

Run: `UCRT ./scripts/build.sh`
Expected: FAIL — `data/DataStore.h: No such file or directory`.

- [ ] **Step 4: Implement `DataStore`**

```cpp
// src/data/DataStore.h
#pragma once
#include "core/Types.h"

namespace voctv {

class DataStore {
public:
    static constexpr int kMaxVibrationSamples = 20000;

    void setParams(const AcquisitionParams &p) { params_ = p; }
    const AcquisitionParams &params() const { return params_; }

    void recordBscan(const BScanFrame &f, bool intoVolume);
    const BScanFrame &latestBscan() const { return latest_; }
    void clearVolume();
    const QVector<BScanFrame> &volume() const { return volume_; }
    int filledVolumeSlices() const;

    void appendVibration(const VibrationChunk &c);
    void clearVibration() { vibration_.clear(); }
    const QVector<float> &vibrationSamples() const { return vibration_; }
    double vibrationSampleRate() const { return sampleRateHz_; }

    void appendTuning(const TuningPoint &p) { tuning_.append(p); }
    void clearTuning() { tuning_.clear(); }
    const QVector<TuningPoint> &tuning() const { return tuning_; }

    // For each captured slice: depth index of the brightest pixel within [depthFrom, depthTo) per A-scan.
    QVector<QVector<float>> layerSurface(int depthFrom, int depthTo) const;

    QString save(const QString &dir) const;  // empty string on success
    QString load(const QString &dir);        // empty string on success; state unchanged on failure

private:
    AcquisitionParams params_;
    BScanFrame latest_;
    QVector<BScanFrame> volume_;
    QVector<float> vibration_;
    double sampleRateHz_ = 0.0;
    QVector<TuningPoint> tuning_;
};

}  // namespace voctv
```

```cpp
// src/data/DataStore.cpp
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
```

- [ ] **Step 5: Run tests to verify pass**

Run: `UCRT ./scripts/build.sh`
Expected: `100% tests passed, 0 tests failed out of 5`.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/data tests/tst_datastore.cpp
git commit -m "feat: data store with layer surface extraction and session save/load"
```

---

### Task 6: GUI (views, main window, dark theme, demo/screenshot mode)

**Files:**
- Create: `src/ui/Theme.h`, `src/ui/Theme.cpp`, `src/ui/BScanView.h`, `src/ui/BScanView.cpp`, `src/ui/PlotViews.h`, `src/ui/PlotViews.cpp`, `src/ui/VolumeView.h`, `src/ui/VolumeView.cpp`, `src/ui/MainWindow.h`, `src/ui/MainWindow.cpp`, `src/main.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: everything from Tasks 1–5.
- Produces: executable `voctv-console` with CLI flags `--screenshot <file>` and `--capture-frames <dir>` (run a scripted demo, save output, exit). `MainWindow::runDemo(std::function<void()> done)`, `MainWindow::setHeadless(bool)`.

- [ ] **Step 1: Add the app target to `CMakeLists.txt`** (after the library, before `enable_testing()`)

```cmake
qt_add_executable(voctv-console WIN32
    src/main.cpp
    src/ui/Theme.h src/ui/Theme.cpp
    src/ui/BScanView.h src/ui/BScanView.cpp
    src/ui/PlotViews.h src/ui/PlotViews.cpp
    src/ui/VolumeView.h src/ui/VolumeView.cpp
    src/ui/MainWindow.h src/ui/MainWindow.cpp
)
target_link_libraries(voctv-console PRIVATE voctv_core Qt6::Widgets Qt6::Charts Qt6::DataVisualization)
```

- [ ] **Step 2: Run to verify it fails**

Run: `UCRT ./scripts/build.sh`
Expected: FAIL — `src/main.cpp` missing.

- [ ] **Step 3: Implement `Theme`**

```cpp
// src/ui/Theme.h
#pragma once
class QApplication;
void applyDarkTheme(QApplication &app);
```

```cpp
// src/ui/Theme.cpp
#include "ui/Theme.h"
#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

void applyDarkTheme(QApplication &app) {
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    p.setColor(QPalette::Window, QColor(30, 32, 36));
    p.setColor(QPalette::WindowText, QColor(220, 222, 226));
    p.setColor(QPalette::Base, QColor(22, 24, 27));
    p.setColor(QPalette::AlternateBase, QColor(36, 38, 43));
    p.setColor(QPalette::Text, QColor(220, 222, 226));
    p.setColor(QPalette::Button, QColor(44, 47, 53));
    p.setColor(QPalette::ButtonText, QColor(220, 222, 226));
    p.setColor(QPalette::Highlight, QColor(0, 170, 190));
    p.setColor(QPalette::HighlightedText, Qt::black);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(110, 112, 118));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(110, 112, 118));
    app.setPalette(p);
    app.setStyleSheet(
        "QGroupBox { border: 1px solid #3a3d44; border-radius: 6px; margin-top: 14px; padding: 8px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; color: #00b4c8; font-weight: bold; }"
        "QPushButton { padding: 6px 10px; border-radius: 4px; }"
        "QPushButton#start { background: #0b7a4b; } QPushButton#stop { background: #8a2c2c; }");
}
```

- [ ] **Step 4: Implement `BScanView`**

```cpp
// src/ui/BScanView.h
#pragma once
#include <QImage>
#include <QWidget>
#include "core/Types.h"

class BScanView : public QWidget {
    Q_OBJECT
public:
    explicit BScanView(QWidget *parent = nullptr);
    void setFrame(const voctv::BScanFrame &frame);
    void setSelectedPixel(QPoint pixel);
    QSize sizeHint() const override { return {640, 520}; }

signals:
    void pixelSelected(QPoint pixel);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QRect imageRect() const;
    QImage image_;
    QPoint selected_{-1, -1};
};
```

```cpp
// src/ui/BScanView.cpp
#include "ui/BScanView.h"
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>

BScanView::BScanView(QWidget *parent) : QWidget(parent) {
    setMinimumSize(320, 260);
    setCursor(Qt::CrossCursor);
}

void BScanView::setFrame(const voctv::BScanFrame &frame) {
    if (image_.width() != frame.width || image_.height() != frame.height)
        image_ = QImage(frame.width, frame.height, QImage::Format_Grayscale8);
    for (int y = 0; y < frame.height; ++y) {
        uchar *line = image_.scanLine(y);
        for (int x = 0; x < frame.width; ++x)
            line[x] = uchar(std::clamp(std::sqrt(frame.at(x, y)) * 255.0f, 0.0f, 255.0f));  // sqrt = display gamma
    }
    update();
}

void BScanView::setSelectedPixel(QPoint pixel) {
    selected_ = pixel;
    update();
}

QRect BScanView::imageRect() const {
    if (image_.isNull()) return rect();
    QSize s = image_.size().scaled(size(), Qt::KeepAspectRatio);
    return QRect(QPoint((width() - s.width()) / 2, (height() - s.height()) / 2), s);
}

void BScanView::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), QColor(10, 11, 13));
    if (image_.isNull()) {
        p.setPen(QColor(120, 124, 130));
        p.drawText(rect(), Qt::AlignCenter, tr("Connect and start an acquisition"));
        return;
    }
    const QRect r = imageRect();
    p.drawImage(r, image_);
    if (selected_.x() >= 0 && selected_.x() < image_.width() && selected_.y() >= 0 && selected_.y() < image_.height()) {
        const double sx = double(r.width()) / image_.width(), sy = double(r.height()) / image_.height();
        const QPointF c(r.left() + (selected_.x() + 0.5) * sx, r.top() + (selected_.y() + 0.5) * sy);
        p.setPen(QPen(QColor(0, 220, 240, 170), 1, Qt::DashLine));
        p.drawLine(QPointF(c.x(), r.top()), QPointF(c.x(), r.bottom()));
        p.drawLine(QPointF(r.left(), c.y()), QPointF(r.right(), c.y()));
        p.setPen(QPen(QColor(255, 90, 90), 2));
        p.drawEllipse(c, 5, 5);
    }
    p.setPen(QColor(200, 204, 210));
    p.drawText(r.adjusted(8, 6, -8, -6), Qt::AlignTop | Qt::AlignLeft,
               tr("B-scan  %1 x %2").arg(image_.width()).arg(image_.height()));
}

void BScanView::mousePressEvent(QMouseEvent *event) {
    if (image_.isNull()) return;
    const QRect r = imageRect();
    if (!r.contains(event->position().toPoint())) return;
    const int x = std::clamp(int((event->position().x() - r.left()) * image_.width() / r.width()), 0, image_.width() - 1);
    const int y = std::clamp(int((event->position().y() - r.top()) * image_.height() / r.height()), 0, image_.height() - 1);
    setSelectedPixel({x, y});
    emit pixelSelected({x, y});
}
```

- [ ] **Step 5: Implement `PlotViews`**

```cpp
// src/ui/PlotViews.h
#pragma once
#include <QChartView>
#include "core/Types.h"

class QLineSeries;
class QValueAxis;
class QLogValueAxis;

class AScanPlot : public QChartView {
public:
    explicit AScanPlot(QWidget *parent = nullptr);
    void setProfile(const QVector<float> &profile);
private:
    QLineSeries *series_;
    QValueAxis *x_, *y_;
};

class VibrationPlot : public QChartView {
public:
    explicit VibrationPlot(QWidget *parent = nullptr);
    void setSamples(const QVector<float> &samples, double sampleRateHz);
private:
    QLineSeries *series_;
    QValueAxis *x_, *y_;
};

class TuningCurvePlot : public QChartView {
public:
    explicit TuningCurvePlot(QWidget *parent = nullptr);
    void setPoints(const QVector<voctv::TuningPoint> &points);
private:
    QLineSeries *series_;
    QLogValueAxis *x_;
    QValueAxis *y_;
};
```

```cpp
// src/ui/PlotViews.cpp
#include "ui/PlotViews.h"
#include <QLineSeries>
#include <QLogValueAxis>
#include <QValueAxis>
#include <algorithm>
#include <cmath>

static QChart *makeChart(const QString &title) {
    auto *chart = new QChart;
    chart->setTheme(QChart::ChartThemeDark);
    chart->setBackgroundBrush(QColor(22, 24, 27));
    chart->legend()->hide();
    chart->setTitle(title);
    chart->setMargins(QMargins(4, 4, 4, 4));
    return chart;
}

AScanPlot::AScanPlot(QWidget *parent) : QChartView(parent) {
    QChart *chart = makeChart(tr("A-scan at crosshair"));
    series_ = new QLineSeries;
    series_->setPen(QPen(QColor(0, 200, 220), 1.5));
    chart->addSeries(series_);
    x_ = new QValueAxis; x_->setTitleText(tr("Depth (px)")); x_->setLabelFormat("%d");
    y_ = new QValueAxis; y_->setTitleText(tr("Reflectivity")); y_->setRange(0, 1);
    chart->addAxis(x_, Qt::AlignBottom); chart->addAxis(y_, Qt::AlignLeft);
    series_->attachAxis(x_); series_->attachAxis(y_);
    setChart(chart);
    setRenderHint(QPainter::Antialiasing);
}

void AScanPlot::setProfile(const QVector<float> &profile) {
    QList<QPointF> pts;
    pts.reserve(profile.size());
    for (int i = 0; i < profile.size(); ++i) pts.append(QPointF(i, profile[i]));
    series_->replace(pts);
    x_->setRange(0, qMax(1, int(profile.size()) - 1));
}

VibrationPlot::VibrationPlot(QWidget *parent) : QChartView(parent) {
    QChart *chart = makeChart(tr("Vibration at selected point"));
    series_ = new QLineSeries;
    series_->setPen(QPen(QColor(255, 170, 60), 1.2));
    chart->addSeries(series_);
    x_ = new QValueAxis; x_->setTitleText(tr("Time (ms)"));
    y_ = new QValueAxis; y_->setTitleText(tr("Displacement (nm)"));
    chart->addAxis(x_, Qt::AlignBottom); chart->addAxis(y_, Qt::AlignLeft);
    series_->attachAxis(x_); series_->attachAxis(y_);
    setChart(chart);
    setRenderHint(QPainter::Antialiasing);
}

void VibrationPlot::setSamples(const QVector<float> &samples, double sampleRateHz) {
    constexpr int kShow = 2000;
    const int start = qMax(0, int(samples.size()) - kShow);
    QList<QPointF> pts;
    pts.reserve(samples.size() - start);
    float peak = 0.01f;
    for (int i = start; i < samples.size(); ++i) {
        pts.append(QPointF((i - start) * 1000.0 / qMax(1.0, sampleRateHz), samples[i]));
        peak = std::max(peak, std::abs(samples[i]));
    }
    series_->replace(pts);
    x_->setRange(0, qMax(0.001, (samples.size() - start) * 1000.0 / qMax(1.0, sampleRateHz)));
    y_->setRange(-peak * 1.2, peak * 1.2);
}

TuningCurvePlot::TuningCurvePlot(QWidget *parent) : QChartView(parent) {
    QChart *chart = makeChart(tr("Frequency tuning curve"));
    series_ = new QLineSeries;
    series_->setPen(QPen(QColor(120, 220, 120), 2));
    series_->setPointsVisible(true);
    chart->addSeries(series_);
    x_ = new QLogValueAxis; x_->setTitleText(tr("Frequency (Hz)")); x_->setBase(10); x_->setLabelFormat("%g");
    x_->setRange(100, 80000);
    y_ = new QValueAxis; y_->setTitleText(tr("Displacement (nm)"));
    chart->addAxis(x_, Qt::AlignBottom); chart->addAxis(y_, Qt::AlignLeft);
    series_->attachAxis(x_); series_->attachAxis(y_);
    setChart(chart);
    setRenderHint(QPainter::Antialiasing);
}

void TuningCurvePlot::setPoints(const QVector<voctv::TuningPoint> &points) {
    QList<QPointF> pts;
    double lo = 1e9, hi = 0, peak = 0.01;
    for (const auto &p : points) {
        pts.append(QPointF(p.frequencyHz, p.magnitudeNm));
        lo = std::min(lo, p.frequencyHz); hi = std::max(hi, p.frequencyHz); peak = std::max(peak, p.magnitudeNm);
    }
    series_->replace(pts);
    if (!points.isEmpty()) x_->setRange(lo * 0.9, hi * 1.1);
    y_->setRange(0, peak * 1.15);
}
```

- [ ] **Step 6: Implement `VolumeView`**

```cpp
// src/ui/VolumeView.h
#pragma once
#include <QWidget>

class Q3DSurface;
class QSurfaceDataProxy;
class QSurface3DSeries;
class QLabel;

// 3D view of the basilar-membrane depth across the captured volume.
class VolumeView : public QWidget {
public:
    explicit VolumeView(QWidget *parent = nullptr);
    void setSurface(const QVector<QVector<float>> &depthRows, int depthPixels);
private:
    Q3DSurface *graph_;
    QSurfaceDataProxy *proxy_;
    QSurface3DSeries *series_;
    QLabel *hint_;
};
```

```cpp
// src/ui/VolumeView.cpp
#include "ui/VolumeView.h"
#include <QLabel>
#include <QLinearGradient>
#include <QVBoxLayout>
#include <QtDataVisualization/Q3DCamera>
#include <QtDataVisualization/Q3DScene>
#include <QtDataVisualization/Q3DSurface>
#include <QtDataVisualization/Q3DTheme>
#include <QtDataVisualization/QSurface3DSeries>
#include <QtDataVisualization/QSurfaceDataProxy>
#include <QtDataVisualization/QValue3DAxis>

VolumeView::VolumeView(QWidget *parent) : QWidget(parent) {
    graph_ = new Q3DSurface;
    QWidget *container = QWidget::createWindowContainer(graph_, this);
    container->setMinimumSize(300, 200);
    hint_ = new QLabel(tr("Run a Volume acquisition to build the 3D basilar-membrane surface."), this);
    hint_->setStyleSheet("color: #8a8f98; padding: 4px;");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(hint_);
    layout->addWidget(container, 1);

    graph_->activeTheme()->setType(Q3DTheme::ThemeEbony);
    graph_->scene()->activeCamera()->setCameraPreset(Q3DCamera::CameraPresetIsometricRight);
    graph_->axisX()->setTitle(tr("Lateral (A-scan)"));
    graph_->axisY()->setTitle(tr("Depth (px)"));
    graph_->axisZ()->setTitle(tr("Slice"));
    graph_->axisX()->setTitleVisible(true);
    graph_->axisY()->setTitleVisible(true);
    graph_->axisZ()->setTitleVisible(true);
    graph_->axisY()->setReversed(true);

    proxy_ = new QSurfaceDataProxy;
    series_ = new QSurface3DSeries(proxy_);
    series_->setDrawMode(QSurface3DSeries::DrawSurface);
    QLinearGradient g;
    g.setColorAt(0.0, QColor(20, 40, 160));
    g.setColorAt(0.5, QColor(0, 190, 200));
    g.setColorAt(1.0, QColor(250, 230, 120));
    series_->setBaseGradient(g);
    series_->setColorStyle(Q3DTheme::ColorStyleRangeGradient);
    graph_->addSeries(series_);
}

void VolumeView::setSurface(const QVector<QVector<float>> &depthRows, int depthPixels) {
    if (depthRows.size() < 2) return;
    auto *array = new QSurfaceDataArray;
    array->reserve(depthRows.size());
    for (int z = 0; z < depthRows.size(); ++z) {
        auto *row = new QSurfaceDataRow(depthRows[z].size());
        for (int x = 0; x < depthRows[z].size(); ++x) (*row)[x].setPosition(QVector3D(x, depthRows[z][x], z));
        array->append(row);
    }
    proxy_->resetArray(array);
    graph_->axisY()->setRange(0, depthPixels);
    hint_->setText(tr("Basilar-membrane surface across %1 slices (drag to rotate, scroll to zoom)").arg(depthRows.size()));
}
```

- [ ] **Step 7: Implement `MainWindow`**

```cpp
// src/ui/MainWindow.h
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
    double fps_ = 0.0;
    int volumeFramesSinceUpdate_ = 0;
};
```

```cpp
// src/ui/MainWindow.cpp
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
    panel->setFixedWidth(300);
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
        fps_ = framesInWindow_ * 1000.0 / fpsClock_.restart();
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
    if (state != DeviceState::Acquiring) fps_ = 0.0;
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
    fpsLabel_->setText(tr("%1 fps").arg(fps_, 0, 'f', 1));
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
```

- [ ] **Step 8: Implement `src/main.cpp`**

```cpp
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QScreen>
#include <QTimer>
#include "core/Types.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("VOCTV Console");
    voctv::registerMetaTypes();
    applyDarkTheme(app);

    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption shotOpt("screenshot", "Run the scripted demo, save a screenshot to <file>, then exit.", "file");
    QCommandLineOption framesOpt("capture-frames", "Run the scripted demo, save PNG frames into <dir>, then exit.", "dir");
    parser.addOptions({shotOpt, framesOpt});
    parser.process(app);

    MainWindow window;
    window.resize(1440, 900);
    window.show();

    if (parser.isSet(shotOpt) || parser.isSet(framesOpt)) {
        window.setHeadless(true);
        auto grab = [&window] { return window.screen()->grabWindow(window.winId()); };
        auto *capture = new QTimer(&window);
        int frameNo = 0;
        const QString dir = parser.value(framesOpt);
        if (!dir.isEmpty()) {
            QDir().mkpath(dir);
            QObject::connect(capture, &QTimer::timeout, &window, [&, dir] {
                grab().save(QStringLiteral("%1/frame_%2.png").arg(dir).arg(frameNo++, 4, 10, QChar('0')));
            });
            capture->start(100);
        }
        window.runDemo([&] {
            capture->stop();
            if (parser.isSet(shotOpt)) grab().save(parser.value(shotOpt));
            QTimer::singleShot(200, &app, &QApplication::quit);
        });
    }
    return app.exec();
}
```

- [ ] **Step 9: Build and run tests**

Run: `UCRT ./scripts/build.sh`
Expected: build succeeds; `100% tests passed, 0 tests failed out of 5`.

- [ ] **Step 10: Smoke test with a scripted screenshot**

Run: `UCRT "./build/voctv-console.exe --screenshot docs/screenshot.png"`
Expected: the window appears for about 8 s, exits with code 0, and `docs/screenshot.png` exists. Open the PNG and confirm: a B-scan with 4 bright layers, the A-scan plot, and the 3D tab showing a surface.

- [ ] **Step 11: Commit**

```bash
git add CMakeLists.txt src/main.cpp src/ui docs/screenshot.png
git commit -m "feat: Qt GUI with live B-scan, A-scan, vibrometry, tuning curve and 3D volume"
```

---

### Task 7: Packaging, demo GIF and README

**Files:**
- Create: `scripts/package.sh`, `README.md`, `docs/demo.gif`
- Modify: none

**Interfaces:**
- Consumes: `voctv-console.exe` CLI flags (Task 6).
- Produces: `voctv-console-win64.zip` (git-ignored), `docs/demo.gif`, `README.md`.

- [ ] **Step 1: Create `scripts/package.sh`**

```bash
#!/usr/bin/env bash
set -euo pipefail
rm -rf dist && mkdir -p dist
cp build/voctv-console.exe dist/
windeployqt6 --release --no-translations dist/voctv-console.exe
cp /ucrt64/bin/libgcc_s_seh-1.dll /ucrt64/bin/libstdc++-6.dll /ucrt64/bin/libwinpthread-1.dll dist/ 2>/dev/null || true
rm -f voctv-console-win64.zip
(cd dist && zip -qr ../voctv-console-win64.zip .)
echo "Packaged voctv-console-win64.zip"
```

- [ ] **Step 2: Package and verify the portable build runs outside MSYS2**

Run: `UCRT "pacman -S --needed --noconfirm zip" && UCRT ./scripts/package.sh`
Then, from plain PowerShell (no MSYS2 on PATH): `./dist/voctv-console.exe --screenshot "$env:TEMP/pkg_check.png"`
Expected: exits 0 and the PNG exists. If a DLL is missing, the error names it; copy it from `C:\msys64\ucrt64\bin` in `package.sh` and repeat.

- [ ] **Step 3: Record the demo GIF**

```bash
UCRT "./build/voctv-console.exe --capture-frames frames"
ffmpeg -y -framerate 10 -i frames/frame_%04d.png -vf "fps=10,scale=960:-1:flags=lanczos,split[a][b];[a]palettegen[p];[b][p]paletteuse" docs/demo.gif
```
Expected: `docs/demo.gif` (about 70 frames) shows connect → volume → tuning → vibration → live scan → 3D.

- [ ] **Step 4: Write `README.md`**

````markdown
# VOCTV Console

A C++/Qt desktop app that simulates a **Volumetric Optical Coherence Tomography and Vibrometry (VOCTV)** acquisition system for cochlear imaging: it controls a (simulated) device, receives data on a worker thread, and visualizes it live in 2D and 3D.

![demo](docs/demo.gif)

## What it does
- **Structural imaging:** live B-scans (cross-sections) with tissue layers and speckle; an A-scan (depth profile) at the crosshair.
- **Volume:** captures a stack of B-scans and renders the basilar-membrane surface in 3D.
- **Vibrometry:** click a point, play a virtual tone, and watch displacement over time.
- **Tuning sweep:** sweeps tone frequency and plots the frequency tuning curve (tonotopy: each position responds best to one pitch).
- **Sessions:** save/load to `session.json` + binary data; simulated device faults handled gracefully.

## Architecture
```
MainWindow (GUI thread)
   │  commands (queued)            ▲ frames / chunks / points (queued, latest B-scan wins)
   ▼                               │
AcquisitionController ── owns ──► QThread ──► IDevice  (SimulatedVoctvDevice ← TissueModel)
   │
DataStore (session state, 3D surface extraction, save/load)
```
- `IDevice` is the only contract the app depends on. **Supporting real hardware = one new `IDevice` subclass** (wrap the vendor SDK, emit the same signals); nothing else changes.
- All acquisition runs off the GUI thread; B-scans are coalesced so a slow display never backs up the device.

## Build (Windows, MSYS2 UCRT64)
```bash
pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,qt6-base,qt6-charts,qt6-datavis3d,qt6-tools}
./scripts/build.sh          # configure, build, run tests
./build/voctv-console.exe
```
Portable build: `./scripts/package.sh` → `voctv-console-win64.zip`.

## Tests
Qt Test suites cover parameter validation, the tissue model (layer depths, determinism, tuning peak), the device (modes, errors, faults), the controller (threading, start/stop, shutdown, frame coalescing, live reconfiguration) and session persistence (round-trip, corrupt files). Run `ctest --test-dir build --output-on-failure`.

## Notes
Simulated data only: the tissue and vibration models are simplified for demonstration and are not physiologically calibrated.
````

- [ ] **Step 5: Commit**

```bash
git add scripts/package.sh README.md docs/demo.gif
git commit -m "docs: README, demo GIF and Windows packaging script"
```

---

## Self-Review Notes

- Spec coverage: purpose/success criteria (Tasks 1–7), toolchain (Task 1), each component (Tasks 2–6), threading + latest-wins (Task 4), UI layout (Task 6), error handling (Tasks 3, 5, 6), tests (Tasks 1–5), packaging/docs (Task 7). GitHub publishing is intentionally outside the plan (requires the author's approval).
- Types are consistent across tasks: `BScanFrame::at`, `TissueModel::layerDepthFrac/basilarMembraneLayer/bestFrequencyHz/response`, `DataStore::layerSurface/recordBscan/filledVolumeSlices`, `AcquisitionController::droppedFrames`.
