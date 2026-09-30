# VOCTV Console — Design Spec

Date: 2026-09-29
Status: Approved in conversation; pending written-spec review

## Purpose

A portfolio desktop application demonstrating C++/Qt skill for a USC auditory research lab
role (Oghalai Lab, Volumetric Optical Coherence Tomography and Vibrometry). The app is a
**simulated VOCTV acquisition console**: it sends commands to a (simulated) device, receives
data on a worker thread, and visualizes it live in 2D and 3D.

Success criteria:
- Builds and runs on Windows from a clean clone with one documented build command.
- UI never freezes during acquisition (all device work off the GUI thread).
- Real hardware could be supported by adding one new `IDevice` implementation, with no other changes.
- Unit tests pass for the tissue model, vibration tuning, session save/load, and controller lifecycle.
- Ships as a runnable Windows `.zip` plus a README with a demo GIF and architecture diagram.
- The author can explain every component in an interview.

Non-goals: real hardware drivers, clinical accuracy, true volume rendering (VTK), cross-platform packaging.

## Toolchain

- C++17, CMake, Ninja, GCC via MSYS2 UCRT64 (installed with winget + pacman).
- Qt 6: Core, Gui, Widgets, Charts, Graphs (3D surface), Test.

## Components

| Unit | Responsibility | Depends on |
|---|---|---|
| `TissueModel` | Pure functions: layered reflectivity profile with speckle noise → A-scan; displacement response vs. stimulus frequency at a depth (resonance peaking near a depth-dependent best frequency). Deterministic given a seed. | none |
| `IDevice` | Abstract `QObject` interface: `connectDevice()`, `disconnectDevice()`, `configure(AcquisitionParams)`, `startAcquisition(Mode)`, `stop()`; signals `bscanReady(BScanFrame)`, `vibrationReady(VibrationChunk)`, `tuningPointReady(TuningPoint)`, `error(QString)`, `stateChanged(DeviceState)`. | Qt Core |
| `SimulatedVoctvDevice` | Implements `IDevice` using `TissueModel` and a `QTimer`. Modes: Structural (continuous B-scans), Volume (N B-scans then stop), Vibrometry (trace chunks at a selected pixel and tone), Tuning Sweep (frequencies across a range). Supports "simulate disconnect". | `IDevice`, `TissueModel` |
| `AcquisitionController` | Owns a `QThread`, moves the device to it, relays GUI commands via queued invocations, forwards device signals to the GUI, keeps only the latest pending B-scan (drops stale frames and counts drops), tears down safely on stop and exit. | `IDevice`, `QThread` |
| `DataStore` | Current session: params, latest B-scans, assembled volume, vibration samples, tuning curve. Save/load to a folder: `session.json` (metadata and params) + `volume.bin` + `vibration.bin`. | Qt Core |
| `MainWindow` + views | Control panel; `BScanView` (QImage grayscale, click selects vibrometry point, crosshair); `AScanPlot` (Qt Charts); `VibrationPlot` and `TuningCurvePlot` (Qt Charts); `VolumeView` (Qt Graphs Surface3D of a selected layer depth map); status bar (state, fps, dropped frames, log). Dark theme. | All above via signals/slots |

Data types: `AcquisitionParams` (depth pixels, A-scans per B-scan, frames per second,
noise level, volume slice count, tone frequency Hz, tone level dB, sweep start/end/steps,
selected pixel), `BScanFrame` (index, width, height, float intensities), `VibrationChunk`
(time-stamped displacement samples, nm), `TuningPoint` (frequency, magnitude, phase).

## Data Flow and Threading

GUI → `AcquisitionController` (queued) → device on worker thread → `QTimer` tick produces a
frame → signal → controller → GUI slot draws and `DataStore` records it. If the GUI is still
drawing when a new B-scan arrives, the older pending frame is replaced (latest-wins) and a
drop counter increments. Stop halts the timer; application exit stops acquisition, quits and
joins the thread before destruction.

## UI Layout

Left: controls (Connect/Disconnect, Mode, parameters, Start/Stop, Simulate Disconnect,
Save/Load). Center: live B-scan with crosshair. Right: A-scan at the crosshair column.
Bottom tabs: Vibration trace | Tuning curve | 3D volume. Status bar: state, fps, dropped
frames, last log line.

## Error Handling

- Device `error` signal (including simulated disconnect) → acquisition stops, UI returns to a
  disconnected state, message shown in the status bar and a dialog.
- Parameter inputs are range-limited in the UI and re-validated in `configure()`.
- Save/load failures (missing files, bad JSON, size mismatch) → descriptive dialog; the current
  session is unchanged.

## Testing (Qt Test)

- `TissueModel`: reflectivity peaks at the configured layer depths; the same seed gives an identical A-scan.
- Tuning: the response magnitude peaks within ±1 sweep step of the configured best frequency.
- `DataStore`: save → load round-trip yields identical params, volume, and vibration data; corrupt input returns an error.
- `AcquisitionController`: start/stop/start cycles and destruction during acquisition complete without crash or hang; B-scans arrive on the GUI thread.

## Packaging and Docs

- `windeployqt` into `dist/`, zipped as `voctv-console-win64.zip`.
- README: purpose, screenshot/GIF, architecture diagram, build steps, "adding real hardware"
  (implement `IDevice`), test instructions.
- Public GitHub repo created only after the author approves.
